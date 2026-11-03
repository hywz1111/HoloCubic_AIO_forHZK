#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "media_core.h"

namespace gift {
struct Date { int year, month, day; };
inline bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
inline int monthDays(int year, int month) {
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return month >= 1 && month <= 12 ? lengths[month - 1] + (month == 2 && leap(year)) : 0;
}
inline bool validDate(Date date) {
    return date.year >= 1970 && date.year <= 2199 && date.month >= 1 && date.month <= 12 &&
           date.day >= 1 && date.day <= monthDays(date.year, date.month);
}
inline int32_t dayNumber(Date date) {
    int32_t days = 0;
    for (int year = 1970; year < date.year; ++year) days += leap(year) ? 366 : 365;
    for (int month = 1; month < date.month; ++month) days += monthDays(date.year, month);
    return days + date.day - 1;
}
inline Date dateFromDay(int32_t days) {
    Date date{1970, 1, 1};
    while (days >= (leap(date.year) ? 366 : 365)) days -= leap(date.year++) ? 366 : 365;
    while (days >= monthDays(date.year, date.month)) days -= monthDays(date.year, date.month++);
    date.day = days + 1;
    return date;
}
inline bool parseDate(const char *text, Date &date) {
    if (!text || strlen(text) != 10 || text[4] != '-' || text[7] != '-') return false;
    char year[5] = {}, month[3] = {}, day[3] = {};
    memcpy(year, text, 4); memcpy(month, text + 5, 2); memcpy(day, text + 8, 2);
    uint32_t y, m, d;
    if (!number(year, 1970, 2099, y) || !number(month, 1, 12, m) || !number(day, 1, 31, d)) return false;
    date = {static_cast<int>(y), static_cast<int>(m), static_cast<int>(d)};
    return validDate(date);
}
inline int32_t eventDays(Date today, Date event, bool annual) {
    if (annual) {
        event.year = today.year;
        while (!validDate(event) || dayNumber(event) < dayNumber(today)) ++event.year;
    }
    return dayNumber(event) - dayNumber(today);
}
inline bool nightWindow(uint16_t minute, uint16_t start, uint16_t end) {
    if (start == end) return false;
    return start < end ? minute >= start && minute < end : minute >= start || minute < end;
}
inline bool signedNumber(const char *text, int minimum, int maximum, int &value) {
    if (!text || !*text) return false;
    const bool negative = *text == '-';
    uint32_t magnitude;
    if (!number(text + (negative ? 1 : 0), 0, 10000, magnitude)) return false;
    const int n = negative ? -static_cast<int>(magnitude) : static_cast<int>(magnitude);
    if (n < minimum || n > maximum) return false;
    value = n; return true;
}
inline bool utf8Text(const char *text, size_t size, size_t maxCharacters, bool multiline) {
    size_t characters = 0, lines = 1;
    for (size_t i = 0; i < size;) {
        const uint8_t lead = static_cast<uint8_t>(text[i++]);
        uint32_t code = 0; size_t tail = 0;
        if (lead < 0x80) code = lead;
        else if (lead >= 0xc2 && lead <= 0xdf) { code = lead & 0x1f; tail = 1; }
        else if (lead >= 0xe0 && lead <= 0xef) { code = lead & 0x0f; tail = 2; }
        else if (lead >= 0xf0 && lead <= 0xf4) { code = lead & 7; tail = 3; }
        else return false;
        if (i + tail > size) return false;
        for (size_t n = 0; n < tail; ++n) {
            const uint8_t c = static_cast<uint8_t>(text[i++]);
            if ((c & 0xc0) != 0x80) return false;
            code = (code << 6) | (c & 0x3f);
        }
        if ((tail == 1 && code < 0x80) || (tail == 2 && code < 0x800) || (tail == 3 && code < 0x10000) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
        if (code < 32 || code == 127) {
            if (code != 10 || !multiline || ++lines > 3) return false;
        }
        if (++characters > maxCharacters) return false;
    }
    return true;
}
inline bool hexBitmap(const char *text, size_t length, uint8_t *data, size_t bytes) {
    if (length != bytes * 2) return false;
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t value = 0;
        for (size_t n = 0; n < 2; ++n) {
            const char c = text[2 * i + n];
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                        c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) return false;
            value = static_cast<uint8_t>(value * 16 + digit);
        }
        data[i] = value;
    }
    return true;
}
class Clock {
public:
    bool ready = false;
    int offset = 480;
    bool sync(uint32_t epoch, int minutes, uint64_t monotonic) {
        if (epoch < 1704067200UL || epoch > 4102444799UL || minutes < -720 || minutes > 840) return false;
        epoch_ = epoch; syncedAt_ = monotonic; offset = minutes; ready = true; return true;
    }
    uint64_t utc(uint64_t monotonic) const { return epoch_ + (monotonic - syncedAt_) / 1000; }
    uint64_t local(uint64_t monotonic) const { return static_cast<int64_t>(utc(monotonic)) + offset * 60; }
    Date date(uint64_t monotonic) const { return dateFromDay(local(monotonic) / 86400); }
    uint16_t minute(uint64_t monotonic) const { return local(monotonic) / 60 % 1440; }
private:
    uint64_t epoch_ = 0, syncedAt_ = 0;
};
class Countdown {
public:
    bool running = false, finished = false;
    bool start(uint32_t minutes, uint64_t now) {
        if (minutes < 1 || minutes > 180) return false;
        due_ = now + static_cast<uint64_t>(minutes) * 60000;
        running = true; finished = false; return true;
    }
    void tick(uint64_t now) { if (running && now >= due_) { running = false; finished = true; } }
    void cancel() { running = finished = false; }
    uint32_t seconds(uint64_t now) const { return running && due_ > now ? (due_ - now + 999) / 1000 : 0; }
private:
    uint64_t due_ = 0;
};
} // namespace gift
