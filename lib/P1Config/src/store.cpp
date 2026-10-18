#include "P1Config.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#if __has_include("eid_keys_gen.h")
#include "eid_keys_gen.h"
#endif
#ifndef P1_EID_KEYS
#define P1_EID_KEYS 0
#endif

namespace p1cfg {
namespace {

void copy_str(char* dst, std::size_t cap, const char* src)
{
    if (cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::size_t n = 0;
    while (n + 1 < cap && src[n] != '\0') {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

void copy_network(NetworkSettings& d, const Settings& s)
{
    copy_str(d.wifi_ssid, sizeof(d.wifi_ssid), s.wifi_ssid);
    copy_str(d.wifi_pass, sizeof(d.wifi_pass), s.wifi_pass);
    d.fip_en = s.fip_en;
    copy_str(d.fipaddr, sizeof(d.fipaddr), s.fipaddr);
    copy_str(d.fsubn, sizeof(d.fsubn), s.fsubn);
    copy_str(d.fdefgtw, sizeof(d.fdefgtw), s.fdefgtw);
    copy_str(d.fdns1, sizeof(d.fdns1), s.fdns1);
    copy_str(d.fdns2, sizeof(d.fdns2), s.fdns2);
}

bool network_differs(const Settings& a, const Settings& b)
{
    return std::strcmp(a.wifi_ssid, b.wifi_ssid) != 0 ||
           std::strcmp(a.wifi_pass, b.wifi_pass) != 0 || a.fip_en != b.fip_en ||
           std::strcmp(a.fipaddr, b.fipaddr) != 0 || std::strcmp(a.fsubn, b.fsubn) != 0 ||
           std::strcmp(a.fdefgtw, b.fdefgtw) != 0 || std::strcmp(a.fdns1, b.fdns1) != 0 ||
           std::strcmp(a.fdns2, b.fdns2) != 0;
}

bool network_differs(const NetworkSettings& a, const Settings& b)
{
    return std::strcmp(a.wifi_ssid, b.wifi_ssid) != 0 ||
           std::strcmp(a.wifi_pass, b.wifi_pass) != 0 || a.fip_en != b.fip_en ||
           std::strcmp(a.fipaddr, b.fipaddr) != 0 || std::strcmp(a.fsubn, b.fsubn) != 0 ||
           std::strcmp(a.fdefgtw, b.fdefgtw) != 0 || std::strcmp(a.fdns1, b.fdns1) != 0 ||
           std::strcmp(a.fdns2, b.fdns2) != 0;
}

int field_index(std::string_view id)
{
    const Field* all = catalogue();
    const std::size_t n = catalogue_size();
    for (std::size_t i = 0; i < n; ++i) {
        if (id == all[i].id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

bool parse_ipv4(const char* s, std::uint32_t& host_order)
{
    if (s == nullptr || s[0] == '\0') {
        return false;
    }
    unsigned parts = 0;
    const char* p = s;
    std::uint32_t acc = 0;
    while (*p != '\0') {
        if (parts >= 4) {
            return false;
        }
        if (*p < '0' || *p > '9') {
            return false;
        }
        unsigned v = 0;
        unsigned digits = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10u + static_cast<unsigned>(*p - '0');
            if (v > 255u || ++digits > 3u) {
                return false;
            }
            ++p;
        }
        if (digits == 0) {
            return false;
        }
        acc = (acc << 8) | static_cast<std::uint32_t>(v);
        ++parts;
        if (*p == '.') {
            ++p;
            if (*p == '\0') {
                return false;
            }
        } else if (*p != '\0') {
            return false;
        }
    }
    if (parts != 4) {
        return false;
    }
    host_order = acc;
    return true;
}

bool ip_ok(const char* s)
{
    std::uint32_t unused = 0;
    return parse_ipv4(s, unused);
}

std::size_t write_legacy_update_reason(char* out, std::size_t cap, const char* version_display)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    const char* kBase = "Updated from legacy firmware";
    if (version_display == nullptr || version_display[0] == '\0') {
        const int n = std::snprintf(out, cap, "%s", kBase);
        if (n <= 0 || static_cast<std::size_t>(n) >= cap) {
            out[0] = '\0';
            return 0;
        }
        return static_cast<std::size_t>(n);
    }
    const int n = std::snprintf(out, cap, "%s %s", kBase, version_display);
    if (n <= 0 || static_cast<std::size_t>(n) >= cap) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

const char* ha_reason_or_none(const char* reason)
{
    if (reason == nullptr || reason[0] == '\0') {
        return "none";
    }
    return reason;
}

bool official_build()
{
#if P1_EID_KEYS
    return true;
#else
    return false;
#endif
}

void apply_update_defaults(Settings& v, bool official)
{
    v.upd_auto = official;
    v.upd_autochk = official;
}

bool mask_contiguous(std::uint32_t mask_host)
{
    if (mask_host == 0) {
        return false;
    }
    const std::uint32_t inv = ~mask_host;
    return (inv & (inv + 1u)) == 0;
}

bool static_ip_consistent(const char* ip_s, const char* mask_s, const char* gw_s)
{
    std::uint32_t ip = 0;
    std::uint32_t mask = 0;
    std::uint32_t gw = 0;
    if (!parse_ipv4(ip_s, ip) || !parse_ipv4(mask_s, mask) || !parse_ipv4(gw_s, gw)) {
        return false;
    }
    if (!mask_contiguous(mask)) {
        return false;
    }
    if ((ip & mask) != (gw & mask)) {
        return false;
    }
    const std::uint32_t host = ~mask;
    if (host == 0) {
        return false;  // /32 — no distinct network/broadcast; reject for STA use
    }
    if ((ip & host) == 0 || (ip & host) == host) {
        return false;  // network or broadcast
    }
    if ((gw & host) == 0 || (gw & host) == host) {
        return false;
    }
    return true;
}

bool rel_chan_ok(const char* s)
{
    return s != nullptr && (std::strcmp(s, "main") == 0 || std::strcmp(s, "develop") == 0);
}

bool format_arduino_ipv4(std::uint32_t v, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return false;
    }
    if (v == 0) {
        out[0] = '\0';
        return true;
    }
    const int n = std::snprintf(out, cap, "%u.%u.%u.%u", static_cast<unsigned>(v & 0xffu),
                                static_cast<unsigned>((v >> 8) & 0xffu),
                                static_cast<unsigned>((v >> 16) & 0xffu),
                                static_cast<unsigned>((v >> 24) & 0xffu));
    return n > 0 && static_cast<std::size_t>(n) < cap;
}

bool format_uuid(char* out, std::size_t cap, const std::uint8_t* rnd4)
{
    if (out == nullptr || rnd4 == nullptr || cap < 11) {
        return false;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    out[0] = 'P';
    out[1] = '1';
    for (std::size_t i = 0; i < 4; ++i) {
        out[2 + i * 2] = kHex[(rnd4[i] >> 4) & 0xf];
        out[3 + i * 2] = kHex[rnd4[i] & 0xf];
    }
    out[10] = '\0';
    return true;
}

bool ensure_uuid(char* uuid, std::size_t cap, const std::uint8_t* rnd4)
{
    if (uuid == nullptr || uuid[0] != '\0') {
        return false;
    }
    return format_uuid(uuid, cap, rnd4);
}

bool ensure_claim_code(const char* uuid, char* claim, std::size_t cap)
{
    if (claim == nullptr || cap == 0 || claim[0] != '\0') {
        return false;
    }
    if (uuid == nullptr || uuid[0] != 'P' || uuid[1] != '1' || uuid[2] == '\0') {
        return false;
    }
    copy_str(claim, cap, uuid + 2);
    return true;
}

const char* json_status_name(JsonStatus s)
{
    switch (s) {
    case JsonStatus::Ok:
        return "Ok";
    case JsonStatus::Parse:
        return "Parse";
    case JsonStatus::UnknownKey:
        return "UnknownKey";
    case JsonStatus::WrongType:
        return "WrongType";
    case JsonStatus::Invalid:
        return "Invalid";
    }
    return "?";
}

namespace {

bool region_ok(const char* s)
{
    return std::strcmp(s, "auto") == 0 || std::strcmp(s, "be") == 0 ||
           std::strcmp(s, "nl") == 0;
}

const char* type_name(CfgType t)
{
    switch (t) {
    case CfgType::Bool:
        return "bool";
    case CfgType::I32:
        return "i32";
    case CfgType::U32:
        return "u32";
    case CfgType::Str:
        return "str";
    }
    return "";
}

bool append(char* out, std::size_t cap, std::size_t& n, const char* s)
{
    while (*s != '\0') {
        if (n + 1 >= cap) {
            return false;
        }
        out[n++] = *s++;
    }
    return true;
}

bool append_json_str(char* out, std::size_t cap, std::size_t& n, const char* s)
{
    if (!append(out, cap, n, "\"")) {
        return false;
    }
    for (; *s != '\0'; ++s) {
        const char c = *s;
        if (c == '"' || c == '\\') {
            if (n + 3 >= cap) {
                return false;
            }
            out[n++] = '\\';
            out[n++] = c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            continue;
        } else {
            if (n + 1 >= cap) {
                return false;
            }
            out[n++] = c;
        }
    }
    return append(out, cap, n, "\"");
}

struct Cur {
    const char* p;
    const char* end;

    void skip()
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            ++p;
        }
    }

    bool eat(char c)
    {
        skip();
        if (p < end && *p == c) {
            ++p;
            return true;
        }
        return false;
    }

    bool peek(char c)
    {
        skip();
        return p < end && *p == c;
    }
};

// false = not a JSON string. overflow = decoded value does not fit cap (NUL).
bool parse_string(Cur& c, char* out, std::size_t cap, bool& overflow)
{
    overflow = false;
    c.skip();
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    std::size_t n = 0;
    while (c.p < c.end && *c.p != '"') {
        char ch = *c.p++;
        if (ch == '\\' && c.p < c.end) {
            ch = *c.p++;
        }
        if (n + 1 < cap) {
            out[n++] = ch;
        } else {
            overflow = true;
        }
    }
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    if (cap > 0) {
        out[n] = '\0';
    }
    return true;
}

bool parse_bool(Cur& c, bool& v)
{
    c.skip();
    if (c.p + 4 <= c.end && std::memcmp(c.p, "true", 4) == 0) {
        c.p += 4;
        v = true;
        return true;
    }
    if (c.p + 5 <= c.end && std::memcmp(c.p, "false", 5) == 0) {
        c.p += 5;
        v = false;
        return true;
    }
    return false;
}

bool parse_i32(Cur& c, std::int32_t& v)
{
    c.skip();
    if (c.p >= c.end) {
        return false;
    }
    const char* p = c.p;
    bool neg = false;
    if (*p == '-') {
        neg = true;
        ++p;
    }
    if (p >= c.end || *p < '0' || *p > '9') {
        return false;
    }
    std::uint32_t acc = 0;
    while (p < c.end && *p >= '0' && *p <= '9') {
        const std::uint32_t d = static_cast<std::uint32_t>(*p - '0');
        if (acc > (UINT32_MAX - d) / 10) {
            return false;
        }
        acc = acc * 10 + d;
        ++p;
    }
    constexpr std::uint32_t kI32Max = 2147483647u;
    if (neg) {
        if (acc > kI32Max + 1) {
            return false;
        }
        v = (acc == kI32Max + 1) ? static_cast<std::int32_t>(-2147483647 - 1)
                                 : -static_cast<std::int32_t>(acc);
    } else {
        if (acc > kI32Max) {
            return false;
        }
        v = static_cast<std::int32_t>(acc);
    }
    c.p = p;
    return true;
}

bool parse_u32(Cur& c, std::uint32_t& v)
{
    c.skip();
    if (c.p >= c.end || *c.p < '0' || *c.p > '9') {
        return false;
    }
    std::uint32_t acc = 0;
    const char* p = c.p;
    while (p < c.end && *p >= '0' && *p <= '9') {
        const std::uint32_t d = static_cast<std::uint32_t>(*p - '0');
        if (acc > (UINT32_MAX - d) / 10) {
            return false;
        }
        acc = acc * 10 + d;
        ++p;
    }
    v = acc;
    c.p = p;
    return true;
}

}  // namespace

Store::Store(StoreBackend& backend, StoreBackend* legacy) : backend_(backend), legacy_(legacy)
{
    apply_defaults();
    bind_table();
}

void Store::apply_defaults()
{
    v_ = Settings{};
    copy_str(v_.mqtt_pfix, sizeof(v_.mqtt_pfix), "data/devices/utility_meter/");
    copy_str(v_.ha_device, sizeof(v_.ha_device), "Utility meter");
    copy_str(v_.region, sizeof(v_.region), "auto");
    copy_str(v_.rel_chan, sizeof(v_.rel_chan), "main");
    v_.mqtt_port = 1883;
    v_.frmt_pyld = 3;
    v_.upl_throttle = 10;
    apply_update_defaults(v_, official_build());
    std::memset(field_over_, 0, sizeof(field_over_));
}

void Store::bind_table()
{
    std::size_t n = 0;
    auto add_bool = [&](const char* k, const char* lab, CfgFlags f, bool* p, bool d) {
        CfgEntry e{};
        e.key = k;
        e.label = lab;
        e.type = CfgType::Bool;
        e.flags = f;
        e.var.b = p;
        e.def.b = d;
        table_[n++] = e;
    };
    auto add_u32 = [&](const char* k, const char* lab, CfgFlags f, std::uint32_t* p,
                       std::uint32_t d) {
        CfgEntry e{};
        e.key = k;
        e.label = lab;
        e.type = CfgType::U32;
        e.flags = f;
        e.var.u = p;
        e.def.u = d;
        table_[n++] = e;
    };
    auto add_i32 = [&](const char* k, const char* lab, CfgFlags f, std::int32_t* p,
                       std::int32_t d) {
        CfgEntry e{};
        e.key = k;
        e.label = lab;
        e.type = CfgType::I32;
        e.flags = f;
        e.var.i = p;
        e.def.i = d;
        table_[n++] = e;
    };
    auto add_str = [&](const char* k, const char* lab, CfgFlags f, char* p, std::size_t cap,
                       const char* d) {
        CfgEntry e{};
        e.key = k;
        e.label = lab;
        e.type = CfgType::Str;
        e.flags = f;
        e.var.s = p;
        e.def.s = d;
        e.str_cap = cap;
        table_[n++] = e;
    };

    add_bool("MQTT_EN", "MQTT enabled", CFG_IN_API, &v_.mqtt_en, false);
    add_bool("MQTT_TLS", "MQTT over TLS", CFG_IN_API, &v_.mqtt_tls, false);
    add_bool("MQTT_AUTH", "MQTT authentication", CFG_IN_API, &v_.mqtt_auth, false);
    add_u32("MQTT_PORT", "MQTT port", CFG_IN_API, &v_.mqtt_port, 1883);
    add_str("MQTT_HOST", "MQTT hostname", CFG_IN_API, v_.mqtt_host, sizeof(v_.mqtt_host), "");
    add_str("MQTT_ID", "MQTT client id", CFG_IN_API, v_.mqtt_id, sizeof(v_.mqtt_id), "");
    add_str("MQTT_USER", "MQTT username", CFG_IN_API, v_.mqtt_user, sizeof(v_.mqtt_user), "");
    add_str("MQTT_PASS", "MQTT password", CFG_IN_API | CFG_SECRET, v_.mqtt_pass,
            sizeof(v_.mqtt_pass), "");
    add_str("MQTT_PFIX", "MQTT topic prefix", CFG_IN_API, v_.mqtt_pfix, sizeof(v_.mqtt_pfix),
            "data/devices/utility_meter/");
    add_bool("HA_EN", "Home Assistant enabled", CFG_IN_API, &v_.ha_en, false);
    add_str("HA_DEVICE", "Home Assistant device name", CFG_IN_API, v_.ha_device,
            sizeof(v_.ha_device), "Utility meter");
    add_i32("FRMT_PYLD", "Reading payload format", CFG_IN_API, &v_.frmt_pyld, 3);
    add_u32("UPL_THROTTLE", "Upload throttle", CFG_IN_API, &v_.upl_throttle, 10);
    add_str("REGION", "Meter region", CFG_IN_API, v_.region, sizeof(v_.region), "auto");
    add_str("WIFI_SSID", "WiFi network", CFG_IN_API | CFG_REBOOT, v_.wifi_ssid,
            sizeof(v_.wifi_ssid), "");
    add_str("WIFI_PASS", "WiFi password", CFG_IN_API | CFG_SECRET | CFG_REBOOT, v_.wifi_pass,
            sizeof(v_.wifi_pass), "");
    add_u32("REBOOTS", "Boot count", CFG_IN_API, &v_.reboots, 0);
    add_str("LAST_RESET", "Last reset reason (firmware)", CFG_IN_API, v_.last_reset,
            sizeof(v_.last_reset), "");
    add_bool("FIP_EN", "Use fixed IP", CFG_IN_API | CFG_REBOOT, &v_.fip_en, false);
    add_str("FIPADDR", "IP address", CFG_IN_API | CFG_REBOOT, v_.fipaddr, sizeof(v_.fipaddr), "");
    add_str("FSUBN", "Subnet mask", CFG_IN_API | CFG_REBOOT, v_.fsubn, sizeof(v_.fsubn), "");
    add_str("FDEFGTW", "Default gateway", CFG_IN_API | CFG_REBOOT, v_.fdefgtw, sizeof(v_.fdefgtw),
            "");
    add_str("FDNS1", "Primary DNS", CFG_IN_API | CFG_REBOOT, v_.fdns1, sizeof(v_.fdns1), "");
    add_str("FDNS2", "Secondary DNS", CFG_IN_API | CFG_REBOOT, v_.fdns2, sizeof(v_.fdns2), "");
    add_bool("EID_EN", "EnergieID enabled", CFG_IN_API, &v_.eid_en, false);
    add_str("EIDCLAIM", "EnergieID claim code", CFG_IN_API | CFG_READONLY, v_.eidclaim,
            sizeof(v_.eidclaim), "");
    add_str("UUID", "Device UUID", CFG_IN_API | CFG_READONLY, v_.uuid, sizeof(v_.uuid), "");
    add_str("REL_CHAN", "Release channel", CFG_IN_API, v_.rel_chan, sizeof(v_.rel_chan), "main");
    add_bool("UPD_AUTO", "Automatic updates", CFG_IN_API, &v_.upd_auto, official_build());
    add_bool("UPD_AUTOCHK", "Check for updates", CFG_IN_API, &v_.upd_autochk,
             official_build());
    add_str("EMAIL", "User email", CFG_IN_API, v_.email, sizeof(v_.email), "");
    add_bool("PUSH_FULL", "Publish full telegram (~1 KB)", CFG_IN_API, &v_.push_full, false);
    add_bool("RINT_SPIFFS", "Reinitialise filesystem", CFG_IN_API, &v_.rint_spiffs, false);
    // Internal store-and-forward cursor — not a user setting.
    add_u32("LOG_MARK", "Syslog replay watermark", CFG_HIDDEN, &v_.log_mark, 0);
    add_u32("CFG_VER", "Config schema version", CFG_HIDDEN, &v_.cfg_ver, 0);
}

const CfgEntry* Store::find_setting(std::string_view key) const
{
    for (std::size_t i = 0; i < kSettingCount; ++i) {
        if (key == table_[i].key) {
            return &table_[i];
        }
    }
    return nullptr;
}

void Store::load_one(const CfgEntry& e)
{
    if (!backend_.has(e.key)) {
        return;
    }
    switch (e.type) {
    case CfgType::Bool:
        *e.var.b = backend_.get_bool(e.key, e.def.b);
        break;
    case CfgType::I32:
        *e.var.i = backend_.get_i32(e.key, e.def.i);
        break;
    case CfgType::U32:
        *e.var.u = backend_.get_u32(e.key, e.def.u);
        break;
    case CfgType::Str:
        backend_.get_str(e.key, e.var.s, e.str_cap, e.def.s);
        break;
    }
}

void Store::save_one(const CfgEntry& e)
{
    switch (e.type) {
    case CfgType::Bool:
        backend_.put_bool(e.key, *e.var.b);
        break;
    case CfgType::I32:
        backend_.put_i32(e.key, *e.var.i);
        break;
    case CfgType::U32:
        backend_.put_u32(e.key, *e.var.u);
        break;
    case CfgType::Str:
        backend_.put_str(e.key, e.var.s);
        break;
    }
}

bool Store::settings_storage_ok()
{
    if (!backend_.begin(false)) {
        return false;
    }
    backend_.put_bool("NVSOK", true);
    const bool ok = backend_.has("NVSOK") && backend_.get_bool("NVSOK", false);
    backend_.remove("NVSOK");
    backend_.end();
    return ok;
}

void Store::load()
{
    migrated_ = false;
    legacy_import_ran_ = false;
    legacy_import_again_ = false;
    import_pending_ = false;
    legacy_imported_ = 0;
    legacy_skip_n_ = 0;
    apply_defaults();
    bind_table();

    backend_.begin(true);
    const bool ver_present = backend_.has("CFG_VER");
    const bool pending = backend_.has("IMP_PEND") && backend_.get_bool("IMP_PEND", false);
    import_pending_ = pending;
    backend_.end();

    backend_.begin(true);
    for (std::size_t i = 0; i < kSettingCount; ++i) {
        load_one(table_[i]);
    }
    load_overrides();
    ha_mig_ = backend_.has("HA_MIG") && backend_.get_bool("HA_MIG", false);
    ha_pub_have_ = backend_.has("HA_SLUG");
    ha_pub_slug_[0] = '\0';
    ha_pub_pfix_[0] = '\0';
    if (ha_pub_have_) {
        backend_.get_str("HA_SLUG", ha_pub_slug_, sizeof(ha_pub_slug_), "");
        backend_.get_str("HA_PFIX", ha_pub_pfix_, sizeof(ha_pub_pfix_), "");
    }
    ota_note_have_ = backend_.has("OTA_NPART");
    ota_note_part_[0] = '\0';
    ota_note_ver_ = 0;
    if (ota_note_have_) {
        backend_.get_str("OTA_NPART", ota_note_part_, sizeof(ota_note_part_), "");
        ota_note_ver_ = backend_.get_u32("OTA_NVER", 0);
    }
    ota_fail_have_ = backend_.has("OTA_FAIL");
    ota_fail_ver_ = ota_fail_have_ ? backend_.get_u32("OTA_FAIL", 0) : 0;
    const std::uint32_t ver = ver_present ? v_.cfg_ver : 0;
    backend_.end();

    const bool want_import = (!ver_present || pending) && legacy_ != nullptr;
    if (want_import) {
        legacy_->begin(true);
        const bool have = legacy_->stored_type("WIFI_SSID") == StoredType::Str ||
                          legacy_->stored_type("MQTT_HOST") == StoredType::Str;
        if (have) {
            import_from(*legacy_);
            legacy_import_ran_ = true;
            legacy_import_again_ = pending;
        }
        legacy_->end();
    }

    if (!ver_present || ver < kSchemaVersion) {
        migrate();
    }
    if (legacy_import_ran_) {
        save();
        backend_.begin(false);
        backend_.put_bool("IMP_PEND", true);
        backend_.end();
        import_pending_ = true;
    }
}

void Store::migrate()
{
    backend_.begin(false);
    for (std::size_t i = 0; i < kSettingCount; ++i) {
        const CfgEntry& e = table_[i];
        if (std::strcmp(e.key, "CFG_VER") == 0) {
            continue;
        }
        if (!backend_.has(e.key)) {
            save_one(e);
        }
    }
    if (std::strcmp(v_.rel_chan, "alpha") == 0 || std::strcmp(v_.rel_chan, "V2") == 0) {
        std::snprintf(v_.rel_chan, sizeof(v_.rel_chan), "develop");
        backend_.put_str("REL_CHAN", v_.rel_chan);
    }
    backend_.remove("EID_PROVKEY");
    backend_.remove("EID_PROVSEC");
    v_.cfg_ver = kSchemaVersion;
    backend_.put_u32("CFG_VER", kSchemaVersion);
    backend_.end();
    migrated_ = true;
}

void Store::save()
{
    backend_.begin(false);
    for (std::size_t i = 0; i < kSettingCount; ++i) {
        save_one(table_[i]);
    }
    save_overrides();
    backend_.end();
}

void Store::reset_wifi()
{
    v_.wifi_ssid[0] = '\0';
    v_.wifi_pass[0] = '\0';
    v_.fip_en = false;
    save();
}

void Store::clear_import_pending()
{
    import_pending_ = false;
    backend_.begin(false);
    backend_.remove("IMP_PEND");
    backend_.end();
}

void Store::factory_reset()
{
    apply_defaults();
    save();
    ha_mig_ = false;
    clear_published_ha_identity();
    clear_ota_note();
    clear_ota_failed();
    clear_import_pending();
    backend_.begin(false);
    backend_.remove("HA_MIG");
    backend_.end();
    if (legacy_ != nullptr) {
        legacy_->begin(false);
        legacy_->clear();
        legacy_->end();
    }
}

const char* Store::legacy_skip_key(std::size_t i) const
{
    return i < legacy_skip_n_ ? legacy_skip_[i] : "";
}

void Store::skip_legacy(const char* key)
{
    if (key == nullptr || legacy_skip_n_ >= kLegacySkipMax) {
        return;
    }
    std::snprintf(legacy_skip_[legacy_skip_n_], sizeof(legacy_skip_[0]), "%s", key);
    ++legacy_skip_n_;
}

void Store::load_overrides()
{
    std::memset(field_over_, 0, sizeof(field_over_));
    if (!backend_.has("FLD_OVR")) {
        return;
    }
    char buf[kOverrideBufCap];
    backend_.get_str("FLD_OVR", buf, sizeof(buf), "");
    const char* p = buf;
    while (*p != '\0') {
        const char* start = p;
        while (*p != '\0' && *p != '=' && *p != ',') {
            ++p;
        }
        const std::string_view id(start, static_cast<std::size_t>(p - start));
        bool on = false;
        if (*p == '=') {
            ++p;
            on = (*p == '1');
            while (*p != '\0' && *p != ',') {
                ++p;
            }
        }
        if (*p == ',') {
            ++p;
        }
        const int idx = field_index(id);
        if (idx < 0) {
            continue;
        }
        const bool def = catalogue()[static_cast<std::size_t>(idx)].default_on;
        field_over_[idx] = (on == def) ? 0 : (on ? 1 : 2);
    }
}

void Store::save_overrides()
{
    char buf[kOverrideBufCap];
    std::size_t n = 0;
    const Field* all = catalogue();
    const std::size_t nf = catalogue_size();
    for (std::size_t i = 0; i < nf; ++i) {
        if (field_over_[i] == 0) {
            continue;
        }
        const char* id = all[i].id;
        std::size_t id_len = 0;
        while (id[id_len] != '\0') {
            ++id_len;
        }
        std::size_t need = id_len + 2;  // '=' and digit
        if (n > 0) {
            need += 1;
        }
        if (n + need + 1 > sizeof(buf)) {
            // Drop this override rather than emit a truncated id.
            continue;
        }
        if (n > 0) {
            buf[n++] = ',';
        }
        for (std::size_t k = 0; k < id_len; ++k) {
            buf[n++] = id[k];
        }
        buf[n++] = '=';
        buf[n++] = (field_over_[i] == 1) ? '1' : '0';
    }
    buf[n] = '\0';
    if (n == 0) {
        if (backend_.has("FLD_OVR")) {
            backend_.remove("FLD_OVR");
        }
        return;
    }
    backend_.put_str("FLD_OVR", buf);
}

void Store::set_ha_migrated()
{
    ha_mig_ = true;
    backend_.begin(false);
    backend_.put_bool("HA_MIG", true);
    backend_.end();
}

void Store::set_published_ha_identity(const char* slug, const char* pfix)
{
    auto copy = [](char* dst, std::size_t cap, const char* src) {
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }
        std::size_t n = 0;
        while (src[n] != '\0' && n + 1 < cap) {
            dst[n] = src[n];
            ++n;
        }
        dst[n] = '\0';
    };
    copy(ha_pub_slug_, sizeof(ha_pub_slug_), slug);
    copy(ha_pub_pfix_, sizeof(ha_pub_pfix_), pfix);
    ha_pub_have_ = true;
    backend_.begin(false);
    backend_.put_str("HA_SLUG", ha_pub_slug_);
    backend_.put_str("HA_PFIX", ha_pub_pfix_);
    backend_.end();
}

void Store::clear_published_ha_identity()
{
    ha_pub_have_ = false;
    ha_pub_slug_[0] = '\0';
    ha_pub_pfix_[0] = '\0';
    backend_.begin(false);
    backend_.remove("HA_SLUG");
    backend_.remove("HA_PFIX");
    backend_.end();
}

void Store::set_ota_note(const char* part, std::uint32_t ver)
{
    ota_note_part_[0] = '\0';
    if (part != nullptr) {
        std::size_t n = 0;
        while (part[n] != '\0' && n + 1 < sizeof(ota_note_part_)) {
            ota_note_part_[n] = part[n];
            ++n;
        }
        ota_note_part_[n] = '\0';
    }
    ota_note_ver_ = ver;
    ota_note_have_ = true;
    backend_.begin(false);
    backend_.put_str("OTA_NPART", ota_note_part_);
    backend_.put_u32("OTA_NVER", ota_note_ver_);
    backend_.end();
}

void Store::clear_ota_note()
{
    ota_note_have_ = false;
    ota_note_part_[0] = '\0';
    ota_note_ver_ = 0;
    backend_.begin(false);
    backend_.remove("OTA_NPART");
    backend_.remove("OTA_NVER");
    backend_.end();
}

void Store::set_ota_failed(std::uint32_t ver)
{
    ota_fail_have_ = true;
    ota_fail_ver_ = ver;
    backend_.begin(false);
    backend_.put_u32("OTA_FAIL", ota_fail_ver_);
    backend_.end();
}

void Store::clear_ota_failed()
{
    ota_fail_have_ = false;
    ota_fail_ver_ = 0;
    backend_.begin(false);
    backend_.remove("OTA_FAIL");
    backend_.end();
}

bool Store::field_on(std::string_view id) const
{
    const int idx = field_index(id);
    if (idx < 0) {
        return false;
    }
    if (field_over_[idx] == 1) {
        return true;
    }
    if (field_over_[idx] == 2) {
        return false;
    }
    return catalogue()[static_cast<std::size_t>(idx)].default_on;
}

void Store::set_field_on(std::string_view id, bool on)
{
    const int idx = field_index(id);
    if (idx < 0) {
        return;
    }
    const bool def = catalogue()[static_cast<std::size_t>(idx)].default_on;
    field_over_[idx] = (on == def) ? 0 : (on ? 1 : 2);
}

JsonStatus Store::apply_json(std::string_view json, char* bad_key, std::size_t bad_key_cap)
{
    if (bad_key != nullptr && bad_key_cap > 0) {
        bad_key[0] = '\0';
    }
    auto set_bad = [&](const char* key) {
        if (bad_key == nullptr || bad_key_cap == 0 || key == nullptr) {
            return;
        }
        std::snprintf(bad_key, bad_key_cap, "%s", key);
    };

    const Settings saved = v_;
    auto fail = [&](JsonStatus st, const char* key = nullptr) {
        if (key != nullptr) {
            set_bad(key);
        }
        v_ = saved;
        bind_table();
        return st;
    };

    Cur c{json.data(), json.data() + json.size()};
    if (!c.eat('{')) {
        return fail(JsonStatus::Parse);
    }
    bool first = true;
    while (!c.peek('}')) {
        if (!first && !c.eat(',')) {
            return fail(JsonStatus::Parse);
        }
        first = false;
        char key[24];
        bool overflow = false;
        if (!parse_string(c, key, sizeof(key), overflow)) {
            return fail(JsonStatus::Parse);
        }
        if (overflow) {
            return fail(JsonStatus::UnknownKey);
        }
        if (!c.eat(':')) {
            return fail(JsonStatus::Parse);
        }
        const CfgEntry* e = find_setting(key);
        if (e == nullptr || (e->flags & CFG_IN_API) == 0) {
            return fail(JsonStatus::UnknownKey, key);
        }
        if ((e->flags & CFG_READONLY) != 0) {
            return fail(JsonStatus::Invalid, key);
        }
        switch (e->type) {
        case CfgType::Bool: {
            bool v = false;
            if (!parse_bool(c, v)) {
                return fail(JsonStatus::WrongType, key);
            }
            *e->var.b = v;
            break;
        }
        case CfgType::I32: {
            std::int32_t v = 0;
            if (!parse_i32(c, v)) {
                return fail(JsonStatus::WrongType, key);
            }
            if (std::strcmp(e->key, "FRMT_PYLD") == 0 && (v < 0 || v > 3)) {
                return fail(JsonStatus::Invalid, key);
            }
            *e->var.i = v;
            break;
        }
        case CfgType::U32: {
            std::uint32_t v = 0;
            if (!parse_u32(c, v)) {
                return fail(JsonStatus::WrongType, key);
            }
            if (std::strcmp(e->key, "MQTT_PORT") == 0 && (v < 1u || v > 65535u)) {
                return fail(JsonStatus::Invalid, key);
            }
            *e->var.u = v;
            break;
        }
        case CfgType::Str: {
            overflow = false;
            if (!parse_string(c, e->var.s, e->str_cap, overflow)) {
                return fail(JsonStatus::WrongType, key);
            }
            if (overflow) {
                return fail(JsonStatus::Invalid, key);
            }
            if (std::strcmp(e->key, "REGION") == 0 && !region_ok(e->var.s)) {
                return fail(JsonStatus::Invalid, key);
            }
            if (std::strcmp(e->key, "REL_CHAN") == 0 && !rel_chan_ok(e->var.s)) {
                return fail(JsonStatus::Invalid, key);
            }
            // IP field format is checked only when FIP_EN is true (post-pass).
            break;
        }
        }
        c.skip();
    }
    if (!c.eat('}')) {
        return fail(JsonStatus::Parse);
    }
    c.skip();
    if (c.p != c.end) {
        return fail(JsonStatus::Parse);
    }
    if (v_.fip_en) {
        if (!ip_ok(v_.fipaddr)) {
            return fail(JsonStatus::Invalid, "FIPADDR");
        }
        if (!ip_ok(v_.fsubn)) {
            return fail(JsonStatus::Invalid, "FSUBN");
        }
        if (!ip_ok(v_.fdefgtw)) {
            return fail(JsonStatus::Invalid, "FDEFGTW");
        }
        if (!ip_ok(v_.fdns1)) {
            return fail(JsonStatus::Invalid, "FDNS1");
        }
        if (v_.fdns2[0] != '\0' && !ip_ok(v_.fdns2)) {
            return fail(JsonStatus::Invalid, "FDNS2");
        }
        if (!static_ip_consistent(v_.fipaddr, v_.fsubn, v_.fdefgtw)) {
            // Prefer the address when the subnet pair is inconsistent.
            return fail(JsonStatus::Invalid, "FIPADDR");
        }
    }
    if (std::strcmp(saved.rel_chan, v_.rel_chan) != 0) {
        rel_chan_changed_ = true;
    }
    reboot_needed_ = network_differs(saved, v_);
    return JsonStatus::Ok;
}

bool Store::consume_rel_chan_changed()
{
    const bool changed = rel_chan_changed_;
    rel_chan_changed_ = false;
    return changed;
}

void Store::capture_boot_network()
{
    copy_network(boot_net_, v_);
}

bool Store::network_pending() const
{
    return network_differs(boot_net_, v_);
}

bool Store::consume_reboot_needed()
{
    const bool needed = reboot_needed_;
    reboot_needed_ = false;
    return needed;
}

JsonStatus Store::apply_fields_json(std::string_view json, char* bad_key, std::size_t bad_key_cap)
{
    if (bad_key != nullptr && bad_key_cap > 0) {
        bad_key[0] = '\0';
    }
    auto set_bad = [&](const char* key) {
        if (bad_key == nullptr || bad_key_cap == 0 || key == nullptr) {
            return;
        }
        std::snprintf(bad_key, bad_key_cap, "%s", key);
    };

    std::uint8_t saved[kMaxFields];
    std::memcpy(saved, field_over_, sizeof(field_over_));
    auto fail = [&](JsonStatus st, const char* key = nullptr) {
        std::memcpy(field_over_, saved, sizeof(field_over_));
        if (key != nullptr) {
            set_bad(key);
        }
        return st;
    };

    Cur c{json.data(), json.data() + json.size()};
    if (!c.eat('{')) {
        return fail(JsonStatus::Parse);
    }
    bool first = true;
    while (!c.peek('}')) {
        if (!first && !c.eat(',')) {
            return fail(JsonStatus::Parse);
        }
        first = false;
        char key[kMaxFieldId + 1];
        bool overflow = false;
        if (!parse_string(c, key, sizeof(key), overflow)) {
            return fail(JsonStatus::Parse);
        }
        if (overflow) {
            return fail(JsonStatus::UnknownKey);
        }
        if (!c.eat(':')) {
            return fail(JsonStatus::Parse);
        }
        const Field* f = find_field(key);
        if (f == nullptr || !f->selectable) {
            return fail(JsonStatus::UnknownKey, key);
        }
        bool on = false;
        if (!parse_bool(c, on)) {
            return fail(JsonStatus::WrongType, key);
        }
        set_field_on(key, on);
        c.skip();
    }
    if (!c.eat('}')) {
        return fail(JsonStatus::Parse);
    }
    c.skip();
    if (c.p != c.end) {
        return fail(JsonStatus::Parse);
    }
    return JsonStatus::Ok;
}

std::size_t Store::to_json(char* out, std::size_t cap) const
{
    if (cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!append(out, cap, n, "{")) {
        out[0] = '\0';
        return 0;
    }
    bool first = true;
    for (std::size_t i = 0; i < kSettingCount; ++i) {
        const CfgEntry& e = table_[i];
        if ((e.flags & CFG_IN_API) == 0) {
            continue;
        }
        if (!first && !append(out, cap, n, ",")) {
            out[0] = '\0';
            return 0;
        }
        first = false;
        if (!append_json_str(out, cap, n, e.key) || !append(out, cap, n, ":{") ||
            !append(out, cap, n, "\"type\":") ||
            !append_json_str(out, cap, n, type_name(e.type)) ||
            !append(out, cap, n, ",\"label\":") || !append_json_str(out, cap, n, e.label) ||
            !append(out, cap, n, ",\"flags\":")) {
            out[0] = '\0';
            return 0;
        }
        char num[16];
        std::snprintf(num, sizeof(num), "%u", static_cast<unsigned>(e.flags));
        if (!append(out, cap, n, num)) {
            out[0] = '\0';
            return 0;
        }
        if (e.flags & CFG_SECRET) {
            if (!append(out, cap, n, ",\"set\":")) {
                out[0] = '\0';
                return 0;
            }
            if (!append(out, cap, n, (e.var.s[0] != '\0') ? "true" : "false")) {
                out[0] = '\0';
                return 0;
            }
        } else {
            if (!append(out, cap, n, ",\"value\":")) {
                out[0] = '\0';
                return 0;
            }
            switch (e.type) {
            case CfgType::Bool:
                if (!append(out, cap, n, *e.var.b ? "true" : "false")) {
                    out[0] = '\0';
                    return 0;
                }
                break;
            case CfgType::I32:
                std::snprintf(num, sizeof(num), "%ld", static_cast<long>(*e.var.i));
                if (!append(out, cap, n, num)) {
                    out[0] = '\0';
                    return 0;
                }
                break;
            case CfgType::U32:
                std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(*e.var.u));
                if (!append(out, cap, n, num)) {
                    out[0] = '\0';
                    return 0;
                }
                break;
            case CfgType::Str:
                if (!append_json_str(out, cap, n, e.var.s)) {
                    out[0] = '\0';
                    return 0;
                }
                break;
            }
        }
        if (!append(out, cap, n, "}")) {
            out[0] = '\0';
            return 0;
        }
    }
    if (!append(out, cap, n, "}")) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return n;
}

}  // namespace p1cfg
