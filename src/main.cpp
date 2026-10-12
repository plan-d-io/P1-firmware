#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <P1Button.h>
#include <P1Config.h>
#include <P1DSMR.h>
#include <P1Led.h>
#include <P1Mqtt.h>
#include <P1Ota.h>
#include <P1Publish.h>
#include <P1Syslog.h>
#include <P1Web.h>
#include <Update.h>
#include <WiFi.h>
#include <cerrno>
#include <esp_crt_bundle.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp32-hal-rgb-led.h>
#include <fcntl.h>
#include <fw_version.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <span>
#include <string_view>

// Match the C-linkage weak hook in cores/esp32/esp32-hal-misc.c.
extern "C" bool verifyRollbackLater()
{
    return true;
}

// Pins from archive/P1-dongle/P1-dongle.ino:
//   HWSERIAL.begin(115200, SERIAL_8N1, 21, 22);
//   TRIGGER 25, HIGH for continuous telegrams.
// Face button: M5Atom.h Button Btn = Button(39, true, 10) — GPIO 39, active low.
static constexpr int kP1Rx = 21;
static constexpr int kP1Tx = 22;
static constexpr int kDataRequest = 25;
static constexpr int kBtn = 39;
// Single SK6812 on the Atom Lite (archive/P1-LoRa-receiver/ledControl.h).
static constexpr int kLedPin = 27;
// ~25% brightness; blue kept at the legacy whitish-looking Atom value.
struct LedRgb {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};
static constexpr LedRgb kLedBlue{0x00, 0x00, 0x40};
static constexpr LedRgb kLedGreen{0x00, 0x40, 0x00};
static constexpr LedRgb kLedRed{0x40, 0x00, 0x00};
static constexpr LedRgb kLedOff{0x00, 0x00, 0x00};
// Display / integer version: include/fw_version.h — bump both together.

static void request_reboot(const char* reason);
static void perform_factory_reset();
static void publish_config_snapshot();
static void restart_mqtt();
static bool ota_check_now();
static void ota_install_now();
static void ota_on_schedule();
static bool ota_auto_should_install(unsigned found);
static void remember_ota_dest();
static void write_ota_note(unsigned ver);

static p1dsmr::Reader g_reader;
static p1pub::Builder g_builder;
static p1mqtt::Mqtt g_mqtt;
static p1cfg::NvsBackend g_nvs;
static p1cfg::NvsBackend g_legacy_nvs{"cofy-config"};
static p1cfg::Store g_cfg{g_nvs, &g_legacy_nvs};
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
static bool g_sta_joining = false;
static bool g_scan_started = false;
static bool g_want_sta_scan = false;
static std::uint32_t g_last_scan_ms = 0;
static bool g_force_dhcp_this_boot = false;
static bool g_using_static_sta = false;
static constexpr std::uint32_t kGwReachTimeoutMs = 15000;
enum class GwCheck : std::uint8_t { Idle, Pending, Done };
static GwCheck g_gw_check = GwCheck::Idle;
static int g_gw_sock = -1;
static std::uint32_t g_gw_check_started_ms = 0;
static char g_status_ip[16]{};
static char g_status_uptime[24]{};
static char g_status_heap[16]{};
static char g_status_rssi[12]{};
static char g_status_tele[48]{};
static char g_mqtt_err[48]{};
static bool g_upd_checked = false;
static bool g_upd_available = false;
static unsigned g_upd_found = 0;
static char g_upd_channel[p1cfg::kRelChanMax]{};
static char g_upd_fail_msg[64]{};
static char g_ota_dest_label[p1cfg::kOtaPartMax]{};
static bool g_on_trial = false;
static bool g_trial_confirmed = false;
static bool g_trial_reach_seen = false;
static std::uint32_t g_trial_reach_ms = 0;
static constexpr std::uint32_t kTrialConfirmMs = 60000;

static void clear_ota_check_result()
{
    g_upd_checked = false;
    g_upd_available = false;
    g_upd_found = 0;
    g_upd_channel[0] = '\0';
}
static std::uint32_t g_last_ota_check_ms = 0;
static bool g_ota_boot_check_done = false;
static constexpr std::uint32_t kOtaFirstDelayMs = 3UL * 60UL * 1000UL;
static constexpr std::uint32_t kOtaIntervalMs = 24UL * 60UL * 60UL * 1000UL;
static p1web::StatusView g_status_view{};
static p1web::FieldReading g_last_readings[p1cfg::kMaxFields]{};
static p1web::ReadingsView g_readings_view{g_last_readings, 0};

static unsigned g_ok = 0;
static unsigned g_rejected = 0;
static std::uint32_t g_last_telegram_ms = 0;
static bool g_hold_p1 = false;  // desk: pause telegram handling without unplugging
static volatile unsigned g_ovf = 0;
static volatile unsigned g_full = 0;
static volatile unsigned g_frm = 0;
static volatile bool g_got_ip = false;
static std::atomic<int> g_disc_reason{0};
static char g_wifi_fail[p1web::kWifiFailMax]{};

static bool g_wifi_started = false;
static bool g_mdns_started = false;
static bool g_mqtt_started = false;
static bool g_host_empty_logged = false;
static bool g_ntp_started = false;
static bool g_mqtt_was_connected = false;
static bool g_wifi_outcome_seen = false;
static bool g_fs_fault = false;
static bool g_mqtt_https_paused = false;
static bool g_mqtt_down_timing = false;
static std::uint32_t g_mqtt_down_since = 0;
static p1led::Player g_led_player{};
static p1led::Condition g_led_logged = p1led::Condition::Busy;
static bool g_led_logged_have = false;
static p1btn::Helper g_btn{};
static std::atomic<bool> g_have_tap{false};
static std::atomic<std::uint32_t> g_tap_ms{0};
static std::atomic<bool> g_fw_busy{false};
static bool g_lan_reboot_armed = false;
static std::uint32_t g_lan_reboot_at = 0;
static p1web::UploadIo g_upload_io{};
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
static char g_json[p1cfg::kToJsonBufMin];
static_assert(sizeof(g_json) >= p1cfg::kToJsonBufMin);
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

static const p1cfg::NetworkSettings& net_in_use()
{
    return g_cfg.boot_network();
}

static bool on_station_network()
{
    return WiFi.status() == WL_CONNECTED;
}

static const char* wifi_state_name()
{
    if (net_in_use().wifi_ssid[0] == '\0') {
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

static void start_sta();

static void close_gw_sock()
{
    if (g_gw_sock >= 0) {
        lwip_close(g_gw_sock);
        g_gw_sock = -1;
    }
}

static void start_gateway_reach_check()
{
    close_gw_sock();
    g_gw_check = GwCheck::Idle;
    if (!g_using_static_sta || g_force_dhcp_this_boot) {
        return;
    }
    IPAddress gw;
    if (!gw.fromString(net_in_use().fdefgtw)) {
        return;
    }
    const int s = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        return;
    }
    const int flags = lwip_fcntl(s, F_GETFL, 0);
    if (flags < 0 || lwip_fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
        lwip_close(s);
        return;
    }
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);  // DNS — open or refused both prove L3 reachability
    dest.sin_addr.s_addr = static_cast<std::uint32_t>(gw);
    const int r = lwip_connect(s, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    if (r == 0) {
        lwip_close(s);
        g_gw_check = GwCheck::Done;
        return;
    }
    if (errno != EINPROGRESS && errno != EALREADY) {
        lwip_close(s);
        g_gw_sock = -1;
        g_gw_check = GwCheck::Pending;
        g_gw_check_started_ms = millis() - kGwReachTimeoutMs;
        return;
    }
    g_gw_sock = s;
    g_gw_check = GwCheck::Pending;
    g_gw_check_started_ms = millis();
}

static void fallback_static_to_dhcp()
{
    g_log.log(p1log::Level::Error, "Static IP failed; using DHCP this boot");
    g_force_dhcp_this_boot = true;
    g_using_static_sta = false;
    g_wifi_started = false;
    g_got_ip = false;
    g_ntp_started = false;
    close_gw_sock();
    g_gw_check = GwCheck::Done;
    start_sta();
}

static void service_gateway_reach_check()
{
    if (g_gw_check != GwCheck::Pending) {
        return;
    }
    if (g_gw_sock >= 0) {
        fd_set wset;
        fd_set eset;
        FD_ZERO(&wset);
        FD_ZERO(&eset);
        FD_SET(g_gw_sock, &wset);
        FD_SET(g_gw_sock, &eset);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        const int sel = lwip_select(g_gw_sock + 1, nullptr, &wset, &eset, &tv);
        if (sel > 0) {
            int soerr = 0;
            socklen_t len = sizeof(soerr);
            lwip_getsockopt(g_gw_sock, SOL_SOCKET, SO_ERROR, &soerr, &len);
            close_gw_sock();
            if (soerr == 0 || soerr == ECONNREFUSED || soerr == ECONNRESET) {
                g_gw_check = GwCheck::Done;
                return;
            }
            g_gw_check_started_ms = millis() - kGwReachTimeoutMs;
        }
    }
    if (millis() - g_gw_check_started_ms < kGwReachTimeoutMs) {
        return;
    }
    close_gw_sock();
    fallback_static_to_dhcp();
}

static void start_sta()
{
    const auto& net = net_in_use();
    if (net.wifi_ssid[0] == '\0') {
        return;
    }
    const bool want_static = net.fip_en && !g_force_dhcp_this_boot;
    if (g_wifi_started && g_using_static_sta == want_static) {
        return;
    }
    if (g_scan_started) {
        WiFi.scanDelete();
        g_scan_started = false;
        g_scan.scanning = false;
    }
    WiFi.setHostname(p1web::kApSsid);
    WiFi.setAutoReconnect(true);
    if (g_wifi_started) {
        WiFi.disconnect(false, false);
    }

    g_using_static_sta = false;
    if (want_static) {
        IPAddress ip;
        IPAddress sn;
        IPAddress gw;
        IPAddress d1;
        IPAddress d2;
        const bool ok = ip.fromString(net.fipaddr) && sn.fromString(net.fsubn) &&
                        gw.fromString(net.fdefgtw) && d1.fromString(net.fdns1) &&
                        (net.fdns2[0] == '\0' || d2.fromString(net.fdns2));
        if (!ok) {
            g_log.log(p1log::Level::Warn, "Static IP incomplete or malformed; using DHCP");
            // Must be 0.0.0.0 — INADDR_NONE is 255.255.255.255 and selects static mode.
            WiFi.config(IPAddress((std::uint32_t)0), IPAddress((std::uint32_t)0),
                        IPAddress((std::uint32_t)0));
        } else if (net.fdns2[0] == '\0') {
            WiFi.config(ip, gw, sn, d1);
            g_using_static_sta = true;
            g_log.log(p1log::Level::Info, "Static IP configured");
        } else {
            WiFi.config(ip, gw, sn, d1, d2);
            g_using_static_sta = true;
            g_log.log(p1log::Level::Info, "Static IP configured");
        }
    } else {
        WiFi.config(IPAddress((std::uint32_t)0), IPAddress((std::uint32_t)0),
                    IPAddress((std::uint32_t)0));
    }

    g_disc_reason.store(0, std::memory_order_relaxed);
    WiFi.begin(net.wifi_ssid, net.wifi_pass);
    g_wifi_started = true;
    close_gw_sock();
    g_gw_check = GwCheck::Idle;
    char msg[96];
    std::snprintf(msg, sizeof(msg), "WiFi begin ssid=%s hostname=%s", net.wifi_ssid, p1web::kApSsid);
    g_log.log(p1log::Level::Info, msg);
}

static void start_ap()
{
    if (g_ap_up) {
        return;
    }
    // AP+STA so scans and later join attempts keep the setup network up.
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
    g_web.set_status(&g_status_view);
    g_readings_view.fields = g_last_readings;
    g_readings_view.count = p1cfg::catalogue_size();
    g_web.set_readings(&g_readings_view);
    g_last_scan_ms = 0;
    g_scan_started = false;
    g_scan.scanning = false;
}

static void idle_sta()
{
    WiFi.setAutoReconnect(false);
    if (!g_wifi_started) {
        return;
    }
    WiFi.disconnect(false, false);
    g_wifi_started = false;
    g_got_ip = false;
    g_ntp_started = false;
    close_gw_sock();
    g_gw_check = GwCheck::Idle;
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
    g_web.set_scan(&g_scan);
    g_readings_view.fields = g_last_readings;
    g_readings_view.count = p1cfg::catalogue_size();
    g_web.set_readings(&g_readings_view);
}

static void refresh_status_view()
{
    IPAddress ip;
    if (WiFi.status() == WL_CONNECTED) {
        ip = WiFi.localIP();
    } else if (g_ap_up) {
        ip = WiFi.softAPIP();
    }
    std::snprintf(g_status_ip, sizeof(g_status_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    const std::uint32_t sec = millis() / 1000u;
    std::snprintf(g_status_uptime, sizeof(g_status_uptime), "%lu s",
                  static_cast<unsigned long>(sec));
    std::snprintf(g_status_heap, sizeof(g_status_heap), "%u",
                  static_cast<unsigned>(ESP.getFreeHeap()));
    std::snprintf(g_status_rssi, sizeof(g_status_rssi), "%ld", static_cast<long>(WiFi.RSSI()));
    std::snprintf(g_status_tele, sizeof(g_status_tele), "ok=%u rejected=%u", g_ok, g_rejected);
    g_status_view.device_id = g_device_id;
    g_status_view.fw_ver = p1fw::kFwVer;
    g_status_view.ip = g_status_ip;
    g_status_view.uptime = g_status_uptime;
    g_status_view.heap = g_status_heap;
    g_status_view.rssi = g_status_rssi;
    g_status_view.mqtt = mqtt_state_name(g_mqtt.state());
    g_status_view.mqtt_err = g_mqtt_err;
    g_status_view.telegrams = g_status_tele;
    g_status_view.wifi = (WiFi.status() == WL_CONNECTED);
    g_status_view.meter =
        (g_ok > 0) && (millis() - g_last_telegram_ms < p1led::kMeterStaleMs);
    g_status_view.ha_en = g_cfg.values().ha_en;
    g_status_view.mqtt_en = g_cfg.values().mqtt_en;
    g_status_view.telegrams_ok = g_ok;
    g_status_view.telegrams_rejected = g_rejected;
    g_status_view.upd_checked = g_upd_checked;
    g_status_view.upd_available = g_upd_available;
    g_status_view.upd_found = g_upd_found;
    g_status_view.upd_channel = g_upd_channel;
    g_status_view.upd_fail = g_upd_fail_msg;
    g_status_view.net_pending = g_cfg.network_pending();
    g_status_view.wifi_fail = g_wifi_fail;
}

static void poll_wifi_scan()
{
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
        g_scan.scanning = false;
        g_scan_started = false;
        g_last_scan_ms = millis();
        g_want_sta_scan = false;
        return;
    }
    if (done == -1) {
        g_scan.scanning = true;
        return;  // still scanning
    }
    // Do not start a scan during a setup-mode join attempt (AP still up).
    if (g_ap_up && g_sta_joining) {
        return;
    }
    const bool due =
        !g_scan_started && (g_last_scan_ms == 0 || millis() - g_last_scan_ms > 15000u);
    const bool want = g_ap_up ? due : (g_want_sta_scan && due);
    if (want) {
        if (WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING) {
            g_scan_started = true;
            g_scan.scanning = true;
        }
    }
}

static void handle_web_actions()
{
    p1web::Actions* act = g_web.actions();
    if (act == nullptr) {
        return;
    }
    if (act->wifi_scan) {
        act->wifi_scan = false;
        g_want_sta_scan = true;
    }
    if (act->republish_config) {
        if (on_station_network()) {
            act->republish_config = false;
            publish_config_snapshot();
        }
    }
    if (act->restart_mqtt) {
        if (on_station_network()) {
            act->restart_mqtt = false;
            restart_mqtt();
        }
    }
    if (act->reset_wifi) {
        act->reset_wifi = false;
        g_cfg.reset_wifi();
        g_log.log(p1log::Level::Info, "WiFi credentials reset by user");
        request_reboot("Rebooting for WiFi reset");
        return;
    }
    if (act->reset_factory) {
        act->reset_factory = false;
        perform_factory_reset();
        return;
    }
    if (act->reboot) {
        act->reboot = false;
        request_reboot("Reboot requested from web UI");
    }
    if (act->ota_check) {
        if (on_station_network()) {
            act->ota_check = false;
            const bool newer = ota_check_now();
            g_last_ota_check_ms = millis();
            g_ota_boot_check_done = true;
            if (newer && g_cfg.values().upd_auto && ota_auto_should_install(g_upd_found)) {
                ota_install_now();
            }
        }
    }
    if (act->ota_install) {
        if (on_station_network()) {
            act->ota_install = false;
            ota_install_now();
        }
    }
    if (act->fw_upload_started) {
        act->fw_upload_started = false;
        g_log.log(p1log::Level::Info, "Firmware upload started");
    }
    if (act->fw_upload_failed) {
        act->fw_upload_failed = false;
        char msg[112];
        std::snprintf(msg, sizeof(msg), "Firmware upload failed: %s", act->fw_upload_err);
        g_log.log(p1log::Level::Error, msg);
        act->fw_upload_err[0] = '\0';
    }
    if (act->fw_upload_complete) {
        act->fw_upload_complete = false;
        g_log.log(p1log::Level::Info, "Firmware upload complete");
        write_ota_note(0);
        g_lan_reboot_at = millis() + 1000u;
        g_lan_reboot_armed = true;
    }
}

static bool scan_has_stored_ssid()
{
    const char* ssid = net_in_use().wifi_ssid;
    if (!g_scan.ready || ssid[0] == '\0') {
        return false;
    }
    for (std::size_t i = 0; i < g_scan.count; ++i) {
        if (std::strcmp(g_scan.entries[i].ssid, ssid) == 0) {
            return true;
        }
    }
    return false;
}

static void service_provisioning()
{
    p1web::ProvInput in{};
    in.have_ssid = net_in_use().wifi_ssid[0] != '\0';
    in.sta_connected = WiFi.status() == WL_CONNECTED;
    in.ssid_seen = scan_has_stored_ssid();
    in.ap_clients = g_ap_up ? WiFi.softAPgetStationNum() : 0;
    in.now_ms = millis();
    const p1web::ProvOutput out = g_prov.tick(in);

    if (out.event == p1web::ProvEvent::RetryStart) {
        g_log.log(p1log::Level::Info, "Setup mode: stored WiFi seen, trying to join");
    } else if (out.event == p1web::ProvEvent::RetryFailed ||
               out.event == p1web::ProvEvent::ConnectFailed) {
        int reason = g_disc_reason.load(std::memory_order_relaxed);
        if (p1web::classify_wifi_reason(reason) == p1web::WifiJoinFail::Other &&
            WiFi.status() == WL_NO_SSID_AVAIL) {
            reason = 201;  // WIFI_REASON_NO_AP_FOUND
        }
        char line[160];
        if (p1web::format_wifi_join_log(net_in_use().wifi_ssid, reason, line, sizeof(line)) > 0) {
            g_log.log(p1log::Level::Warn, line);
        }
        p1web::format_wifi_join_page(net_in_use().wifi_ssid, reason, g_wifi_fail,
                                     sizeof(g_wifi_fail));
    } else if (out.event == p1web::ProvEvent::Rejoined) {
        g_log.log(p1log::Level::Info, "Rejoined stored WiFi");
    }
    if (out.phase == p1web::ProvPhase::Station) {
        g_wifi_fail[0] = '\0';
    }

    if (out.want_ap && !g_ap_up) {
        start_ap();
    } else if (!out.want_ap && g_ap_up) {
        stop_ap();
    }

    if (out.want_sta) {
        start_sta();
        g_sta_joining = true;
    } else {
        if (g_sta_joining || g_wifi_started) {
            idle_sta();
        }
        g_sta_joining = false;
    }
    if (out.phase == p1web::ProvPhase::Station) {
        WiFi.setAutoReconnect(true);
    }

    if (!g_wifi_outcome_seen && (out.phase == p1web::ProvPhase::Station ||
                                 out.phase == p1web::ProvPhase::NeedAp)) {
        g_wifi_outcome_seen = true;
    }
    refresh_status_view();
    if (out.phase == p1web::ProvPhase::Station) {
        ensure_web_station();
    }

    if (g_ap_up) {
        g_dns.loop();
        g_web.set_mode(p1web::Mode::Ap);
        g_web.set_scan(&g_scan);
        g_web.set_status(&g_status_view);
    }
    poll_wifi_scan();

    handle_web_actions();
}

static void open_upload_window()
{
    g_tap_ms.store(millis(), std::memory_order_relaxed);
    g_have_tap.store(true, std::memory_order_release);
    g_log.log(p1log::Level::Info, "Upload window opened by button");
}

static bool claim_fw_busy()
{
    bool expected = false;
    return g_fw_busy.compare_exchange_strong(expected, true);
}

static void release_fw_busy()
{
    g_fw_busy.store(false, std::memory_order_release);
}

static p1web::UploadWindow fw_window()
{
    p1web::UploadWindow w{};
    w.busy = g_fw_busy.load(std::memory_order_relaxed);
    if (!g_have_tap.load(std::memory_order_acquire)) {
        return w;
    }
    const std::uint32_t elapsed = millis() - g_tap_ms.load(std::memory_order_relaxed);
    if (elapsed < p1led::kUploadWindowMs) {
        w.open = true;
        w.left_s = (p1led::kUploadWindowMs - elapsed) / 1000u;
    }
    return w;
}

static bool fw_begin(std::size_t nbytes)
{
    remember_ota_dest();
    return Update.begin(nbytes);
}

static bool fw_write(const std::uint8_t* data, std::size_t n)
{
    return Update.write(const_cast<std::uint8_t*>(data), n) == n;
}

static bool fw_end()
{
    return Update.end() && Update.isFinished();
}

static void fw_abort()
{
    Update.abort();
}

static std::size_t fw_max_image()
{
    return static_cast<std::size_t>(ESP.getFreeSketchSpace());
}

static void poll_button()
{
    const bool pressed = digitalRead(kBtn) == LOW;
    switch (g_btn.update(pressed, millis())) {
    case p1btn::Event::None:
        break;
    case p1btn::Event::Tap:
        open_upload_window();
        break;
    case p1btn::Event::WifiReset:
        g_cfg.reset_wifi();
        g_log.log(p1log::Level::Info, "WiFi credentials reset by user");
        request_reboot("Rebooting for WiFi reset");
        break;
    case p1btn::Event::FactoryReset:
        perform_factory_reset();
        break;
    }
}

static void start_wifi()
{
    // Legacy name: provisioning state machine owns AP/STA now.
    service_provisioning();
}

static void ensure_mdns()
{
    if (g_mdns_started || !g_got_ip) {
        return;
    }
    if (MDNS.begin(p1web::kApSsid)) {
        g_mdns_started = true;
        char msg[48];
        std::snprintf(msg, sizeof(msg), "mDNS %s.local", p1web::kApSsid);
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

    const unsigned heap_before = ESP.getFreeHeap();
    if (g_cfg.values().mqtt_tls) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "MQTT TLS connect heap=%u", heap_before);
        g_log.log(p1log::Level::Info, msg);
    }

    if (g_mqtt.begin(g_cfg, override_host)) {
        g_mqtt_started = true;
        Serial.printf("MQTT connecting uri host=%s port=%u tls=%d id=%s heap=%u\n",
                      override_host != nullptr ? override_host : host,
                      static_cast<unsigned>(g_cfg.values().mqtt_port),
                      g_cfg.values().mqtt_tls ? 1 : 0, g_cfg.values().mqtt_id, heap_before);
    }
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
        p1mqtt::config_snapshot(g_cfg, g_device_id, p1fw::kFwVer, g_json, sizeof(g_json));
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

    std::snprintf(quoted, sizeof(quoted), "\"%s\"", p1fw::kFwVer);
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

static void led_write(const LedRgb& c)
{
    rgbLedWrite(static_cast<std::uint8_t>(kLedPin), c.r, c.g, c.b);
}

static void led_show_busy_red()
{
    led_write(kLedRed);
    g_log.log(p1log::Level::Debug, "LED busy red steady");
}

static void led_apply(p1led::Colour colour, bool on)
{
    if (!on) {
        led_write(kLedOff);
        return;
    }
    switch (colour) {
    case p1led::Colour::Blue:
        led_write(kLedBlue);
        break;
    case p1led::Colour::Green:
        led_write(kLedGreen);
        break;
    case p1led::Colour::Red:
        led_write(kLedRed);
        break;
    }
}

static void service_status_led()
{
    const std::uint32_t now = millis();
    p1led::Facts f{};
    f.busy = !g_wifi_outcome_seen || g_fw_busy.load(std::memory_order_relaxed);
    f.ap_up = g_ap_up;
    f.wifi_ssid_stored = net_in_use().wifi_ssid[0] != '\0';
    f.sta_connected = WiFi.status() == WL_CONNECTED;
    f.mqtt_en = g_cfg.values().mqtt_en;
    f.mqtt_connected = g_mqtt.state() == p1mqtt::Mqtt::State::Connected;
    f.mqtt_https_paused = g_mqtt_https_paused;
    f.fs_fault = g_fs_fault;
    if (g_ok == 0) {
        f.ms_since_meter = now;
    } else {
        f.ms_since_meter = now - g_last_telegram_ms;
    }
    f.ms_mqtt_down = 0;
    if (f.mqtt_en && !f.mqtt_https_paused) {
        if (f.mqtt_connected) {
            g_mqtt_down_timing = false;
        } else {
            if (!g_mqtt_down_timing) {
                g_mqtt_down_timing = true;
                g_mqtt_down_since = now;
            }
            f.ms_mqtt_down = now - g_mqtt_down_since;
        }
    } else if (!f.mqtt_en) {
        g_mqtt_down_timing = false;
    }
    f.have_button_press = g_have_tap.load(std::memory_order_acquire);
    if (f.have_button_press) {
        f.ms_since_button = now - g_tap_ms.load(std::memory_order_relaxed);
    }

    const p1led::Appearance a = p1led::decide(f);
    if (!g_led_logged_have || a.condition != g_led_logged) {
        g_led_logged = a.condition;
        g_led_logged_have = true;
        char msg[72];
        std::snprintf(msg, sizeof(msg), "LED %s %s %s", p1led::condition_name(a.condition),
                      p1led::colour_name(a.colour), p1led::pattern_name(a.pattern));
        g_log.log(p1log::Level::Debug, msg);
    }
    g_led_player.set(a.colour, a.pattern);
    if (g_led_player.tick(now)) {
        led_apply(g_led_player.colour(), g_led_player.on());
    }
}

static void request_reboot(const char* reason)
{
    led_show_busy_red();
    if (reason != nullptr) {
        std::snprintf(g_cfg.values().last_reset, sizeof(g_cfg.values().last_reset), "%s", reason);
    } else {
        g_cfg.values().last_reset[0] = '\0';
    }
    g_cfg.save();
    char msg[96];
    std::snprintf(msg, sizeof(msg), "Reboot requested: %s", reason != nullptr ? reason : "");
    g_log.log(p1log::Level::Info, msg);
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
    g_log.log(p1log::Level::Debug, msg);
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
            p1pub::meter_discovery(g_cfg, p1fw::kFwVer, config_url, include_mask, stub_mask, buf,
                                   p1pub::kDiscoveryCap);
        ok = publish_discovery_buf(topic, buf, n) && ok;
    } else {
        ok = false;
    }

    if (g_device_id[0] != '\0' &&
        p1pub::device_discovery_topic(g_device_id, topic, sizeof(topic)) > 0) {
        const std::size_t n = p1pub::dongle_discovery(g_cfg, g_device_id, p1fw::kFwVer, config_url, buf,
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

static bool mqtt_outbox_drained(std::uint32_t limit_ms)
{
    const std::uint32_t start = millis();
    while (g_mqtt.outbox_size() > 0) {
        if (millis() - start >= limit_ms) {
            return false;
        }
        delay(10);
    }
    return true;
}

static void perform_factory_reset()
{
    const bool have = g_cfg.has_published_ha_identity();
    const bool mqtt_ok = g_mqtt.state() == p1mqtt::Mqtt::State::Connected;
    if (have && mqtt_ok) {
        char slug[p1cfg::kHaMax];
        std::snprintf(slug, sizeof(slug), "%s", g_cfg.published_ha_slug());
        p1pub::withdraw_ha_discovery(slug, g_device_id, &ha_clear_topic, nullptr);
        if (mqtt_outbox_drained(3000)) {
            g_log.log(p1log::Level::Info, "Home Assistant device withdrawn before factory reset");
        } else {
            g_log.log(p1log::Level::Info,
                      "Home Assistant device not withdrawn: MQTT not connected");
        }
    } else if (have) {
        g_log.log(p1log::Level::Info, "Home Assistant device not withdrawn: MQTT not connected");
    }
    g_cfg.factory_reset();
    g_log.log(p1log::Level::Info, "Factory reset by user");
    request_reboot("Rebooting for factory reset");
}

// Standing reconcile: ha_en off with a published identity still on record →
// withdraw retained discovery. Idempotent; covers web, MQTT config, and reboot.
static void reconcile_ha_discovery()
{
    if (g_mqtt.state() != p1mqtt::Mqtt::State::Connected) {
        return;
    }
    if (!g_cfg.values().ha_en) {
        if (!g_cfg.has_published_ha_identity()) {
            return;
        }
        char slug[p1cfg::kHaMax];
        std::snprintf(slug, sizeof(slug), "%s", g_cfg.published_ha_slug());
        p1pub::withdraw_ha_discovery(slug, g_device_id, &ha_clear_topic, nullptr);
        g_cfg.clear_published_ha_identity();
        char msg[96];
        std::snprintf(msg, sizeof(msg), "HA discovery withdrawn for %s", slug);
        g_log.log(p1log::Level::Info, msg);
        return;
    }
    // Re-enable with g_disc_have still set: same as a first publish.
    if (!g_cfg.has_published_ha_identity() && g_disc_have) {
        if (!g_cfg.ha_migrated()) {
            run_ha_migration();
        }
        sync_ha_identity(true);
    }
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

// NetworkClientSecure only exposes the framework bundle via setCACertBundle with
// a custom buffer. Subclass so we can attach the compiled-in bundle the same way
// MQTT TLS does (esp_crt_bundle_attach).
class BundleClient : public NetworkClientSecure {
public:
    void use_framework_bundle()
    {
        attach_ssl_certificate_bundle(sslclient.get(), true);
        _use_ca_bundle = true;
        _use_insecure = false;
    }
};

static void mqtt_pause_for_https()
{
    char avail[p1pub::kTopicMax];
    if (fill_availability(avail, sizeof(avail)) &&
        g_mqtt.state() == p1mqtt::Mqtt::State::Connected) {
        g_mqtt.publish(avail, "offline", true);
    }
    g_mqtt.stop();
    g_mqtt_started = false;
    g_log.set_mqtt_ready(false);
    g_mqtt_https_paused = true;
}

static void mqtt_resume_after_https()
{
    g_mqtt_https_paused = false;
    // Connection-problem clock starts afresh after the deliberate pause.
    g_mqtt_down_timing = false;
    try_start_mqtt();
}

static void remember_ota_dest()
{
    g_ota_dest_label[0] = '\0';
    const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
    if (dest != nullptr && dest->label[0] != '\0') {
        std::snprintf(g_ota_dest_label, sizeof(g_ota_dest_label), "%s", dest->label);
    }
}

static void write_ota_note(unsigned ver)
{
    if (g_ota_dest_label[0] == '\0') {
        remember_ota_dest();
    }
    g_cfg.set_ota_note(g_ota_dest_label, ver);
}

static void fill_ota_fail_msg()
{
    g_upd_fail_msg[0] = '\0';
    if (!g_cfg.has_ota_failed()) {
        return;
    }
    const unsigned ver = g_cfg.ota_failed_ver();
    if (ver == 0) {
        std::snprintf(g_upd_fail_msg, sizeof(g_upd_fail_msg),
                      "Uploaded firmware failed to start and was undone.");
        return;
    }
    char disp[16];
    p1ota::format_version(ver, disp, sizeof(disp));
    std::snprintf(g_upd_fail_msg, sizeof(g_upd_fail_msg),
                  "Update to %s failed to start and was undone.", disp);
}

static bool ota_auto_should_install(unsigned found)
{
    const unsigned failed = g_cfg.has_ota_failed() ? g_cfg.ota_failed_ver() : 0;
    if (p1ota::should_install(found, p1fw::kFwVerNum, failed, true)) {
        return true;
    }
    if (failed != 0 && found == failed) {
        char disp[16];
        p1ota::format_version(found, disp, sizeof(disp));
        char msg[80];
        std::snprintf(msg, sizeof(msg), "Skipping automatic install of %s: it failed before",
                      disp);
        g_log.log(p1log::Level::Info, msg);
    }
    return false;
}

static bool reachable_for_trial()
{
    if (g_ap_up) {
        return true;
    }
    return WiFi.status() == WL_CONNECTED && g_web.running();
}

static void service_trial()
{
    const bool want_confirm = g_on_trial && !g_trial_confirmed;
    const bool want_import_final = g_cfg.import_pending();
    if (!want_confirm && !want_import_final) {
        return;
    }
    if (!reachable_for_trial()) {
        return;
    }
    if (!g_trial_reach_seen) {
        g_trial_reach_seen = true;
        g_trial_reach_ms = millis();
        return;
    }
    if (millis() - g_trial_reach_ms < kTrialConfirmMs) {
        return;
    }
    if (want_confirm) {
        esp_ota_mark_app_valid_cancel_rollback();
        g_trial_confirmed = true;
        g_on_trial = false;
        g_cfg.clear_ota_note();
        g_cfg.clear_ota_failed();
        g_upd_fail_msg[0] = '\0';
        g_log.log(p1log::Level::Info, "New firmware confirmed");
    }
    if (g_cfg.import_pending()) {
        g_cfg.clear_import_pending();
        g_log.log(p1log::Level::Info, "Imported settings are now final");
    }
}

static bool running_on_trial()
{
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (running != nullptr && esp_ota_get_state_partition(running, &st) == ESP_OK) {
        return st == ESP_OTA_IMG_PENDING_VERIFY;
    }
#endif
    return false;
}

static void rollback_storage_on_trial(const char* why)
{
    char line[48];
    std::snprintf(line, sizeof(line), "Storage failed on trial: %s", why);
    Serial.println(line);
    delay(1000);
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

static void apply_ota_note_at_boot(const char* running_label)
{
    if (g_on_trial) {
        g_log.log(p1log::Level::Info, "New firmware on trial");
    }

    if (!g_cfg.has_ota_note()) {
        fill_ota_fail_msg();
        return;
    }
    const char* target = g_cfg.ota_note_part();
    const bool same = running_label != nullptr && target != nullptr &&
                      std::strcmp(running_label, target) == 0;
    if (!same) {
        const unsigned ver = g_cfg.ota_note_ver();
        g_cfg.set_ota_failed(ver);
        g_cfg.clear_ota_note();
        fill_ota_fail_msg();
        g_log.log(p1log::Level::Warn, g_upd_fail_msg);
        return;
    }
    if (!g_on_trial) {
        g_cfg.clear_ota_note();
    }
    fill_ota_fail_msg();
}

// Fetch version file. Returns true if a newer version is available.
static bool ota_check_now()
{
    if (!g_got_ip || WiFi.status() != WL_CONNECTED) {
        g_log.log(p1log::Level::Warn, "OTA check skipped: no WiFi");
        return false;
    }

    char url[p1ota::kUrlMax];
    if (p1ota::version_url(g_cfg.values().rel_chan, url, sizeof(url)) == 0) {
        g_log.log(p1log::Level::Warn, "OTA check: bad release channel");
        return false;
    }

    mqtt_pause_for_https();

    bool newer = false;
    g_upd_available = false;
    g_upd_found = 0;
    g_upd_channel[0] = '\0';

    BundleClient client;
    client.setTimeout(15000);
    client.use_framework_bundle();

    HTTPClient https;
    https.setTimeout(15000);
    char msg[160];
    std::snprintf(msg, sizeof(msg), "OTA checking %s", url);
    g_log.log(p1log::Level::Info, msg);

    if (!https.begin(client, url)) {
        g_log.log(p1log::Level::Warn, "OTA check: HTTPS begin failed");
        mqtt_resume_after_https();
        return false;
    }

    const int code = https.GET();
    if (code != HTTP_CODE_OK && code != HTTP_CODE_MOVED_PERMANENTLY) {
        std::snprintf(msg, sizeof(msg), "OTA check: HTTP %d", code);
        g_log.log(p1log::Level::Warn, msg);
        https.end();
        mqtt_resume_after_https();
        return false;
    }

    const String body = https.getString();
    https.end();

    unsigned found = 0;
    if (!p1ota::parse_version(std::string_view(body.c_str(), body.length()), found)) {
        g_log.log(p1log::Level::Warn, "OTA check: version file not a positive integer");
        g_upd_checked = true;
        std::snprintf(g_upd_channel, sizeof(g_upd_channel), "%s", g_cfg.values().rel_chan);
        mqtt_resume_after_https();
        return false;
    }

    g_upd_checked = true;
    g_upd_found = found;
    std::snprintf(g_upd_channel, sizeof(g_upd_channel), "%s", g_cfg.values().rel_chan);
    char found_disp[16];
    p1ota::format_version(found, found_disp, sizeof(found_disp));
    std::snprintf(msg, sizeof(msg), "OTA running %s (%u), online %s (%u)", p1fw::kFwVer,
                  p1fw::kFwVerNum, found_disp, found);
    g_log.log(p1log::Level::Info, msg);

    if (found > p1fw::kFwVerNum) {
        g_upd_available = true;
        newer = true;
        g_log.log(p1log::Level::Info, "OTA update available");
    } else {
        g_log.log(p1log::Level::Info, "OTA no update available");
    }

    mqtt_resume_after_https();
    return newer;
}

// Download and install. Reboots on success; restores MQTT on failure.
static void ota_install_now()
{
    if (!g_got_ip || WiFi.status() != WL_CONNECTED) {
        g_log.log(p1log::Level::Warn, "OTA install skipped: no WiFi");
        return;
    }

    char url[p1ota::kUrlMax];
    if (p1ota::firmware_url(g_cfg.values().rel_chan, url, sizeof(url)) == 0) {
        g_log.log(p1log::Level::Warn, "OTA install: bad release channel");
        return;
    }

    if (!claim_fw_busy()) {
        g_log.log(p1log::Level::Warn, "OTA install skipped: an update is already in progress");
        return;
    }

    // Steady red before the blocking download (loop cannot animate during it).
    led_show_busy_red();
    mqtt_pause_for_https();

    const unsigned heap_at_dl = ESP.getFreeHeap();
    char msg[160];
    std::snprintf(msg, sizeof(msg), "OTA download begin heap=%u url=%s", heap_at_dl, url);
    g_log.log(p1log::Level::Info, msg);

    BundleClient client;
    client.setTimeout(30000);
    client.use_framework_bundle();

    HTTPClient https;
    https.setTimeout(60000);
    if (!https.begin(client, url)) {
        g_log.log(p1log::Level::Error, "OTA install: HTTPS begin failed");
        mqtt_resume_after_https();
        release_fw_busy();
        return;
    }

    const int code = https.GET();
    if (code != HTTP_CODE_OK && code != HTTP_CODE_MOVED_PERMANENTLY) {
        std::snprintf(msg, sizeof(msg), "OTA install: HTTP %d", code);
        g_log.log(p1log::Level::Error, msg);
        https.end();
        mqtt_resume_after_https();
        release_fw_busy();
        return;
    }

    const int content_len = https.getSize();
    if (content_len <= 0) {
        g_log.log(p1log::Level::Error, "OTA install: missing Content-Length");
        https.end();
        mqtt_resume_after_https();
        release_fw_busy();
        return;
    }

    remember_ota_dest();
    if (!Update.begin(static_cast<size_t>(content_len))) {
        std::snprintf(msg, sizeof(msg), "OTA install: Update.begin failed err=%u len=%d",
                      static_cast<unsigned>(Update.getError()), content_len);
        g_log.log(p1log::Level::Error, msg);
        https.end();
        mqtt_resume_after_https();
        release_fw_busy();
        return;
    }

    NetworkClient& stream = https.getStream();
    const size_t written = Update.writeStream(stream);
    if (written != static_cast<size_t>(content_len)) {
        std::snprintf(msg, sizeof(msg), "OTA install: wrote %u of %d",
                      static_cast<unsigned>(written), content_len);
        g_log.log(p1log::Level::Error, msg);
        Update.abort();
        https.end();
        mqtt_resume_after_https();
        release_fw_busy();
        return;
    }

    if (!Update.end() || !Update.isFinished()) {
        std::snprintf(msg, sizeof(msg), "OTA install: Update.end failed err=%u",
                      static_cast<unsigned>(Update.getError()));
        g_log.log(p1log::Level::Error, msg);
        https.end();
        mqtt_resume_after_https();
        release_fw_busy();
        return;
    }

    https.end();
    write_ota_note(g_upd_found);
    g_log.log(p1log::Level::Info, "OTA install complete, rebooting");
    // Do not restore MQTT — we are about to reboot.
    request_reboot("Firmware update installed");
}

static void ota_on_schedule()
{
    if (!g_cfg.values().upd_autochk || !g_got_ip || WiFi.status() != WL_CONNECTED) {
        return;
    }
    const std::uint32_t now = millis();
    if (!g_ota_boot_check_done) {
        if (now < kOtaFirstDelayMs) {
            return;
        }
        g_ota_boot_check_done = true;
        g_last_ota_check_ms = now;
    } else if ((now - g_last_ota_check_ms) < kOtaIntervalMs) {
        return;
    } else {
        g_last_ota_check_ms = now;
    }

    const bool newer = ota_check_now();
    if (newer && g_cfg.values().upd_auto && ota_auto_should_install(g_upd_found)) {
        ota_install_now();
    }
}

static void on_just_connected()
{
    g_mqtt_err[0] = '\0';
    g_log.set_mqtt_ready(true);
    // Replay runs one line per loop via replay_undelivered() — not in a burst here.
    subscribe_inbound();
    publish_online();
    publish_reboot_state("off");
    publish_config_snapshot();
    {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "MQTT connected heap=%u min=%u",
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getMinFreeHeap()));
        g_log.log(p1log::Level::Info, msg);
    }
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
            std::snprintf(msg, sizeof(msg), "MQTT config rejected %s", p1cfg::json_status_name(st));
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

static void snapshot_readings(const p1dsmr::Data& data)
{
    const p1cfg::Field* cat = p1cfg::catalogue();
    const std::size_t nf = p1cfg::catalogue_size();
    for (std::size_t i = 0; i < nf && i < p1cfg::kMaxFields; ++i) {
        g_last_readings[i].value[0] = '\0';
        g_last_readings[i].unit[0] = '\0';
        p1pub::reading_text(data, cat[i].id, g_last_readings[i].value,
                            sizeof(g_last_readings[i].value), g_last_readings[i].unit,
                            sizeof(g_last_readings[i].unit));
    }
    g_readings_view.count = nf;
}

static void on_telegram(const p1dsmr::Data& data)
{
    if (g_hold_p1) {
        return;
    }
    g_last_telegram_ms = millis();
    snapshot_readings(data);

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
    if (g_cfg.values().push_full) {
        char topic[p1mqtt::kTopicBuf];
        const int tw =
            std::snprintf(topic, sizeof(topic), "%stelegram", g_cfg.values().mqtt_pfix);
        if (tw > 0 && static_cast<std::size_t>(tw) < sizeof(topic)) {
            const std::string_view raw = g_reader.raw();
            if (!raw.empty()) {
                g_mqtt.publish(topic, raw, false);
                ++n;
            }
        }
    }
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

    char prev_rel_chan[p1cfg::kRelChanMax]{};
    const bool tracking_rel =
        e->type == p1cfg::CfgType::Str && std::strcmp(e->key, "REL_CHAN") == 0;
    if (tracking_rel) {
        std::snprintf(prev_rel_chan, sizeof(prev_rel_chan), "%s", e->var.s);
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

    if (tracking_rel && std::strcmp(prev_rel_chan, e->var.s) != 0) {
        clear_ota_check_result();
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
    if (std::strcmp(line, "reboot") == 0) {
        Serial.println("rebooting");
        request_reboot("Reboot requested from serial");
        return;
    }
    if (std::strcmp(line, "factory") == 0) {
        Serial.println("factory reset");
        perform_factory_reset();
        return;
    }
    if (std::strcmp(line, "status") == 0) {
        cmd_status();
        return;
    }
    if (std::strcmp(line, "nvs") == 0) {
        nvs_stats_t st{};
        if (nvs_get_stats(NVS_DEFAULT_PART_NAME, &st) != ESP_OK) {
            Serial.println("nvs stats failed");
            return;
        }
        Serial.printf("nvs used=%u free=%u namespaces=%u total=%u\n",
                      static_cast<unsigned>(st.used_entries),
                      static_cast<unsigned>(st.free_entries),
                      static_cast<unsigned>(st.namespace_count),
                      static_cast<unsigned>(st.total_entries));
        return;
    }
    if (std::strcmp(line, "legacy") == 0) {
        g_legacy_nvs.begin(true);
        Serial.printf("legacy WIFI_SSID=%d MQTT_HOST=%d\n",
                      g_legacy_nvs.has("WIFI_SSID") ? 1 : 0,
                      g_legacy_nvs.has("MQTT_HOST") ? 1 : 0);
        g_legacy_nvs.end();
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
    if (std::strcmp(line, "tap") == 0) {
        open_upload_window();
        Serial.println("upload window open");
        return;
    }
    if (std::memcmp(line, "hold ", 5) == 0) {
        // Desk helper: drop parsed telegrams so the meter indicator can go false
        // without unplugging (DATA_REQUEST is ignored by some sources).
        if (std::strcmp(line + 5, "on") == 0) {
            g_hold_p1 = true;
            Serial.println("hold on");
        } else if (std::strcmp(line + 5, "off") == 0) {
            g_hold_p1 = false;
            Serial.println("hold off");
        } else {
            Serial.println("usage: hold on|off");
        }
        return;
    }
    if (std::memcmp(line, "req ", 4) == 0) {
        // Desk helper: drop DATA_REQUEST to stop meter telegrams (Fluvius continuous mode).
        if (std::strcmp(line + 4, "on") == 0) {
            digitalWrite(kDataRequest, HIGH);
            Serial.println("req on");
        } else if (std::strcmp(line + 4, "off") == 0) {
            digitalWrite(kDataRequest, LOW);
            Serial.println("req off");
        } else {
            Serial.println("usage: req on|off");
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
        "commands: set <KEY> <value> | get | save | reboot | factory | status | field <id> on|off | "
        "req on|off | hold on|off | tap | syslog");
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

static void on_wifi_event(arduino_event_id_t event, arduino_event_info_t info)
{
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        g_got_ip = true;
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        g_got_ip = false;
        const int reason = static_cast<int>(info.wifi_sta_disconnected.reason);
        // STA_LEAVING / ASSOC_LEAVE / ROAMING are our own disconnect or a roam,
        // not the join failure. Keep an earlier AUTH / NO_AP_FOUND reason.
        if (reason == 8 || reason == 36 || reason == 207) {
            return;
        }
        const int prev = g_disc_reason.load(std::memory_order_relaxed);
        const auto prev_kind = p1web::classify_wifi_reason(prev);
        const auto next_kind = p1web::classify_wifi_reason(reason);
        if (prev_kind == p1web::WifiJoinFail::Password) {
            return;
        }
        if (prev_kind == p1web::WifiJoinFail::NotFound &&
            next_kind != p1web::WifiJoinFail::Password) {
            return;
        }
        g_disc_reason.store(reason, std::memory_order_relaxed);
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
    // Busy red before any blocking WiFi / filesystem work.
    led_show_busy_red();

    pinMode(kDataRequest, OUTPUT);
    digitalWrite(kDataRequest, HIGH);
    // GPIO 39 is input-only on ESP32-PICO-D4 — no internal pull-up. The Atom
    // board provides an external pull-up; pressed = LOW (M5Atom Button invert).
    pinMode(kBtn, INPUT);

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

    g_on_trial = running_on_trial();
#if defined(P1_TEST_STORAGE_FAIL)
    if (p1ota::storage_on_trial_should_rollback(g_on_trial, false, false)) {
        rollback_storage_on_trial("filesystem");
    }
#endif

    bool fs_ok = false;
    bool formatted = false;
    if (!g_fs.begin(&formatted, p1ota::storage_format_allowed(g_on_trial))) {
        Serial.println("ERROR filesystem mount failed");
        g_fs_fault = true;
    } else {
        if (formatted) {
            g_log.log(p1log::Level::Error, "Filesystem formatted on mount failure");
        }
        if (!g_fs.self_test()) {
            g_log.log(p1log::Level::Error, "Filesystem self-test failed");
            g_fs_fault = true;
        } else {
            char fsmsg[64];
            std::snprintf(fsmsg, sizeof(fsmsg), "Filesystem mounted %u bytes",
                          static_cast<unsigned>(LittleFS.totalBytes()));
            g_log.log(p1log::Level::Info, fsmsg);
            fs_ok = true;
        }
    }

    const bool settings_ok = g_cfg.settings_storage_ok();
    if (p1ota::storage_on_trial_should_rollback(g_on_trial, fs_ok, settings_ok)) {
        rollback_storage_on_trial(!fs_ok ? "filesystem" : "settings");
    }

    g_cfg.load();
    if (g_cfg.legacy_import_ran()) {
        for (std::size_t i = 0; i < g_cfg.legacy_skip_count(); ++i) {
            char w[48];
            std::snprintf(w, sizeof(w), "Legacy import skipped %s", g_cfg.legacy_skip_key(i));
            g_log.log(p1log::Level::Warn, w);
        }
        char msg[80];
        std::snprintf(msg, sizeof(msg),
                      g_cfg.legacy_import_again()
                          ? "Settings imported again from legacy firmware (%u settings)"
                          : "Settings imported from legacy firmware (%u settings)",
                      static_cast<unsigned>(g_cfg.legacy_imported()));
        g_log.log(p1log::Level::Info, msg);
    }
    {
        nvs_stats_t st{};
        if (nvs_get_stats(NVS_DEFAULT_PART_NAME, &st) == ESP_OK) {
            char msg[80];
            std::snprintf(msg, sizeof(msg), "NVS used=%u free=%u namespaces=%u",
                          static_cast<unsigned>(st.used_entries),
                          static_cast<unsigned>(st.free_entries),
                          static_cast<unsigned>(st.namespace_count));
            g_log.log(p1log::Level::Info, msg);
        }
    }
    g_cfg.capture_boot_network();
    g_log.set_mark_persist(&g_persist_log_mark, nullptr);
    g_log.set_log_mark(g_cfg.values().log_mark);

    if (g_cfg.values().rint_spiffs) {
        g_cfg.values().rint_spiffs = false;
        g_cfg.values().log_mark = 0;
        g_cfg.save();
        LittleFS.end();
        LittleFS.format();
        bool reformatted = false;
        if (!g_fs.begin(&reformatted)) {
            Serial.println("ERROR filesystem remount failed after RINT_SPIFFS");
            g_fs_fault = true;
        } else {
            g_log.set_log_mark(0);
            g_log.log(p1log::Level::Warn, "Filesystem reinitialised");
        }
    }

    {
        std::uint8_t rnd[8];
        esp_fill_random(rnd, sizeof(rnd));
        if (p1cfg::ensure_uuid(g_cfg.values().uuid, sizeof(g_cfg.values().uuid), rnd)) {
            g_cfg.save();
            char msg[48];
            std::snprintf(msg, sizeof(msg), "UUID generated %s", g_cfg.values().uuid);
            g_log.log(p1log::Level::Info, msg);
        }
    }

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

    const esp_partition_t* running = esp_ota_get_running_partition();
    const char* part = (running != nullptr && running->label[0] != '\0') ? running->label : "?";
    char boot[112];
    std::snprintf(boot, sizeof(boot), "Boot fw=%s id=%s reboots=%lu uart_rx=%u part=%s",
                  p1fw::kFwVer, g_device_id, static_cast<unsigned long>(g_cfg.values().reboots),
                  static_cast<unsigned>(rx), part);
    g_log.log(p1log::Level::Info, boot);
    char reasons[128];
    std::snprintf(reasons, sizeof(reasons), "Reset hw=%s fw=%s", g_hw_reset_reason,
                  g_fw_reset_reason[0] != '\0' ? g_fw_reset_reason : "(none)");
    g_log.log(p1log::Level::Info, reasons);
    apply_ota_note_at_boot(part);

    g_upload_io.window = &fw_window;
    g_upload_io.claim_busy = &claim_fw_busy;
    g_upload_io.release_busy = &release_fw_busy;
    g_upload_io.begin = &fw_begin;
    g_upload_io.write = &fw_write;
    g_upload_io.end = &fw_end;
    g_upload_io.abort = &fw_abort;
    g_upload_io.max_image = &fw_max_image;
    g_web.set_upload_io(&g_upload_io);
    g_web.set_store(&g_cfg);
    g_web.set_files(&g_fs);
    WiFi.onEvent(on_wifi_event);
    start_wifi();
}

void loop()
{
    if (g_cfg.consume_rel_chan_changed()) {
        clear_ota_check_result();
    }

    poll_serial();
    poll_button();
    if (g_lan_reboot_armed &&
        static_cast<std::int32_t>(millis() - g_lan_reboot_at) >= 0) {
        g_lan_reboot_armed = false;
        request_reboot("Firmware uploaded over LAN");
        return;
    }
    service_provisioning();
    service_trial();
    service_gateway_reach_check();
    service_status_led();

    if (WiFi.status() == WL_CONNECTED) {
        if (!g_got_ip) {
            g_got_ip = true;
        }
        if (g_using_static_sta && g_gw_check == GwCheck::Idle) {
            start_gateway_reach_check();
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

    int cert_flags = 0;
    int tls_esp_err = 0;
    int tls_stack_err = 0;
    const p1mqtt::Mqtt::Fail fail = g_mqtt.take_fail(&cert_flags, &tls_esp_err, &tls_stack_err);
    if (fail != p1mqtt::Mqtt::Fail::None) {
        switch (fail) {
        case p1mqtt::Mqtt::Fail::TlsCertRejected:
            std::snprintf(g_mqtt_err, sizeof(g_mqtt_err), "certificate rejected");
            {
                char msg[96];
                std::snprintf(msg, sizeof(msg),
                              "MQTT TLS certificate rejected flags=0x%x esp=0x%x stack=%d",
                              static_cast<unsigned>(cert_flags),
                              static_cast<unsigned>(tls_esp_err), tls_stack_err);
                g_log.log(p1log::Level::Warn, msg);
            }
            break;
        case p1mqtt::Mqtt::Fail::TlsOther:
            std::snprintf(g_mqtt_err, sizeof(g_mqtt_err), "TLS error");
            {
                char msg[96];
                std::snprintf(msg, sizeof(msg),
                              "MQTT TLS error esp=0x%x stack=%d flags=0x%x",
                              static_cast<unsigned>(tls_esp_err), tls_stack_err,
                              static_cast<unsigned>(cert_flags));
                g_log.log(p1log::Level::Warn, msg);
            }
            break;
        case p1mqtt::Mqtt::Fail::Transport:
            std::snprintf(g_mqtt_err, sizeof(g_mqtt_err), "transport error");
            g_log.log(p1log::Level::Warn, "MQTT transport error");
            break;
        case p1mqtt::Mqtt::Fail::Auth:
            std::snprintf(g_mqtt_err, sizeof(g_mqtt_err), "authentication failed");
            g_log.log(p1log::Level::Warn, "MQTT authentication failed");
            break;
        default:
            break;
        }
    }

    handle_inbound();
    reconcile_ha_discovery();

    ota_on_schedule();

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

#ifdef P1_TEST_CRASH_AT_MS
    if (millis() >= static_cast<std::uint32_t>(P1_TEST_CRASH_AT_MS)) {
        abort();
    }
#endif
#ifdef P1_TEST_HANG_AT_MS
    if (millis() >= static_cast<std::uint32_t>(P1_TEST_HANG_AT_MS)) {
        for (;;) {
            delay(1000);
        }
    }
#endif

    yield();
}
