#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#if defined(ARDUINO)
#include <P1Config.h>
#include <atomic>
#endif

namespace p1mqtt {

// "P1" + 6 hex digits + NUL. Always eight visible characters.
constexpr std::size_t kDeviceIdCap = 9;
// mqtt:// + host (kHostMax-1) + : + port + NUL. 7+63+1+5+1 = 77.
constexpr std::size_t kUriMax = 80;
constexpr std::size_t kTopicBuf = 128;

// False if mac is null or every byte is 0x00 — that is a failed read, not a
// real station address. device_id() writes an empty string in that case.
bool mac_usable(const std::uint8_t mac[6]);

// Corrected id: last three MAC bytes as two uppercase hex digits each.
// Empty output if the MAC is not usable (would otherwise become P1000000).
void device_id(const std::uint8_t mac[6], char* out, std::size_t cap);

// Fielded form: Arduino String(byte, HEX) then toUpperCase — no leading zero
// on a byte below 0x10. Unused here; kept so a factory reset can clear old discovery.
void legacy_device_id(const std::uint8_t mac[6], char* out, std::size_t cap);

// Rewrite stored MQTT_ID in place. Returns true if the buffer changed.
// empty → device_id; exact legacy_device_id → device_id; anything else left.
bool migrate_mqtt_id(char* mqtt_id, std::size_t cap, const std::uint8_t mac[6]);

// mqtt://<host>:<port>. 0 if host is empty, port is out of range, or cap is tight.
std::size_t broker_uri(const char* host, std::uint32_t port, char* out, std::size_t cap);

// UPL_THROTTLE == 0 → every telegram. Else due when (now - last) >= throttle seconds.
bool throttle_due(std::uint32_t throttle_s, std::uint32_t now_ms, std::uint32_t last_ms);

#if defined(ARDUINO)
class Mqtt {
public:
    enum class State : std::uint8_t { Idle, Connecting, Connected, Unsupported };

    Mqtt() = default;
    ~Mqtt();

    Mqtt(const Mqtt&) = delete;
    Mqtt& operator=(const Mqtt&) = delete;

    // host_override, when non-null and non-empty, is used for the URI instead of
    // MQTT_HOST. Callers use it for a resolved .local address; the setting is
    // not written.
    bool begin(const p1cfg::Store& cfg, const char* host_override = nullptr);
    void stop();

    State state() const { return state_.load(std::memory_order_relaxed); }

    bool publish(std::string_view topic, std::string_view payload, bool retain);

    unsigned connects() const { return connects_.load(std::memory_order_relaxed); }
    unsigned disconnects() const { return disconnects_.load(std::memory_order_relaxed); }
    unsigned publish_fails() const { return publish_fails_.load(std::memory_order_relaxed); }

    // Set in the esp-mqtt task; consumed on the loop task, which publishes
    // the retained `online` availability payload. Never publish from the handler.
    bool take_just_connected() { return just_connected_.exchange(false, std::memory_order_relaxed); }

private:
    static void on_event(void* arg, void* base, std::int32_t id, void* data);

    void copy_str(char* dst, std::size_t cap, const char* src);

    void* client_ = nullptr;  // esp_mqtt_client_handle_t
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> just_connected_{false};
    std::atomic<unsigned> connects_{0};
    std::atomic<unsigned> disconnects_{0};
    std::atomic<unsigned> publish_fails_{0};

    char uri_[kUriMax]{};
    char client_id_[p1cfg::kIdMax]{};
    char user_[p1cfg::kUserMax]{};
    char pass_[p1cfg::kPassMax]{};
    char will_topic_[p1cfg::kPrefixMax]{};
    char topic_buf_[kTopicBuf]{};
};
#endif

}  // namespace p1mqtt
