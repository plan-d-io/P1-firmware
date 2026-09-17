#include "P1Config.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

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

Store::Store(StoreBackend& backend) : backend_(backend)
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
    v_.mqtt_port = 1883;
    v_.frmt_pyld = 3;
    v_.upl_throttle = 10;
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

void Store::load()
{
    migrated_ = false;
    apply_defaults();
    bind_table();

    backend_.begin(true);
    const bool ver_present = backend_.has("CFG_VER");
    for (std::size_t i = 0; i < kSettingCount; ++i) {
        load_one(table_[i]);
    }
    load_overrides();
    const std::uint32_t ver = ver_present ? v_.cfg_ver : 0;
    backend_.end();

    if (!ver_present || ver < kSchemaVersion) {
        migrate();
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

JsonStatus Store::apply_json(std::string_view json)
{
    const Settings saved = v_;
    auto fail = [this, &saved](JsonStatus st) {
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
            return fail(JsonStatus::UnknownKey);
        }
        switch (e->type) {
        case CfgType::Bool: {
            bool v = false;
            if (!parse_bool(c, v)) {
                return fail(JsonStatus::WrongType);
            }
            *e->var.b = v;
            break;
        }
        case CfgType::I32: {
            std::int32_t v = 0;
            if (!parse_i32(c, v)) {
                return fail(JsonStatus::WrongType);
            }
            if (std::strcmp(e->key, "FRMT_PYLD") == 0 && (v < 0 || v > 3)) {
                return fail(JsonStatus::Invalid);
            }
            *e->var.i = v;
            break;
        }
        case CfgType::U32: {
            std::uint32_t v = 0;
            if (!parse_u32(c, v)) {
                return fail(JsonStatus::WrongType);
            }
            if (std::strcmp(e->key, "MQTT_PORT") == 0 && (v < 1u || v > 65535u)) {
                return fail(JsonStatus::Invalid);
            }
            *e->var.u = v;
            break;
        }
        case CfgType::Str: {
            overflow = false;
            if (!parse_string(c, e->var.s, e->str_cap, overflow)) {
                return fail(JsonStatus::WrongType);
            }
            if (overflow) {
                return fail(JsonStatus::Invalid);
            }
            if (std::strcmp(e->key, "REGION") == 0 && !region_ok(e->var.s)) {
                return fail(JsonStatus::Invalid);
            }
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
