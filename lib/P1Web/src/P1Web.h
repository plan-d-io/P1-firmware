#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <P1Config.h>

#include "assets_gen.h"

namespace p1web {

constexpr char kApSsid[] = "p1dongle";
constexpr std::uint32_t kConnectTimeoutMs = 60000;
constexpr std::size_t kMaxScanResults = 24;
constexpr std::size_t kSsidMax = 33;
constexpr std::size_t kPassMax = 64;
constexpr std::size_t kHtmlBuf = 3072;
constexpr char kCacheControl[] = "public, max-age=300";
constexpr std::size_t kConfigBodyMax = 2048;
constexpr std::size_t kConfigJsonMax = 8192;
constexpr std::size_t kApiStatusMax = 768;
constexpr std::size_t kApiDataMax = 6144;
constexpr std::size_t kReadingValueMax = 16;
constexpr std::size_t kReadingUnitMax = 8;
constexpr std::size_t kConfigErrMax = 48;

enum class Mode : std::uint8_t { Ap, Station };

enum class Route : std::uint8_t {
    Setup,
    Status,
    ConfigPage,
    Style,
    Save,
    Restarting,
    Rebooting,
    ConfigGet,
    ConfigPost,
    ApiStatus,
    ApiData,
    Reboot,
    NotFound,
};

enum class Method : std::uint8_t { Get, Post, Other };

Method parse_method(std::string_view m);
Route resolve(Mode mode, Method method, std::string_view uri);

enum class ProvPhase : std::uint8_t {
    NeedAp,       // soft AP should be up
    Connecting,   // STA attempt in progress
    Station,      // STA connected, AP down
};

struct ProvInput {
    bool have_ssid = false;
    bool sta_connected = false;
    std::uint32_t now_ms = 0;
};

struct ProvOutput {
    ProvPhase phase = ProvPhase::NeedAp;
    bool want_ap = false;
    bool want_sta = false;
};

class ProvisionMachine {
public:
    ProvOutput tick(const ProvInput& in);

    ProvPhase phase() const { return phase_; }
    std::uint32_t connect_started_ms() const { return connect_started_ms_; }

private:
    ProvPhase phase_ = ProvPhase::NeedAp;
    std::uint32_t connect_started_ms_ = 0;
    bool timing_ = false;
};

struct ScanEntry {
    char ssid[kSsidMax]{};
    std::int32_t rssi = 0;
};

struct ScanCache {
    ScanEntry entries[kMaxScanResults]{};
    std::size_t count = 0;
    bool ready = false;
};

struct StatusView {
    const char* device_id = "";
    const char* fw_ver = "";
    const char* ip = "";
    const char* uptime = "";
    const char* heap = "";
    const char* rssi = "";
    const char* mqtt = "";
    const char* telegrams = "";
    bool wifi = false;
    bool meter = false;
    bool ha_en = false;
    bool mqtt_en = false;
    unsigned telegrams_ok = 0;
    unsigned telegrams_rejected = 0;
};

struct FieldReading {
    char value[kReadingValueMax]{};
    char unit[kReadingUnitMax]{};
};

struct ReadingsView {
    const FieldReading* fields = nullptr;  // catalogue_size() entries
    std::size_t count = 0;
};

// Pending side effects for the loop task. Handlers only set these.
struct Actions {
    bool save_wifi = false;
    char wifi_ssid[kSsidMax]{};
    char wifi_pass[kPassMax]{};
    bool reboot = false;
    bool republish_config = false;
};

void sort_scan_strongest_first(ScanCache& cache);

std::size_t render_network_radios(const ScanCache& cache, char* out, std::size_t cap);
std::size_t render_setup_page(const ScanCache& cache, char* out, std::size_t cap);
std::size_t render_status_page(const StatusView& v, char* out, std::size_t cap);
std::size_t render_api_status(const StatusView& v, char* out, std::size_t cap);
std::size_t render_api_data(const ReadingsView& r, char* out, std::size_t cap);

// "Invalid:MQTT_PORT" style body for POST /config failures.
std::size_t format_config_error(p1cfg::JsonStatus st, const char* bad_key, char* out,
                                std::size_t cap);

// Replace first occurrence of needle with value into out. Returns bytes written
// (excluding NUL), or 0 on overflow.
std::size_t replace_one(std::string_view tpl, std::string_view needle, std::string_view value,
                        char* out, std::size_t cap);

// Parse application/x-www-form-urlencoded body for ssid / ssid_other / pass.
bool parse_save_form(std::string_view body, char* ssid, std::size_t ssid_cap, char* pass,
                     std::size_t pass_cap);

// Build {"WIFI_SSID":"...","WIFI_PASS":"..."} into out.
std::size_t wifi_creds_json(std::string_view ssid, std::string_view pass, char* out,
                             std::size_t cap);

#if defined(ARDUINO)
class Server {
public:
    bool begin(Mode mode);
    void end();
    void set_mode(Mode mode);
    void set_scan(const ScanCache* scan);
    void set_status(const StatusView* status);
    void set_readings(const ReadingsView* readings);
    void set_store(p1cfg::Store* store);
    Actions* actions();
    bool running() const { return running_; }

    // Used by the HTTP handler (same translation unit).
    Mode mode_for_handlers() const;
    const ScanCache* scan_ptr() const;
    const StatusView* status_ptr() const;
    const ReadingsView* readings_ptr() const;
    p1cfg::Store* store_ptr() const;

private:
    bool running_ = false;
    Mode mode_ = Mode::Ap;
    void* handle_ = nullptr;  // httpd_handle_t
    const ScanCache* scan_ = nullptr;
    const StatusView* status_ = nullptr;
    const ReadingsView* readings_ = nullptr;
    p1cfg::Store* store_ = nullptr;
    Actions actions_{};
};

class CaptiveDns {
public:
    bool begin(std::uint32_t ap_ip_host_order);
    void loop();
    void end();

private:
    int sock_ = -1;
    std::uint32_t ip_ = 0;
};
#endif

}  // namespace p1web
