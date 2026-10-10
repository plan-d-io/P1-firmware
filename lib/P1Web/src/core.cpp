#include "P1Web.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace p1web {
namespace {

bool starts_with(std::string_view s, std::string_view p)
{
    return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

}  // namespace

Method parse_method(std::string_view m)
{
    if (m == "GET" || m == "get") {
        return Method::Get;
    }
    if (m == "POST" || m == "post") {
        return Method::Post;
    }
    return Method::Other;
}

Route resolve(Mode mode, Method method, std::string_view uri)
{
    // Strip query string.
    const auto q = uri.find('?');
    if (q != std::string_view::npos) {
        uri = uri.substr(0, q);
    }
    if (uri.empty()) {
        uri = "/";
    }

    if (uri == "/style.css") {
        return Route::Style;
    }
    if (uri == "/config") {
        if (method == Method::Get) {
            return Route::ConfigGet;
        }
        if (method == Method::Post) {
            return Route::ConfigPost;
        }
        return Route::NotFound;
    }
    if (uri == "/api/status" && method == Method::Get) {
        return Route::ApiStatus;
    }
    if (uri == "/api/data" && method == Method::Get) {
        return Route::ApiData;
    }
    if (uri == "/api/fields" && method == Method::Get) {
        return Route::ApiFields;
    }
    if (uri == "/fields" && method == Method::Post) {
        return Route::FieldsPost;
    }
    if (uri == "/syslog" && method == Method::Get) {
        return Route::Syslog;
    }
    if (uri == "/syslog0" && method == Method::Get) {
        return Route::Syslog0;
    }
    if (uri == "/log" || uri == "/log.html") {
        return Route::LogPage;
    }
    if (uri == "/reboot" && method == Method::Post) {
        return Route::Reboot;
    }
    if (uri == "/reset/wifi" && method == Method::Post) {
        return Route::ResetWifi;
    }
    if (uri == "/reset/factory" && method == Method::Post) {
        return Route::ResetFactory;
    }
    if (uri == "/update/check" && method == Method::Post) {
        return Route::OtaCheck;
    }
    if (uri == "/update/install" && method == Method::Post) {
        return Route::OtaInstall;
    }
    if (uri == "/update/window" && method == Method::Get) {
        return Route::UpdateWindow;
    }
    if (uri == "/update") {
        if (method == Method::Get) {
            return Route::UpdatePage;
        }
        if (method == Method::Post) {
            return Route::UpdateUpload;
        }
        return Route::NotFound;
    }
    if (uri == "/rebooting.html" || uri == "/rebooting") {
        return Route::Rebooting;
    }
    if (method == Method::Post && uri == "/save") {
        return Route::Save;
    }
    if (uri == "/restarting.html" || uri == "/restarting") {
        return Route::Restarting;
    }

    if (mode == Mode::Ap) {
        if (uri == "/" || uri == "/setup" || uri == "/setup.html" || uri == "/index.html" ||
            uri == "/generate_204" || uri == "/hotspot-detect.html" || uri == "/connectivitycheck" ||
            starts_with(uri, "/connectivitycheck") || starts_with(uri, "/ncsi") ||
            uri == "/fwlink" || uri == "/canonical.html") {
            return Route::Setup;
        }
        // Captive portal probes and anything else land on setup.
        return Route::Setup;
    }

    // Station mode.
    if (uri == "/" || uri == "/index.html") {
        return Route::ConfigPage;
    }
    if (uri == "/status" || uri == "/status.html") {
        return Route::Status;
    }
    if (uri == "/setup" || uri == "/setup.html") {
        return Route::Setup;  // still reachable if someone bookmarks it
    }
    return Route::NotFound;
}

ProvOutput ProvisionMachine::tick(const ProvInput& in)
{
    ProvOutput out{};

    if (!in.have_ssid) {
        phase_ = ProvPhase::NeedAp;
        holding_ap_ = true;
        retrying_ = false;
        timing_ = false;
        last_attempt_end_ms_ = 0;
        out.phase = phase_;
        out.want_ap = true;
        out.want_sta = false;
        return out;
    }

    if (in.sta_connected) {
        const bool from_setup = (phase_ == ProvPhase::NeedAp && holding_ap_);
        phase_ = ProvPhase::Station;
        holding_ap_ = false;
        retrying_ = false;
        timing_ = false;
        last_attempt_end_ms_ = 0;
        out.phase = phase_;
        out.want_ap = false;
        out.want_sta = true;
        if (from_setup) {
            out.event = ProvEvent::Rejoined;
        }
        return out;
    }

    if (phase_ == ProvPhase::Station) {
        holding_ap_ = false;
        retrying_ = false;
        phase_ = ProvPhase::Connecting;
        connect_started_ms_ = in.now_ms;
        timing_ = true;
    }

    if (holding_ap_ && phase_ == ProvPhase::NeedAp) {
        out.phase = ProvPhase::NeedAp;
        out.want_ap = true;
        if (retrying_) {
            if ((in.now_ms - retry_started_ms_) >= kRetryAttemptMs) {
                retrying_ = false;
                last_attempt_end_ms_ = in.now_ms;
                out.want_sta = false;
                out.event = ProvEvent::RetryFailed;
            } else {
                out.want_sta = true;
            }
            return out;
        }
        out.want_sta = false;
        const std::uint32_t since =
            last_attempt_end_ms_ != 0 ? last_attempt_end_ms_ : setup_since_ms_;
        if (in.ssid_seen && in.ap_clients == 0 &&
            (in.now_ms - since) >= kRetryIntervalMs) {
            retrying_ = true;
            retry_started_ms_ = in.now_ms;
            out.want_sta = true;
            out.event = ProvEvent::RetryStart;
        }
        return out;
    }

    if (!timing_ || phase_ != ProvPhase::Connecting) {
        phase_ = ProvPhase::Connecting;
        connect_started_ms_ = in.now_ms;
        timing_ = true;
        holding_ap_ = false;
        retrying_ = false;
    }

    if ((in.now_ms - connect_started_ms_) >= kConnectTimeoutMs) {
        phase_ = ProvPhase::NeedAp;
        holding_ap_ = true;
        timing_ = false;
        retrying_ = false;
        setup_since_ms_ = in.now_ms;
        last_attempt_end_ms_ = 0;
        out.phase = phase_;
        out.want_ap = true;
        out.want_sta = false;
        return out;
    }

    phase_ = ProvPhase::Connecting;
    out.phase = phase_;
    out.want_ap = false;
    out.want_sta = true;
    return out;
}

void sort_scan_strongest_first(ScanCache& cache)
{
    for (std::size_t i = 1; i < cache.count; ++i) {
        ScanEntry key = cache.entries[i];
        std::size_t j = i;
        while (j > 0 && cache.entries[j - 1].rssi < key.rssi) {
            cache.entries[j] = cache.entries[j - 1];
            --j;
        }
        cache.entries[j] = key;
    }
}

std::size_t replace_one(std::string_view tpl, std::string_view needle, std::string_view value,
                        char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    const auto pos = tpl.find(needle);
    if (pos == std::string_view::npos) {
        if (tpl.size() + 1 > cap) {
            out[0] = '\0';
            return 0;
        }
        for (std::size_t i = 0; i < tpl.size(); ++i) {
            out[i] = tpl[i];
        }
        out[tpl.size()] = '\0';
        return tpl.size();
    }
    const std::size_t need = pos + value.size() + (tpl.size() - pos - needle.size());
    if (need + 1 > cap) {
        out[0] = '\0';
        return 0;
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < pos; ++i) {
        out[n++] = tpl[i];
    }
    for (char c : value) {
        out[n++] = c;
    }
    for (std::size_t i = pos + needle.size(); i < tpl.size(); ++i) {
        out[n++] = tpl[i];
    }
    out[n] = '\0';
    return n;
}

static void html_escape_append(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    for (char c : s) {
        const char* rep = nullptr;
        switch (c) {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        default:
            break;
        }
        if (rep != nullptr) {
            for (const char* p = rep; *p != '\0'; ++p) {
                if (n + 1 >= cap) {
                    return;
                }
                out[n++] = *p;
            }
        } else {
            if (n + 1 >= cap) {
                return;
            }
            out[n++] = c;
        }
    }
}

std::size_t render_network_radios(const ScanCache& cache, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!cache.ready || cache.count == 0) {
        const char* empty = "<p class=\"empty\">Scanning for networks…</p>";
        while (empty[n] != '\0' && n + 1 < cap) {
            out[n] = empty[n];
            ++n;
        }
        out[n] = '\0';
        return n;
    }
    for (std::size_t i = 0; i < cache.count; ++i) {
        const ScanEntry& e = cache.entries[i];
        if (e.ssid[0] == '\0') {
            continue;
        }
        const char* pre = "<label class=\"net\"><input type=\"radio\" name=\"ssid\" value=\"";
        for (const char* p = pre; *p != '\0'; ++p) {
            if (n + 1 >= cap) {
                out[0] = '\0';
                return 0;
            }
            out[n++] = *p;
        }
        html_escape_append(out, cap, n, e.ssid);
        const char* mid = "\"> <span class=\"name\">";
        for (const char* p = mid; *p != '\0'; ++p) {
            if (n + 1 >= cap) {
                out[0] = '\0';
                return 0;
            }
            out[n++] = *p;
        }
        html_escape_append(out, cap, n, e.ssid);
        char rssi[64];
        const int rn = std::snprintf(rssi, sizeof(rssi), "</span> <span class=\"rssi\">%ld dBm</span></label>\n",
                                     static_cast<long>(e.rssi));
        if (rn <= 0) {
            out[0] = '\0';
            return 0;
        }
        for (int k = 0; k < rn; ++k) {
            if (n + 1 >= cap) {
                out[0] = '\0';
                return 0;
            }
            out[n++] = rssi[k];
        }
    }
    out[n] = '\0';
    return n;
}

std::size_t render_setup_page(const ScanCache& cache, char* out, std::size_t cap)
{
    char* nets = static_cast<char*>(std::malloc(kNetsBuf));
    if (nets == nullptr) {
        return 0;
    }
    const std::size_t nr = render_network_radios(cache, nets, kNetsBuf);
    if (nr == 0 && nets[0] == '\0') {
        std::free(nets);
        return 0;
    }
    const std::size_t n = replace_one(assets::kSetupHtmlTpl, "{{NETWORKS}}", nets, out, cap);
    std::free(nets);
    return n;
}

UploadRefuse check_upload_start(bool window_open, bool busy, bool have_length,
                                std::size_t content_length, std::size_t max_image)
{
    if (!window_open) {
        return UploadRefuse::WindowClosed;
    }
    if (busy) {
        return UploadRefuse::Busy;
    }
    if (!have_length || content_length == 0) {
        return UploadRefuse::NoFile;
    }
    if (content_length > max_image) {
        return UploadRefuse::TooLarge;
    }
    return UploadRefuse::Ok;
}

UploadRefuse check_firmware_magic(std::uint8_t first)
{
    if (first != kFirmwareMagic) {
        return UploadRefuse::NotFirmware;
    }
    return UploadRefuse::Ok;
}

const char* upload_refuse_reason(UploadRefuse r)
{
    switch (r) {
    case UploadRefuse::Ok:
        return "";
    case UploadRefuse::WindowClosed:
        return "Press the button on the dongle first.";
    case UploadRefuse::Busy:
        return "An update is already in progress.";
    case UploadRefuse::NoFile:
        return "No file received.";
    case UploadRefuse::TooLarge:
        return "File is too large to be firmware.";
    case UploadRefuse::NotFirmware:
        return "Not a firmware image.";
    }
    return "";
}

std::size_t render_update_window(bool open, unsigned left_s, bool busy, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    const unsigned shown = open ? left_s : 0;
    const int n = std::snprintf(out, cap, "{\"open\":%s,\"left_s\":%u,\"busy\":%s}",
                                open ? "true" : "false", shown, busy ? "true" : "false");
    if (n <= 0 || static_cast<std::size_t>(n) >= cap) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

std::size_t render_status_page(const StatusView& v, char* out, std::size_t cap)
{
    if (out == nullptr || cap < 64) {
        return 0;
    }
    char* scratch = static_cast<char*>(std::malloc(cap));
    if (scratch == nullptr) {
        return 0;
    }
    const char* src = assets::kStatusHtmlTpl;
    auto step = [&](std::string_view needle, const char* value) -> bool {
        const char* val = value != nullptr ? value : "";
        if (replace_one(src, needle, val, scratch, cap) == 0) {
            return false;
        }
        std::memcpy(out, scratch, std::strlen(scratch) + 1);
        src = out;
        return true;
    };
    const bool ok = step("{{DEVICE_ID}}", v.device_id) && step("{{FW_VER}}", v.fw_ver) &&
                    step("{{IP}}", v.ip) && step("{{UPTIME}}", v.uptime) &&
                    step("{{HEAP}}", v.heap) && step("{{RSSI}}", v.rssi) &&
                    step("{{MQTT}}", v.mqtt) && step("{{TELEGRAMS}}", v.telegrams);
    std::free(scratch);
    return ok ? std::strlen(out) : 0;
}

static bool json_escape_append(char* out, std::size_t cap, std::size_t& n, std::string_view s);

std::size_t render_api_status(const StatusView& v, char* out, std::size_t cap)
{
    if (out == nullptr || cap < 32) {
        return 0;
    }
    std::size_t n = 0;
    auto put = [&](const char* s) -> bool {
        for (const char* p = s; *p; ++p) {
            if (n + 1 >= cap) {
                return false;
            }
            out[n++] = *p;
        }
        return true;
    };
    auto put_str = [&](const char* key, const char* val) -> bool {
        if (!put("\"") || !put(key) || !put("\":\"")) {
            return false;
        }
        if (!json_escape_append(out, cap, n, val != nullptr ? val : "")) {
            return false;
        }
        return put("\"");
    };
    auto put_bool = [&](const char* key, bool val) -> bool {
        return put("\"") && put(key) && put("\":") && put(val ? "true" : "false");
    };
    auto put_u = [&](const char* key, unsigned val) -> bool {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%u", val);
        return put("\"") && put(key) && put("\":") && put(buf);
    };

    if (!put("{") || !put_str("device_id", v.device_id) || !put(",") ||
        !put_str("fw_ver", v.fw_ver) || !put(",") || !put_str("ip", v.ip) || !put(",") ||
        !put_str("uptime", v.uptime) || !put(",") || !put_str("heap", v.heap) || !put(",") ||
        !put_str("rssi", v.rssi) || !put(",") || !put_str("mqtt", v.mqtt) || !put(",") ||
        !put_str("mqtt_err", v.mqtt_err) || !put(",") || !put_bool("wifi", v.wifi) ||
        !put(",") || !put_bool("meter", v.meter) || !put(",") || !put_bool("ha_en", v.ha_en) ||
        !put(",") || !put_bool("mqtt_en", v.mqtt_en) || !put(",") ||
        !put_u("telegrams_ok", v.telegrams_ok) || !put(",") ||
        !put_u("telegrams_rejected", v.telegrams_rejected) || !put(",") ||
        !put_bool("upd_checked", v.upd_checked) || !put(",") ||
        !put_bool("upd_available", v.upd_available) || !put(",") ||
        !put_u("upd_found", v.upd_found) || !put(",") ||
        !put_str("upd_channel", v.upd_channel) || !put("}")) {
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t render_api_data(const ReadingsView& r, char* out, std::size_t cap)
{
    if (out == nullptr || cap < 2) {
        return 0;
    }
    std::size_t n = 0;
    auto putc = [&](char c) -> bool {
        if (n + 1 >= cap) {
            return false;
        }
        out[n++] = c;
        return true;
    };
    auto put = [&](const char* s) -> bool {
        for (const char* p = s; *p; ++p) {
            if (!putc(*p)) {
                return false;
            }
        }
        return true;
    };
    if (!putc('[')) {
        return 0;
    }
    bool first = true;
    const p1cfg::Field* cat = p1cfg::catalogue();
    const std::size_t nf = p1cfg::catalogue_size();
    const std::size_t lim = r.count < nf ? r.count : nf;
    for (std::size_t i = 0; i < lim; ++i) {
        if (r.fields == nullptr || r.fields[i].value[0] == '\0') {
            continue;
        }
        if (!first && !putc(',')) {
            return 0;
        }
        first = false;
        if (!put("{\"id\":\"") || !json_escape_append(out, cap, n, cat[i].id) ||
            !put("\",\"label\":\"") || !json_escape_append(out, cap, n, cat[i].label) ||
            !put("\",\"value\":\"") || !json_escape_append(out, cap, n, r.fields[i].value) ||
            !put("\",\"unit\":\"") || !json_escape_append(out, cap, n, r.fields[i].unit) ||
            !put("\"}")) {
            return 0;
        }
    }
    if (!putc(']')) {
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t render_api_fields(const p1cfg::Store& store, char* out, std::size_t cap)
{
    if (out == nullptr || cap < 2) {
        return 0;
    }
    std::size_t n = 0;
    auto putc = [&](char c) -> bool {
        if (n + 1 >= cap) {
            return false;
        }
        out[n++] = c;
        return true;
    };
    auto put = [&](const char* s) -> bool {
        for (const char* p = s; *p; ++p) {
            if (!putc(*p)) {
                return false;
            }
        }
        return true;
    };
    if (!putc('[')) {
        return 0;
    }
    bool first = true;
    const p1cfg::Field* cat = p1cfg::catalogue();
    const std::size_t nf = p1cfg::catalogue_size();
    for (std::size_t i = 0; i < nf; ++i) {
        if (!cat[i].selectable) {
            continue;
        }
        if (!first && !putc(',')) {
            return 0;
        }
        first = false;
        const char* region = "common";
        if (cat[i].region == p1cfg::RegionTag::Be) {
            region = "be";
        } else if (cat[i].region == p1cfg::RegionTag::Nl) {
            region = "nl";
        }
        if (!put("{\"id\":\"") || !json_escape_append(out, cap, n, cat[i].id) ||
            !put("\",\"label\":\"") || !json_escape_append(out, cap, n, cat[i].label) ||
            !put("\",\"region\":\"") || !put(region) || !put("\",\"on\":") ||
            !put(store.field_on(cat[i].id) ? "true" : "false") || !putc('}')) {
            return 0;
        }
    }
    if (!putc(']')) {
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t format_config_error(p1cfg::JsonStatus st, const char* bad_key, char* out,
                                std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    const char* name = p1cfg::json_status_name(st);
    if (bad_key == nullptr || bad_key[0] == '\0') {
        const int n = std::snprintf(out, cap, "%s", name);
        return n > 0 ? static_cast<std::size_t>(n) : 0;
    }
    const int n = std::snprintf(out, cap, "%s:%s", name, bad_key);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool url_decode_append(char* out, std::size_t cap, std::size_t& n, std::string_view in)
{
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && i + 2 < in.size()) {
            const int hi = hex_nibble(in[i + 1]);
            const int lo = hex_nibble(in[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            c = static_cast<char>((hi << 4) | lo);
            i += 2;
        }
        if (n + 1 >= cap) {
            return false;
        }
        out[n++] = c;
    }
    return true;
}

bool parse_save_form(std::string_view body, char* ssid, std::size_t ssid_cap, char* pass,
                     std::size_t pass_cap)
{
    if (ssid == nullptr || pass == nullptr || ssid_cap == 0 || pass_cap == 0) {
        return false;
    }
    ssid[0] = '\0';
    pass[0] = '\0';
    char other[kSsidMax]{};

    auto take = [&](std::string_view key, char* dest, std::size_t cap) {
        std::size_t start = 0;
        while (start < body.size()) {
            std::size_t amp = body.find('&', start);
            if (amp == std::string_view::npos) {
                amp = body.size();
            }
            std::string_view pair = body.substr(start, amp - start);
            const auto eq = pair.find('=');
            std::string_view k = eq == std::string_view::npos ? pair : pair.substr(0, eq);
            std::string_view v = eq == std::string_view::npos ? std::string_view{} : pair.substr(eq + 1);
            if (k == key) {
                std::size_t n = 0;
                if (!url_decode_append(dest, cap, n, v)) {
                    return false;
                }
                dest[n] = '\0';
                return true;
            }
            start = amp + 1;
        }
        return true;
    };

    if (!take("ssid", ssid, ssid_cap) || !take("ssid_other", other, sizeof(other)) ||
        !take("pass", pass, pass_cap)) {
        return false;
    }
    if (other[0] != '\0') {
        std::snprintf(ssid, ssid_cap, "%s", other);
    }
    return ssid[0] != '\0';
}

static bool json_escape_append(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    for (char c : s) {
        if (c == '"' || c == '\\') {
            if (n + 2 >= cap) {
                return false;
            }
            out[n++] = '\\';
            out[n++] = c;
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            continue;
        }
        if (n + 1 >= cap) {
            return false;
        }
        out[n++] = c;
    }
    return true;
}

std::size_t wifi_creds_json(std::string_view ssid, std::string_view pass, char* out,
                            std::size_t cap)
{
    if (out == nullptr || cap < 32) {
        return 0;
    }
    std::size_t n = 0;
    const char* pre = "{\"WIFI_SSID\":\"";
    for (const char* p = pre; *p; ++p) {
        out[n++] = *p;
    }
    if (!json_escape_append(out, cap, n, ssid)) {
        return 0;
    }
    const char* mid = "\",\"WIFI_PASS\":\"";
    for (const char* p = mid; *p; ++p) {
        if (n + 1 >= cap) {
            return 0;
        }
        out[n++] = *p;
    }
    if (!json_escape_append(out, cap, n, pass)) {
        return 0;
    }
    if (n + 3 >= cap) {
        return 0;
    }
    out[n++] = '"';
    out[n++] = '}';
    out[n] = '\0';
    return n;
}

}  // namespace p1web
