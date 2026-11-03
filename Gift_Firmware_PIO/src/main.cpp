#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <FastLED.h>
#include <algorithm>
#include <math.h>
#include "media_core.h"
#include "control_page.h"
#include "name_bitmap.h"

// Original HoloCubic / forHZK pin map; no additional hardware required.
constexpr int Backlight = 5, LedPin = 27;
constexpr int SdSck = 14, SdMiso = 26, SdMosi = 13, SdCs = 15;
constexpr uint32_t SdClock = 20000000;
constexpr char MovieDir[] = "/mv";
constexpr char Staging[] = "/mv/.incoming.part";
#ifndef HAZUKI_AP_PASSWORD
#define HAZUKI_AP_PASSWORD "hazuki-orange"
#endif

struct Settings {
    uint32_t magic = 0x48475a01;
    uint8_t brightness = 55, led = 8, fps = 12, rotation = 4;
};
struct Movie { String name; bool blocked = false; };
Settings settings;
Movie movies[gift::MaxMovies];
size_t movieCount = 0;
int current = -1;
bool sdReady = false, paused = false, preferencesReady = false;
const char *statusError = "";
TFT_eSPI screen;
SPIClass sdSpi(HSPI);
Preferences preferences;
WebServer server(80);
DNSServer dns;
CRGB leds[2];
File movieFile, uploadFile;
uint8_t *frameBuffer = nullptr;
gift::Frames *frames = nullptr;
uint8_t input[1024];
size_t inputPosition = 0, inputSize = 0;
uint32_t nextFrame = 0, bytesWithoutFrame = 0, goodFrames = 0;
uint8_t badFrames = 0;
bool fallbackVisible = false;
uint32_t lastFallback = 0, lastLed = 0, lastRetry = 0;
int dotX[8] = {}, dotY[8] = {};
bool uploadActive = false, uploadSeen = false, uploadEnded = false;
String uploadName;
const char *uploadError = "";
uint32_t uploadBytes = 0, uploadFrames = 0, uploadTouched = 0;

String pathFor(const String &name) { return String(MovieDir) + "/" + name; }
bool authorized() { return server.header("X-Hazuki-Request") == "1"; }
void replyError(int code, const char *error) {
    server.send(code, "application/json; charset=utf-8", String("{\"error\":\"") + error + "\"}");
}
void ok() { server.send(200, "application/json", "{\"ok\":true}"); }
bool writableRequest() {
    if (!authorized()) { replyError(403, "请从小舞台的管理页面操作"); return false; }
    if (uploadActive || uploadSeen) { replyError(409, "正在添加视频，请稍候"); return false; }
    return true;
}

void applySettings(bool resetScreen = true) {
    if (resetScreen) {
        screen.setRotation(settings.rotation); // bundled TFT driver supports mirrored modes 4/5
        screen.fillScreen(TFT_BLACK);
        fallbackVisible = false;
    }
    ledcWrite(0, 255 - (255U * settings.brightness / 100)); // original board is active-low
    nextFrame = millis();
}
bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *data) {
    if (x < 0 || y < 0 || x + w > 240 || y + h > 240) return false;
    screen.pushImage(x, y, w, h, data);
    return true;
}
bool frameHeaderValid() {
    uint16_t w = 0, h = 0;
    return TJpgDec.getJpgSize(&w, &h, frameBuffer, frames->size()) == JDR_OK && w == 240 && h == 240;
}
void stopMovie() {
    if (movieFile) movieFile.close();
    if (frames) frames->reset();
    inputPosition = inputSize = 0;
    bytesWithoutFrame = goodFrames = 0;
    badFrames = 0;
}
bool openMovie(int index) {
    stopMovie();
    if (!frames || index < 0 || index >= static_cast<int>(movieCount) || movies[index].blocked) return false;
    movieFile = SD.open(pathFor(movies[index].name), FILE_READ);
    if (!movieFile || movieFile.size() == 0 || movieFile.size() > gift::MaxMovieBytes) {
        if (movieFile) movieFile.close();
        movies[index].blocked = true;
        return false;
    }
    current = index;
    if (fallbackVisible) screen.fillScreen(TFT_BLACK);
    fallbackVisible = false;
    nextFrame = millis();
    return true;
}
void nextMovie() {
    stopMovie();
    const int start = current;
    for (size_t n = 0; n < movieCount; ++n) {
        const int index = (start + 1 + n) % movieCount;
        if (openMovie(index)) return;
    }
    current = -1;
    lastRetry = millis();
    fallbackVisible = false;
}
void scanMovies(bool remount) {
    stopMovie();
    current = -1; movieCount = 0; paused = false;
    if (remount) {
        SD.end();
        sdReady = SD.begin(SdCs, sdSpi, SdClock) && SD.cardType() != CARD_NONE;
    }
    if (!sdReady) { statusError = "没有读到内存卡，正在展示备用画面。"; fallbackVisible = false; return; }
    if (!SD.exists(MovieDir) && !SD.mkdir(MovieDir)) {
        statusError = "内存卡暂时不能写入，正在展示备用画面。"; sdReady = false; return;
    }
    // Incomplete uploads are never part of the playlist, even after power loss.
    SD.remove(Staging);
    File directory = SD.open(MovieDir);
    if (!directory || !directory.isDirectory()) { sdReady = false; statusError = "无法读取视频目录。"; return; }
    while (true) {
        File entry = directory.openNextFile();
        if (!entry) break;
        String name = entry.name();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (!entry.isDirectory() && gift::movieName(name.c_str()) && movieCount < gift::MaxMovies) {
            movies[movieCount].name = name; movies[movieCount].blocked = false; ++movieCount;
        }
        entry.close();
    }
    directory.close();
    std::sort(movies, movies + movieCount, [](const Movie &a, const Movie &b) { return a.name < b.name; });
    statusError = movieCount ? "" : "还没有视频，正在展示橙光画面。";
    const String selected = preferencesReady ? preferences.getString("selected", "") : "";
    for (size_t n = 0; n < movieCount; ++n) {
        if (movies[n].name == selected && openMovie(n)) return;
    }
    nextMovie();
}

void showFallback() {
    const uint32_t now = millis();
    if (!fallbackVisible) {
        screen.fillScreen(TFT_BLACK);
        screen.drawBitmap((240 - NameWidth) / 2, 80, NameBitmap, NameWidth, NameHeight, screen.color565(255, 190, 119));
        screen.drawBitmap((240 - KanaWidth) / 2, 139, KanaBitmap, KanaWidth, KanaHeight, screen.color565(255, 241, 219));
        for (int i = 0; i < 8; ++i) dotX[i] = dotY[i] = 0;
        fallbackVisible = true;
    }
    if (now - lastFallback < 100) return;
    lastFallback = now;
    for (int i = 0; i < 8; ++i) {
        screen.fillCircle(dotX[i], dotY[i], 3, TFT_BLACK);
        const float angle = (i * 0.78539816f) + 0.10f * sinf(now / 4000.0f);
        dotX[i] = 120 + static_cast<int>(91 * cosf(angle));
        dotY[i] = 120 + static_cast<int>(91 * sinf(angle));
        const uint8_t glow = 100 + static_cast<int>(60 * (1 + sinf(now / 1800.0f + i)));
        screen.fillCircle(dotX[i], dotY[i], i % 2 ? 1 : 2, screen.color565(glow, glow / 2, glow / 7));
    }
}
void tickPlayback() {
    if (!frames || uploadActive || uploadSeen || !movieFile) { showFallback(); return; }
    if (paused || static_cast<int32_t>(millis() - nextFrame) < 0) return;
    // Limit IO per loop so the phone UI and watchdog remain responsive.
    for (size_t budget = 0; budget < 8192; ++budget) {
        if (inputPosition == inputSize) {
            inputSize = movieFile.read(input, sizeof input); inputPosition = 0;
            if (!inputSize) {
                if (!goodFrames && current >= 0) movies[current].blocked = true;
                if (!goodFrames) statusError = "有一段视频无法播放，已跳过。";
                nextMovie(); return;
            }
        }
        const auto result = frames->feed(input[inputPosition++]);
        ++bytesWithoutFrame;
        if (result == gift::Frames::TooLarge) ++badFrames;
        if (result == gift::Frames::Ready) {
            const bool valid = frameHeaderValid() && TJpgDec.drawJpg(0, 0, frameBuffer, frames->size()) == JDR_OK;
            frames->acknowledge();
            if (valid) { ++goodFrames; badFrames = 0; bytesWithoutFrame = 0; statusError = ""; }
            else ++badFrames;
            nextFrame = millis() + 1000 / settings.fps;
            if (badFrames < 5) return;
        }
        if (badFrames >= 5 || bytesWithoutFrame > gift::MaxFrameBytes * 4) {
            if (current >= 0) movies[current].blocked = true;
            statusError = "有一段视频格式不合适，已跳过。";
            nextMovie(); return;
        }
    }
}

void statePage() {
    String body; body.reserve(2400);
    body = "{\"sd\":"; body += sdReady ? "true" : "false";
    body += ",\"paused\":"; body += paused ? "true" : "false";
    body += ",\"uploading\":"; body += uploadActive ? "true" : "false";
    body += ",\"current\":\""; if (current >= 0 && movieFile) body += movies[current].name;
    body += "\",\"error\":\""; body += statusError;
    body += "\",\"brightness\":"; body += settings.brightness;
    body += ",\"led\":"; body += settings.led;
    body += ",\"fps\":"; body += settings.fps;
    body += ",\"rotation\":"; body += settings.rotation;
    body += ",\"movies\":[";
    for (size_t n = 0; n < movieCount; ++n) {
        if (n) body += ',';
        body += '"'; body += movies[n].name; body += '"';
    }
    body += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", body);
}
void saveSettings() {
    if (!writableRequest()) return;
    uint32_t brightness, led, fps, rotation;
    if (!gift::number(server.arg("brightness").c_str(), 10, 100, brightness) ||
        !gift::number(server.arg("led").c_str(), 0, 40, led) ||
        !gift::number(server.arg("fps").c_str(), 8, 20, fps) ||
        !gift::number(server.arg("rotation").c_str(), 0, 5, rotation)) {
        replyError(400, "设置值不合适，请重新选择"); return;
    }
    Settings updated = settings;
    updated.brightness = brightness; updated.led = led; updated.fps = fps; updated.rotation = rotation;
    if (!preferencesReady || preferences.putBytes("config", &updated, sizeof updated) != sizeof updated) {
        replyError(500, "设置没有保存成功，请重试"); return;
    }
    const bool rotate = settings.rotation != updated.rotation;
    settings = updated;
    if (rotate) paused = false;
    applySettings(rotate); ok();
}
int requestedMovie() {
    const String name = server.arg("name");
    if (!gift::movieName(name.c_str())) return -1;
    for (size_t n = 0; n < movieCount; ++n) if (movies[n].name == name) return n;
    return -1;
}
void cancelUpload() {
    if (uploadFile) uploadFile.close();
    if (sdReady) SD.remove(Staging);
    uploadActive = uploadSeen = uploadEnded = false;
    if (frames) frames->reset();
}
void uploadChunk() {
    HTTPUpload &part = server.upload();
    if (part.status == UPLOAD_FILE_START) {
        if (uploadSeen) { uploadError = "一次只能添加一个视频"; return; }
        uploadSeen = true; uploadEnded = false; uploadActive = true;
        uploadError = ""; uploadBytes = uploadFrames = 0; uploadTouched = millis();
        uploadName = part.filename;
        stopMovie(); current = -1; fallbackVisible = false;
        if (!authorized()) uploadError = "请从小舞台的管理页面操作";
        else if (!sdReady || !frames) uploadError = "请先插入可用的内存卡";
        else if (!gift::movieName(uploadName.c_str())) uploadError = "视频文件名不合适";
        else if (movieCount >= gift::MaxMovies) uploadError = "最多保存 32 段视频，请先移除一些";
        else if (SD.exists(pathFor(uploadName))) uploadError = "已经有同名视频，请换一个名字";
        if (*uploadError) return;
        SD.remove(Staging);
        uploadFile = SD.open(Staging, FILE_WRITE);
        if (!uploadFile) uploadError = "内存卡不能写入";
        showFallback();
    } else if (part.status == UPLOAD_FILE_WRITE) {
        uploadTouched = millis();
        if (!uploadActive || *uploadError) return;
        if (part.currentSize > gift::MaxMovieBytes - uploadBytes) { uploadError = "视频超过 128 MiB，请先分段"; return; }
        if (uploadFile.write(part.buf, part.currentSize) != part.currentSize) { uploadError = "内存卡空间不足或写入失败"; return; }
        uploadBytes += part.currentSize;
        for (size_t n = 0; n < part.currentSize; ++n) {
            const auto result = frames->feed(part.buf[n]);
            if (result == gift::Frames::TooLarge) { uploadError = "视频画质过高，请使用配套转换工具"; break; }
            if (result == gift::Frames::Ready) {
                if (!frameHeaderValid()) { uploadError = "请使用 240×240 的专用视频文件"; break; }
                ++uploadFrames; frames->acknowledge();
            }
        }
    } else if (part.status == UPLOAD_FILE_END) {
        if (uploadFile) { uploadFile.flush(); uploadFile.close(); }
        uploadEnded = true;
        if (!*uploadError && (!uploadFrames || frames->incomplete())) uploadError = "视频文件不完整，请重新添加";
    } else if (part.status == UPLOAD_FILE_ABORTED) {
        cancelUpload(); scanMovies(false);
    }
}
void finishUpload() {
    if (!authorized()) { cancelUpload(); replyError(403, "请从小舞台的管理页面操作"); return; }
    if (!uploadSeen || !uploadEnded) uploadError = "没有收到完整的视频";
    if (!*uploadError && !SD.rename(Staging, pathFor(uploadName))) uploadError = "保存失败，请重试";
    const char *error = uploadError;
    cancelUpload(); scanMovies(false);
    if (*error) replyError(400, error); else ok();
}
void startPhoneControl() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() >> 32) & 0xffff;
    char ssid[32]; snprintf(ssid, sizeof ssid, "Hazuki-%04X", suffix);
    const IPAddress address(192, 168, 4, 1);
    WiFi.softAPConfig(address, address, IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(ssid, HAZUKI_AP_PASSWORD, 1, false, 2)) { Serial.println("AP start failed; offline playback continues"); return; }
    Serial.printf("Phone control: %s / http://192.168.4.1\n", ssid);
    dns.start(53, "*", address);
    const char *headers[] = {"X-Hazuki-Request"}; server.collectHeaders(headers, 1);
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", ControlPage); });
    server.on("/api/state", HTTP_GET, statePage);
    server.on("/api/settings", HTTP_POST, saveSettings);
    server.on("/api/next", HTTP_POST, [] { if (writableRequest()) { paused = false; nextMovie(); ok(); } });
    server.on("/api/pause", HTTP_POST, [] {
        if (!writableRequest()) return;
        uint32_t value;
        if (!gift::number(server.arg("paused").c_str(), 0, 1, value)) { replyError(400, "请选择暂停或播放"); return; }
        paused = value; nextFrame = millis(); ok();
    });
    server.on("/api/rescan", HTTP_POST, [] { if (writableRequest()) { scanMovies(true); ok(); } });
    server.on("/api/play", HTTP_POST, [] {
        if (!writableRequest()) return;
        const int index = requestedMovie();
        if (index < 0) { replyError(404, "没有找到这段视频"); return; }
        movies[index].blocked = false;
        if (!openMovie(index)) { nextMovie(); replyError(400, "视频暂时不能读取"); return; }
        paused = false;
        if (preferencesReady) preferences.putString("selected", movies[index].name);
        ok();
    });
    server.on("/api/delete", HTTP_POST, [] {
        if (!writableRequest()) return;
        const int index = requestedMovie();
        if (index < 0) { replyError(404, "没有找到这段视频"); return; }
        const String path = pathFor(movies[index].name);
        stopMovie();
        const bool removed = SD.remove(path); scanMovies(false);
        if (removed) ok(); else replyError(500, "移除失败，请重试");
    });
    server.on("/api/upload", HTTP_POST, finishUpload, uploadChunk);
    server.onNotFound([] {
        if (server.uri().startsWith("/api/")) { replyError(404, "没有这个操作"); return; }
        server.sendHeader("Location", "http://192.168.4.1/"); server.send(302, "text/plain", "");
    });
    server.begin();
}

void setup() {
    Serial.begin(115200);
    pinMode(Backlight, OUTPUT); digitalWrite(Backlight, HIGH);
    screen.init(); screen.fillScreen(TFT_BLACK);
    screen.writecommand(ST7789_DISPON);
    ledcSetup(0, 5000, 8); ledcAttachPin(Backlight, 0);
    preferencesReady = preferences.begin("hazuki-gift", false);
    Settings stored;
    if (preferencesReady && preferences.getBytes("config", &stored, sizeof stored) == sizeof stored &&
        stored.magic == settings.magic && stored.brightness >= 10 && stored.brightness <= 100 &&
        stored.led <= 40 && stored.fps >= 8 && stored.fps <= 20 && stored.rotation <= 5) settings = stored;
    applySettings();
    FastLED.addLeds<WS2812, LedPin, GRB>(leds, 2);
    FastLED.setBrightness(0); FastLED.show();
    frameBuffer = static_cast<uint8_t *>(malloc(gift::MaxFrameBytes));
    if (frameBuffer) frames = new gift::Frames(frameBuffer, gift::MaxFrameBytes);
    else statusError = "暂时无法播放视频，正在展示备用画面。";
    TJpgDec.setJpgScale(1); TJpgDec.setCallback(jpegOutput);
    screen.setSwapBytes(true);
    showFallback(); // render before SD mount and networking
    sdSpi.begin(SdSck, SdMiso, SdMosi, SdCs);
    scanMovies(true);
    startPhoneControl();
}
void loop() {
    dns.processNextRequest(); server.handleClient();
    const uint32_t now = millis();
    if (uploadSeen && now - uploadTouched > 20000) { cancelUpload(); scanMovies(false); }
    tickPlayback();
    if (!uploadSeen && !movieFile && frames && now - lastRetry > 15000) { lastRetry = now; scanMovies(true); }
    if (now - lastLed >= 80) {
        lastLed = now;
        const float breathing = 0.8f + 0.2f * sinf(now / 2400.0f);
        FastLED.setBrightness(static_cast<uint8_t>(settings.led * 2.55f * breathing));
        leds[0] = leds[1] = CRGB(255, 150, 47); FastLED.show();
    }
    delay(1);
}
