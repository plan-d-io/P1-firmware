#pragma once

#include <cstdint>

namespace p1btn {

constexpr std::uint32_t kBounceMs = 50;
constexpr std::uint32_t kWifiResetMs = 2000;
constexpr std::uint32_t kFactoryResetMs = 5000;

enum class Event : std::uint8_t { None, Tap, WifiReset, FactoryReset };

// Arduino-free button helper. The caller supplies "pressed" and the time;
// polarity and the pin stay in the board code.
class Helper {
public:
    Event update(bool pressed, std::uint32_t now_ms);

private:
    bool seen_up_ = false;
    bool down_ = false;
    std::uint32_t down_ms_ = 0;
};

}  // namespace p1btn
