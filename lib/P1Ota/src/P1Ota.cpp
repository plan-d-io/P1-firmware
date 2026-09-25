#include "P1Ota.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace p1ota {
namespace {

constexpr char kBase[] = "https://raw.githubusercontent.com/plan-d-io/P1-dongle/";

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
    if (channel == "alpha") {
        return "alpha";
    }
    if (channel == "develop") {
        return "develop";
    }
    if (channel == "V2") {
        return "V2-0";
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

}  // namespace p1ota
