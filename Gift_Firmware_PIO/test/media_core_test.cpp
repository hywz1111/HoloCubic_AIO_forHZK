#include "media_core.h"
#include <assert.h>
#include <iostream>

int main() {
    assert(gift::movieName("01_stage.mjpeg"));
    assert(!gift::movieName("../a.mjpeg"));
    assert(!gift::movieName(".mjpeg"));
    assert(!gift::movieName("a.mp4"));
    assert(!gift::movieName("a.mjpeg.part"));
    assert(!gift::movieName("a\".mjpeg"));
    uint32_t n = 99;
    assert(gift::number("12", 8, 20, n) && n == 12);
    for (const char *s : {"", "-1", "+12", "12x", "4294967296", "99999999999999999999", "7", "21"})
        assert(!gift::number(s, 8, 20, n));
    uint8_t buffer[8];
    gift::Frames frames(buffer, sizeof buffer);
    // Junk, stuffed FF, split markers, two frames, then truncated tail.
    const uint8_t stream[] = {0, 0xff, 0xd8, 1, 0xff, 0, 2, 0xff, 0xd9,
                             0xff, 0xd8, 3, 0xff, 0xd9, 0xff, 0xd8, 4};
    int count = 0;
    for (uint8_t b : stream) {
        if (frames.feed(b) == gift::Frames::Ready) {
            assert(buffer[0] == 0xff && buffer[1] == 0xd8);
            assert(buffer[frames.size() - 1] == 0xd9);
            ++count; frames.acknowledge();
        }
    }
    assert(count == 2 && frames.incomplete());
    frames.reset();
    frames.feed(0xff); frames.feed(0xd8);
    for (int i = 0; i < 6; ++i) assert(frames.feed(1) == gift::Frames::More);
    assert(frames.feed(1) == gift::Frames::TooLarge);
    frames.feed(0xff); frames.feed(0xd8); frames.feed(5); frames.feed(0xff);
    assert(frames.feed(0xd9) == gift::Frames::Ready);
    assert(frames.size() == 5);
    frames.reset();
    const uint8_t recovery[] = {0xff, 0xd8, 9, 0xff, 0xd8, 8, 0xff, 0xd9};
    for (uint8_t b : recovery) frames.feed(b);
    assert(frames.size() == 5 && buffer[2] == 8);
    std::cout << "media core checks passed\n";
}
