#include "P1Led.h"

namespace p1led {
namespace {

// On/off literals from the LED tables. 1 = on, 0 = off.
constexpr bool kSteady[] = {true, true, true, true, true, true};
constexpr bool kDouble[] = {true, false, true, false, false, false};
constexpr bool kSlow[] = {true, true, true, false, false, false};
constexpr bool kFast[] = {true, false, true, false, true, false};
constexpr bool kTriple[] = {true, false, true, false, true, false, false, false};

Colour mode_colour(bool ap_up)
{
    return ap_up ? Colour::Blue : Colour::Green;
}

bool connection_problem(const Facts& f)
{
    if (f.ap_up) {
        // Setup mode: stored credentials but fell back to the AP.
        return f.wifi_ssid_stored && !f.sta_connected;
    }
    if (!f.sta_connected) {
        return true;
    }
    if (!f.mqtt_en || f.mqtt_https_paused) {
        return false;
    }
    if (f.mqtt_connected) {
        return false;
    }
    return f.ms_mqtt_down >= kMqttStaleMs;
}

}  // namespace

Appearance decide(const Facts& f)
{
    Appearance a{};
    const Colour mode = mode_colour(f.ap_up);

    if (f.busy) {
        a.colour = Colour::Red;
        a.pattern = Pattern::Steady;
        a.condition = Condition::Busy;
        return a;
    }

    a.colour = mode;

    if (f.have_button_press && f.ms_since_button < kUploadWindowMs) {
        a.pattern = Pattern::TripleBlink;
        a.condition = Condition::UploadWindow;
        return a;
    }

    if (f.fs_fault) {
        a.pattern = Pattern::FastBlink;
        a.condition = Condition::CriticalFault;
        return a;
    }

    if (f.ms_since_meter >= kMeterStaleMs) {
        a.pattern = Pattern::SlowBlink;
        a.condition = Condition::NoMeter;
        return a;
    }

    if (connection_problem(f)) {
        a.pattern = Pattern::DoubleBlink;
        a.condition = Condition::ConnectionProblem;
        return a;
    }

    a.pattern = Pattern::Steady;
    a.condition = Condition::Ok;
    return a;
}

std::size_t pattern_len(Pattern p)
{
    switch (p) {
    case Pattern::Steady:
        return sizeof(kSteady) / sizeof(kSteady[0]);
    case Pattern::DoubleBlink:
        return sizeof(kDouble) / sizeof(kDouble[0]);
    case Pattern::SlowBlink:
        return sizeof(kSlow) / sizeof(kSlow[0]);
    case Pattern::FastBlink:
        return sizeof(kFast) / sizeof(kFast[0]);
    case Pattern::TripleBlink:
        return sizeof(kTriple) / sizeof(kTriple[0]);
    }
    return sizeof(kSteady) / sizeof(kSteady[0]);
}

bool pattern_on(Pattern p, std::size_t step)
{
    const std::size_t n = pattern_len(p);
    const std::size_t i = n == 0 ? 0 : (step % n);
    switch (p) {
    case Pattern::Steady:
        return kSteady[i];
    case Pattern::DoubleBlink:
        return kDouble[i];
    case Pattern::SlowBlink:
        return kSlow[i];
    case Pattern::FastBlink:
        return kFast[i];
    case Pattern::TripleBlink:
        return kTriple[i];
    }
    return true;
}

const char* colour_name(Colour c)
{
    switch (c) {
    case Colour::Blue:
        return "blue";
    case Colour::Green:
        return "green";
    case Colour::Red:
        return "red";
    }
    return "?";
}

const char* pattern_name(Pattern p)
{
    switch (p) {
    case Pattern::Steady:
        return "steady";
    case Pattern::DoubleBlink:
        return "double";
    case Pattern::SlowBlink:
        return "slow";
    case Pattern::FastBlink:
        return "fast";
    case Pattern::TripleBlink:
        return "triple";
    }
    return "?";
}

const char* condition_name(Condition c)
{
    switch (c) {
    case Condition::Busy:
        return "busy";
    case Condition::UploadWindow:
        return "upload";
    case Condition::CriticalFault:
        return "fs_fault";
    case Condition::NoMeter:
        return "no_meter";
    case Condition::ConnectionProblem:
        return "conn";
    case Condition::Ok:
        return "ok";
    }
    return "?";
}

void Player::set(Colour colour, Pattern pattern)
{
    if (colour_ == colour && pattern_ == pattern) {
        return;
    }
    colour_ = colour;
    pattern_ = pattern;
    step_ = 0;
    on_ = pattern_on(pattern_, 0);
    have_deadline_ = false;
}

bool Player::tick(std::uint32_t now_ms)
{
    if (!have_deadline_) {
        next_ms_ = now_ms + kStepMs;
        have_deadline_ = true;
        on_ = pattern_on(pattern_, step_);
        return true;
    }
    if (static_cast<std::int32_t>(now_ms - next_ms_) < 0) {
        return false;
    }
    // Catch up if the loop was blocked for several steps.
    do {
        next_ms_ += kStepMs;
        step_ = (step_ + 1) % pattern_len(pattern_);
    } while (static_cast<std::int32_t>(now_ms - next_ms_) >= 0);
    on_ = pattern_on(pattern_, step_);
    return true;
}

}  // namespace p1led
