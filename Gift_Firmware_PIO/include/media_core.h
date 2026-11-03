#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace gift {
constexpr size_t MaxFrameBytes = 48 * 1024;
constexpr size_t MaxMovies = 32;
constexpr uint32_t MaxMovieBytes = 128UL * 1024 * 1024;

inline bool number(const char *text, uint32_t minimum, uint32_t maximum, uint32_t &value) {
    if (!text || !*text) return false;
    uint32_t n = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(*p - '0');
        if (n > UINT32_MAX / 10 || (n == UINT32_MAX / 10 && digit > UINT32_MAX % 10)) return false;
        n = n * 10 + digit;
    }
    if (n < minimum || n > maximum) return false;
    value = n;
    return true;
}

inline bool movieName(const char *name) {
    if (!name) return false;
    const size_t n = strlen(name);
    if (n < 7 || n > 48 || strcmp(name + n - 6, ".mjpeg")) return false;
    for (size_t i = 0; i < n - 6; ++i) {
        const char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

// Incremental JPEG marker framing. No allocation, no file-system dependency.
// Call acknowledge() only after consuming the complete frame.
class Frames {
public:
    enum Result { More, Ready, TooLarge };
    Frames(uint8_t *buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) { reset(); }
    void reset() { size_ = 0; previous_ = 0; collecting_ = false; }
    void acknowledge() { reset(); }
    size_t size() const { return size_; }
    bool incomplete() const { return collecting_; }
    Result feed(uint8_t byte) {
        if (!collecting_) {
            if (previous_ == 0xff && byte == 0xd8) {
                if (capacity_ < 2) { reset(); return TooLarge; }
                buffer_[0] = 0xff; buffer_[1] = 0xd8;
                size_ = 2; collecting_ = true;
            }
            previous_ = byte;
            return More;
        }
        // A fresh SOI resynchronizes after a truncated JPEG.
        if (previous_ == 0xff && byte == 0xd8) {
            buffer_[0] = 0xff; buffer_[1] = 0xd8; size_ = 2;
            previous_ = byte;
            return More;
        }
        if (size_ == capacity_) { reset(); previous_ = byte; return TooLarge; }
        buffer_[size_++] = byte;
        const bool end = previous_ == 0xff && byte == 0xd9;
        previous_ = byte;
        if (end) { collecting_ = false; return Ready; }
        return More;
    }
private:
    uint8_t *buffer_;
    size_t capacity_, size_;
    uint8_t previous_;
    bool collecting_;
};
} // namespace gift
