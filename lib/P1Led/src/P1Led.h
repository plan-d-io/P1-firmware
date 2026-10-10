#pragma once

#include <cstddef>
#include <cstdint>

namespace p1led {

// Step duration for every pattern.
constexpr std::uint32_t kStepMs = 300;

// Shared with the status-page meter icon: no valid telegram for this long.
constexpr std::uint32_t kMeterStaleMs = 30000;

// MQTT enabled but not connected for this long → connection problem.
constexpr std::uint32_t kMqttStaleMs = 30000;

// Button press opens the upload window for this long (restarted on each press).
constexpr std::uint32_t kUploadWindowMs = 60000;

enum class Colour : std::uint8_t { Blue, Green, Red };

enum class Pattern : std::uint8_t {
    Steady,       // 1 1 1 1 1 1
    DoubleBlink,  // 1 0 1 0 0 0
    SlowBlink,    // 1 1 1 0 0 0
    FastBlink,    // 1 0 1 0 1 0
    TripleBlink,  // 1 0 1 0 1 0 0 0
};

// Priority order, strongest first — matches decide().
enum class Condition : std::uint8_t {
    Busy,
    UploadWindow,
    CriticalFault,
    NoMeter,
    ConnectionProblem,
    Ok,
};

struct Facts {
    bool busy = false;
    bool ap_up = false;
    bool wifi_ssid_stored = false;
    bool sta_connected = false;
    bool mqtt_en = false;
    bool mqtt_connected = false;
    // Deliberate MQTT pause around HTTPS — connection-problem clock is frozen.
    bool mqtt_https_paused = false;
    bool fs_fault = false;
    // Elapsed since last valid telegram, or since boot if none yet.
    // Computed in the caller with unsigned subtraction.
    std::uint32_t ms_since_meter = 0;
    // Elapsed since MQTT became (and stayed) disconnected while enabled.
    // Zero while connected, while MQTT is off, or while https-paused.
    std::uint32_t ms_mqtt_down = 0;
    // Upload window: set when a button press time is known.
    bool have_button_press = false;
    std::uint32_t ms_since_button = 0;
};

struct Appearance {
    Colour colour = Colour::Green;
    Pattern pattern = Pattern::Steady;
    Condition condition = Condition::Ok;
};

Appearance decide(const Facts& f);

std::size_t pattern_len(Pattern p);
bool pattern_on(Pattern p, std::size_t step);

const char* colour_name(Colour c);
const char* pattern_name(Pattern p);
const char* condition_name(Condition c);

// Advances through the current pattern. set() restarts at step 0 when the
// appearance changes. tick() returns true when the on/off step changed.
class Player {
public:
    void set(Colour colour, Pattern pattern);
    bool tick(std::uint32_t now_ms);

    Colour colour() const { return colour_; }
    Pattern pattern() const { return pattern_; }
    std::size_t step() const { return step_; }
    bool on() const { return on_; }

private:
    Colour colour_ = Colour::Green;
    Pattern pattern_ = Pattern::Steady;
    std::size_t step_ = 0;
    bool on_ = true;
    bool have_deadline_ = false;
    std::uint32_t next_ms_ = 0;
};

}  // namespace p1led
