#include "P1Ota.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace p1ota {
namespace {

#if defined(P1_TEST_OTA_BASE)
constexpr char kBase[] = P1_TEST_OTA_BASE;
#else
constexpr char kBase[] = "https://raw.githubusercontent.com/plan-d-io/P1-dongle/";
#endif

bool append(char* out, std::size_t cap, std::size_t& n, const char* s)
{
    if (out == nullptr || s == nullptr) {
        return false;
    }
    for (const char* p = s; *p; ++p) {
        if (n + 1 >= cap) {
            return false;
        }
        out[n++] = *p;
    }
    return true;
}

}  // namespace

const char* channel_path(std::string_view channel)
{
    if (channel == "main") {
        return "main";
    }
    if (channel == "develop") {
        return "develop";
    }
    return nullptr;
}

std::size_t version_url(std::string_view channel, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    const char* seg = channel_path(channel);
    if (seg == nullptr) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, kBase) || !append(out, cap, n, seg) ||
        !append(out, cap, n, "/version")) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t firmware_url(std::string_view channel, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    const char* seg = channel_path(channel);
    if (seg == nullptr) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, kBase) || !append(out, cap, n, seg) ||
        !append(out, cap, n, "/p1-dongle.bin")) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return n;
}

bool parse_version(std::string_view body, unsigned& out)
{
    out = 0;
    while (!body.empty() &&
           (body.front() == ' ' || body.front() == '\t' || body.front() == '\r' ||
            body.front() == '\n')) {
        body.remove_prefix(1);
    }
    while (!body.empty() &&
           (body.back() == ' ' || body.back() == '\t' || body.back() == '\r' ||
            body.back() == '\n')) {
        body.remove_suffix(1);
    }
    if (body.empty()) {
        return false;
    }
    // Reject signs and decimal points — atoi("3.01") == 3 would strand the fleet.
    unsigned long v = 0;
    for (char c : body) {
        if (c < '0' || c > '9') {
            return false;
        }
        const unsigned long next = v * 10u + static_cast<unsigned long>(c - '0');
        if (next < v) {
            return false;  // overflow
        }
        v = next;
    }
    if (v == 0u || v > 0xFFFFFFFFu) {
        return false;
    }
    out = static_cast<unsigned>(v);
    return true;
}

std::size_t format_version(unsigned num, char* out, std::size_t cap)
{
    if (out == nullptr || cap < 5) {
        if (out != nullptr && cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    const unsigned major = num / 100u;
    const unsigned frac = num % 100u;
    const int n = std::snprintf(out, cap, "%u.%02u", major, frac);
    if (n < 0 || static_cast<std::size_t>(n) >= cap) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

bool parse_display_version(std::string_view s, unsigned& out)
{
    out = 0;
    if (s.empty()) {
        return false;
    }
    std::size_t i = 0;
    unsigned major = 0;
    bool any = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        major = major * 10u + static_cast<unsigned>(s[i] - '0');
        ++i;
        any = true;
    }
    if (!any || i >= s.size() || s[i] != '.') {
        return false;
    }
    ++i;
    unsigned frac = 0;
    unsigned places = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        if (places >= 2) {
            return false;
        }
        frac = frac * 10u + static_cast<unsigned>(s[i] - '0');
        ++i;
        ++places;
    }
    if (places == 0 || i != s.size()) {
        return false;
    }
    if (places == 1) {
        frac *= 10u;
    }
    out = major * 100u + frac;
    return true;
}

bool should_install(unsigned found, unsigned running, unsigned failed, bool automatic,
                    std::uint32_t elapsed_s)
{
    if (found <= running) {
        return false;
    }
    if (automatic && failed != 0 && found == failed && elapsed_s < kAutoFailSkipS) {
        return false;
    }
    return true;
}

std::uint32_t fail_elapsed_s(std::uint32_t unix_noticed, std::uint32_t boot_ms_at,
                             bool clock_valid, std::uint32_t now_unix,
                             std::uint32_t now_boot_ms)
{
    if (unix_noticed != 0 && clock_valid) {
        if (now_unix <= unix_noticed) {
            return 0;
        }
        return now_unix - unix_noticed;
    }
    return (now_boot_ms - boot_ms_at) / 1000u;
}

std::uint32_t fail_unix_stamp(std::uint32_t now_unix, std::uint32_t now_boot_ms,
                              std::uint32_t boot_ms_at)
{
    const std::uint32_t up_s = (now_boot_ms - boot_ms_at) / 1000u;
    if (now_unix <= up_s) {
        return now_unix;
    }
    return now_unix - up_s;
}

std::size_t format_unix_utc(std::uint32_t unix, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    const std::time_t t = static_cast<std::time_t>(unix);
    std::tm tm{};
#if defined(_MSC_VER)
    if (gmtime_s(&tm, &t) != 0) {
        return 0;
    }
#else
    const std::tm* p = std::gmtime(&t);
    if (p == nullptr) {
        return 0;
    }
    tm = *p;
#endif
    const int n = std::snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d UTC",
                                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                                tm.tm_min, tm.tm_sec);
    if (n < 0 || static_cast<std::size_t>(n) >= cap) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

std::size_t format_boot_age(std::uint32_t elapsed_s, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    int n = 0;
    if (elapsed_s >= 3600u) {
        n = std::snprintf(out, cap, "boot+%uh", static_cast<unsigned>(elapsed_s / 3600u));
    } else if (elapsed_s >= 60u) {
        n = std::snprintf(out, cap, "boot+%um", static_cast<unsigned>(elapsed_s / 60u));
    } else {
        n = std::snprintf(out, cap, "boot+%us", static_cast<unsigned>(elapsed_s));
    }
    if (n < 0 || static_cast<std::size_t>(n) >= cap) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

bool storage_on_trial_should_rollback(bool on_trial, bool filesystem_ok, bool settings_ok)
{
    return on_trial && (!filesystem_ok || !settings_ok);
}

bool storage_format_allowed(bool on_trial)
{
    return !on_trial;
}

}  // namespace p1ota
