#include "desk_core.h"
#include <assert.h>
#include <iostream>

int main() {
    using namespace gift;
    assert(leap(2000) && leap(2024) && !leap(2100));
    Date date;
    assert(parseDate("2024-02-29", date));
    for (const char *s : {"2025-02-29", "2026-13-01", "2026-04-31", "2026-01-00", "2026-1-01", "2026-01-01x"})
        assert(!parseDate(s, date));
    for (int year = 1970; year <= 2099; ++year)
        for (int month = 1; month <= 12; ++month) {
            Date d{year, month, monthDays(year, month)};
            Date roundtrip = dateFromDay(dayNumber(d));
            assert(d.year == roundtrip.year && d.month == roundtrip.month && d.day == roundtrip.day);
        }
    assert(eventDays({2026,12,31}, {2027,1,1}, false) == 1);
    assert(eventDays({2026,1,2}, {2026,1,1}, false) == -1);
    assert(eventDays({2026,1,2}, {2000,1,1}, true) == 364);
    assert(eventDays({2024,3,1}, {2000,2,29}, true) == 1460);
    assert(eventDays({2024,2,29}, {2000,2,29}, true) == 0);
    assert(nightWindow(23*60, 23*60, 8*60));
    assert(nightWindow(0, 23*60, 8*60));
    assert(!nightWindow(8*60, 23*60, 8*60));
    assert(!nightWindow(12*60, 23*60, 8*60));
    assert(nightWindow(10*60, 9*60, 17*60));
    assert(!nightWindow(10*60, 10*60, 10*60));
    Clock clock;
    assert(!clock.ready && !clock.sync(0, 480, 0));
    assert(clock.sync(1704067200, -480, 1000));
    Date west = clock.date(1000);
    assert(west.year == 2023 && west.month == 12 && west.day == 31);
    assert(clock.minute(1000) == 16*60);
    assert(clock.sync(1704067200, 480, 1000));
    assert(clock.minute(1000) == 8*60);
    Countdown timer;
    const uint64_t nearWrap = 0xffffffffULL - 30000;
    assert(timer.start(1, nearWrap));
    assert(timer.seconds(nearWrap + 1) == 60);
    assert(clock.sync(1800000000, 0, nearWrap + 1000));
    assert(timer.seconds(nearWrap + 30000) == 30); // wall-clock changes do not affect countdowns
    timer.tick(nearWrap + 60000);
    assert(!timer.running && timer.finished && timer.seconds(nearWrap + 60000) == 0);
    timer.cancel(); assert(!timer.finished && !timer.running);
    assert(!timer.start(0, 0) && !timer.start(181, 0));
    assert(utf8Text("晗歌 / はづき", strlen("晗歌 / はづき"), 24, true));
    assert(utf8Text("一\n二\n三", strlen("一\n二\n三"), 24, true));
    assert(!utf8Text("a\nb", 3, 24, false));
    assert(!utf8Text("a\nb\nc\nd", 7, 24, true));
    assert(!utf8Text("\xc0\x80", 2, 24, true));
    assert(!utf8Text("\xed\xa0\x80", 3, 24, true));
    assert(!utf8Text("\xf4\x90\x80\x80", 4, 24, true));
    assert(!utf8Text("a\0b", 3, 24, true));
    uint8_t pixels[2];
    assert(hexBitmap("00fF", 4, pixels, 2) && pixels[1] == 255);
    assert(!hexBitmap("0xFF", 4, pixels, 2) && !hexBitmap("00", 2, pixels, 2));
    int offset;
    assert(signedNumber("-480", -720, 840, offset) && offset == -480);
    assert(!signedNumber("841", -720, 840, offset));
    std::cout << "desk core checks passed\n";
}
