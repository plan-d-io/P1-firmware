#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <P1Config.h>
#include <P1DSMR.h>
#include <P1Mqtt.h>
#include <P1Publish.h>
#include <P1Syslog.h>
#include <P1Web.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <span>
#include <string_view>

// Pins from archive/P1-dongle/P1-dongle.ino:
//   HWSERIAL.begin(115200, SERIAL_8N1, 21, 22);
//   TRIGGER 25, HIGH for continuous telegrams.
static constexpr int kP1Rx = 21;
static constexpr int kP1Tx = 22;
static constexpr int kDataRequest = 25;
// Rebuild generation. Fielded firmware was 2.23; keep this above that line.
static constexpr char kFwVer[] = "3.00";

static void request_reboot(const char* reason);

static p1dsmr::Reader g_reader;
static p1pub::Builder g_builder;
static p1mqtt::Mqtt g_mqtt;
static p1cfg::NvsBackend g_nvs;
static p1cfg::Store g_cfg{g_nvs};
static p1log::LittleFsStore g_fs;
static bool g_syslog_publish(std::string_view topic, std::string_view payload, void* /*ctx*/)
{
    return g_mqtt.publish(topic, payload, false);
}
static void g_syslog_serial(std::string_view line, void* /*ctx*/)
{
    Serial.println(line.data());
}
static void g_persist_log_mark(std::uint32_t mark, void* /*ctx*/)
{
    g_cfg.values().log_mark = mark;
    g_cfg.save();
}
static p1log::Logger g_log{g_fs, &g_syslog_publish, nullptr, &g_syslog_serial, nullptr};

static p1web::Server g_web;
static p1web::CaptiveDns g_dns;
static p1web::ProvisionMachine g_prov;
static p1web::ScanCache g_scan{};
static bool g_ap_up = false;
static bool g_scan_started = false;
static std::uint32_t g_last_scan_ms = 0;
static char g_begun_ssid[p1web::kSsidMax]{};
static char g_begun_pass[p1web::kPassMax]{};
static char g_status_ip[16]{};
static char g_status_uptime[24]{};
static char g_status_heap[16]{};
static char g_status_rssi[12]{};
static char g_status_tele[48]{};
static p1web::StatusView g_status_view{};

static unsigned g_ok = 0;
static unsigned g_rejected = 0;
static volatile unsigned g_ovf = 0;
static volatile unsigned g_full = 0;
static volatile unsigned g_frm = 0;
static volatile bool g_got_ip = false;

static bool g_wifi_started = false;
static bool g_mdns_started = false;
static bool g_mqtt_started = false;
static bool g_host_empty_logged = false;
static bool g_ntp_started = false;
static bool g_mqtt_was_connected = false;
static std::uint32_t g_last_pub_ms = 0;
static std::uint32_t g_last_resolve_ms = 0;
static char g_device_id[p1mqtt::kDeviceIdCap]{};
static std::uint8_t g_mac[6]{};
static char g_resolved_host[p1cfg::kHostMax]{};
static std::uint32_t g_last_diag_ms = 0;
static std::uint64_t g_disc_mask = 0;
static bool g_disc_have = false;
static char g_fw_reset_reason[p1cfg::kLastResetMax]{};
static const char* g_hw_reset_reason = "NO_MEAN";
static char g_json[3072];
static char g_in_topic[p1mqtt::kTopicBuf];
static char g_in_payload[p1mqtt::InboundAssembler::kMax + 1];
static char g_cmd[192];
static std::size_t g_cmd_n = 0;

static void read_mac(std::uint8_t mac[6])
{
    // eFuse, not WiFi.macAddress(): the STA interface still reports zeros
    // until it is brought up, and the id is needed before that.
    std::memset(mac, 0, 6);
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        std::memset(mac, 0, 6);
    }
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

static void start_sta()
{
    if (g_cfg.values().wifi_ssid[0] == '\0') {
        return;
    }
    const bool same = g_wifi_started &&
                      std::strcmp(g_begun_ssid, g_cfg.values().wifi_ssid) == 0 &&
                      std::strcmp(g_begun_pass, g_cfg.values().wifi_pass) == 0;
    if (same && !g_ap_up) {
        return;
    }
    if (g_device_id[0] != '\0') {
        WiFi.setHostname(g_device_id);
    }
    WiFi.setAutoReconnect(true);
    if (g_wifi_started) {
        WiFi.disconnect(false, false);
    }
    WiFi.begin(g_cfg.values().wifi_ssid, g_cfg.values().wifi_pass);
    std::snprintf(g_begun_ssid, sizeof(g_begun_ssid), "%s", g_cfg.values().wifi_ssid);
    std::snprintf(g_begun_pass, sizeof(g_begun_pass), "%s", g_cfg.values().wifi_pass);
    g_wifi_started = true;
    char msg[96];
    std::snprintf(msg, sizeof(msg), "WiFi begin ssid=%s hostname=%s", g_cfg.values().wifi_ssid,
                  g_device_id);
    g_log.log(p1log::Level::Info, msg);
}

static void start_ap()
{
    if (g_ap_up) {
        return;
    }
    // AP+STA so a timed-out connect can keep trying in the background.
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(p1web::kApSsid)) {
        g_log.log(p1log::Level::Error, "SoftAP start failed");
        return;
    }
    g_ap_up = true;
    const IPAddress ip = WiFi.softAPIP();
    char msg[64];
    std::snprintf(msg, sizeof(msg), "SoftAP %s %u.%u.%u.%u", p1web::kApSsid, ip[0], ip[1], ip[2],
                  ip[3]);
    g_log.log(p1log::Level::Info, msg);
    const std::uint32_t host =
        (static_cast<std::uint32_t>(ip[0]) << 24) | (static_cast<std::uint32_t>(ip[1]) << 16) |
        (static_cast<std::uint32_t>(ip[2]) << 8) | static_cast<std::uint32_t>(ip[3]);
    if (!g_dns.begin(host)) {
        g_log.log(p1log::Level::Warn, "Captive DNS failed");
    }
    if (!g_web.running()) {
        if (g_web.begin(p1web::Mode::Ap)) {
            g_log.log(p1log::Level::Info, "HTTP server started (AP)");
        } else {
            g_log.log(p1log::Level::Error, "HTTP server start failed");
        }
    } else {
        g_web.set_mode(p1web::Mode::Ap);
    }
    g_web.set_scan(&g_scan);
    g_last_scan_ms = 0;
    g_scan_started = false;
}

static void stop_ap()
{
    if (!g_ap_up) {
        return;
    }
    g_dns.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    g_ap_up = false;
    g_log.log(p1log::Level::Info, "SoftAP stopped");
    if (g_web.running()) {
        g_web.set_mode(p1web::Mode::Station);
    }
}

static void ensure_web_station()
{
    if (!g_web.running()) {
        if (g_web.begin(p1web::Mode::Station)) {
            g_log.log(p1log::Level::Info, "HTTP server started (station)");
        } else {
            g_log.log(p1log::Level::Error, "HTTP server start failed");
        }
    } else {
        g_web.set_mode(p1web::Mode::Station);
    }
    g_web.set_status(&g_status_view);
}

static void refresh_status_view()
{
    const IPAddress ip = WiFi.localIP();
    std::snprintf(g_status_ip, sizeof(g_status_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    const std::uint32_t sec = millis() / 1000u;
    std::snprintf(g_status_uptime, sizeof(g_status_uptime), "%lu s",
                  static_cast<unsigned long>(sec));
    std::snprintf(g_status_heap, sizeof(g_status_heap), "%u",
                  static_cast<unsigned>(ESP.getFreeHeap()));
    std::snprintf(g_status_rssi, sizeof(g_status_rssi), "%ld", static_cast<long>(WiFi.RSSI()));
    std::snprintf(g_status_tele, sizeof(g_status_tele), "ok=%u rejected=%u", g_ok, g_rejected);
    g_status_view.device_id = g_device_id;
    g_status_view.fw_ver = kFwVer;
    g_status_view.ip = g_status_ip;
    g_status_view.uptime = g_status_uptime;
    g_status_view.heap = g_status_heap;
    g_status_view.rssi = g_status_rssi;
    g_status_view.mqtt = mqtt_state_name(g_mqtt.state());
    g_status_view.telegrams = g_status_tele;
}

static void poll_wifi_scan()
{
    if (!g_ap_up) {
        return;
    }
    const std::int16_t done = WiFi.scanComplete();
    if (done >= 0) {
        g_scan.count = 0;
        for (std::int16_t i = 0; i < done && g_scan.count < p1web::kMaxScanResults; ++i) {
            p1web::ScanEntry& e = g_scan.entries[g_scan.count];
            WiFi.SSID(i).toCharArray(e.ssid, sizeof(e.ssid));
            if (e.ssid[0] == '\0') {
                continue;
            }
            e.rssi = WiFi.RSSI(i);
            ++g_scan.count;
        }
        WiFi.scanDelete();
        p1web::sort_scan_strongest_first(g_scan);
        g_scan.ready = true;
        g_scan_started = false;
        g_last_scan_ms = millis();
        return;
    }
    if (done == -1) {
        return;  // still scanning
    }
    // -2 = not started / idle
    if (!g_scan_started && (g_last_scan_ms == 0 || millis() - g_last_scan_ms > 15000u)) {
        if (WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING) {
            g_scan_started = true;
        }
    }
}

static void handle_web_actions()
{
    p1web::Actions* act = g_web.actions();
    if (act == nullptr) {
        return;
    }
    if (act->save_wifi) {
        act->save_wifi = false;
        char json[192];
        if (p1web::wifi_creds_json(act->wifi_ssid, act->wifi_pass, json, sizeof(json)) == 0) {
            g_log.log(p1log::Level::Error, "WiFi save JSON overflow");
            act->reboot = false;
            return;
        }
        const p1cfg::JsonStatus st = g_cfg.apply_json(json);
        if (st != p1cfg::JsonStatus::Ok) {
            g_log.log(p1log::Level::Warn, "WiFi save rejected");
            act->reboot = false;
            return;
        }
        g_cfg.save();
        g_log.log(p1log::Level::Info, "WiFi credentials saved from setup page");
    }
    if (act->reboot) {
        act->reboot = false;
        request_reboot("Reboot requested from web UI");
    }
}

static void service_provisioning()
{
    p1web::ProvInput in{};
    in.have_ssid = g_cfg.values().wifi_ssid[0] != '\0';
    in.sta_connected = WiFi.status() == WL_CONNECTED;
    in.now_ms = millis();
    const p1web::ProvOutput out = g_prov.tick(in);

    if (out.want_ap && !g_ap_up) {
        start_ap();
    } else if (!out.want_ap && g_ap_up) {
        stop_ap();
    }

    if (out.want_sta) {
        start_sta();
    }

    if (out.phase == p1web::ProvPhase::Station) {
        ensure_web_station();
        refresh_status_view();
    }

    if (g_ap_up) {
        g_dns.loop();
        poll_wifi_scan();
        g_web.set_mode(p1web::Mode::Ap);
        g_web.set_scan(&g_scan);
    }

    handle_web_actions();
}

static void start_wifi()
{
    // Legacy name: provisioning state machine owns AP/STA now.
    service_provisioning();
}

static void ensure_mdns()
{
    if (g_mdns_started || !g_got_ip || g_device_id[0] == '\0') {
        return;
    }
    if (MDNS.begin(g_device_id)) {
        g_mdns_started = true;
        char msg[48];
        std::snprintf(msg, sizeof(msg), "mDNS %s.local", g_device_id);
        g_log.log(p1log::Level::Info, msg);
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
            g_log.log(p1log::Level::Warn, "MQTT_HOST empty; not connecting");
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

static const char* json_status_name(p1cfg::JsonStatus s)
{
    switch (s) {
    case p1cfg::JsonStatus::Ok:
        return "Ok";
    case p1cfg::JsonStatus::Parse:
        return "Parse";
    case p1cfg::JsonStatus::UnknownKey:
        return "UnknownKey";
    case p1cfg::JsonStatus::WrongType:
        return "WrongType";
    case p1cfg::JsonStatus::Invalid:
        return "Invalid";
    }
    return "?";
}

static bool fill_availability(char* out, std::size_t cap)
{
    return p1pub::availability_topic(g_cfg, out, cap) > 0;
}

static void publish_online()
{
    char topic[p1pub::kTopicMax];
    if (!fill_availability(topic, sizeof(topic))) {
        return;
    }
    if (g_mqtt.publish(topic, "online", true)) {
        Serial.printf("MQTT online %s\n", topic);
    }
}

static void subscribe_inbound()
{
    char avail[p1pub::kTopicMax];
    char topic[p1mqtt::kTopicBuf];
    if (!fill_availability(avail, sizeof(avail))) {
        return;
    }
    if (p1mqtt::inbound_topic(avail, p1mqtt::Inbound::Reboot, topic, sizeof(topic)) > 0) {
        g_mqtt.subscribe(topic);
    }
    if (p1mqtt::inbound_topic(avail, p1mqtt::Inbound::Config, topic, sizeof(topic)) > 0) {
        g_mqtt.subscribe(topic);
    }
    if (g_cfg.values().ha_en) {
        g_mqtt.subscribe(p1mqtt::kHaStatusTopic);
    }
}

static void publish_reboot_state(const char* on_off)
{
    char topic[p1mqtt::kTopicBuf];
    if (g_device_id[0] == '\0' ||
        p1mqtt::sys_topic(g_device_id, "reboot", topic, sizeof(topic)) == 0) {
        return;
    }
    char payload[24];
    std::snprintf(payload, sizeof(payload), "{\"value\":\"%s\"}", on_off);
    g_mqtt.publish(topic, payload, true);
}

static void publish_config_snapshot()
{
    char avail[p1pub::kTopicMax];
    char topic[p1mqtt::kTopicBuf];
    if (!fill_availability(avail, sizeof(avail))) {
        return;
    }
    if (p1mqtt::config_snapshot_topic(avail, topic, sizeof(topic)) == 0) {
        return;
    }
    const std::size_t n =
        p1mqtt::config_snapshot(g_cfg, g_device_id, kFwVer, g_json, sizeof(g_json));
    if (n == 0) {
        return;
    }
    g_mqtt.publish(topic, std::string_view(g_json, n), true);
}

static void publish_one_diag(const char* name, const char* friendly, const char* value_json)
{
    char topic[p1mqtt::kTopicBuf];
    char payload[256];
    if (g_device_id[0] == '\0' ||
        p1mqtt::sys_topic(g_device_id, name, topic, sizeof(topic)) == 0) {
        return;
    }
    const std::uint64_t ts = p1log::time_valid() ? p1log::unix_now() : 0;
    if (p1mqtt::diag_payload(friendly, value_json, g_device_id, name, payload, sizeof(payload),
                             ts) == 0) {
        return;
    }
    g_mqtt.publish(topic, payload, true);
}

static void publish_diagnostics()
{
    char num[24];
    char quoted[96];

    std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(ESP.getFreeHeap()));
    publish_one_diag("free_heap_size", "Free heap size", num);

    std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    publish_one_diag("max_allocatable_block", "Allocatable block size", num);

    std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(ESP.getMinFreeHeap()));
    publish_one_diag("min_free_heap", "Lowest free heap size", num);

    const IPAddress ip = WiFi.localIP();
    std::snprintf(quoted, sizeof(quoted), "\"%u.%u.%u.%u\"", ip[0], ip[1], ip[2], ip[3]);
    publish_one_diag("ip", "IP", quoted);

    std::snprintf(quoted, sizeof(quoted), "\"%s\"", kFwVer);
    publish_one_diag("firmware", "Firmware", quoted);

    std::snprintf(num, sizeof(num), "%ld", static_cast<long>(WiFi.RSSI()));
    publish_one_diag("rssi", "RSSI", num);

    std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(g_cfg.values().reboots));
    publish_one_diag("reboots", "Reboots", num);

    std::snprintf(quoted, sizeof(quoted), "\"%s\"", g_hw_reset_reason);
    publish_one_diag("last_reset_reason_hw", "Last reset reason (hardware)", quoted);

    std::snprintf(quoted, sizeof(quoted), "\"%s\"", g_fw_reset_reason);
    publish_one_diag("last_reset_reason_fw", "Last reset reason (firmware)", quoted);

    publish_one_diag("reboot", "Reboot", "\"off\"");
}

static void request_reboot(const char* reason)
{
    if (reason != nullptr) {
        std::snprintf(g_cfg.values().last_reset, sizeof(g_cfg.values().last_reset), "%s", reason);
    } else {
        g_cfg.values().last_reset[0] = '\0';
    }
    g_cfg.save();
    char msg[96];
    std::snprintf(msg, sizeof(msg), "Reboot requested: %s", reason != nullptr ? reason : "");
    g_log.log(p1log::Level::Info, msg);
    g_log.flush_repeat();
    esp_restart();
}

static bool publish_retained(std::string_view topic, std::string_view payload)
{
    return g_mqtt.publish(topic, payload, true);
}

static bool publish_discovery_buf(std::string_view topic, const char* buf, std::size_t n)
{
    if (n == 0) {
        g_log.log(p1log::Level::Error, "HA discovery overflow");
        return false;
    }
    if (!publish_retained(topic, std::string_view(buf, n))) {
        g_log.log(p1log::Level::Error, "HA discovery publish failed");
        return false;
    }
    char msg[96];
    std::snprintf(msg, sizeof(msg), "HA discovery %.*s n=%u", static_cast<int>(topic.size()),
                  topic.data(), static_cast<unsigned>(n));
    g_log.log(p1log::Level::Info, msg);
    return true;
}

static bool publish_ha_devices(std::uint64_t include_mask, std::uint64_t stub_mask)
{
    if (!g_cfg.values().ha_en || g_mqtt.state() != p1mqtt::Mqtt::State::Connected) {
        return false;
    }
    char* buf = new (std::nothrow) char[p1pub::kDiscoveryCap];
    if (buf == nullptr) {
        g_log.log(p1log::Level::Error, "HA discovery alloc failed");
        return false;
    }

    bool ok = true;
    char topic[p1pub::kTopicMax];
    char slug_id[p1pub::kUniqueIdMax];
    char config_url[32];
    const IPAddress ip = WiFi.localIP();
    std::snprintf(config_url, sizeof(config_url), "http://%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    if (p1pub::slug(g_cfg.values().ha_device, slug_id, sizeof(slug_id)) > 0 &&
        p1pub::device_discovery_topic(slug_id, topic, sizeof(topic)) > 0) {
        const std::size_t n =
            p1pub::meter_discovery(g_cfg, kFwVer, config_url, include_mask, stub_mask, buf,
                                   p1pub::kDiscoveryCap);
        ok = publish_discovery_buf(topic, buf, n) && ok;
    } else {
        ok = false;
    }

    if (g_device_id[0] != '\0' &&
        p1pub::device_discovery_topic(g_device_id, topic, sizeof(topic)) > 0) {
        const std::size_t n = p1pub::dongle_discovery(g_cfg, g_device_id, kFwVer, config_url, buf,
                                                     p1pub::kDiscoveryCap);
        ok = publish_discovery_buf(topic, buf, n) && ok;
    } else {
        ok = false;
    }

    delete[] buf;
    return ok;
}

static void for_legacy_pub(std::string_view topic, void* ctx)
{
    const char* payload = static_cast<const char*>(ctx);
    g_mqtt.publish(topic, payload != nullptr ? std::string_view(payload) : std::string_view{},
                   true);
}

static void run_ha_migration()
{
    char legacy[p1mqtt::kDeviceIdCap];
    p1mqtt::legacy_device_id(g_mac, legacy, sizeof(legacy));

    g_log.log(p1log::Level::Info, "HA migrate step 1");
    p1pub::for_each_legacy_discovery_topic(g_cfg, g_device_id, legacy, &for_legacy_pub,
                                           const_cast<char*>(p1pub::kMigrateDiscovery));

    g_log.log(p1log::Level::Info, "HA migrate step 2");
    if (!publish_ha_devices(g_disc_mask, 0)) {
        g_log.log(p1log::Level::Error, "HA migrate step 2 failed; not clearing old topics");
        return;
    }

    g_log.log(p1log::Level::Info, "HA migrate step 3");
    p1pub::for_each_legacy_discovery_topic(g_cfg, g_device_id, legacy, &for_legacy_pub, nullptr);
    g_cfg.set_ha_migrated();
    g_log.log(p1log::Level::Info, "HA migrate done");
}

static void sync_ha_identity(bool republish);
static void restart_mqtt();

static void ha_on_field_set(std::uint64_t next)
{
    if (!g_cfg.values().ha_en || g_mqtt.state() != p1mqtt::Mqtt::State::Connected) {
        g_disc_mask = next;
        g_disc_have = true;
        return;
    }
    if (!g_disc_have) {
        g_disc_mask = next;
        g_disc_have = true;
        if (!g_cfg.ha_migrated()) {
            run_ha_migration();
        }
        sync_ha_identity(true);
        return;
    }
    if (next == g_disc_mask) {
        return;
    }
    const std::uint64_t removed = g_disc_mask & ~next;
    if (removed != 0) {
        publish_ha_devices(next, removed);
    }
    g_disc_mask = next;
    publish_ha_devices(g_disc_mask, 0);
}

static void ha_clear_topic(std::string_view topic, void* /*ctx*/)
{
    g_mqtt.publish(topic, std::string_view{}, true);
    char msg[96];
    std::snprintf(msg, sizeof(msg), "HA clear %.*s", static_cast<int>(topic.size()), topic.data());
    g_log.log(p1log::Level::Info, msg);
}

static void sync_ha_identity(bool republish)
{
    if (!g_cfg.values().ha_en || g_mqtt.state() != p1mqtt::Mqtt::State::Connected) {
        return;
    }
    char cur_slug[p1pub::kUniqueIdMax];
    if (p1pub::slug(g_cfg.values().ha_device, cur_slug, sizeof(cur_slug)) == 0) {
        return;
    }
    const char* last_slug = g_cfg.has_published_ha_identity() ? g_cfg.published_ha_slug() : "";
    const char* last_pfix = g_cfg.has_published_ha_identity() ? g_cfg.published_mqtt_pfix() : "";
    const std::size_t n = p1pub::identity_clear_topics(
        last_slug, last_pfix, cur_slug, g_cfg.values().mqtt_pfix, &ha_clear_topic, nullptr);
    if (!g_disc_have) {
        return;
    }
    if (n > 0 || republish || !g_cfg.has_published_ha_identity()) {
        if (!publish_ha_devices(g_disc_mask, 0)) {
            return;
        }
    }
    if (n > 0 || !g_cfg.has_published_ha_identity()) {
        g_cfg.set_published_ha_identity(cur_slug, g_cfg.values().mqtt_pfix);
    }
}

static void restart_mqtt()
{
    g_mqtt.stop();
    g_mqtt_started = false;
    try_start_mqtt();
}

static void on_just_connected()
{
    g_log.set_mqtt_ready(true);
    // Replay runs one line per loop via replay_undelivered() — not in a burst here.
    subscribe_inbound();
    publish_online();
    publish_reboot_state("off");
    publish_config_snapshot();
    g_log.log(p1log::Level::Info, "MQTT connected");
    if (g_cfg.values().ha_en && g_disc_have) {
        if (!g_cfg.ha_migrated()) {
            run_ha_migration();
        }
        sync_ha_identity(true);
    }
}

static void handle_inbound()
{
    if (!g_mqtt.take_inbound(g_in_topic, sizeof(g_in_topic), g_in_payload, sizeof(g_in_payload))) {
        return;
    }

    char avail[p1pub::kTopicMax];
    if (!fill_availability(avail, sizeof(avail))) {
        return;
    }

    const p1mqtt::Inbound kind = p1mqtt::match_inbound(avail, g_in_topic);
    if (kind == p1mqtt::Inbound::Reboot) {
        if (!p1mqtt::reboot_command(g_in_payload)) {
            return;
        }
        g_cfg.save();
        publish_reboot_state("on");
        request_reboot("Reboot requested by MQTT");
    }

    if (kind == p1mqtt::Inbound::Config) {
        // WIFI_SSID and WIFI_PASS are writable here, same as the future web UI.
        // A wrong value leaves the dongle on a network that does not exist,
        // reachable only over USB — which is why the serial console stays.
        char old_pfix[p1cfg::kPrefixMax];
        char old_dev[p1cfg::kHaMax];
        std::snprintf(old_pfix, sizeof(old_pfix), "%s", g_cfg.values().mqtt_pfix);
        std::snprintf(old_dev, sizeof(old_dev), "%s", g_cfg.values().ha_device);
        const p1cfg::JsonStatus st = g_cfg.apply_json(g_in_payload);
        if (st != p1cfg::JsonStatus::Ok) {
            char msg[64];
            std::snprintf(msg, sizeof(msg), "MQTT config rejected %s", json_status_name(st));
            g_log.log(p1log::Level::Warn, msg);
            return;
        }
        g_cfg.save();
        publish_config_snapshot();
        g_log.log(p1log::Level::Info, "MQTT config applied");
        if (std::strcmp(old_pfix, g_cfg.values().mqtt_pfix) != 0) {
            restart_mqtt();
            return;
        }
        if (g_cfg.values().ha_en) {
            g_mqtt.subscribe(p1mqtt::kHaStatusTopic);
            if (std::strcmp(old_dev, g_cfg.values().ha_device) != 0) {
                sync_ha_identity(false);
            } else if (g_disc_have) {
                ha_on_field_set(g_disc_mask);
            }
        }
        return;
    }

    if (kind == p1mqtt::Inbound::HaStatus) {
        if (std::strcmp(g_in_payload, "online") != 0) {
            return;
        }
        g_log.log(p1log::Level::Info, "HA status online");
        if (g_disc_have) {
            publish_ha_devices(g_disc_mask, 0);
        }
    }
}

static void on_telegram(const p1dsmr::Data& data)
{
    if (g_cfg.values().ha_en) {
        const std::uint64_t next = p1pub::discovery_field_mask(data, g_cfg);
        if (g_mqtt.state() == p1mqtt::Mqtt::State::Connected) {
            ha_on_field_set(next);
        } else {
            g_disc_mask = next;
            g_disc_have = true;
        }
    }

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
    char msg[64];
    std::snprintf(msg, sizeof(msg), "P n=%u heap=%u fails=%u", n,
                  static_cast<unsigned>(ESP.getFreeHeap()), g_mqtt.publish_fails());
    g_log.log(p1log::Level::Debug, msg);
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

    if (std::strcmp(e->key, "MQTT_PFIX") == 0) {
        restart_mqtt();
    } else if (std::strcmp(e->key, "HA_DEVICE") == 0) {
        sync_ha_identity(false);
    }
}

static void cmd_status()
{
    const IPAddress ip = WiFi.localIP();
    Serial.printf(
        "wifi=%s ip=%u.%u.%u.%u mqtt=%s id=%s connects=%u disconnects=%u pub_fails=%u "
        "in_drops=%u heap=%u\n",
        wifi_state_name(), ip[0], ip[1], ip[2], ip[3], mqtt_state_name(g_mqtt.state()),
        g_cfg.values().mqtt_id, g_mqtt.connects(), g_mqtt.disconnects(), g_mqtt.publish_fails(),
        g_mqtt.inbound_drops(), static_cast<unsigned>(ESP.getFreeHeap()));
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
        g_log.log(p1log::Level::Info, "Config saved");
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
    if (std::memcmp(line, "field ", 6) == 0) {
        char* rest = line + 6;
        while (*rest == ' ') {
            ++rest;
        }
        char* id = rest;
        char* val = rest;
        while (*val != '\0' && *val != ' ') {
            ++val;
        }
        if (*val == '\0') {
            Serial.println("usage: field <id> on|off");
            return;
        }
        *val++ = '\0';
        while (*val == ' ') {
            ++val;
        }
        bool on = false;
        if (std::strcmp(val, "on") == 0) {
            on = true;
        } else if (std::strcmp(val, "off") == 0) {
            on = false;
        } else {
            Serial.println("field wants on or off");
            return;
        }
        if (p1cfg::find_field(id) == nullptr) {
            Serial.printf("unknown field %s\n", id);
            return;
        }
        g_cfg.set_field_on(id, on);
        g_cfg.save();
        Serial.printf("field %s %s\n", id, on ? "on" : "off");
        if (g_disc_have) {
            std::uint64_t next = g_disc_mask;
            const p1cfg::Field* all = p1cfg::catalogue();
            for (std::size_t i = 0; i < p1cfg::catalogue_size(); ++i) {
                if (std::strcmp(all[i].id, id) == 0) {
                    const std::uint64_t bit = std::uint64_t{1} << i;
                    if (on) {
                        next |= bit;
                    } else {
                        next &= ~bit;
                    }
                    break;
                }
            }
            ha_on_field_set(next);
        }
        return;
    }
    if (std::memcmp(line, "syslog", 6) == 0) {
        const char* rest = line + 6;
        while (*rest == ' ') {
            ++rest;
        }
        if (*rest == '\0' || std::strcmp(rest, "dump") == 0) {
            const std::size_t n = g_fs.size(p1log::kActivePath);
            Serial.printf("--- %s %u bytes mark=%u ---\n", p1log::kActivePath,
                          static_cast<unsigned>(n),
                          static_cast<unsigned>(g_cfg.values().log_mark));
            char buf[128];
            std::size_t off = 0;
            while (off < n) {
                const std::size_t got = g_fs.read(p1log::kActivePath, off, buf, sizeof(buf) - 1);
                if (got == 0) {
                    break;
                }
                Serial.write(reinterpret_cast<const std::uint8_t*>(buf), got);
                off += got;
            }
            Serial.println("--- end ---");
            return;
        }
        if (std::strcmp(rest, "prev") == 0) {
            const std::size_t n = g_fs.size(p1log::kPrevPath);
            Serial.printf("--- %s %u bytes ---\n", p1log::kPrevPath, static_cast<unsigned>(n));
            char buf[128];
            std::size_t off = 0;
            while (off < n) {
                const std::size_t got = g_fs.read(p1log::kPrevPath, off, buf, sizeof(buf) - 1);
                if (got == 0) {
                    break;
                }
                Serial.write(reinterpret_cast<const std::uint8_t*>(buf), got);
                off += got;
            }
            Serial.println("--- end ---");
            return;
        }
        if (std::strcmp(rest, "fill") == 0) {
            // Force rotation for hardware check 7. ~80 B/line → ~450 lines past 32 KB.
            for (unsigned i = 0; i < 500; ++i) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "fill line %u for rotation test", i);
                g_log.log(p1log::Level::Info, msg);
            }
            g_log.flush_repeat();
            Serial.printf("syslog fill done rotations=%u active=%u prev=%u\n",
                          static_cast<unsigned>(g_log.rotations()),
                          static_cast<unsigned>(g_fs.size(p1log::kActivePath)),
                          static_cast<unsigned>(g_fs.size(p1log::kPrevPath)));
            return;
        }
        Serial.println("usage: syslog | syslog dump | syslog prev | syslog fill");
        return;
    }
    Serial.println(
        "commands: set <KEY> <value> | get | save | status | field <id> on|off | syslog");
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
    std::memcpy(g_mac, mac, 6);
    if (!p1mqtt::mac_usable(mac)) {
        g_device_id[0] = '\0';
    } else {
        p1mqtt::device_id(mac, g_device_id, sizeof(g_device_id));
    }
    g_log.set_device_id(g_device_id);

    g_hw_reset_reason = p1log::hw_reset_reason_string(static_cast<int>(esp_reset_reason()));

    bool formatted = false;
    if (!g_fs.begin(&formatted)) {
        Serial.println("ERROR filesystem mount failed");
    } else {
        if (formatted) {
            g_log.log(p1log::Level::Error, "Filesystem formatted on mount failure");
        }
        if (!g_fs.self_test()) {
            g_log.log(p1log::Level::Error, "Filesystem self-test failed");
        } else {
            g_log.log(p1log::Level::Info, "Filesystem mounted");
        }
    }

    g_cfg.load();
    g_log.set_mark_persist(&g_persist_log_mark, nullptr);
    g_log.set_log_mark(g_cfg.values().log_mark);
    // Capture last deliberate reason for this boot, then clear NVS so a later
    // unexpected restart shows empty.
    std::snprintf(g_fw_reset_reason, sizeof(g_fw_reset_reason), "%s", g_cfg.values().last_reset);
    g_cfg.values().last_reset[0] = '\0';
    g_cfg.values().reboots += 1;
    g_cfg.save();

    if (p1mqtt::mac_usable(mac) &&
        p1mqtt::migrate_mqtt_id(g_cfg.values().mqtt_id, sizeof(g_cfg.values().mqtt_id), mac)) {
        g_cfg.save();
        char msg[48];
        std::snprintf(msg, sizeof(msg), "MQTT_ID migrated to %s", g_cfg.values().mqtt_id);
        g_log.log(p1log::Level::Info, msg);
    }

    char boot[96];
    std::snprintf(boot, sizeof(boot), "Boot fw=%s id=%s reboots=%lu uart_rx=%u", kFwVer,
                  g_device_id, static_cast<unsigned long>(g_cfg.values().reboots),
                  static_cast<unsigned>(rx));
    g_log.log(p1log::Level::Info, boot);
    char reasons[128];
    std::snprintf(reasons, sizeof(reasons), "Reset hw=%s fw=%s", g_hw_reset_reason,
                  g_fw_reset_reason[0] != '\0' ? g_fw_reset_reason : "(none)");
    g_log.log(p1log::Level::Info, reasons);

    WiFi.onEvent(on_wifi_event);
    start_wifi();
}

void loop()
{
    poll_serial();
    service_provisioning();

    if (WiFi.status() == WL_CONNECTED) {
        if (!g_got_ip) {
            g_got_ip = true;
        }
        if (!g_ntp_started) {
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            g_ntp_started = true;
            g_log.log(p1log::Level::Info, "NTP started");
            const IPAddress ip = WiFi.localIP();
            char msg[64];
            std::snprintf(msg, sizeof(msg), "WiFi connected %u.%u.%u.%u", ip[0], ip[1], ip[2],
                          ip[3]);
            g_log.log(p1log::Level::Info, msg);
        }
        ensure_mdns();
        try_start_mqtt();
    }

    const bool mqtt_up = g_mqtt.state() == p1mqtt::Mqtt::State::Connected;
    if (g_mqtt_was_connected && !mqtt_up) {
        g_log.set_mqtt_ready(false);
        g_log.log(p1log::Level::Error, "MQTT connection lost");
    }
    g_mqtt_was_connected = mqtt_up;

    if (g_mqtt.take_just_connected()) {
        on_just_connected();
    }
    handle_inbound();

    if (mqtt_up) {
        g_log.replay_undelivered();
    }

    if (mqtt_up && p1mqtt::interval_due(p1mqtt::kDiagIntervalMs, millis(), g_last_diag_ms)) {
        publish_diagnostics();
        g_last_diag_ms = millis();
    }

    std::uint8_t buf[64];
    const size_t n = Serial1.read(buf, sizeof(buf));
    if (n > 0) {
        const unsigned prev = g_ok + g_rejected;
        const auto stats =
            g_reader.feed(std::span<const std::uint8_t>(buf, n), on_telegram);
        g_ok += static_cast<unsigned>(stats.ok);
        g_rejected += static_cast<unsigned>(stats.rejected);
        if (stats.rejected > 0) {
            g_log.log(p1log::Level::Warn, "Telegram rejected (CRC)");
        }

        const unsigned now = g_ok + g_rejected;
        for (unsigned k = prev + 1; k <= now; ++k) {
            if (k % 10 == 0) {
                char msg[96];
                std::snprintf(msg, sizeof(msg),
                              "S ok=%u rejected=%u heap=%u ovf=%u full=%u frm=%u", g_ok, g_rejected,
                              static_cast<unsigned>(ESP.getFreeHeap()),
                              static_cast<unsigned>(g_ovf), static_cast<unsigned>(g_full),
                              static_cast<unsigned>(g_frm));
                g_log.log(p1log::Level::Debug, msg);
            }
        }
    }

    yield();
}
