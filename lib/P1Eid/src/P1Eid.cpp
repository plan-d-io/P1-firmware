#include "P1Eid.h"

#include <cstdio>
#include <cstring>

namespace p1eid {
namespace {

bool append(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    if (out == nullptr) {
        return false;
    }
    for (char c : s) {
        if (n + 1 >= cap) {
            return false;
        }
        out[n++] = c;
    }
    return true;
}

bool append_json_str(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    if (!append(out, cap, n, "\"")) {
        return false;
    }
    for (char c : s) {
        if (c == '"' || c == '\\') {
            if (!append(out, cap, n, "\\")) {
                return false;
            }
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            continue;
        }
        if (n + 1 >= cap) {
            return false;
        }
        out[n++] = c;
    }
    return append(out, cap, n, "\"");
}

bool append_i64(char* out, std::size_t cap, std::size_t& n, std::int64_t v)
{
    char buf[24];
    const int w = std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    if (w <= 0) {
        return false;
    }
    return append(out, cap, n, std::string_view(buf, static_cast<std::size_t>(w)));
}

bool eq_key(std::string_view a, const char* b)
{
    return a == std::string_view(b);
}

struct Cur {
    const char* p = nullptr;
    const char* end = nullptr;

    void skip()
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            ++p;
        }
    }

    bool eat(char c)
    {
        skip();
        if (p >= end || *p != c) {
            return false;
        }
        ++p;
        return true;
    }

    bool peek(char c)
    {
        skip();
        return p < end && *p == c;
    }
};

bool parse_string(Cur& c, char* out, std::size_t cap)
{
    c.skip();
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    std::size_t n = 0;
    while (c.p < c.end) {
        char ch = *c.p++;
        if (ch == '"') {
            if (out != nullptr && cap > 0) {
                if (n >= cap) {
                    return false;
                }
                out[n] = '\0';
            }
            return true;
        }
        if (ch == '\\') {
            if (c.p >= c.end) {
                return false;
            }
            const char e = *c.p++;
            if (e == '"' || e == '\\' || e == '/') {
                ch = e;
            } else if (e == 'n') {
                ch = '\n';
            } else if (e == 't') {
                ch = '\t';
            } else {
                ch = e;
            }
        }
        if (out != nullptr && cap > 0) {
            if (n + 1 >= cap) {
                return false;
            }
            out[n++] = ch;
        }
    }
    return false;
}

bool skip_string(Cur& c)
{
    return parse_string(c, nullptr, 0);
}

bool parse_i64(Cur& c, std::int64_t& v)
{
    c.skip();
    if (c.p >= c.end) {
        return false;
    }
    bool neg = false;
    if (*c.p == '-') {
        neg = true;
        ++c.p;
    }
    if (c.p >= c.end || *c.p < '0' || *c.p > '9') {
        return false;
    }
    std::uint64_t acc = 0;
    while (c.p < c.end && *c.p >= '0' && *c.p <= '9') {
        acc = acc * 10 + static_cast<std::uint64_t>(*c.p - '0');
        ++c.p;
    }
    v = neg ? -static_cast<std::int64_t>(acc) : static_cast<std::int64_t>(acc);
    return true;
}

bool skip_value(Cur& c);

bool skip_object(Cur& c)
{
    if (!c.eat('{')) {
        return false;
    }
    bool first = true;
    while (!c.peek('}')) {
        if (!first && !c.eat(',')) {
            return false;
        }
        first = false;
        if (!skip_string(c) || !c.eat(':') || !skip_value(c)) {
            return false;
        }
    }
    return c.eat('}');
}

bool skip_array(Cur& c)
{
    if (!c.eat('[')) {
        return false;
    }
    bool first = true;
    while (!c.peek(']')) {
        if (!first && !c.eat(',')) {
            return false;
        }
        first = false;
        if (!skip_value(c)) {
            return false;
        }
    }
    return c.eat(']');
}

bool skip_value(Cur& c)
{
    c.skip();
    if (c.p >= c.end) {
        return false;
    }
    const char ch = *c.p;
    if (ch == '"') {
        return skip_string(c);
    }
    if (ch == '{') {
        return skip_object(c);
    }
    if (ch == '[') {
        return skip_array(c);
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
        std::int64_t dummy = 0;
        return parse_i64(c, dummy);
    }
    if (ch == 't' || ch == 'f' || ch == 'n') {
        while (c.p < c.end && *c.p >= 'a' && *c.p <= 'z') {
            ++c.p;
        }
        return true;
    }
    return false;
}

bool parse_headers(Cur& c, HelloReply& out)
{
    if (!c.eat('{')) {
        return false;
    }
    bool first = true;
    while (!c.peek('}')) {
        if (!first && !c.eat(',')) {
            return false;
        }
        first = false;
        char key[24]{};
        if (!parse_string(c, key, sizeof(key)) || !c.eat(':')) {
            return false;
        }
        if (eq_key(key, "authorization") || eq_key(key, "Authorization")) {
            if (!parse_string(c, out.authorization, sizeof(out.authorization))) {
                return false;
            }
        } else if (eq_key(key, "x-twin-id") || eq_key(key, "X-Twin-Id")) {
            if (!parse_string(c, out.twin_id, sizeof(out.twin_id))) {
                return false;
            }
        } else if (!skip_value(c)) {
            return false;
        }
    }
    return c.eat('}');
}

bool parse_policy(Cur& c, HelloReply& out)
{
    if (!c.eat('{')) {
        return false;
    }
    bool first = true;
    while (!c.peek('}')) {
        if (!first && !c.eat(',')) {
            return false;
        }
        first = false;
        char key[24]{};
        if (!parse_string(c, key, sizeof(key)) || !c.eat(':')) {
            return false;
        }
        if (eq_key(key, "allowedInterval")) {
            if (!parse_string(c, out.allowed_interval, sizeof(out.allowed_interval))) {
                return false;
            }
        } else if (eq_key(key, "uploadInterval")) {
            std::int64_t v = 0;
            if (!parse_i64(c, v) || v < 0) {
                return false;
            }
            out.have_upload_interval = true;
            out.upload_interval = static_cast<std::uint32_t>(v);
        } else if (!skip_value(c)) {
            return false;
        }
    }
    return c.eat('}');
}

bool append_eid_milli(char* out, std::size_t cap, std::size_t& n, std::int64_t milli)
{
    // round2(milli/1000): (int)(value*100 + 0.05)/100, truncating toward zero.
    const bool neg = milli < 0;
    std::uint64_t u = 0;
    if (neg) {
        u = static_cast<std::uint64_t>(-(milli + 1)) + 1;
    } else {
        u = static_cast<std::uint64_t>(milli);
    }
    const int cents = static_cast<int>(static_cast<double>(u) / 10.0 + 0.05);
    if (neg && !append(out, cap, n, "-")) {
        return false;
    }
    const int whole = cents / 100;
    const int frac = cents % 100;
    char buf[16];
    const int w = std::snprintf(buf, sizeof(buf), "%d", whole);
    if (w <= 0 || !append(out, cap, n, std::string_view(buf, static_cast<std::size_t>(w)))) {
        return false;
    }
    if (frac == 0) {
        return true;
    }
    buf[0] = '.';
    buf[1] = static_cast<char>('0' + (frac / 10));
    buf[2] = static_cast<char>('0' + (frac % 10));
    return append(out, cap, n, std::string_view(buf, 3));
}

bool append_field(char* out, std::size_t cap, std::size_t& n, bool& first, const char* key,
                  bool present, std::int64_t milli)
{
    if (!present) {
        return true;
    }
    if (!first && !append(out, cap, n, ",")) {
        return false;
    }
    first = false;
    return append(out, cap, n, "\"") && append(out, cap, n, key) && append(out, cap, n, "\":") &&
           append_eid_milli(out, cap, n, milli);
}

bool append_record(char* out, std::size_t cap, std::size_t& n, bool& first_rec, std::int64_t ts,
                   const char* key, std::int64_t milli)
{
    if (!first_rec && !append(out, cap, n, ",")) {
        return false;
    }
    first_rec = false;
    return append(out, cap, n, "{\"ts\":") && append_i64(out, cap, n, ts) &&
           append(out, cap, n, ",\"") && append(out, cap, n, key) && append(out, cap, n, "\":") &&
           append_eid_milli(out, cap, n, milli) && append(out, cap, n, "}");
}

}  // namespace

std::size_t hello_body(char* out, std::size_t cap, const char* claim_code, const char* device_id,
                       const char* firmware_version, const char* ip)
{
    if (out == nullptr || cap < 2) {
        return 0;
    }
    const char* claim = claim_code != nullptr ? claim_code : "";
    const char* uuid = device_id != nullptr ? device_id : "";
    const char* fw = firmware_version != nullptr ? firmware_version : "";
    const char* addr = ip != nullptr ? ip : "";
    char url[32];
    const int uw = std::snprintf(url, sizeof(url), "http://%s", addr);
    if (uw <= 0 || static_cast<std::size_t>(uw) >= sizeof(url)) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, "{") || !append(out, cap, n, "\"claimCode\":") ||
        !append_json_str(out, cap, n, claim) || !append(out, cap, n, ",\"deviceId\":") ||
        !append_json_str(out, cap, n, uuid) || !append(out, cap, n, ",\"deviceName\":") ||
        !append_json_str(out, cap, n, kDeviceName) || !append(out, cap, n, ",\"firmwareVersion\":") ||
        !append_json_str(out, cap, n, fw) || !append(out, cap, n, ",\"ipAddress\":") ||
        !append_json_str(out, cap, n, addr) || !append(out, cap, n, ",\"lcoalDeviceUrl\":") ||
        !append_json_str(out, cap, n, url) || !append(out, cap, n, "}")) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::uint32_t parse_iso_interval(std::string_view iso)
{
    if (iso.size() < 3 || (iso[0] != 'P' && iso[0] != 'p')) {
        return 0;
    }
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    const char last = iso[iso.size() - 1];
    if (last == 'D' || last == 'd') {
        unsigned n = 0;
        for (std::size_t i = 1; i + 1 < iso.size(); ++i) {
            if (!is_digit(iso[i])) {
                return 0;
            }
            n = n * 10 + static_cast<unsigned>(iso[i] - '0');
        }
        return n * 86400u;
    }
    if (iso[1] != 'T' && iso[1] != 't') {
        return 0;
    }
    unsigned n = 0;
    for (std::size_t i = 2; i + 1 < iso.size(); ++i) {
        if (!is_digit(iso[i])) {
            return 0;
        }
        n = n * 10 + static_cast<unsigned>(iso[i] - '0');
    }
    if (last == 'S' || last == 's') {
        return n;
    }
    if (last == 'M' || last == 'm') {
        return n * 60u;
    }
    if (last == 'H' || last == 'h') {
        return n * 3600u;
    }
    return 0;
}

std::uint32_t choose_interval(bool have_upload, std::uint32_t upload_s, std::string_view allowed_iso)
{
    const std::uint32_t allowed = parse_iso_interval(allowed_iso);
    std::uint32_t v = kIntervalDefaultS;
    if (have_upload) {
        v = upload_s;
    } else if (allowed != 0) {
        v = allowed;
    }
    if (allowed != 0 && v < allowed) {
        v = allowed;
    }
    if (v < kIntervalMinS) {
        v = kIntervalMinS;
    }
    if (v > kIntervalMaxS) {
        v = kIntervalMaxS;
    }
    return v;
}

bool parse_sas_expiry(std::string_view authorization, std::int64_t& out)
{
    const char* p = authorization.data();
    const char* end = p + authorization.size();
    while (p + 3 < end) {
        if ((p == authorization.data() || p[-1] == '&' || p[-1] == '?') && p[0] == 's' &&
            p[1] == 'e' && p[2] == '=') {
            p += 3;
            if (p >= end || *p < '0' || *p > '9') {
                return false;
            }
            std::int64_t v = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                ++p;
            }
            out = v;
            return true;
        }
        ++p;
    }
    return false;
}

bool parse_hello_reply(std::string_view json, HelloReply& out)
{
    out = HelloReply{};
    Cur c{json.data(), json.data() + json.size()};
    if (!c.eat('{')) {
        return false;
    }
    bool first = true;
    while (!c.peek('}')) {
        if (!first && !c.eat(',')) {
            return false;
        }
        first = false;
        char key[24]{};
        if (!parse_string(c, key, sizeof(key)) || !c.eat(':')) {
            return false;
        }
        if (eq_key(key, "claimCode")) {
            if (!parse_string(c, out.claim_code, sizeof(out.claim_code))) {
                return false;
            }
        } else if (eq_key(key, "claimUrl")) {
            if (!parse_string(c, out.claim_url, sizeof(out.claim_url))) {
                return false;
            }
        } else if (eq_key(key, "exp")) {
            if (!parse_i64(c, out.exp)) {
                return false;
            }
        } else if (eq_key(key, "webhookUrl")) {
            if (!parse_string(c, out.webhook_url, sizeof(out.webhook_url))) {
                return false;
            }
        } else if (eq_key(key, "headers")) {
            if (!parse_headers(c, out)) {
                return false;
            }
        } else if (eq_key(key, "webhookPolicy")) {
            if (!parse_policy(c, out)) {
                return false;
            }
        } else if (eq_key(key, "recordName")) {
            if (!parse_string(c, out.record_name, sizeof(out.record_name))) {
                return false;
            }
        } else if (eq_key(key, "recordNumber")) {
            if (!parse_string(c, out.record_number, sizeof(out.record_number))) {
                return false;
            }
        } else if (!skip_value(c)) {
            return false;
        }
    }
    if (!c.eat('}')) {
        return false;
    }
    if (out.webhook_url[0] != '\0') {
        if (out.authorization[0] == '\0') {
            out.kind = ReplyKind::Error;
            return false;
        }
        out.kind = ReplyKind::Claimed;
        out.interval_s = choose_interval(out.have_upload_interval, out.upload_interval,
                                         out.allowed_interval);
        parse_sas_expiry(out.authorization, out.token_expiry);
        return true;
    }
    if (out.claim_code[0] != '\0') {
        out.kind = ReplyKind::Unclaimed;
        return true;
    }
    out.kind = ReplyKind::Error;
    return false;
}

bool dsmr_to_unix(std::string_view ts, std::int64_t& out)
{
    if (ts.size() != 13) {
        return false;
    }
    auto two = [](char a, char b, int& v) {
        if (a < '0' || a > '9' || b < '0' || b > '9') {
            return false;
        }
        v = (a - '0') * 10 + (b - '0');
        return true;
    };
    int yy = 0, mo = 0, dd = 0, hh = 0, mm = 0, ss = 0;
    if (!two(ts[0], ts[1], yy) || !two(ts[2], ts[3], mo) || !two(ts[4], ts[5], dd) ||
        !two(ts[6], ts[7], hh) || !two(ts[8], ts[9], mm) || !two(ts[10], ts[11], ss)) {
        return false;
    }
    const char x = ts[12];
    if (x != 'S' && x != 'W') {
        return false;
    }
    if (mo < 1 || mo > 12 || dd < 1 || dd > 31 || hh > 23 || mm > 59 || ss > 59) {
        return false;
    }
    int y = 2000 + yy;
    const unsigned m = static_cast<unsigned>(mo);
    const unsigned d = static_cast<unsigned>(dd);
    y -= (m <= 2) ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = era * 146097 + static_cast<std::int64_t>(doe) - 719468;
    out = days * 86400 + static_cast<std::int64_t>(hh) * 3600 + mm * 60 + ss;
    out -= (x == 'S') ? 7200 : 3600;
    return true;
}

std::size_t upload_batch(const p1dsmr::Data& data, char* out, std::size_t cap)
{
    if (out == nullptr || cap < 2) {
        return 0;
    }
    std::int64_t meter_ts = 0;
    if (!data.timestamp_present || !dsmr_to_unix(data.timestamp, meter_ts)) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, "[")) {
        return 0;
    }
    bool first_rec = true;
    if (!append(out, cap, n, "{\"ts\":") || !append_i64(out, cap, n, meter_ts)) {
        return 0;
    }
    bool first_f = false;
    auto milli_of = [](bool present, const auto& v, std::int64_t& m) {
        if (!present) {
            return false;
        }
        m = v.int_val();
        return true;
    };
    std::int64_t m = 0;
    if (milli_of(data.energy_delivered_tariff1_present, data.energy_delivered_tariff1, m) &&
        !append_field(out, cap, n, first_f, "t1", true, m)) {
        return 0;
    }
    if (milli_of(data.energy_delivered_tariff2_present, data.energy_delivered_tariff2, m) &&
        !append_field(out, cap, n, first_f, "t2", true, m)) {
        return 0;
    }
    if (milli_of(data.energy_returned_tariff1_present, data.energy_returned_tariff1, m) &&
        !append_field(out, cap, n, first_f, "t1-i", true, m)) {
        return 0;
    }
    if (milli_of(data.energy_returned_tariff2_present, data.energy_returned_tariff2, m) &&
        !append_field(out, cap, n, first_f, "t2-i", true, m)) {
        return 0;
    }
    if (milli_of(data.power_delivered_present, data.power_delivered, m) &&
        !append_field(out, cap, n, first_f, "pwr", true, m)) {
        return 0;
    }
    if (milli_of(data.power_returned_present, data.power_returned, m) &&
        !append_field(out, cap, n, first_f, "pwr-i", true, m)) {
        return 0;
    }
    if (milli_of(data.active_energy_import_current_average_demand_present,
                 data.active_energy_import_current_average_demand, m) &&
        !append_field(out, cap, n, first_f, "pp", true, m)) {
        return 0;
    }
    if (milli_of(data.voltage_l1_present, data.voltage_l1, m) &&
        !append_field(out, cap, n, first_f, "volt1", true, m)) {
        return 0;
    }
    if (milli_of(data.current_l1_present, data.current_l1, m) &&
        !append_field(out, cap, n, first_f, "cur1", true, m)) {
        return 0;
    }
    if (!append(out, cap, n, "}")) {
        return 0;
    }
    first_rec = false;

    if (data.active_energy_import_maximum_demand_running_month_present) {
        std::int64_t ts = meter_ts;
        dsmr_to_unix(data.active_energy_import_maximum_demand_running_month.timestamp, ts);
        if (!append_record(out, cap, n, first_rec, ts, "high-pp",
                           data.active_energy_import_maximum_demand_running_month.int_val())) {
            return 0;
        }
    }
    if (data.gas_delivered_be_present) {
        std::int64_t ts = meter_ts;
        dsmr_to_unix(data.gas_delivered_be.timestamp, ts);
        if (!append_record(out, cap, n, first_rec, ts, "g", data.gas_delivered_be.int_val())) {
            return 0;
        }
    }
    if (data.water_delivered_present) {
        std::int64_t ts = meter_ts;
        dsmr_to_unix(data.water_delivered.timestamp, ts);
        if (!append_record(out, cap, n, first_rec, ts, "w", data.water_delivered.int_val())) {
            return 0;
        }
    }
    if (!append(out, cap, n, "]")) {
        return 0;
    }
    out[n] = '\0';
    return n;
}

}  // namespace p1eid
