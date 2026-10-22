#pragma once

#include <cstdint>

// GPIO 25 request-line policy as 2.23 implemented it:
// type 0: HIGH all the time (continuous telegrams);
// type 1: LOW, HIGH every interval seconds, LOW again as soon as a telegram
// has been read.
namespace p1req {

inline constexpr std::uint32_t kTypeContinuous = 0;
inline constexpr std::uint32_t kTypeInterval = 1;
inline constexpr std::uint32_t kDefaultIntervalS = 10;
inline constexpr std::uint32_t kIntervalMinS = 1;
inline constexpr std::uint32_t kIntervalMaxS = 3600;

inline bool type_ok(std::uint32_t t)
{
    return t <= kTypeInterval;
}

inline bool interval_ok(std::uint32_t s)
{
    return s >= kIntervalMinS && s <= kIntervalMaxS;
}

struct Line {
    std::uint32_t type = kTypeContinuous;
    std::uint32_t interval_ms = kDefaultIntervalS * 1000u;
    bool high = true;
    std::uint32_t mark_ms = 0;

    void begin(std::uint32_t now_ms)
    {
        if (type == kTypeInterval) {
            high = false;
            mark_ms = now_ms;
        } else {
            high = true;
        }
    }

    void configure(std::uint32_t t, std::uint32_t interval_s, std::uint32_t now_ms)
    {
        type = type_ok(t) ? t : kTypeContinuous;
        const std::uint32_t s = interval_ok(interval_s) ? interval_s : kDefaultIntervalS;
        interval_ms = s * 1000u;
        begin(now_ms);
    }

    void tick(std::uint32_t now_ms)
    {
        if (type != kTypeInterval) {
            high = true;
            return;
        }
        if (static_cast<std::int32_t>(now_ms - mark_ms) >= static_cast<std::int32_t>(interval_ms)) {
            high = true;
            mark_ms = now_ms;
        }
    }

    void on_telegram(std::uint32_t now_ms)
    {
        if (type != kTypeInterval) {
            return;
        }
        high = false;
        mark_ms = now_ms;
    }
};

}  // namespace p1req
