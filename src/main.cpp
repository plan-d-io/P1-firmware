#include <Arduino.h>
#include <ESPmDNS.h>
#include <P1Config.h>
#include <P1DSMR.h>
#include <P1Mqtt.h>
#include <P1Publish.h>
#include <WiFi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

// Pins from archive/P1-dongle/P1-dongle.ino:
//   HWSERIAL.begin(115200, SERIAL_8N1, 21, 22);
//   TRIGGER 25, HIGH for continuous telegrams.
static constexpr int kP1Rx = 21;
static constexpr int kP1Tx = 22;
static constexpr int kDataRequest = 25;

static p1dsmr::Reader g_reader;
static p1pub::Builder g_builder;
static p1mqtt::Mqtt g_mqtt;
static p1cfg::NvsBackend g_nvs;
static p1cfg::Store g_cfg{g_nvs};

static unsigned g_ok = 0;
static unsigned g_rejected = 0;
static volatile unsigned g_ovf = 0;
static volatile unsigned g_full = 0;
static volatile unsigned g_frm = 0;
static volatile bool g_got_ip = false;

static bool g_wifi_started = false;
static bool g_mdns_started = false;
static bool g_mqtt_started = false;
static bool g_ssid_empty_logged = false;
static bool g_host_empty_logged = false;
static std::uint32_t g_last_pub_ms = 0;
static std::uint32_t g_last_resolve_ms = 0;
static char g_device_id[p1mqtt::kDeviceIdCap]{};
static char g_resolved_host[p1cfg::kHostMax]{};
static char g_json[3072];
static char g_cmd[192];
static std::size_t g_cmd_n = 0;

static void read_mac(std::uint8_t mac[6])
{
    WiFi.macAddress(mac);
}

static bool host_is_local(const char* host)
{
    if (host == nullptr) {
        return false;
    }
    const std::size_t n = std::strlen(host);
    return n > 6 && std::strcmp(host + n - 6, ".local") == 0;
}

static const char* mqtt_state_name(p1mqtt::Mqtt::State s)
{
    switch (s) {
    case p1mqtt::Mqtt::State::Idle:
        return "Idle";
    case p1mqtt::Mqtt::State::Connecting:
        return "Connecting";
    case p1mqtt::Mqtt::State::Connected:
        return "Connected";
    case p1mqtt::Mqtt::State::Unsupported:
        return "Unsupported";
    }
    return "?";
}

static const char* wifi_state_name()
{
    if (g_cfg.values().wifi_ssid[0] == '\0') {
        return "idle";
    }
    if (WiFi.status() == WL_CONNECTED) {
        return "connected";
    }
    if (g_wifi_started) {
        return "connecting";
    }
    return "idle";
}

static void start_wifi()
{
    if (g_cfg.values().wifi_ssid[0] == '\0') {
        if (!g_ssid_empty_logged) {
            Serial.println("WIFI_SSID empty; not connecting");
            g_ssid_empty_logged = true;
        }
        return;
    }
    if (g_wifi_started) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(g_device_id);
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_cfg.values().wifi_ssid, g_cfg.values().wifi_pass);
    g_wifi_started = true;
    Serial.printf("WiFi begin ssid=%s hostname=%s\n", g_cfg.values().wifi_ssid, g_device_id);
}

static void ensure_mdns()
{
    if (g_mdns_started || !g_got_ip) {
        return;
    }
    if (MDNS.begin(g_device_id)) {
        g_mdns_started = true;
        Serial.printf("mDNS %s.local\n", g_device_id);
    }
}

static bool resolve_local_host(const char* host, char* out, std::size_t cap)
{
    const std::size_t n = std::strlen(host);
    if (n <= 6 || n - 6 >= p1cfg::kHostMax) {
        return false;
    }
    char short_name[p1cfg::kHostMax];
    std::memcpy(short_name, host, n - 6);
    short_name[n - 6] = '\0';
    const IPAddress ip = MDNS.queryHost(short_name, 500);
    if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
        return false;
    }
    std::snprintf(out, cap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return true;
}

static void try_start_mqtt()
{
    if (g_mqtt_started || !g_got_ip || !g_cfg.values().mqtt_en) {
        return;
    }

    if (g_cfg.values().mqtt_tls) {
        g_mqtt.begin(g_cfg);
        g_mqtt_started = true;
        return;
    }

    const char* host = g_cfg.values().mqtt_host;
    if (host[0] == '\0') {
        if (!g_host_empty_logged) {
            Serial.println("MQTT_HOST empty; not connecting");
            g_host_empty_logged = true;
        }
        return;
    }

    const char* override_host = nullptr;
    if (host_is_local(host)) {
        ensure_mdns();
        const std::uint32_t now = millis();
        if (g_resolved_host[0] == '\0') {
            if (now - g_last_resolve_ms < 5000 && g_last_resolve_ms != 0) {
                return;
            }
            g_last_resolve_ms = now;
            if (!resolve_local_host(host, g_resolved_host, sizeof(g_resolved_host))) {
                Serial.printf("mDNS resolve failed for %s\n", host);
                return;
            }
            Serial.printf("MQTT_HOST %s -> %s\n", host, g_resolved_host);
        }
        override_host = g_resolved_host;
    }

    if (g_mqtt.begin(g_cfg, override_host)) {
        g_mqtt_started = true;
        Serial.printf("MQTT connecting uri host=%s port=%u id=%s\n",
                      override_host != nullptr ? override_host : host,
                      static_cast<unsigned>(g_cfg.values().mqtt_port),
                      g_cfg.values().mqtt_id);
    }
}

static void publish_online()
{
    char topic[p1pub::kTopicMax];
    if (p1pub::availability_topic(g_cfg, topic, sizeof(topic)) == 0) {
        return;
    }
    if (g_mqtt.publish(topic, "online", true)) {
        Serial.printf("MQTT online %s\n", topic);
    }
}

static void on_telegram(const p1dsmr::Data& data)
{
    if (g_mqtt.state() != p1mqtt::Mqtt::State::Connected) {
        return;
    }
    if (!p1mqtt::throttle_due(g_cfg.values().upl_throttle, millis(), g_last_pub_ms)) {
        return;
    }

    unsigned n = 0;
    g_builder.emit(data, g_cfg, [&](std::string_view topic, std::string_view payload) {
        g_mqtt.publish(topic, payload, false);
        ++n;
    });
    g_last_pub_ms = millis();
    Serial.printf("P n=%u heap=%u fails=%u\n", n,
                  static_cast<unsigned>(ESP.getFreeHeap()), g_mqtt.publish_fails());
}

static bool parse_u32(const char* s, std::uint32_t& v)
{
    if (s == nullptr || s[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long x = std::strtoul(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    v = static_cast<std::uint32_t>(x);
    return true;
}

static bool parse_i32(const char* s, std::int32_t& v)
{
    if (s == nullptr || s[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const long x = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    v = static_cast<std::int32_t>(x);
    return true;
}

static void cmd_set(char* rest)
{
    while (*rest == ' ') {
        ++rest;
    }
    char* key = rest;
    char* value = rest;
    while (*value != '\0' && *value != ' ') {
        ++value;
    }
    if (*value == '\0') {
        Serial.println("usage: set <KEY> <value>");
        return;
    }
    *value++ = '\0';
    while (*value == ' ') {
        ++value;
    }

    const p1cfg::CfgEntry* e = g_cfg.find_setting(key);
    if (e == nullptr || (e->flags & p1cfg::CFG_IN_API) == 0) {
        Serial.printf("unknown key %s\n", key);
        return;
    }

    switch (e->type) {
    case p1cfg::CfgType::Bool: {
        bool v = false;
        if (std::strcmp(value, "true") == 0) {
            v = true;
        } else if (std::strcmp(value, "false") == 0) {
            v = false;
        } else {
            Serial.println("bool wants true or false");
            return;
        }
        *e->var.b = v;
        break;
    }
    case p1cfg::CfgType::I32: {
        std::int32_t v = 0;
        if (!parse_i32(value, v)) {
            Serial.println("invalid int");
            return;
        }
        if (std::strcmp(e->key, "FRMT_PYLD") == 0 && (v < 0 || v > 3)) {
            Serial.println("FRMT_PYLD 0..3");
            return;
        }
        *e->var.i = v;
        break;
    }
    case p1cfg::CfgType::U32: {
        std::uint32_t v = 0;
        if (!parse_u32(value, v)) {
            Serial.println("invalid uint");
            return;
        }
        if (std::strcmp(e->key, "MQTT_PORT") == 0 && (v < 1u || v > 65535u)) {
            Serial.println("MQTT_PORT 1..65535");
            return;
        }
        *e->var.u = v;
        break;
    }
    case p1cfg::CfgType::Str: {
        const std::size_t n = std::strlen(value);
        if (n + 1 > e->str_cap) {
            Serial.println("value too long");
            return;
        }
        if (std::strcmp(e->key, "REGION") == 0 && std::strcmp(value, "auto") != 0 &&
            std::strcmp(value, "be") != 0 && std::strcmp(value, "nl") != 0) {
            Serial.println("REGION auto|be|nl");
            return;
        }
        std::memcpy(e->var.s, value, n + 1);
        break;
    }
    }

    if (e->flags & p1cfg::CFG_SECRET) {
        Serial.printf("%s set\n", e->key);
    } else {
        Serial.printf("%s updated\n", e->key);
    }
}

static void cmd_status()
{
    const IPAddress ip = WiFi.localIP();
    Serial.printf(
        "wifi=%s ip=%u.%u.%u.%u mqtt=%s id=%s connects=%u disconnects=%u pub_fails=%u heap=%u\n",
        wifi_state_name(), ip[0], ip[1], ip[2], ip[3], mqtt_state_name(g_mqtt.state()),
        g_cfg.values().mqtt_id, g_mqtt.connects(), g_mqtt.disconnects(), g_mqtt.publish_fails(),
        static_cast<unsigned>(ESP.getFreeHeap()));
}

static void handle_line(char* line)
{
    while (*line == ' ' || *line == '\t') {
        ++line;
    }
    if (line[0] == '\0') {
        return;
    }
    if (std::strcmp(line, "get") == 0) {
        const std::size_t n = g_cfg.to_json(g_json, sizeof(g_json));
        if (n > 0) {
            Serial.write(reinterpret_cast<const std::uint8_t*>(g_json), n);
            Serial.write('\n');
        }
        return;
    }
    if (std::strcmp(line, "save") == 0) {
        g_cfg.save();
        Serial.println("saved");
        return;
    }
    if (std::strcmp(line, "status") == 0) {
        cmd_status();
        return;
    }
    if (std::memcmp(line, "set ", 4) == 0) {
        cmd_set(line + 4);
        return;
    }
    Serial.println("commands: set <KEY> <value> | get | save | status");
}

static void poll_serial()
{
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            g_cmd[g_cmd_n] = '\0';
            handle_line(g_cmd);
            g_cmd_n = 0;
            return;
        }
        if (g_cmd_n + 1 < sizeof(g_cmd)) {
            g_cmd[g_cmd_n++] = c;
        }
    }
}

static void on_wifi_event(arduino_event_id_t event, arduino_event_info_t /*info*/)
{
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        g_got_ip = true;
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        g_got_ip = false;
    }
}

// Increments only — this runs on uart_event_task (~2 KB stack).
static void on_uart_err(hardwareSerial_error_t err)
{
    switch (err) {
    case UART_FIFO_OVF_ERROR:
        g_ovf = g_ovf + 1;
        break;
    case UART_BUFFER_FULL_ERROR:
        g_full = g_full + 1;
        break;
    case UART_FRAME_ERROR:
        g_frm = g_frm + 1;
        break;
    default:
        break;
    }
}

void setup()
{
    Serial.begin(115200);

    pinMode(kDataRequest, OUTPUT);
    digitalWrite(kDataRequest, HIGH);

    const size_t rx = Serial1.setRxBufferSize(1024);
    Serial1.begin(115200, SERIAL_8N1, kP1Rx, kP1Tx);
    Serial1.onReceiveError(on_uart_err);

    std::uint8_t mac[6]{};
    read_mac(mac);
    p1mqtt::device_id(mac, g_device_id, sizeof(g_device_id));

    Serial.printf("P1 UART rx=%u id=%s\n", static_cast<unsigned>(rx), g_device_id);

    g_cfg.load();
    if (p1mqtt::migrate_mqtt_id(g_cfg.values().mqtt_id, sizeof(g_cfg.values().mqtt_id), mac)) {
        g_cfg.save();
        Serial.printf("MQTT_ID migrated to %s\n", g_cfg.values().mqtt_id);
    }

    Serial.printf("P1Config ver=%u mqtt_en=%d host=%s\n",
                  static_cast<unsigned>(g_cfg.values().cfg_ver),
                  static_cast<int>(g_cfg.values().mqtt_en), g_cfg.values().mqtt_host);

    WiFi.onEvent(on_wifi_event);
    start_wifi();
}

void loop()
{
    poll_serial();

    if (!g_wifi_started) {
        start_wifi();
    }
    if (WiFi.status() == WL_CONNECTED) {
        g_got_ip = true;
        ensure_mdns();
        try_start_mqtt();
    }

    if (g_mqtt.take_just_connected()) {
        publish_online();
    }

    std::uint8_t buf[64];
    const size_t n = Serial1.read(buf, sizeof(buf));
    if (n > 0) {
        const unsigned prev = g_ok + g_rejected;
        const auto stats =
            g_reader.feed(std::span<const std::uint8_t>(buf, n), on_telegram);
        g_ok += static_cast<unsigned>(stats.ok);
        g_rejected += static_cast<unsigned>(stats.rejected);

        const unsigned now = g_ok + g_rejected;
        for (unsigned k = prev + 1; k <= now; ++k) {
            if (k % 10 == 0) {
                Serial.printf("S ok=%u rejected=%u heap=%u ovf=%u full=%u frm=%u\n",
                              g_ok, g_rejected,
                              static_cast<unsigned>(ESP.getFreeHeap()),
                              static_cast<unsigned>(g_ovf),
                              static_cast<unsigned>(g_full),
                              static_cast<unsigned>(g_frm));
            }
        }
    }

    yield();
}
