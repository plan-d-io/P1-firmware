#include "P1Publish.h"

#include <cstdio>
#include <cstring>

namespace p1pub {
namespace {

struct Wr {
    char* out;
    std::size_t cap;
    std::size_t n = 0;
    bool overflow = false;
};

void slug_char(char& c)
{
    if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
    } else if (c == ' ') {
        c = '_';
    }
}

bool wr_append(Wr& w, std::string_view s)
{
    if (w.n + s.size() + 1 > w.cap) {
        w.overflow = true;
        w.n += s.size();
        return true;
    }
    if (w.out != nullptr) {
        for (char c : s) {
            w.out[w.n++] = c;
        }
    } else {
        w.n += s.size();
    }
    return true;
}

bool wr_char(Wr& w, char c)
{
    return wr_append(w, std::string_view(&c, 1));
}

bool wr_json_str(Wr& w, std::string_view s)
{
    if (!wr_char(w, '"')) {
        return false;
    }
    for (char c : s) {
        if (c == '"' || c == '\\') {
            if (!wr_char(w, '\\') || !wr_char(w, c)) {
                return false;
            }
        } else if (static_cast<unsigned char>(c) < 0x20) {
            continue;
        } else if (!wr_char(w, c)) {
            return false;
        }
    }
    return wr_char(w, '"');
}

bool wr_key(Wr& w, std::string_view key, bool& first)
{
    if (!first && !wr_char(w, ',')) {
        return false;
    }
    first = false;
    return wr_json_str(w, key) && wr_char(w, ':');
}

std::size_t finish(Wr& w)
{
    if (w.overflow) {
        if (w.out != nullptr && w.cap > 0) {
            w.out[0] = '\0';
        }
        return 0;
    }
    if (w.out != nullptr && w.n < w.cap) {
        w.out[w.n] = '\0';
    }
    return w.n;
}

bool write_slug_into(char* out, std::size_t cap, std::size_t& n, std::string_view s)
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

const char* kLegacyDiag[] = {
    "free_heap_size",
    "max_allocatable_block",
    "min_free_heap",
    "ip",
    "firmware",
    "rssi",
    "reboots",
    "last_reset_reason_hw",
    "last_reset_reason_fw",
    "syslog",
    "release_channel",
};

struct LiveDiag {
    const char* channel;
    const char* name;
    bool is_switch;
};

constexpr LiveDiag kLiveDiag[] = {
    {"free_heap_size", "Free heap size", false},
    {"max_allocatable_block", "Allocatable block size", false},
    {"min_free_heap", "Lowest free heap size", false},
    {"ip", "IP", false},
    {"firmware", "Firmware", false},
    {"rssi", "RSSI", false},
    {"reboots", "Reboots", false},
    {"last_reset_reason_hw", "Last reset reason (hardware)", false},
    {"last_reset_reason_fw", "Last reset reason (firmware)", false},
    {"syslog", "System log", false},
    {"reboot", "Reboot", true},
};

bool write_origin(Wr& w, const char* fw_ver, bool& first)
{
    if (!wr_key(w, "origin", first) || !wr_char(w, '{')) {
        return false;
    }
    bool f = true;
    if (!wr_key(w, "name", f) || !wr_json_str(w, kOriginName) || !wr_key(w, "sw_version", f) ||
        !wr_json_str(w, fw_ver != nullptr ? fw_ver : "") || !wr_key(w, "support_url", f) ||
        !wr_json_str(w, kSupportUrl) || !wr_char(w, '}')) {
        return false;
    }
    return true;
}

bool write_device(Wr& w, std::string_view name, std::string_view ident, const char* model,
                  const char* fw_ver, const char* config_url, bool& first)
{
    if (!wr_key(w, "device", first) || !wr_char(w, '{')) {
        return false;
    }
    bool f = true;
    if (!wr_key(w, "name", f) || !wr_json_str(w, name) || !wr_key(w, "identifiers", f) ||
        !wr_char(w, '[') || !wr_json_str(w, ident) || !wr_char(w, ']') ||
        !wr_key(w, "manufacturer", f) || !wr_json_str(w, kManufacturer) || !wr_key(w, "model", f) ||
        !wr_json_str(w, model != nullptr ? model : "") || !wr_key(w, "configuration_url", f) ||
        !wr_json_str(w, config_url != nullptr ? config_url : "") || !wr_key(w, "sw_version", f) ||
        !wr_json_str(w, fw_ver != nullptr ? fw_ver : "") || !wr_char(w, '}')) {
        return false;
    }
    return true;
}

bool write_meter_component(Wr& w, const p1cfg::Store& cfg, const p1cfg::Field& field, bool stub,
                           bool& first)
{
    char uid[kUniqueIdMax];
    if (meter_unique_id(cfg.values().ha_device, field.label, uid, sizeof(uid)) == 0) {
        return false;
    }
    if (!wr_key(w, uid, first) || !wr_char(w, '{')) {
        return false;
    }
    bool f = true;
    if (!wr_key(w, "p", f) || !wr_json_str(w, "sensor")) {
        return false;
    }
    if (stub) {
        return wr_char(w, '}');
    }
    if (!wr_key(w, "uniq_id", f) || !wr_json_str(w, uid) || !wr_key(w, "name", f) ||
        !wr_json_str(w, field.label)) {
        return false;
    }
    char topic[kTopicMax];
    if (reading_topic(cfg, field, topic, sizeof(topic)) == 0) {
        return false;
    }
    if (!wr_key(w, "stat_t", f) || !wr_json_str(w, topic)) {
        return false;
    }
    if (cfg.values().frmt_pyld > 0) {
        if (!wr_key(w, "val_tpl", f) || !wr_json_str(w, "{{ value_json.value }}")) {
            return false;
        }
    }
    if (field.ha_class != nullptr && field.ha_class[0] != '\0') {
        if (!wr_key(w, "dev_cla", f) || !wr_json_str(w, field.ha_class)) {
            return false;
        }
    }
    if (const char* sc = ha_state_class(field)) {
        if (!wr_key(w, "stat_cla", f) || !wr_json_str(w, sc)) {
            return false;
        }
    }
    if (const char* u = ha_unit(field)) {
        if (!wr_key(w, "unit_of_meas", f) || !wr_json_str(w, u)) {
            return false;
        }
    }
    return wr_char(w, '}');
}

bool unique_id_already(const p1cfg::Store& cfg, std::uint64_t mask, std::size_t before,
                       std::string_view label)
{
    char want[kUniqueIdMax];
    char have[kUniqueIdMax];
    if (meter_unique_id(cfg.values().ha_device, label, want, sizeof(want)) == 0) {
        return false;
    }
    const p1cfg::Field* all = p1cfg::catalogue();
    for (std::size_t j = 0; j < before; ++j) {
        if ((mask & (std::uint64_t{1} << j)) == 0) {
            continue;
        }
        if (meter_unique_id(cfg.values().ha_device, all[j].label, have, sizeof(have)) == 0) {
            continue;
        }
        if (std::strcmp(want, have) == 0) {
            return true;
        }
    }
    return false;
}

void emit_topic(void (*fn)(std::string_view, void*), void* ctx, const char* s)
{
    if (fn != nullptr && s != nullptr && s[0] != '\0') {
        fn(s, ctx);
    }
}

}  // namespace

std::size_t slug(std::string_view s, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!write_slug_into(out, cap, n, s)) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t meter_unique_id(std::string_view ha_device, std::string_view label, char* out,
                            std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!write_slug_into(out, cap, n, ha_device) || n + 2 > cap) {
        out[0] = '\0';
        return 0;
    }
    out[n++] = '_';
    if (!write_slug_into(out, cap, n, label)) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t device_discovery_topic(std::string_view object_id, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0 || object_id.empty()) {
        return 0;
    }
    Wr w{out, cap};
    if (!wr_append(w, "homeassistant/device/") || !wr_append(w, object_id) ||
        !wr_append(w, "/config")) {
        return 0;
    }
    return finish(w);
}

const char* ha_state_class(const p1cfg::Field& field)
{
    const char* c = field.ha_class;
    if (c == nullptr || c[0] == '\0') {
        return nullptr;
    }
    if (std::strcmp(c, "energy") == 0 || std::strcmp(c, "gas") == 0 ||
        std::strcmp(c, "water") == 0) {
        return "total_increasing";
    }
    if (std::strcmp(c, "power") == 0 || std::strcmp(c, "voltage") == 0 ||
        std::strcmp(c, "current") == 0) {
        return "measurement";
    }
    return nullptr;
}

const char* ha_unit(const p1cfg::Field& field)
{
    const char* c = field.ha_class;
    if (c == nullptr || c[0] == '\0') {
        return nullptr;
    }
    if (std::strcmp(c, "energy") == 0) {
        return "kWh";
    }
    if (std::strcmp(c, "power") == 0) {
        return "kW";
    }
    if (std::strcmp(c, "voltage") == 0) {
        return "V";
    }
    if (std::strcmp(c, "current") == 0) {
        return "A";
    }
    if (std::strcmp(c, "gas") == 0 || std::strcmp(c, "water") == 0) {
        return "m\xC2\xB3";
    }
    return nullptr;
}

std::size_t meter_discovery(const p1cfg::Store& cfg, const char* fw_ver, const char* config_url,
                            std::uint64_t include_mask, std::uint64_t stub_mask, char* out,
                            std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    Wr w{out, cap};
    if (!wr_char(w, '{')) {
        return 0;
    }
    bool first = true;
    char slug_id[kUniqueIdMax];
    if (slug(cfg.values().ha_device, slug_id, sizeof(slug_id)) == 0) {
        return 0;
    }
    if (!write_device(w, cfg.values().ha_device, slug_id, kMeterModel, fw_ver, config_url, first) ||
        !write_origin(w, fw_ver, first)) {
        return 0;
    }
    char avail[kTopicMax];
    if (availability_topic(cfg, avail, sizeof(avail)) == 0) {
        return 0;
    }
    if (!wr_key(w, "availability_topic", first) || !wr_json_str(w, avail) ||
        !wr_key(w, "components", first) || !wr_char(w, '{')) {
        return 0;
    }

    bool cfirst = true;
    const p1cfg::Field* all = p1cfg::catalogue();
    const std::size_t nf = p1cfg::catalogue_size();
    const std::uint64_t any = include_mask | stub_mask;
    for (std::size_t i = 0; i < nf; ++i) {
        const std::uint64_t bit = std::uint64_t{1} << i;
        if ((any & bit) == 0) {
            continue;
        }
        if (unique_id_already(cfg, any, i, all[i].label)) {
            continue;
        }
        const bool stub = (stub_mask & bit) != 0 && (include_mask & bit) == 0;
        if (!write_meter_component(w, cfg, all[i], stub, cfirst)) {
            return 0;
        }
    }
    if (!wr_append(w, "}}")) {
        return 0;
    }
    const std::size_t n = finish(w);
    if (n == 0 && w.overflow) {
        return 0;
    }
    return n;
}

std::size_t dongle_discovery(const p1cfg::Store& cfg, const char* device_id, const char* fw_ver,
                             const char* config_url, char* out, std::size_t cap)
{
    if (device_id == nullptr || device_id[0] == '\0' || out == nullptr || cap == 0) {
        return 0;
    }
    Wr w{out, cap};
    if (!wr_char(w, '{')) {
        return 0;
    }
    bool first = true;
    if (!write_device(w, device_id, device_id, kDongleModel, fw_ver, config_url, first) ||
        !write_origin(w, fw_ver, first)) {
        return 0;
    }
    char avail[kTopicMax];
    if (availability_topic(cfg, avail, sizeof(avail)) == 0) {
        return 0;
    }
    if (!wr_key(w, "availability_topic", first) || !wr_json_str(w, avail) ||
        !wr_key(w, "components", first) || !wr_char(w, '{')) {
        return 0;
    }

    bool cfirst = true;
    char cmd_full[kTopicMax];
    Wr cw{cmd_full, sizeof(cmd_full)};
    wr_append(cw, std::string_view(avail));
    wr_append(cw, "/set/reboot");
    if (finish(cw) == 0) {
        return 0;
    }

    for (const LiveDiag& d : kLiveDiag) {
        char uid[kUniqueIdMax];
        Wr uw{uid, sizeof(uid)};
        wr_append(uw, device_id);
        wr_char(uw, '_');
        wr_append(uw, d.channel);
        if (finish(uw) == 0) {
            return 0;
        }
        if (!wr_key(w, uid, cfirst) || !wr_char(w, '{')) {
            return 0;
        }
        bool f = true;
        if (!wr_key(w, "p", f) || !wr_json_str(w, d.is_switch ? "switch" : "sensor") ||
            !wr_key(w, "uniq_id", f) || !wr_json_str(w, uid) || !wr_key(w, "name", f) ||
            !wr_json_str(w, d.name)) {
            return 0;
        }
        char state[kTopicMax];
        Wr sw{state, sizeof(state)};
        wr_append(sw, "sys/devices/");
        wr_append(sw, device_id);
        wr_char(sw, '/');
        wr_append(sw, d.channel);
        if (finish(sw) == 0) {
            return 0;
        }
        if (!wr_key(w, "stat_t", f) || !wr_json_str(w, state) || !wr_key(w, "val_tpl", f) ||
            !wr_json_str(w, "{{ value_json.value }}") || !wr_key(w, "ent_cat", f) ||
            !wr_json_str(w, "diagnostic")) {
            return 0;
        }
        if (d.is_switch) {
            if (!wr_key(w, "cmd_t", f) || !wr_json_str(w, cmd_full) || !wr_key(w, "pl_on", f) ||
                !wr_json_str(w, "{\"value\":\"true\"}") || !wr_key(w, "pl_off", f) ||
                !wr_json_str(w, "{\"value\":\"false\"}") || !wr_key(w, "stat_on", f) ||
                !wr_json_str(w, "on") || !wr_key(w, "stat_off", f) || !wr_json_str(w, "off")) {
                return 0;
            }
        }
        if (!wr_char(w, '}')) {
            return 0;
        }
    }
    if (!wr_append(w, "}}")) {
        return 0;
    }
    return finish(w);
}

std::size_t for_each_legacy_discovery_topic(const p1cfg::Store& cfg, const char* device_id,
                                            const char* legacy_id,
                                            void (*fn)(std::string_view, void*), void* ctx)
{
    std::size_t n = 0;
    char topic[160];
    char uid[kUniqueIdMax];
    const p1cfg::Field* all = p1cfg::catalogue();
    const std::size_t nf = p1cfg::catalogue_size();
    for (std::size_t i = 0; i < nf; ++i) {
        if (meter_unique_id(cfg.values().ha_device, all[i].label, uid, sizeof(uid)) == 0) {
            continue;
        }
        Wr w{topic, sizeof(topic)};
        wr_append(w, "homeassistant/sensor/");
        wr_append(w, uid);
        wr_append(w, "/config");
        if (finish(w) == 0) {
            continue;
        }
        emit_topic(fn, ctx, topic);
        ++n;
    }

    auto one_id = [&](const char* id) {
        if (id == nullptr || id[0] == '\0') {
            return;
        }
        for (const char* ch : kLegacyDiag) {
            Wr w{topic, sizeof(topic)};
            wr_append(w, "homeassistant/sensor/");
            wr_append(w, id);
            wr_char(w, '_');
            wr_append(w, ch);
            wr_append(w, "/config");
            if (finish(w) == 0) {
                continue;
            }
            emit_topic(fn, ctx, topic);
            ++n;
        }
        Wr w{topic, sizeof(topic)};
        wr_append(w, "homeassistant/switch/");
        wr_append(w, id);
        wr_append(w, "_reboot/config");
        if (finish(w) != 0) {
            emit_topic(fn, ctx, topic);
            ++n;
        }
    };
    one_id(device_id);
    one_id(legacy_id);
    return n;
}

std::size_t identity_clear_topics(std::string_view last_slug, std::string_view last_pfix,
                                  std::string_view cur_slug, std::string_view cur_pfix,
                                  void (*fn)(std::string_view, void*), void* ctx)
{
    if (last_slug.empty() && last_pfix.empty()) {
        return 0;
    }
    const bool slug_changed = last_slug != cur_slug;
    const bool pfix_changed = last_pfix != cur_pfix;
    if (!slug_changed && !pfix_changed) {
        return 0;
    }

    std::size_t n = 0;
    char topic[kTopicMax];
    if (!last_slug.empty() && device_discovery_topic(last_slug, topic, sizeof(topic)) > 0) {
        emit_topic(fn, ctx, topic);
        ++n;
    }
    if (pfix_changed && !last_pfix.empty() &&
        availability_from_prefix(last_pfix, topic, sizeof(topic)) > 0) {
        emit_topic(fn, ctx, topic);
        ++n;
    }
    return n;
}

}  // namespace p1pub
