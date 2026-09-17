#include "P1Publish.h"

#include <cstring>

namespace p1pub {
namespace {

bool append(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    if (n + s.size() + 1 > cap) {
        return false;
    }
    for (char c : s) {
        out[n++] = c;
    }
    return true;
}

bool append_char(char* out, std::size_t cap, std::size_t& n, char c)
{
    if (n + 2 > cap) {
        return false;
    }
    out[n++] = c;
    return true;
}

void slug_char(char& c)
{
    if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
    } else if (c == ' ') {
        c = '_';
    }
}

bool append_slugged(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    if (n + s.size() + 1 > cap) {
        return false;
    }
    for (char c : s) {
        slug_char(c);
        out[n++] = c;
    }
    return true;
}

bool append_json_str(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    if (!append_char(out, cap, n, '"')) {
        return false;
    }
    for (char c : s) {
        if (c == '"' || c == '\\') {
            if (!append_char(out, cap, n, '\\') || !append_char(out, cap, n, c)) {
                return false;
            }
        } else if (static_cast<unsigned char>(c) < 0x20) {
            continue;
        } else if (!append_char(out, cap, n, c)) {
            return false;
        }
    }
    return append_char(out, cap, n, '"');
}

bool append_i64(char* out, std::size_t cap, std::size_t& n, std::int64_t v)
{
    char tmp[24];
    std::size_t i = 0;
    std::uint64_t u = 0;
    if (v < 0) {
        tmp[i++] = '-';
        u = static_cast<std::uint64_t>(-(v + 1)) + 1;
    } else {
        u = static_cast<std::uint64_t>(v);
    }
    char digits[20];
    std::size_t nd = 0;
    do {
        digits[nd++] = static_cast<char>('0' + (u % 10));
        u /= 10;
    } while (u != 0);
    while (nd > 0) {
        tmp[i++] = digits[--nd];
    }
    return append(out, cap, n, std::string_view(tmp, i));
}

// Thousandths → decimal text. Trailing zeros dropped; a whole number has no point.
bool append_milli(char* out, std::size_t cap, std::size_t& n, std::int64_t milli)
{
    char tmp[24];
    std::size_t i = 0;
    std::uint64_t u = 0;
    if (milli < 0) {
        tmp[i++] = '-';
        u = static_cast<std::uint64_t>(-(milli + 1)) + 1;
    } else {
        u = static_cast<std::uint64_t>(milli);
    }
    const std::uint64_t ip = u / 1000;
    std::uint32_t frac = static_cast<std::uint32_t>(u % 1000);

    char digits[20];
    std::size_t nd = 0;
    std::uint64_t x = ip;
    do {
        digits[nd++] = static_cast<char>('0' + (x % 10));
        x /= 10;
    } while (x != 0);
    while (nd > 0) {
        tmp[i++] = digits[--nd];
    }
    if (frac != 0) {
        tmp[i++] = '.';
        tmp[i++] = static_cast<char>('0' + (frac / 100));
        frac %= 100;
        tmp[i++] = static_cast<char>('0' + (frac / 10));
        frac %= 10;
        tmp[i++] = static_cast<char>('0' + frac);
        while (i > 0 && tmp[i - 1] == '0') {
            --i;
        }
    }
    return append(out, cap, n, std::string_view(tmp, i));
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

p1cfg::RegionTag region_of(const p1cfg::Settings& s)
{
    if (std::strcmp(s.region, "be") == 0) {
        return p1cfg::RegionTag::Be;
    }
    if (std::strcmp(s.region, "nl") == 0) {
        return p1cfg::RegionTag::Nl;
    }
    return p1cfg::RegionTag::Auto;
}

const char* kind_name(p1cfg::MetricKind k)
{
    switch (k) {
    case p1cfg::MetricKind::Cumulative:
        return "cumulative";
    case p1cfg::MetricKind::Gauge:
        return "gauge";
    case p1cfg::MetricKind::None:
        break;
    }
    return nullptr;
}

struct Val {
    enum class Kind : std::uint8_t { Absent, Milli, UInt, Str };
    Kind kind = Kind::Absent;
    std::int64_t milli = 0;
    std::uint32_t u = 0;
    std::string_view s;
    const char* unit = "";
    std::string_view capture_ts;
};

Val milli(bool present, const dsmr_parser::FixedValue& v, const char* unit)
{
    if (!present) {
        return {};
    }
    Val r;
    r.kind = Val::Kind::Milli;
    r.milli = v.int_val();
    r.unit = unit;
    return r;
}

Val milli_captured(bool present, const dsmr_parser::TimestampedFixedValue& v, const char* unit)
{
    Val r = milli(present, v, unit);
    if (r.kind != Val::Kind::Absent) {
        r.capture_ts = v.timestamp;
    }
    return r;
}

Val uint_val(bool present, std::uint32_t v)
{
    if (!present) {
        return {};
    }
    Val r;
    r.kind = Val::Kind::UInt;
    r.u = v;
    return r;
}

Val tariff_val(bool present, std::string_view s)
{
    if (!present) {
        return {};
    }
    std::uint32_t acc = 0;
    bool any = false;
    for (char c : s) {
        if (c < '0' || c > '9') {
            if (!any) {
                return {};
            }
            break;
        }
        any = true;
        if (acc > (0xFFFFFFFFu - static_cast<std::uint32_t>(c - '0')) / 10) {
            return {};
        }
        acc = acc * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (!any) {
        return {};
    }
    return uint_val(true, acc);
}

Val str_val(bool present, std::string_view v)
{
    if (!present) {
        return {};
    }
    Val r;
    r.kind = Val::Kind::Str;
    r.s = v;
    return r;
}

const char* publish_unit(const char* unit)
{
    if (unit != nullptr && std::strcmp(unit, "m3") == 0) {
        return "m\xC2\xB3";
    }
    return unit;
}

namespace f = dsmr_parser::fields;

Val value_of(const p1dsmr::Data& d, std::string_view id)
{
    if (id == "A-0:0.0.1") {
        if (!d.energy_delivered_tariff1_present || !d.energy_delivered_tariff2_present) {
            return {};
        }
        Val r;
        r.kind = Val::Kind::Milli;
        r.milli = static_cast<std::int64_t>(d.energy_delivered_tariff1.int_val()) +
                  d.energy_delivered_tariff2.int_val();
        r.unit = f::energy_delivered_tariff1::unit();
        return r;
    }
    if (id == "A-0:0.0.2") {
        if (!d.energy_returned_tariff1_present || !d.energy_returned_tariff2_present) {
            return {};
        }
        Val r;
        r.kind = Val::Kind::Milli;
        r.milli = static_cast<std::int64_t>(d.energy_returned_tariff1.int_val()) +
                  d.energy_returned_tariff2.int_val();
        r.unit = f::energy_returned_tariff1::unit();
        return r;
    }
    if (id == "A-0:0.0.3") {
        if (!d.power_delivered_present || !d.power_returned_present) {
            return {};
        }
        Val r;
        r.kind = Val::Kind::Milli;
        r.milli = static_cast<std::int64_t>(d.power_delivered.int_val()) -
                  d.power_returned.int_val();
        r.unit = f::power_delivered::unit();
        return r;
    }

    if (id == "1-0:1.8.1") {
        return milli(d.energy_delivered_tariff1_present, d.energy_delivered_tariff1,
                     f::energy_delivered_tariff1::unit());
    }
    if (id == "1-0:1.8.2") {
        return milli(d.energy_delivered_tariff2_present, d.energy_delivered_tariff2,
                     f::energy_delivered_tariff2::unit());
    }
    if (id == "1-0:2.8.1") {
        return milli(d.energy_returned_tariff1_present, d.energy_returned_tariff1,
                     f::energy_returned_tariff1::unit());
    }
    if (id == "1-0:2.8.2") {
        return milli(d.energy_returned_tariff2_present, d.energy_returned_tariff2,
                     f::energy_returned_tariff2::unit());
    }
    if (id == "0-0:96.14.0") {
        return tariff_val(d.electricity_tariff_present, d.electricity_tariff);
    }
    if (id == "1-0:1.7.0") {
        return milli(d.power_delivered_present, d.power_delivered, f::power_delivered::unit());
    }
    if (id == "1-0:2.7.0") {
        return milli(d.power_returned_present, d.power_returned, f::power_returned::unit());
    }
    if (id == "1-0:21.7.0") {
        return milli(d.power_delivered_l1_present, d.power_delivered_l1,
                     f::power_delivered_l1::unit());
    }
    if (id == "1-0:41.7.0") {
        return milli(d.power_delivered_l2_present, d.power_delivered_l2,
                     f::power_delivered_l2::unit());
    }
    if (id == "1-0:61.7.0") {
        return milli(d.power_delivered_l3_present, d.power_delivered_l3,
                     f::power_delivered_l3::unit());
    }
    if (id == "1-0:22.7.0") {
        return milli(d.power_returned_l1_present, d.power_returned_l1, f::power_returned_l1::unit());
    }
    if (id == "1-0:42.7.0") {
        return milli(d.power_returned_l2_present, d.power_returned_l2, f::power_returned_l2::unit());
    }
    if (id == "1-0:62.7.0") {
        return milli(d.power_returned_l3_present, d.power_returned_l3, f::power_returned_l3::unit());
    }
    if (id == "1-0:32.7.0") {
        return milli(d.voltage_l1_present, d.voltage_l1, f::voltage_l1::unit());
    }
    if (id == "1-0:52.7.0") {
        return milli(d.voltage_l2_present, d.voltage_l2, f::voltage_l2::unit());
    }
    if (id == "1-0:72.7.0") {
        return milli(d.voltage_l3_present, d.voltage_l3, f::voltage_l3::unit());
    }
    if (id == "1-0:31.7.0") {
        return milli(d.current_l1_present, d.current_l1, f::current_l1::unit());
    }
    if (id == "1-0:51.7.0") {
        return milli(d.current_l2_present, d.current_l2, f::current_l2::unit());
    }
    if (id == "1-0:71.7.0") {
        return milli(d.current_l3_present, d.current_l3, f::current_l3::unit());
    }
    if (id == "1-0:1.4.0") {
        return milli(d.active_energy_import_current_average_demand_present,
                     d.active_energy_import_current_average_demand,
                     f::active_energy_import_current_average_demand::unit());
    }
    if (id == "1-0:1.6.0") {
        return milli_captured(d.active_energy_import_maximum_demand_running_month_present,
                              d.active_energy_import_maximum_demand_running_month,
                              f::active_energy_import_maximum_demand_running_month::unit());
    }
    if (id == "0-1:24.2.3") {
        return milli_captured(d.gas_delivered_be_present, d.gas_delivered_be,
                              f::gas_delivered_be::unit());
    }
    if (id == "0-1:24.2.1") {
        return milli_captured(d.gas_delivered_present, d.gas_delivered, f::gas_delivered::unit());
    }
    if (id == "identification") {
        return str_val(d.identification_present, d.identification);
    }
    if (id == "1-3:0.2.8") {
        return str_val(d.p1_version_present, d.p1_version);
    }
    if (id == "0-0:96.1.4") {
        return str_val(d.p1_version_be_present, d.p1_version_be);
    }
    if (id == "0-0:1.0.0") {
        return str_val(d.timestamp_present, d.timestamp);
    }
    if (id == "0-0:96.1.1") {
        return str_val(d.equipment_id_present, d.equipment_id);
    }
    if (id == "0-0:17.0.0") {
        return milli(d.electricity_threshold_present, d.electricity_threshold,
                     f::electricity_threshold::unit());
    }
    if (id == "0-0:96.3.10") {
        return str_val(d.electricity_switch_position_present, d.electricity_switch_position);
    }
    if (id == "0-0:96.7.21") {
        return uint_val(d.electricity_failures_present, d.electricity_failures);
    }
    if (id == "0-0:96.7.9") {
        return uint_val(d.electricity_long_failures_present, d.electricity_long_failures);
    }
    if (id == "1-0:99.97.0") {
        return str_val(d.electricity_failure_log_present, d.electricity_failure_log);
    }
    if (id == "1-0:32.32.0") {
        return uint_val(d.electricity_sags_l1_present, d.electricity_sags_l1);
    }
    if (id == "1-0:52.32.0") {
        return uint_val(d.electricity_sags_l2_present, d.electricity_sags_l2);
    }
    if (id == "1-0:72.32.0") {
        return uint_val(d.electricity_sags_l3_present, d.electricity_sags_l3);
    }
    if (id == "1-0:32.36.0") {
        return uint_val(d.electricity_swells_l1_present, d.electricity_swells_l1);
    }
    if (id == "1-0:52.36.0") {
        return uint_val(d.electricity_swells_l2_present, d.electricity_swells_l2);
    }
    if (id == "1-0:72.36.0") {
        return uint_val(d.electricity_swells_l3_present, d.electricity_swells_l3);
    }
    if (id == "0-0:96.13.0") {
        return str_val(d.message_long_present, d.message_long);
    }
    if (id == "1-0:31.4.0") {
        return milli(d.current_fuse_l1_present, d.current_fuse_l1, f::current_fuse_l1::unit());
    }
    if (id == "0-1:24.1.0") {
        return uint_val(d.gas_device_type_present, d.gas_device_type);
    }
    if (id == "0-1:96.1.0") {
        return str_val(d.gas_equipment_id_present, d.gas_equipment_id);
    }
    if (id == "0-1:96.1.1") {
        return str_val(d.gas_equipment_id_be_present, d.gas_equipment_id_be);
    }
    if (id == "0-1:24.4.0") {
        return uint_val(d.gas_valve_position_present, d.gas_valve_position);
    }
    if (id == "0-2:24.1.0") {
        return uint_val(d.water_device_type_present, d.water_device_type);
    }
    if (id == "0-2:24.2.1") {
        return milli_captured(d.water_delivered_present, d.water_delivered,
                              f::water_delivered::unit());
    }
    return {};
}

bool append_value(char* out, std::size_t cap, std::size_t& n, const Val& v, bool json_str)
{
    switch (v.kind) {
    case Val::Kind::Milli:
        return append_milli(out, cap, n, v.milli);
    case Val::Kind::UInt:
        return append_i64(out, cap, n, v.u);
    case Val::Kind::Str:
        return json_str ? append_json_str(out, cap, n, v.s) : append(out, cap, n, v.s);
    case Val::Kind::Absent:
        break;
    }
    return false;
}

bool write_payload(char* out, std::size_t cap, std::size_t& n, const p1cfg::Field& field,
                   const Val& v, const p1cfg::Store& cfg, std::string_view telegram_ts)
{
    n = 0;
    const int fmt = cfg.values().frmt_pyld;
    if (fmt <= 0) {
        if (!append_value(out, cap, n, v, false)) {
            return false;
        }
        out[n] = '\0';
        return true;
    }

    if (!append_char(out, cap, n, '{') || !append(out, cap, n, "\"value\":") ||
        !append_value(out, cap, n, v, true)) {
        return false;
    }
    const char* unit = publish_unit(v.unit);
    if (unit != nullptr && unit[0] != '\0') {
        if (!append(out, cap, n, ",\"unit\":") || !append_json_str(out, cap, n, unit)) {
            return false;
        }
    }
    std::string_view ts = v.capture_ts.empty() ? telegram_ts : v.capture_ts;
    std::int64_t unix_ts = 0;
    if (dsmr_to_unix(ts, unix_ts)) {
        if (!append(out, cap, n, ",\"timestamp\":") || !append_i64(out, cap, n, unix_ts)) {
            return false;
        }
    }
    if (fmt >= 2) {
        if (!append(out, cap, n, ",\"friendly_name\":") ||
            !append_json_str(out, cap, n, field.label) ||
            !append(out, cap, n, ",\"sensorId\":\"")) {
            return false;
        }
        if (!append_slugged(out, cap, n, cfg.values().ha_device) ||
            !append_char(out, cap, n, '.') || !append_slugged(out, cap, n, field.label) ||
            !append_char(out, cap, n, '"')) {
            return false;
        }
    }
    if (fmt >= 3) {
        if (!append(out, cap, n, ",\"entity\":") ||
            !append_json_str(out, cap, n, cfg.values().ha_device)) {
            return false;
        }
        if (field.metric != nullptr && field.metric[0] != '\0') {
            if (!append(out, cap, n, ",\"metric\":") ||
                !append_json_str(out, cap, n, field.metric)) {
                return false;
            }
        }
        if (const char* kn = kind_name(field.kind)) {
            if (!append(out, cap, n, ",\"metricKind\":") || !append_json_str(out, cap, n, kn)) {
                return false;
            }
        }
    }
    if (!append_char(out, cap, n, '}')) {
        return false;
    }
    out[n] = '\0';
    return true;
}

}  // namespace

std::size_t reading_topic(const p1cfg::Store& cfg, const p1cfg::Field& field, char* out,
                          std::size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, cfg.values().mqtt_pfix) ||
        !append_slugged(out, cap, n, field.label)) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t availability_topic(const p1cfg::Store& cfg, char* out, std::size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, cfg.values().mqtt_pfix)) {
        out[0] = '\0';
        return 0;
    }
    if (n > 0 && out[n - 1] == '/') {
        --n;
    }
    out[n] = '\0';
    return n;
}

std::size_t Builder::emit_impl(const p1dsmr::Data& data, const p1cfg::Store& cfg, void* ctx,
                               void (*on_msg)(void*, std::string_view, std::string_view))
{
    const p1cfg::RegionTag filter = region_of(cfg.values());
    const std::string_view telegram_ts =
        data.timestamp_present ? data.timestamp : std::string_view{};
    const p1cfg::Field* all = p1cfg::catalogue();
    const std::size_t nf = p1cfg::catalogue_size();
    std::size_t count = 0;
    for (std::size_t i = 0; i < nf; ++i) {
        const p1cfg::Field& field = all[i];
        if (!cfg.field_on(field.id) || !p1cfg::field_in_region(field, filter)) {
            continue;
        }
        const Val v = value_of(data, field.id);
        if (v.kind == Val::Kind::Absent) {
            continue;
        }
        const std::size_t tlen = reading_topic(cfg, field, topic_, sizeof(topic_));
        if (tlen == 0) {
            continue;
        }
        std::size_t plen = 0;
        if (!write_payload(payload_, sizeof(payload_), plen, field, v, cfg, telegram_ts)) {
            continue;
        }
        on_msg(ctx, std::string_view(topic_, tlen), std::string_view(payload_, plen));
        ++count;
    }
    return count;
}

}  // namespace p1pub
