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
    if (uri == "/" || uri == "/status" || uri == "/status.html" || uri == "/index.html") {
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
        timing_ = false;
        out.phase = phase_;
        out.want_ap = true;
        out.want_sta = false;
        return out;
    }

    if (in.sta_connected) {
        phase_ = ProvPhase::Station;
        timing_ = false;
        out.phase = phase_;
        out.want_ap = false;
        out.want_sta = true;
        return out;
    }

    // Have credentials but not connected.
    if (phase_ == ProvPhase::Station || phase_ == ProvPhase::NeedAp || !timing_) {
        phase_ = ProvPhase::Connecting;
        connect_started_ms_ = in.now_ms;
        timing_ = true;
    }

    if (timing_ && (in.now_ms - connect_started_ms_) >= kConnectTimeoutMs) {
        phase_ = ProvPhase::NeedAp;
        timing_ = false;
        out.phase = phase_;
        out.want_ap = true;
        out.want_sta = true;  // keep trying STA in background
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
        char rssi[32];
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
    char nets[2048];
    if (render_network_radios(cache, nets, sizeof(nets)) == 0 && nets[0] == '\0') {
        // render_network_radios always writes something; treat overflow as fail
    }
    return replace_one(assets::kSetupHtmlTpl, "{{NETWORKS}}", nets, out, cap);
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
