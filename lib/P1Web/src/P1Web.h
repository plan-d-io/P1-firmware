#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <P1Config.h>
#include <P1Syslog.h>

#include "assets_gen.h"

namespace p1web {

constexpr char kApSsid[] = "p1dongle";
constexpr std::uint32_t kConnectTimeoutMs = 60000;
constexpr std::uint32_t kRetryIntervalMs = 120000;
constexpr std::uint32_t kRetryAttemptMs = 20000;
constexpr std::size_t kMaxScanResults = 24;
constexpr std::size_t kSsidMax = 33;
constexpr std::size_t kPassMax = 64;
// Status page: 838 B template + worst-case replacements (~60 B). malloc.
constexpr std::size_t kHtmlBuf = 1024;
constexpr std::size_t kUpdateWindowJsonMax = 64;
constexpr std::uint8_t kFirmwareMagic = 0xE9;
constexpr std::size_t kUploadChunk = 4096;
constexpr char kCacheControl[] = "public, max-age=300";
constexpr std::size_t kConfigBodyMax = 2048;
constexpr std::size_t kConfigJsonMax = 8192;
// Previous 1024 plus `"wifi_fail":"<kWifiFailMax escaped>"`. Headroom for escaping.
constexpr std::size_t kWifiFailMax = 128;
// Previous 1280 plus eid (8), claim (32 escaped), record (64 escaped),
// interval, last_upload unix, error (64 escaped). 2048 is headroom.
constexpr std::size_t kApiStatusMax = 2048;
// 24 entries, 32-char SSIDs fully escaped (64), rssi int32 min:
// {"ssid":"<64>","rssi":-2147483648} = 93; 24*93 + 23 commas +
// {"scanning":false,"networks":[]} (33) = 2288. 2560 is headroom.
constexpr std::size_t kApiNetworksMax = 2560;
constexpr std::size_t kApiDataMax = 6144;
// Worst case: every catalogue slot selectable, id at kMaxFieldId, label at
// kFieldLabelMax, region "common", on false.
// Entry: {"id":"<16>","label":"<40>","region":"common","on":false} = 104 B
// Array: '[' + N*104 + (N-1)*',' + ']' with N = kMaxFields (64) → 6721.
constexpr std::size_t kFieldLabelMax = 40;
constexpr std::size_t kApiFieldsEntryMax =
    7 + p1cfg::kMaxFieldId + 10 + kFieldLabelMax + 12 + 6 + 7 + 5 + 1;
constexpr std::size_t kApiFieldsMax =
    2 + p1cfg::kMaxFields * kApiFieldsEntryMax + (p1cfg::kMaxFields - 1);
constexpr std::size_t kReadingValueMax = 16;
constexpr std::size_t kReadingUnitMax = 8;
constexpr std::size_t kConfigErrMax = 48;
constexpr std::size_t kSyslogChunk = 512;

enum class Mode : std::uint8_t { Ap, Station };

enum class Route : std::uint8_t {
    Status,
    ConfigPage,
    Style,
    RedirectHome,
    Rebooting,
    ConfigGet,
    ConfigPost,
    ApiStatus,
    ApiData,
    ApiFields,
    ApiNetworks,
    FieldsPost,
    Syslog,
    Syslog0,
    LogPage,
    Reboot,
    ResetWifi,
    ResetFactory,
    OtaCheck,
    OtaInstall,
    UpdatePage,
    UpdateWindow,
    UpdateUpload,
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

enum class ProvEvent : std::uint8_t {
    None = 0,
    RetryStart,
    RetryFailed,
    Rejoined,
    ConnectFailed,  // 60 s boot / drop attempt ended without a connection
};

struct ProvInput {
    bool have_ssid = false;
    bool sta_connected = false;
    bool ssid_seen = false;
    std::uint8_t ap_clients = 0;
    std::uint32_t now_ms = 0;
};

struct ProvOutput {
    ProvPhase phase = ProvPhase::NeedAp;
    bool want_ap = false;
    bool want_sta = false;
    ProvEvent event = ProvEvent::None;
};

class ProvisionMachine {
public:
    ProvOutput tick(const ProvInput& in);

    ProvPhase phase() const { return phase_; }
    std::uint32_t connect_started_ms() const { return connect_started_ms_; }

private:
    ProvPhase phase_ = ProvPhase::NeedAp;
    std::uint32_t connect_started_ms_ = 0;
    std::uint32_t setup_since_ms_ = 0;
    std::uint32_t retry_started_ms_ = 0;
    std::uint32_t last_attempt_end_ms_ = 0;
    bool timing_ = false;
    bool holding_ap_ = false;
    bool retrying_ = false;
};

struct ScanEntry {
    char ssid[kSsidMax]{};
    std::int32_t rssi = 0;
};

struct ScanCache {
    ScanEntry entries[kMaxScanResults]{};
    std::size_t count = 0;
    bool ready = false;
    bool scanning = false;
};

struct StatusView {
    const char* device_id = "";
    const char* fw_ver = "";
    const char* ip = "";
    const char* uptime = "";
    const char* heap = "";
    const char* rssi = "";
    const char* mqtt = "";
    const char* mqtt_err = "";
    const char* telegrams = "";
    bool wifi = false;
    bool meter = false;
    bool ha_en = false;
    bool mqtt_en = false;
    unsigned telegrams_ok = 0;
    unsigned telegrams_rejected = 0;
    bool upd_checked = false;
    bool upd_available = false;
    unsigned upd_found = 0;
    const char* upd_channel = "";
    const char* upd_fail = "";
    bool net_pending = false;
    const char* wifi_fail = "";
    const char* eid = "off";
    const char* eid_claim = "";
    const char* eid_record = "";
    unsigned eid_interval = 0;
    unsigned eid_last_upload = 0;
    const char* eid_error = "";
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
    bool wifi_scan = false;
    bool reboot = false;
    bool reset_wifi = false;
    bool reset_factory = false;
    bool republish_config = false;
    bool restart_mqtt = false;
    bool ota_check = false;
    bool ota_install = false;
    bool fw_upload_started = false;
    bool fw_upload_failed = false;
    bool fw_upload_complete = false;
    char fw_upload_err[80]{};
};

enum class UploadRefuse : std::uint8_t {
    Ok = 0,
    WindowClosed,
    Busy,
    NoFile,
    TooLarge,
    NotFirmware,
};

struct UploadWindow {
    bool open = false;
    unsigned left_s = 0;
    bool busy = false;
};

// Board code fills this in. P1Web must not include Update.h.
struct UploadIo {
    UploadWindow (*window)() = nullptr;
    bool (*claim_busy)() = nullptr;
    void (*release_busy)() = nullptr;
    bool (*begin)(std::size_t nbytes) = nullptr;
    bool (*write)(const std::uint8_t* data, std::size_t n) = nullptr;
    bool (*end)() = nullptr;
    void (*abort)() = nullptr;
    std::size_t (*max_image)() = nullptr;
};

UploadRefuse check_upload_start(bool window_open, bool busy, bool have_length,
                                std::size_t content_length, std::size_t max_image);
UploadRefuse check_firmware_magic(std::uint8_t first);
const char* upload_refuse_reason(UploadRefuse r);

std::size_t render_update_window(bool open, unsigned left_s, bool busy, char* out,
                                 std::size_t cap);

void sort_scan_strongest_first(ScanCache& cache);

std::size_t render_status_page(const StatusView& v, char* out, std::size_t cap);
std::size_t render_api_status(const StatusView& v, char* out, std::size_t cap);

// wifi_err_reason_t values from esp_wifi_types_generic.h (ESP-IDF 5.5 /
// pioarduino 55.03.37). Kept numeric so native tests do not need the SDK.
enum class WifiJoinFail : std::uint8_t { Password, NotFound, Other };

WifiJoinFail classify_wifi_reason(int reason);
std::size_t format_wifi_join_log(const char* ssid, int reason, char* out, std::size_t cap);
std::size_t format_wifi_join_page(const char* ssid, int reason, char* out, std::size_t cap);

// Configuration-page save contract: an empty selection (placeholder) is never
// sent; only a user-chosen SSID that differs from the loaded value is.
bool wifi_ssid_in_save(std::string_view loaded, std::string_view selected);
std::size_t render_api_data(const ReadingsView& r, char* out, std::size_t cap);
std::size_t render_api_fields(const p1cfg::Store& store, char* out, std::size_t cap);
std::size_t render_api_networks(const ScanCache& cache, char* out, std::size_t cap);

// "Invalid:MQTT_PORT" style body for POST /config and POST /fields failures.
std::size_t format_config_error(p1cfg::JsonStatus st, const char* bad_key, char* out,
                                std::size_t cap);

// Replace first occurrence of needle with value into out. Returns bytes written
// (excluding NUL), or 0 on overflow.
std::size_t replace_one(std::string_view tpl, std::string_view needle, std::string_view value,
                        char* out, std::size_t cap);

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
    void set_files(p1log::FileStore* files);
    void set_upload_io(const UploadIo* io);
    Actions* actions();
    bool running() const { return running_; }

    // Used by the HTTP handler (same translation unit).
    Mode mode_for_handlers() const;
    const ScanCache* scan_ptr() const;
    const StatusView* status_ptr() const;
    const ReadingsView* readings_ptr() const;
    p1cfg::Store* store_ptr() const;
    p1log::FileStore* files_ptr() const;
    const UploadIo* upload_io_ptr() const;

private:
    bool running_ = false;
    Mode mode_ = Mode::Ap;
    void* handle_ = nullptr;  // httpd_handle_t
    const ScanCache* scan_ = nullptr;
    const StatusView* status_ = nullptr;
    const ReadingsView* readings_ = nullptr;
    p1cfg::Store* store_ = nullptr;
    p1log::FileStore* files_ = nullptr;
    const UploadIo* upload_io_ = nullptr;
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
