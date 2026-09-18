#include "P1Mqtt.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#include <mqtt_client.h>
#endif

namespace p1mqtt {
namespace {

void write_nul(char* out, std::size_t cap)
{
    if (out != nullptr && cap > 0) {
        out[0] = '\0';
    }
}

void append_hex_unpadded(char*& p, char* end, std::uint8_t b)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (b >= 16u && p < end) {
        *p++ = kHex[b >> 4];
    }
    if (p < end) {
        *p++ = kHex[b & 0x0Fu];
    }
}

void copy_id(char* dst, std::size_t cap, const char* src)
{
    if (dst == nullptr || cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::size_t n = 0;
    while (n + 1 < cap && src[n] != '\0') {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

}  // namespace

bool mac_usable(const std::uint8_t mac[6])
{
    if (mac == nullptr) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        if (mac[i] != 0) {
            return true;
        }
    }
    return false;
}

void device_id(const std::uint8_t mac[6], char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    if (!mac_usable(mac) || cap < kDeviceIdCap) {
        out[0] = '\0';
        return;
    }
    std::snprintf(out, cap, "P1%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void legacy_device_id(const std::uint8_t mac[6], char* out, std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap < 2 || mac == nullptr) {
        return;
    }
    char* p = out;
    char* const end = out + cap - 1;
    if (p + 2 > end) {
        return;
    }
    *p++ = 'P';
    *p++ = '1';
    append_hex_unpadded(p, end, mac[3]);
    append_hex_unpadded(p, end, mac[4]);
    append_hex_unpadded(p, end, mac[5]);
    *p = '\0';
}

bool migrate_mqtt_id(char* mqtt_id, std::size_t cap, const std::uint8_t mac[6])
{
    if (mqtt_id == nullptr || cap < kDeviceIdCap || mac == nullptr) {
        return false;
    }
    char corrected[kDeviceIdCap];
    char legacy[kDeviceIdCap];
    device_id(mac, corrected, sizeof(corrected));
    legacy_device_id(mac, legacy, sizeof(legacy));
    if (corrected[0] == '\0') {
        return false;
    }

    const bool empty = mqtt_id[0] == '\0';
    const bool was_legacy = std::strcmp(mqtt_id, legacy) == 0;
    if (!empty && !was_legacy) {
        return false;
    }
    if (std::strcmp(mqtt_id, corrected) == 0) {
        return false;
    }
    copy_id(mqtt_id, cap, corrected);
    return true;
}

std::size_t broker_uri(const char* host, std::uint32_t port, char* out, std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap == 0) {
        return 0;
    }
    if (host == nullptr || host[0] == '\0' || port < 1u || port > 65535u) {
        return 0;
    }
    const int n = std::snprintf(out, cap, "mqtt://%s:%lu", host, static_cast<unsigned long>(port));
    if (n < 0 || static_cast<std::size_t>(n) >= cap) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

bool throttle_due(std::uint32_t throttle_s, std::uint32_t now_ms, std::uint32_t last_ms)
{
    if (throttle_s == 0) {
        return true;
    }
    return (now_ms - last_ms) >= throttle_s * 1000u;
}

#if defined(ARDUINO)

Mqtt::~Mqtt()
{
    stop();
}

void Mqtt::copy_str(char* dst, std::size_t cap, const char* src)
{
    copy_id(dst, cap, src);
}

void Mqtt::on_event(void* arg, void* /*base*/, std::int32_t id, void* /*data*/)
{
    auto* self = static_cast<Mqtt*>(arg);
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        self->state_.store(State::Connected, std::memory_order_relaxed);
        self->connects_.fetch_add(1, std::memory_order_relaxed);
        self->just_connected_.store(true, std::memory_order_relaxed);
        break;
    case MQTT_EVENT_DISCONNECTED:
        self->state_.store(State::Connecting, std::memory_order_relaxed);
        self->disconnects_.fetch_add(1, std::memory_order_relaxed);
        break;
    default:
        break;
    }
}

bool Mqtt::begin(const p1cfg::Store& cfg, const char* host_override)
{
    stop();

    if (cfg.values().mqtt_tls) {
        state_.store(State::Unsupported, std::memory_order_relaxed);
        Serial.println("MQTT_TLS is set; TLS is not supported yet, not connecting");
        return false;
    }

    const char* host = (host_override != nullptr && host_override[0] != '\0')
                           ? host_override
                           : cfg.values().mqtt_host;
    if (broker_uri(host, cfg.values().mqtt_port, uri_, sizeof(uri_)) == 0) {
        Serial.println("MQTT broker URI could not be built; not connecting");
        return false;
    }

    copy_str(client_id_, sizeof(client_id_), cfg.values().mqtt_id);
    copy_str(user_, sizeof(user_), cfg.values().mqtt_user);
    copy_str(pass_, sizeof(pass_), cfg.values().mqtt_pass);

    copy_str(will_topic_, sizeof(will_topic_), cfg.values().mqtt_pfix);
    std::size_t wn = std::strlen(will_topic_);
    if (wn > 0 && will_topic_[wn - 1] == '/') {
        will_topic_[wn - 1] = '\0';
    }

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.broker.address.uri = uri_;
    mqtt_cfg.credentials.client_id = client_id_;
    if (cfg.values().mqtt_auth) {
        mqtt_cfg.credentials.username = user_;
        mqtt_cfg.credentials.authentication.password = pass_;
    }
    mqtt_cfg.session.last_will.topic = will_topic_;
    mqtt_cfg.session.last_will.msg = "offline";
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = 1;
    mqtt_cfg.buffer.size = 2048;
    mqtt_cfg.buffer.out_size = 4096;

    auto* client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == nullptr) {
        Serial.println("MQTT client init failed");
        return false;
    }
    if (esp_mqtt_client_register_event(
            client, MQTT_EVENT_ANY, reinterpret_cast<esp_event_handler_t>(&Mqtt::on_event),
            this) != ESP_OK) {
        Serial.println("MQTT event register failed");
        esp_mqtt_client_destroy(client);
        return false;
    }
    if (esp_mqtt_client_start(client) != ESP_OK) {
        Serial.println("MQTT client start failed");
        esp_mqtt_client_destroy(client);
        return false;
    }
    client_ = client;
    state_.store(State::Connecting, std::memory_order_relaxed);
    return true;
}

void Mqtt::stop()
{
    just_connected_.store(false, std::memory_order_relaxed);
    if (client_ != nullptr) {
        auto* client = static_cast<esp_mqtt_client_handle_t>(client_);
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client_ = nullptr;
    }
    state_.store(State::Idle, std::memory_order_relaxed);
}

bool Mqtt::publish(std::string_view topic, std::string_view payload, bool retain)
{
    auto* client = static_cast<esp_mqtt_client_handle_t>(client_);
    if (client == nullptr || topic.empty() || topic.size() + 1 > sizeof(topic_buf_)) {
        publish_fails_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    for (std::size_t i = 0; i < topic.size(); ++i) {
        topic_buf_[i] = topic[i];
    }
    topic_buf_[topic.size()] = '\0';

    const char* data = payload.empty() ? "" : payload.data();
    const int rc = esp_mqtt_client_publish(client, topic_buf_, data,
                                           static_cast<int>(payload.size()), 0, retain ? 1 : 0);
    if (rc < 0) {
        publish_fails_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

#endif  // ARDUINO

}  // namespace p1mqtt
