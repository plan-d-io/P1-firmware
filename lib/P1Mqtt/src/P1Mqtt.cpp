#include "P1Mqtt.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_tls_errors.h>
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

bool append(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    if (out == nullptr || n + s.size() + 1 > cap) {
        return false;
    }
    for (char c : s) {
        out[n++] = c;
    }
    return true;
}

bool append_char(char* out, std::size_t cap, std::size_t& n, char c)
{
    if (out == nullptr || n + 2 > cap) {
        return false;
    }
    out[n++] = c;
    return true;
}

bool append_json_str(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    if (!append_char(out, cap, n, '"')) {
        return false;
    }
    for (char c : s) {
        if (c == '"' || c == '\\') {
            if (!append_char(out, cap, n, '\\') || !append_char(out, cap, n, c)) {
                return false;
            }
        } else if (static_cast<unsigned char>(c) < 0x20) {
            continue;
        } else if (!append_char(out, cap, n, c)) {
            return false;
        }
    }
    return append_char(out, cap, n, '"');
}

void skip_ws(std::string_view& s)
{
    while (!s.empty()) {
        const char c = s[0];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        s.remove_prefix(1);
    }
}

bool parse_json_string(std::string_view& s, std::string_view& out)
{
    skip_ws(s);
    if (s.empty() || s[0] != '"') {
        return false;
    }
    s.remove_prefix(1);
    std::size_t i = 0;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\') {
            if (i + 1 >= s.size()) {
                return false;
            }
            i += 2;
            continue;
        }
        ++i;
    }
    if (i >= s.size()) {
        return false;
    }
    out = s.substr(0, i);
    s.remove_prefix(i + 1);
    return true;
}

bool skip_json_value(std::string_view& s)
{
    skip_ws(s);
    if (s.empty()) {
        return false;
    }
    if (s[0] == '"') {
        std::string_view unused;
        return parse_json_string(s, unused);
    }
    if (s[0] == '{' || s[0] == '[') {
        const char open = s[0];
        const char close = (open == '{') ? '}' : ']';
        int depth = 1;
        s.remove_prefix(1);
        bool in_str = false;
        bool esc = false;
        while (!s.empty() && depth > 0) {
            const char c = s[0];
            s.remove_prefix(1);
            if (in_str) {
                if (esc) {
                    esc = false;
                } else if (c == '\\') {
                    esc = true;
                } else if (c == '"') {
                    in_str = false;
                }
                continue;
            }
            if (c == '"') {
                in_str = true;
            } else if (c == open) {
                ++depth;
            } else if (c == close) {
                --depth;
            }
        }
        return depth == 0;
    }
    while (!s.empty()) {
        const char c = s[0];
        if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            break;
        }
        s.remove_prefix(1);
    }
    return true;
}

std::size_t join_topic(std::string_view base, std::string_view suffix, char* out, std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, base) || !append(out, cap, n, suffix)) {
        write_nul(out, cap);
        return 0;
    }
    out[n] = '\0';
    return n;
}

bool snapshot_append_entry(char* out, std::size_t cap, std::size_t& n, bool& first,
                           const p1cfg::CfgEntry& e)
{
    if (!first && !append(out, cap, n, ",")) {
        return false;
    }
    first = false;
    if (!append_json_str(out, cap, n, e.key) || !append(out, cap, n, ":")) {
        return false;
    }
    switch (e.type) {
    case p1cfg::CfgType::Bool:
        return append(out, cap, n, *e.var.b ? "true" : "false");
    case p1cfg::CfgType::I32: {
        char num[16];
        std::snprintf(num, sizeof(num), "%ld", static_cast<long>(*e.var.i));
        return append(out, cap, n, num);
    }
    case p1cfg::CfgType::U32: {
        char num[16];
        std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(*e.var.u));
        return append(out, cap, n, num);
    }
    case p1cfg::CfgType::Str:
        return append_json_str(out, cap, n, e.var.s != nullptr ? e.var.s : "");
    }
    return false;
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

std::size_t broker_uri(const char* host, std::uint32_t port, char* out, std::size_t cap, bool tls)
{
    write_nul(out, cap);
    if (out == nullptr || cap == 0) {
        return 0;
    }
    if (host == nullptr || host[0] == '\0' || port < 1u || port > 65535u) {
        return 0;
    }
    const int n = std::snprintf(out, cap, tls ? "mqtts://%s:%lu" : "mqtt://%s:%lu", host,
                                static_cast<unsigned long>(port));
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

bool interval_due(std::uint32_t interval_ms, std::uint32_t now_ms, std::uint32_t last_ms)
{
    return (now_ms - last_ms) >= interval_ms;
}

std::size_t inbound_topic(std::string_view availability, Inbound kind, char* out, std::size_t cap)
{
    switch (kind) {
    case Inbound::Reboot:
        return join_topic(availability, "/set/reboot", out, cap);
    case Inbound::Config:
        return join_topic(availability, "/set/config", out, cap);
    default:
        write_nul(out, cap);
        return 0;
    }
}

Inbound match_inbound(std::string_view availability, std::string_view topic)
{
    if (topic == kHaStatusTopic) {
        return Inbound::HaStatus;
    }
    char reboot[kTopicBuf];
    char config[kTopicBuf];
    if (inbound_topic(availability, Inbound::Reboot, reboot, sizeof(reboot)) > 0 &&
        topic == reboot) {
        return Inbound::Reboot;
    }
    if (inbound_topic(availability, Inbound::Config, config, sizeof(config)) > 0 &&
        topic == config) {
        return Inbound::Config;
    }
    return Inbound::None;
}

std::size_t config_snapshot_topic(std::string_view availability, char* out, std::size_t cap)
{
    return join_topic(availability, "/sys/config", out, cap);
}

std::size_t sys_topic(std::string_view id, std::string_view name, char* out, std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap == 0 || id.empty() || name.empty()) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, "sys/devices/") || !append(out, cap, n, id) ||
        !append_char(out, cap, n, '/') || !append(out, cap, n, name)) {
        write_nul(out, cap);
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t config_snapshot(const p1cfg::Store& cfg, const char* hostname, const char* fw_ver,
                            char* out, std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, "{") || !append_json_str(out, cap, n, "HOSTNAME") ||
        !append(out, cap, n, ":") ||
        !append_json_str(out, cap, n, hostname != nullptr ? hostname : "") ||
        !append(out, cap, n, ",") || !append_json_str(out, cap, n, "FW_VER") ||
        !append(out, cap, n, ":") ||
        !append_json_str(out, cap, n, fw_ver != nullptr ? fw_ver : "")) {
        write_nul(out, cap);
        return 0;
    }
    bool first = false;
    const p1cfg::CfgEntry* table = cfg.settings();
    for (std::size_t i = 0; i < cfg.setting_count(); ++i) {
        const p1cfg::CfgEntry& e = table[i];
        if ((e.flags & p1cfg::CFG_IN_API) == 0 || (e.flags & p1cfg::CFG_SECRET) != 0) {
            continue;
        }
        if (!snapshot_append_entry(out, cap, n, first, e)) {
            write_nul(out, cap);
            return 0;
        }
    }
    if (!append(out, cap, n, "}")) {
        write_nul(out, cap);
        return 0;
    }
    out[n] = '\0';
    return n;
}

bool reboot_command(std::string_view payload)
{
    skip_ws(payload);
    if (payload.empty() || payload[0] != '{') {
        return false;
    }
    payload.remove_prefix(1);

    bool saw_true = false;
    bool saw_value = false;
    for (;;) {
        skip_ws(payload);
        if (payload.empty()) {
            return false;
        }
        if (payload[0] == '}') {
            break;
        }
        std::string_view key;
        if (!parse_json_string(payload, key)) {
            return false;
        }
        skip_ws(payload);
        if (payload.empty() || payload[0] != ':') {
            return false;
        }
        payload.remove_prefix(1);
        if (key == "value") {
            saw_value = true;
            skip_ws(payload);
            std::string_view val;
            if (!parse_json_string(payload, val)) {
                return false;
            }
            if (val == "true") {
                saw_true = true;
            } else {
                return false;
            }
        } else if (!skip_json_value(payload)) {
            return false;
        }
        skip_ws(payload);
        if (payload.empty()) {
            return false;
        }
        if (payload[0] == ',') {
            payload.remove_prefix(1);
            continue;
        }
        if (payload[0] == '}') {
            break;
        }
        return false;
    }
    return saw_value && saw_true;
}

std::size_t heap_kb_json(unsigned bytes, char* out, std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap < 2) {
        return 0;
    }
    const int n = std::snprintf(out, cap, "%.3f", static_cast<double>(bytes) / 1000.0);
    if (n <= 0 || static_cast<std::size_t>(n) >= cap) {
        write_nul(out, cap);
        return 0;
    }
    char* p = out + n;
    while (p > out && *(p - 1) == '0') {
        --p;
    }
    if (p > out && *(p - 1) == '.') {
        --p;
    }
    *p = '\0';
    return static_cast<std::size_t>(p - out);
}

std::size_t diag_payload(std::string_view friendly, std::string_view value_json,
                         std::string_view entity, std::string_view sensor_id, char* out,
                         std::size_t cap, std::uint64_t unix_ts, const char* unit)
{
    write_nul(out, cap);
    if (out == nullptr || cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, "{\"friendly_name\":") || !append_json_str(out, cap, n, friendly) ||
        !append(out, cap, n, ",\"value\":") || !append(out, cap, n, value_json) ||
        !append(out, cap, n, ",\"entity\":") || !append_json_str(out, cap, n, entity) ||
        !append(out, cap, n, ",\"sensorId\":") || !append_json_str(out, cap, n, sensor_id)) {
        write_nul(out, cap);
        return 0;
    }
    if (unit != nullptr && unit[0] != '\0') {
        if (!append(out, cap, n, ",\"unit_of_measurement\":") ||
            !append_json_str(out, cap, n, unit)) {
            write_nul(out, cap);
            return 0;
        }
    }
    if (unix_ts != 0) {
        char num[24];
        const int w = std::snprintf(num, sizeof(num), "%llu",
                                    static_cast<unsigned long long>(unix_ts));
        if (w <= 0 || !append(out, cap, n, ",\"timestamp\":") ||
            !append(out, cap, n, std::string_view(num, static_cast<std::size_t>(w)))) {
            write_nul(out, cap);
            return 0;
        }
    }
    if (!append_char(out, cap, n, '}')) {
        write_nul(out, cap);
        return 0;
    }
    out[n] = '\0';
    return n;
}

bool InboundAssembler::feed(std::string_view topic, const char* data, int data_len, int total_len,
                            int offset)
{
    if (ready_.load(std::memory_order_acquire)) {
        drops_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (data_len < 0 || total_len < 0 || offset < 0) {
        drops_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (data == nullptr && data_len > 0) {
        drops_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (offset == 0) {
        overflow_ = false;
        n_ = 0;
        topic_[0] = '\0';
        if (!topic.empty()) {
            const std::size_t tn =
                topic.size() < sizeof(topic_) - 1 ? topic.size() : sizeof(topic_) - 1;
            for (std::size_t i = 0; i < tn; ++i) {
                topic_[i] = topic[i];
            }
            topic_[tn] = '\0';
        }
    } else if (n_ == 0 && !overflow_) {
        drops_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (overflow_) {
        return false;
    }

    const auto u_total = static_cast<std::size_t>(total_len);
    const auto u_len = static_cast<std::size_t>(data_len);
    const auto u_off = static_cast<std::size_t>(offset);
    if (u_total > kMax || u_off + u_len > kMax || u_off + u_len > u_total) {
        overflow_ = true;
        n_ = 0;
        drops_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (u_len > 0) {
        std::memcpy(payload_ + u_off, data, u_len);
    }
    n_ = u_off + u_len;
    if (n_ == u_total) {
        ready_.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

bool InboundAssembler::take(char* topic, std::size_t tcap, char* payload, std::size_t pcap)
{
    if (!ready_.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    if (topic != nullptr && tcap > 0) {
        copy_id(topic, tcap, topic_);
    }
    if (payload != nullptr && pcap > 0) {
        const std::size_t pn = n_ < pcap - 1 ? n_ : pcap - 1;
        if (pn > 0) {
            std::memcpy(payload, payload_, pn);
        }
        payload[pn] = '\0';
    }
    n_ = 0;
    overflow_ = false;
    return true;
}

void InboundAssembler::reset()
{
    ready_.store(false, std::memory_order_relaxed);
    n_ = 0;
    overflow_ = false;
    topic_[0] = '\0';
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

void Mqtt::on_event(void* arg, void* /*base*/, std::int32_t id, void* data)
{
    auto* self = static_cast<Mqtt*>(arg);
    auto* event = static_cast<esp_mqtt_event_handle_t>(data);
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        self->state_.store(State::Connected, std::memory_order_relaxed);
        self->connects_.fetch_add(1, std::memory_order_relaxed);
        self->just_connected_.store(true, std::memory_order_relaxed);
        self->fail_.store(Fail::None, std::memory_order_relaxed);
        self->cert_flags_.store(0, std::memory_order_relaxed);
        break;
    case MQTT_EVENT_DISCONNECTED:
        self->state_.store(State::Connecting, std::memory_order_relaxed);
        self->disconnects_.fetch_add(1, std::memory_order_relaxed);
        break;
    case MQTT_EVENT_DATA:
        if (event != nullptr) {
            const std::string_view topic =
                (event->topic != nullptr && event->topic_len > 0)
                    ? std::string_view(event->topic, static_cast<std::size_t>(event->topic_len))
                    : std::string_view{};
            self->inbound_.feed(topic, event->data, event->data_len, event->total_data_len,
                                event->current_data_offset);
        }
        break;
    case MQTT_EVENT_ERROR:
        if (event != nullptr && event->error_handle != nullptr) {
            Fail f = Fail::Transport;
            int flags = 0;
            int esp_err = 0;
            int stack_err = 0;
            const auto* err = event->error_handle;
            if (err->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                f = Fail::Auth;
            } else if (err->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                flags = err->esp_tls_cert_verify_flags;
                esp_err = static_cast<int>(err->esp_tls_last_esp_err);
                stack_err = err->esp_tls_stack_err;
                // Bundle rejection: verify flags, mbedTLS verify-failed, or the
                // handshake-failed wrapper esp-tls uses when the peer cert is bad
                // (flags are not always copied onto the MQTT error handle).
                constexpr int kMbedVerifyFailed = -0x2700;  // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
                constexpr int kEspHandshakeFailed =
                    static_cast<int>(ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED);
                if (flags != 0 || stack_err == kMbedVerifyFailed ||
                    esp_err == kEspHandshakeFailed) {
                    f = Fail::TlsCertRejected;
                } else if (esp_err != 0 || stack_err != 0) {
                    f = Fail::TlsOther;
                } else {
                    f = Fail::Transport;
                }
            }
            // Keep a more specific failure until the loop takes it — a follow-up
            // transport close must not erase certificate rejection.
            const Fail prev = self->fail_.load(std::memory_order_relaxed);
            const bool keep = (prev == Fail::TlsCertRejected && f != Fail::TlsCertRejected) ||
                              (prev == Fail::Auth && f != Fail::Auth && f != Fail::TlsCertRejected);
            if (!keep) {
                self->cert_flags_.store(flags, std::memory_order_relaxed);
                self->tls_esp_err_.store(esp_err, std::memory_order_relaxed);
                self->tls_stack_err_.store(stack_err, std::memory_order_relaxed);
                self->fail_.store(f, std::memory_order_relaxed);
            }
        }
        break;
    default:
        break;
    }
}

Mqtt::Fail Mqtt::take_fail(int* cert_flags, int* tls_esp_err, int* tls_stack_err)
{
    const Fail f = fail_.exchange(Fail::None, std::memory_order_relaxed);
    if (cert_flags != nullptr) {
        *cert_flags = cert_flags_.load(std::memory_order_relaxed);
    }
    if (tls_esp_err != nullptr) {
        *tls_esp_err = tls_esp_err_.load(std::memory_order_relaxed);
    }
    if (tls_stack_err != nullptr) {
        *tls_stack_err = tls_stack_err_.load(std::memory_order_relaxed);
    }
    return f;
}

bool Mqtt::begin(const p1cfg::Store& cfg, const char* host_override)
{
    stop();

    const bool tls = cfg.values().mqtt_tls;
    const char* host = (host_override != nullptr && host_override[0] != '\0')
                           ? host_override
                           : cfg.values().mqtt_host;
    if (broker_uri(host, cfg.values().mqtt_port, uri_, sizeof(uri_), tls) == 0) {
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

    fail_.store(Fail::None, std::memory_order_relaxed);
    cert_flags_.store(0, std::memory_order_relaxed);
    tls_esp_err_.store(0, std::memory_order_relaxed);
    tls_stack_err_.store(0, std::memory_order_relaxed);

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.broker.address.uri = uri_;
    if (tls) {
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
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
    mqtt_cfg.buffer.out_size = kMqttOutSize;

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
    fail_.store(Fail::None, std::memory_order_relaxed);
    cert_flags_.store(0, std::memory_order_relaxed);
    tls_esp_err_.store(0, std::memory_order_relaxed);
    tls_stack_err_.store(0, std::memory_order_relaxed);
    inbound_.reset();
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

int Mqtt::outbox_size() const
{
    auto* client = static_cast<esp_mqtt_client_handle_t>(client_);
    if (client == nullptr) {
        return 0;
    }
    return esp_mqtt_client_get_outbox_size(client);
}

bool Mqtt::subscribe(std::string_view topic)
{
    auto* client = static_cast<esp_mqtt_client_handle_t>(client_);
    if (client == nullptr || topic.empty() || topic.size() + 1 > sizeof(topic_buf_)) {
        return false;
    }
    for (std::size_t i = 0; i < topic.size(); ++i) {
        topic_buf_[i] = topic[i];
    }
    topic_buf_[topic.size()] = '\0';
    return esp_mqtt_client_subscribe(client, topic_buf_, 1) >= 0;
}

#endif  // ARDUINO

}  // namespace p1mqtt
