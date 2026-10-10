#include "P1Button.h"

namespace p1btn {

Event Helper::update(bool pressed, std::uint32_t now_ms)
{
    if (!seen_up_) {
        if (!pressed) {
            seen_up_ = true;
        }
        return Event::None;
    }

    if (pressed) {
        if (!down_) {
            down_ = true;
            down_ms_ = now_ms;
        }
        return Event::None;
    }

    if (!down_) {
        return Event::None;
    }
    down_ = false;

    const std::uint32_t held = now_ms - down_ms_;
    if (held < kBounceMs) {
        return Event::None;
    }
    if (held < kWifiResetMs) {
        return Event::Tap;
    }
    if (held < kFactoryResetMs) {
        return Event::WifiReset;
    }
    return Event::FactoryReset;
}

}  // namespace p1btn
