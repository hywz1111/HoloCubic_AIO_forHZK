#include "desk.h"
#include "desk_core.h"
#include "name_bitmap.h"
#include <esp_timer.h>
#include <algorithm>

namespace desk {
namespace {
enum View { Video, Clock, Events, Message, Timer, Alert, Upload };
constexpr size_t MessageBytes = 192 * 72 / 8, LabelBytes = 160 * 24 / 8;
struct Event {
    char name[33] = {};
    uint16_t year = 2026;
    uint8_t month = 1, day = 1, annual = 0, enabled = 0;
    uint8_t bitmap[LabelBytes] = {};
};
struct Config {
    uint32_t magic = 0x48474402;
    uint8_t mode = Video, night = 1, nightBrightness = 8, overlay = 0, autoEvents = 1;
    uint16_t nightStart = 23 * 60, nightEnd = 8 * 60;
    char note[97] = {};
    uint8_t noteBitmap[MessageBytes] = {};
    Event events[3];
};
static_assert(sizeof(Config) <= 3584, "Keep utility settings bounded for the 20 KiB NVS partition");
Config config;
// WebServer handlers are serialized. Reuse a bounded work record rather than
// placing a ~3.4 KiB settings copy on the ESP32 loop task's stack.
Config pending;
Preferences *storage = nullptr;
WebServer *web = nullptr;
bool (*canWrite)() = nullptr;
gift::Clock clock;
gift::Countdown timer;
bool screenOff = false, showTimer = false, resumeVideo = false, nightWake = false;
int rendered = -1;
uint64_t drawnSecond = UINT64_MAX, nextCard = 600000, cardUntil = 0;
uint32_t lastEventSlot = UINT32_MAX;
uint64_t overlaySecond = UINT64_MAX;
uint64_t drawnMinute = UINT64_MAX, drawnDay = UINT64_MAX;
int cardIndex = 0;

uint64_t nowMs() { return esp_timer_get_time() / 1000; }
String quoted(const char *text) {
    String result = "\"";
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
        if (*p == '"' || *p == '\\') { result += '\\'; result += static_cast<char>(*p); }
        else if (*p == '\n') result += "\\n";
        else if (*p < 32) result += ' ';
        else result += static_cast<char>(*p);
    }
    return result + '"';
}
bool validConfig(const Config &c) {
    if (c.magic != config.magic || c.mode > Message || c.night > 1 || c.nightBrightness > 30 ||
        c.overlay > 1 || c.autoEvents > 1 || c.nightStart >= 1440 || c.nightEnd >= 1440 ||
        (c.night && c.nightStart == c.nightEnd) || !memchr(c.note, 0, sizeof c.note) ||
        !gift::utf8Text(c.note, strlen(c.note), 24, true)) return false;
    for (const auto &e : c.events) {
        if (e.enabled > 1 || e.annual > 1 || !memchr(e.name, 0, sizeof e.name) ||
            !gift::utf8Text(e.name, strlen(e.name), 8, false) ||
            (e.enabled && (!*e.name || !gift::validDate({e.year, e.month, e.day})))) return false;
    }
    return true;
}
bool save(const Config &updated) {
    if (!storage || storage->putBytes("desk", &updated, sizeof updated) != sizeof updated) return false;
    config = updated;
    invalidate();
    return true;
}
void error(int code, const char *text) { web->send(code, "application/json; charset=utf-8", "{\"error\":" + quoted(text) + "}"); }
void ok() { web->send(200, "application/json", "{\"ok\":true}"); }
void saved(const Config &c) {
    if (!save(c)) error(500, "没有保存成功，请重试"); else ok();
}
bool argument(const char *key, uint32_t low, uint32_t high, uint32_t &n) {
    return gift::number(web->arg(key).c_str(), low, high, n);
}
bool scheduledNight() {
    return config.night && clock.ready && gift::nightWindow(clock.minute(nowMs()), config.nightStart, config.nightEnd);
}
bool nightActive() { return scheduledNight() && !nightWake; }
int eventIndex(int slot) {
    int enabled[3], count = 0;
    for (int i = 0; i < 3; ++i) if (config.events[i].enabled) enabled[count++] = i;
    return count ? enabled[slot % count] : -1;
}
int activeView(bool uploading) {
    if (uploading) return Upload;
    if (timer.finished) return Alert;
    if (showTimer && timer.running) return Timer;
    if (clock.ready && cardUntil > nowMs() && eventIndex(cardIndex) >= 0) return Events;
    if ((config.mode == Clock || config.mode == Events) && !clock.ready) return Video;
    return config.mode;
}
uint16_t orange(TFT_eSPI &screen) { return screen.color565(255, 150, 47); }
void label(TFT_eSPI &screen, const uint8_t *data, int width, int height, int y) {
    screen.drawBitmap((240 - width) / 2, y, data, width, height, orange(screen));
}
void digits(TFT_eSPI &screen, const char *text, int y, int size, uint16_t color) {
    screen.setTextColor(color, TFT_BLACK); screen.setTextDatum(MC_DATUM); screen.setTextSize(size);
    screen.drawString(text, 120, y);
    screen.setTextSize(1); screen.setTextDatum(TL_DATUM);
}
void drawClock(TFT_eSPI &screen, uint64_t now) {
    label(screen, KanaBitmap, KanaWidth, KanaHeight, 32);
    const uint64_t local = clock.local(now);
    char text[24]; snprintf(text, sizeof text, "%02u:%02u", static_cast<unsigned>(local / 3600 % 24), static_cast<unsigned>(local / 60 % 60));
    digits(screen, text, 109, 5, orange(screen));
    const gift::Date date = clock.date(now);
    snprintf(text, sizeof text, "%04d-%02d-%02d", date.year, date.month, date.day);
    digits(screen, text, 164, 2, TFT_WHITE);
}
void drawEvent(TFT_eSPI &screen, uint64_t now) {
    label(screen, EventTitleBitmap, EventTitleWidth, EventTitleHeight, 21);
    const int index = eventIndex(cardUntil > now ? cardIndex : now / 8000);
    if (index < 0) { label(screen, EventEmptyBitmap, EventEmptyWidth, EventEmptyHeight, 112); return; }
    const Event &event = config.events[index];
    screen.drawBitmap(40, 67, event.bitmap, 160, 24, TFT_WHITE);
    const int32_t days = gift::eventDays(clock.date(now), {event.year, event.month, event.day}, event.annual);
    if (days == 0) { label(screen, TodayBitmap, TodayWidth, TodayHeight, 126); return; }
    if (days > 0) label(screen, BeforeBitmap, BeforeWidth, BeforeHeight, 103);
    else label(screen, AfterBitmap, AfterWidth, AfterHeight, 103);
    char text[16]; snprintf(text, sizeof text, "%ld", static_cast<long>(days < 0 ? -days : days));
    digits(screen, text, 151, 4, orange(screen));
    label(screen, DayBitmap, DayWidth, DayHeight, 185);
}
void syncTime() {
    if (!canWrite()) return;
    uint32_t epoch; int offset;
    if (!argument("epoch", 1704067200UL, 4102444799UL, epoch) ||
        !gift::signedNumber(web->arg("offset").c_str(), -720, 840, offset) || !clock.sync(epoch, offset, nowMs())) {
        error(400, "手机时间不合适，请检查系统日期"); return;
    }
    nightWake = false; invalidate(); ok();
}
void chooseView() {
    if (!canWrite()) return;
    uint32_t mode;
    if (!argument("mode", Video, Message, mode)) { error(400, "请选择一个展示模式"); return; }
    Config &updated = pending; updated = config; updated.mode = mode;
    if (!save(updated)) { error(500, "展示模式没有保存成功"); return; }
    showTimer = false; cardUntil = 0; nextCard = nowMs() + 600000; screenOff = false;
    if (scheduledNight() && config.nightBrightness == 0) nightWake = true;
    ok();
}
void nightSettings() {
    if (!canWrite()) return;
    uint32_t enabled, start, end, level, overlay, cards;
    if (!argument("night", 0, 1, enabled) || !argument("start", 0, 1439, start) ||
        !argument("end", 0, 1439, end) || !argument("level", 0, 30, level) ||
        !argument("overlay", 0, 1, overlay) || !argument("cards", 0, 1, cards) || (enabled && start == end)) {
        error(400, "请检查夜间时间与亮度，开始和结束不能相同"); return;
    }
    Config &updated = pending; updated = config;
    updated.night = enabled; updated.nightStart = start; updated.nightEnd = end;
    updated.nightBrightness = level; updated.overlay = overlay; updated.autoEvents = cards;
    if (!cards) cardUntil = 0;
    nightWake = false;
    saved(updated);
}
void timerAction() {
    if (!canWrite()) return;
    const String action = web->arg("action");
    if (action == "start") {
        uint32_t minutes;
        if (!argument("minutes", 1, 180, minutes) || !timer.start(minutes, nowMs())) { error(400, "计时范围是 1～180 分钟"); return; }
        showTimer = true; screenOff = false; cardUntil = 0;
        if (scheduledNight() && config.nightBrightness == 0) nightWake = true;
    } else if (action == "stop") { timer.cancel(); showTimer = false; }
    else { error(400, "请选择开始或结束计时"); return; }
    invalidate(); ok();
}
void noteSettings() {
    if (!canWrite()) return;
    const String text = web->arg("text"), bits = web->arg("bitmap");
    if (text.length() >= sizeof config.note || !gift::utf8Text(text.c_str(), text.length(), 24, true)) {
        error(400, "留言最多 24 个字符、3 行"); return;
    }
    Config &updated = pending; updated = config;
    memset(updated.note, 0, sizeof updated.note);
    memcpy(updated.note, text.c_str(), text.length());
    if (!gift::hexBitmap(bits.c_str(), bits.length(), updated.noteBitmap, MessageBytes)) {
        error(400, "留言图像没有生成成功，请重试"); return;
    }
    saved(updated);
}
void eventSettings() {
    if (!canWrite()) return;
    Config &updated = pending; updated = config;
    uint32_t cards;
    if (!argument("cards", 0, 1, cards)) { error(400, "请选择是否穿插纪念卡"); return; }
    updated.autoEvents = cards;
    for (int i = 0; i < 3; ++i) {
        const String suffix(i), name = web->arg("name" + suffix);
        Event &e = updated.events[i];
        if (!name.length()) { e = Event{}; continue; }
        gift::Date date; uint32_t annual, enabled;
        const String bits = web->arg("bitmap" + suffix);
        if (name.length() >= sizeof e.name || !gift::utf8Text(name.c_str(), name.length(), 8, false) ||
            !gift::parseDate(web->arg("date" + suffix).c_str(), date) ||
            !gift::number(web->arg("annual" + suffix).c_str(), 0, 1, annual) ||
            !gift::number(web->arg("enabled" + suffix).c_str(), 0, 1, enabled) ||
            !gift::hexBitmap(bits.c_str(), bits.length(), e.bitmap, LabelBytes)) {
            error(400, "请检查纪念日名称（最多 8 字）和日期（1970～2099）"); return;
        }
        memset(e.name, 0, sizeof e.name); memcpy(e.name, name.c_str(), name.length());
        e.year = date.year; e.month = date.month; e.day = date.day; e.annual = annual; e.enabled = enabled;
    }
    saved(updated);
}
} // namespace

void begin(Preferences *preferences) {
    storage = preferences;
    if (storage && storage->getBytes("desk", &pending, sizeof pending) == sizeof pending && validConfig(pending)) config = pending;
    nextCard = nowMs() + 600000;
}
void invalidate() { rendered = -1; drawnSecond = drawnMinute = drawnDay = UINT64_MAX; lastEventSlot = UINT32_MAX; overlaySecond = UINT64_MAX; }
void tick() {
    const uint64_t now = nowMs();
    if (!scheduledNight()) nightWake = false;
    const bool finished = timer.finished;
    timer.tick(now);
    if (timer.finished != finished) invalidate();
    if (cardUntil && now >= cardUntil) { cardUntil = 0; nextCard = now + 600000; }
    if (now >= nextCard && !cardUntil) {
        nextCard = now + 600000;
        if (config.autoEvents && config.mode == Video && clock.ready && !timer.running && !timer.finished && !screenOff && eventIndex(cardIndex) >= 0) {
            cardUntil = now + 8000; ++cardIndex;
        }
    }
}
bool draw(TFT_eSPI &screen, bool uploading) {
    if (screenOff || (nightActive() && config.nightBrightness == 0)) return true;
    const uint64_t now = nowMs();
    const int view = activeView(uploading);
    const bool changed = view != rendered;
    if (changed) {
        screen.fillScreen(TFT_BLACK);
        if (view == Video) resumeVideo = true;
        rendered = view; drawnSecond = UINT64_MAX;
    }
    if (view == Video || view == Upload) return false;
    const uint32_t slot = view == Events ? now / 8000 : 0;
    const uint64_t minute = clock.ready ? clock.local(now) / 60 : 0;
    const uint64_t day = clock.ready ? clock.local(now) / 86400 : 0;
    if (!changed && ((view == Clock && drawnMinute == minute) ||
        (view == Events && lastEventSlot == slot && drawnDay == day) ||
        view == Message || view == Alert || (view == Timer && drawnSecond == now / 1000))) return true;
    drawnSecond = now / 1000; drawnMinute = minute; drawnDay = day; lastEventSlot = slot;
    if (!changed) {
        if (view == Clock) screen.fillRect(0, 80, 240, 106, TFT_BLACK);
        else if (view == Timer) screen.fillRect(0, 85, 240, 73, TFT_BLACK);
        else screen.fillScreen(TFT_BLACK);
    }
    switch (view) {
    case Clock: drawClock(screen, now); break;
    case Events: drawEvent(screen, now); break;
    case Message:
        label(screen, NoteTitleBitmap, NoteTitleWidth, NoteTitleHeight, 36);
        if (*config.note) screen.drawBitmap(24, 88, config.noteBitmap, 192, 72, TFT_WHITE);
        else label(screen, NoteEmptyBitmap, NoteEmptyWidth, NoteEmptyHeight, 111);
        label(screen, KanaBitmap, KanaWidth, KanaHeight, 188);
        break;
    case Timer: {
        label(screen, RemainingBitmap, RemainingWidth, RemainingHeight, 42);
        const uint32_t seconds = timer.seconds(now);
        char text[16]; snprintf(text, sizeof text, "%02lu:%02lu", static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
        digits(screen, text, 119, 5, orange(screen));
        label(screen, KanaBitmap, KanaWidth, KanaHeight, 184);
        break;
    }
    case Alert:
        label(screen, TimerDoneBitmap, TimerDoneWidth, TimerDoneHeight, 82);
        label(screen, DismissBitmap, DismissWidth, DismissHeight, 139);
        break;
    default: break;
    }
    return true;
}
bool takeVideoResume() { const bool result = resumeVideo; resumeVideo = false; return result; }
void videoClock(TFT_eSPI &screen, bool newFrame) {
    if (!config.overlay || !clock.ready || screenOff) return;
    const uint64_t local = clock.local(nowMs());
    if (!newFrame && overlaySecond == local) return;
    overlaySecond = local;
    char text[8]; snprintf(text, sizeof text, "%02u:%02u", static_cast<unsigned>(local / 3600 % 24), static_cast<unsigned>(local / 60 % 60));
    screen.fillRect(168, 4, 68, 19, TFT_BLACK);
    screen.setTextColor(TFT_WHITE, TFT_BLACK); screen.setTextSize(2); screen.drawString(text, 172, 6); screen.setTextSize(1);
}
uint8_t brightness(uint8_t dayBrightness) {
    if (screenOff) return 0;
    return nightActive() ? std::min(dayBrightness, config.nightBrightness) : dayBrightness;
}
uint8_t ledLevel(uint8_t dayLevel) { return screenOff || nightActive() ? 0 : dayLevel; }
String stateJson() {
    const uint64_t now = nowMs();
    String result; result.reserve(1400);
    result = "{\"mode\":"; result += config.mode;
    result += ",\"view\":"; result += activeView(false);
    result += ",\"off\":"; result += (screenOff || (nightActive() && config.nightBrightness == 0)) ? "true" : "false";
    result += ",\"timeReady\":"; result += clock.ready ? "true" : "false";
    result += ",\"nightActive\":"; result += nightActive() ? "true" : "false";
    result += ",\"night\":"; result += config.night;
    result += ",\"start\":"; result += config.nightStart;
    result += ",\"end\":"; result += config.nightEnd;
    result += ",\"level\":"; result += config.nightBrightness;
    result += ",\"overlay\":"; result += config.overlay;
    result += ",\"cards\":"; result += config.autoEvents;
    result += ",\"timerRunning\":"; result += timer.running ? "true" : "false";
    result += ",\"timerFinished\":"; result += timer.finished ? "true" : "false";
    result += ",\"seconds\":"; result += timer.seconds(now);
    result += ",\"note\":"; result += quoted(config.note);
    result += ",\"events\":[";
    for (int i = 0; i < 3; ++i) {
        if (i) result += ',';
        const Event &e = config.events[i];
        char date[16]; snprintf(date, sizeof date, "%04u-%02u-%02u", e.year, e.month, e.day);
        result += "{\"name\":"; result += quoted(e.name);
        result += ",\"date\":"; result += quoted(date);
        result += ",\"annual\":"; result += e.annual;
        result += ",\"enabled\":"; result += e.enabled; result += '}';
    }
    result += "]}";
    return result;
}
void routes(WebServer &server, bool (*writableRequest)()) {
    web = &server; canWrite = writableRequest;
    server.on("/api/time", HTTP_POST, syncTime);
    server.on("/api/view", HTTP_POST, chooseView);
    server.on("/api/night", HTTP_POST, nightSettings);
    server.on("/api/timer", HTTP_POST, timerAction);
    server.on("/api/note", HTTP_POST, noteSettings);
    server.on("/api/events", HTTP_POST, eventSettings);
    server.on("/api/power", HTTP_POST, [] {
        if (!canWrite()) return;
        uint32_t off;
        if (!argument("off", 0, 1, off)) { error(400, "请选择亮屏或熄屏"); return; }
        screenOff = off;
        if (!screenOff && scheduledNight()) nightWake = true;
        if (!screenOff) invalidate();
        ok();
    });
}
} // namespace desk
