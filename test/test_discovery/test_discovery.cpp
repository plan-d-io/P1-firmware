#include <P1Config.h>
#include <P1Mqtt.h>
#include <P1Publish.h>

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool json_closed(const char* s)
{
    if (s == nullptr || s[0] != '{') {
        return false;
    }
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    const char* p = s;
    for (; *p != '\0'; ++p) {
        const char c = *p;
        if (in_str) {
            if (esc) {
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                in_str = false;
            }
            continue;
        }
        if (c == '"') {
            in_str = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth < 0) {
                return false;
            }
        }
    }
    return depth == 0 && p > s && *(p - 1) == '}';
}

int count_key(const char* json, const char* key)
{
    int n = 0;
    const char* p = json;
    const std::size_t klen = std::strlen(key);
    while ((p = std::strstr(p, key)) != nullptr) {
        ++n;
        p += klen;
    }
    return n;
}

const char* after_components(const char* json)
{
    const char* p = std::strstr(json, "\"components\":");
    if (p == nullptr) {
        p = std::strstr(json, "\"cmps\":");
    }
    return p != nullptr ? p : json;
}

constexpr const char kTestConfigUrl[] = "http://192.0.2.10";

// DEVICE_DISCOVERY_SCHEMA in homeassistant/components/mqtt/schemas.py, after
// abbreviation expansion, plus _MQTT_AVAILABILITY_SCHEMA. `~` is not in this
// list — it is a single-component discovery feature.
const char* const kHaDeviceRootKeys[] = {
    "device",
    "origin",
    "components",
    "availability",
    "availability_mode",
    "availability_topic",
    "availability_template",
    "payload_available",
    "payload_not_available",
    "state_topic",
    "command_topic",
    "message_expiry_interval",
    "qos",
    "encoding",
};

const char* skip_ws(const char* p)
{
    while (p != nullptr && *p != '\0' && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        ++p;
    }
    return p;
}

const char* skip_string(const char* p)
{
    if (p == nullptr || *p != '"') {
        return nullptr;
    }
    ++p;
    while (*p != '\0') {
        if (*p == '\\') {
            ++p;
            if (*p == '\0') {
                return nullptr;
            }
            ++p;
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        ++p;
    }
    return nullptr;
}

const char* skip_value(const char* p);

const char* skip_object_or_array(const char* p, char open_c, char close_c)
{
    if (p == nullptr || *p != open_c) {
        return nullptr;
    }
    ++p;
    p = skip_ws(p);
    if (p != nullptr && *p == close_c) {
        return p + 1;
    }
    while (p != nullptr && *p != '\0') {
        if (open_c == '{') {
            p = skip_string(skip_ws(p));
            p = skip_ws(p);
            if (p == nullptr || *p != ':') {
                return nullptr;
            }
            p = skip_value(p + 1);
        } else {
            p = skip_value(p);
        }
        p = skip_ws(p);
        if (p == nullptr) {
            return nullptr;
        }
        if (*p == close_c) {
            return p + 1;
        }
        if (*p != ',') {
            return nullptr;
        }
        ++p;
        p = skip_ws(p);
    }
    return nullptr;
}

const char* skip_value(const char* p)
{
    p = skip_ws(p);
    if (p == nullptr || *p == '\0') {
        return nullptr;
    }
    if (*p == '"') {
        return skip_string(p);
    }
    if (*p == '{') {
        return skip_object_or_array(p, '{', '}');
    }
    if (*p == '[') {
        return skip_object_or_array(p, '[', ']');
    }
    if (std::strncmp(p, "true", 4) == 0) {
        return p + 4;
    }
    if (std::strncmp(p, "false", 5) == 0) {
        return p + 5;
    }
    if (std::strncmp(p, "null", 4) == 0) {
        return p + 4;
    }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        if (*p == '-') {
            ++p;
        }
        while (*p >= '0' && *p <= '9') {
            ++p;
        }
        if (*p == '.') {
            ++p;
            while (*p >= '0' && *p <= '9') {
                ++p;
            }
        }
        return p;
    }
    return nullptr;
}

bool parse_json_string(const char* p, std::string* out, const char** after)
{
    if (p == nullptr || *p != '"') {
        return false;
    }
    ++p;
    std::string s;
    while (*p != '\0') {
        if (*p == '\\') {
            ++p;
            if (*p == '\0') {
                return false;
            }
            s.push_back(*p++);
            continue;
        }
        if (*p == '"') {
            if (out != nullptr) {
                *out = std::move(s);
            }
            if (after != nullptr) {
                *after = p + 1;
            }
            return true;
        }
        s.push_back(*p++);
    }
    return false;
}

const char* object_body(const char* json)
{
    json = skip_ws(json);
    if (json == nullptr || *json != '{') {
        return nullptr;
    }
    return json + 1;
}

template <typename Fn>
bool for_each_object_key(const char* obj, Fn&& fn)
{
    const char* p = object_body(obj);
    if (p == nullptr) {
        return false;
    }
    p = skip_ws(p);
    if (*p == '}') {
        return true;
    }
    while (p != nullptr && *p != '\0') {
        std::string key;
        const char* after = nullptr;
        p = skip_ws(p);
        if (!parse_json_string(p, &key, &after)) {
            return false;
        }
        p = skip_ws(after);
        if (p == nullptr || *p != ':') {
            return false;
        }
        const char* value = skip_ws(p + 1);
        const char* next = skip_value(value);
        if (next == nullptr) {
            return false;
        }
        if (!fn(key, value)) {
            return false;
        }
        p = skip_ws(next);
        if (*p == '}') {
            return true;
        }
        if (*p != ',') {
            return false;
        }
        ++p;
    }
    return false;
}

bool root_key_allowed(const std::string& key)
{
    for (const char* a : kHaDeviceRootKeys) {
        if (key == a) {
            return true;
        }
    }
    return false;
}

bool has_key_in_object(const char* obj, const char* want)
{
    bool found = false;
    for_each_object_key(obj, [&](const std::string& key, const char*) {
        if (key == want) {
            found = true;
        }
        return true;
    });
    return found;
}

bool any_key_is_tilde(const char* obj)
{
    bool found = false;
    for_each_object_key(obj, [&](const std::string& key, const char* value) {
        if (key == "~") {
            found = true;
            return false;
        }
        const char* v = skip_ws(value);
        if (v != nullptr && *v == '{') {
            if (any_key_is_tilde(v)) {
                found = true;
                return false;
            }
        }
        return true;
    });
    return found;
}

const char* root_value(const char* json, const char* want)
{
    const char* found = nullptr;
    for_each_object_key(json, [&](const std::string& key, const char* value) {
        if (key == want) {
            found = value;
            return false;
        }
        return true;
    });
    return found;
}

bool ha_device_payload_shape_ok(const char* json)
{
    if (!json_closed(json)) {
        return false;
    }
    bool roots_ok = true;
    if (!for_each_object_key(json, [&](const std::string& key, const char*) {
            if (!root_key_allowed(key)) {
                roots_ok = false;
                return false;
            }
            return true;
        })) {
        return false;
    }
    if (!roots_ok) {
        return false;
    }
    if (any_key_is_tilde(json)) {
        return false;
    }
    const char* device = root_value(json, "device");
    if (device == nullptr || skip_ws(device) == nullptr || *skip_ws(device) != '{') {
        return false;
    }
    device = skip_ws(device);
    if (!has_key_in_object(device, "name") || !has_key_in_object(device, "identifiers") ||
        !has_key_in_object(device, "manufacturer") || !has_key_in_object(device, "model") ||
        !has_key_in_object(device, "configuration_url") ||
        !has_key_in_object(device, "sw_version")) {
        return false;
    }
    const char* cmps = root_value(json, "components");
    if (cmps == nullptr || skip_ws(cmps) == nullptr || *skip_ws(cmps) != '{') {
        return false;
    }
    cmps = skip_ws(cmps);
    bool comps_ok = true;
    if (!for_each_object_key(cmps, [&](const std::string&, const char* value) {
            const char* body = skip_ws(value);
            if (body == nullptr || *body != '{') {
                comps_ok = false;
                return false;
            }
            if (!has_key_in_object(body, "p") || !has_key_in_object(body, "uniq_id")) {
                comps_ok = false;
                return false;
            }
            return true;
        })) {
        return false;
    }
    return comps_ok;
}

}  // namespace

void test_unique_id_matches_legacy()
{
    char id[p1pub::kUniqueIdMax];
    TEST_ASSERT_TRUE(p1pub::meter_unique_id("Utility meter", "Total energy consumed", id,
                                            sizeof(id)) > 0);
    TEST_ASSERT_EQUAL_STRING("utility_meter_total_energy_consumed", id);

    TEST_ASSERT_TRUE(
        p1pub::meter_unique_id("Utility meter", "Voltage phase 1", id, sizeof(id)) > 0);
    TEST_ASSERT_EQUAL_STRING("utility_meter_voltage_phase_1", id);
}

void test_no_object_id()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    const std::uint64_t mask = p1pub::enabled_field_mask(cfg);
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, mask, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NULL(std::strstr(buf, "object_id"));
    TEST_ASSERT_NULL(std::strstr(buf, "obj_id"));

    TEST_ASSERT_TRUE(p1pub::dongle_discovery(cfg, "P1AABBCC", "3.00", kTestConfigUrl, buf,
                                            sizeof(buf)) > 0);
    TEST_ASSERT_NULL(std::strstr(buf, "object_id"));
}

void test_component_name_omits_device_name()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, p1pub::enabled_field_mask(cfg), 0, buf,
                                            sizeof(buf)) > 0);
    const char* cmps = after_components(buf);
    TEST_ASSERT_NULL(std::strstr(cmps, "\"name\":\"Utility meter\""));
    TEST_ASSERT_NULL(std::strstr(cmps, "\"name\":\"utility_meter"));
}

void test_state_class_from_device_class()
{
    const p1cfg::Field* energy = p1cfg::find_field("A-0:0.0.1");
    const p1cfg::Field* power = p1cfg::find_field("1-0:1.7.0");
    const p1cfg::Field* volt = p1cfg::find_field("1-0:32.7.0");
    const p1cfg::Field* curr = p1cfg::find_field("1-0:31.7.0");
    const p1cfg::Field* gas = p1cfg::find_field("0-1:24.2.3");
    const p1cfg::Field* water = p1cfg::find_field("0-2:24.2.1");
    const p1cfg::Field* tariff = p1cfg::find_field("0-0:96.14.0");
    const p1cfg::Field* text = p1cfg::find_field("0-0:96.13.0");
    const p1cfg::Field* eq = p1cfg::find_field("0-0:96.1.1");
    TEST_ASSERT_NOT_NULL(energy);
    TEST_ASSERT_NOT_NULL(power);
    TEST_ASSERT_NOT_NULL(volt);
    TEST_ASSERT_NOT_NULL(curr);
    TEST_ASSERT_NOT_NULL(gas);
    TEST_ASSERT_NOT_NULL(water);
    TEST_ASSERT_NOT_NULL(tariff);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_NOT_NULL(eq);

    TEST_ASSERT_EQUAL_STRING("total_increasing", p1pub::ha_state_class(*energy));
    TEST_ASSERT_EQUAL_STRING("total_increasing", p1pub::ha_state_class(*gas));
    TEST_ASSERT_EQUAL_STRING("total_increasing", p1pub::ha_state_class(*water));
    TEST_ASSERT_EQUAL_STRING("measurement", p1pub::ha_state_class(*power));
    TEST_ASSERT_EQUAL_STRING("measurement", p1pub::ha_state_class(*volt));
    TEST_ASSERT_EQUAL_STRING("measurement", p1pub::ha_state_class(*curr));
    TEST_ASSERT_NULL(p1pub::ha_state_class(*tariff));
    TEST_ASSERT_NULL(p1pub::ha_state_class(*text));
    TEST_ASSERT_NULL(p1pub::ha_state_class(*eq));
    TEST_ASSERT_EQUAL_STRING("kWh", p1pub::ha_unit(*energy));
    TEST_ASSERT_EQUAL_STRING("kW", p1pub::ha_unit(*power));
    TEST_ASSERT_EQUAL_STRING("V", p1pub::ha_unit(*volt));
    TEST_ASSERT_EQUAL_STRING("A", p1pub::ha_unit(*curr));
    TEST_ASSERT_EQUAL_STRING("m\xC2\xB3", p1pub::ha_unit(*gas));
    TEST_ASSERT_EQUAL_STRING("m\xC2\xB3", p1pub::ha_unit(*water));
    TEST_ASSERT_NULL(p1pub::ha_unit(*tariff));
}

void test_meter_discovery_attributes_match_legacy()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl,
                                            p1pub::enabled_field_mask(cfg), 0, buf,
                                            sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(
        buf, "\"dev_cla\":\"energy\",\"stat_cla\":\"total_increasing\",\"unit_of_meas\":\"kWh\""));
    TEST_ASSERT_NOT_NULL(std::strstr(
        buf, "\"dev_cla\":\"power\",\"stat_cla\":\"measurement\",\"unit_of_meas\":\"kW\""));
    TEST_ASSERT_NOT_NULL(std::strstr(
        buf, "\"dev_cla\":\"voltage\",\"stat_cla\":\"measurement\",\"unit_of_meas\":\"V\""));
    TEST_ASSERT_NOT_NULL(std::strstr(
        buf, "\"dev_cla\":\"current\",\"stat_cla\":\"measurement\",\"unit_of_meas\":\"A\""));
    const char* tariff = std::strstr(buf, "\"utility_meter_active_tariff_period\":{");
    TEST_ASSERT_NOT_NULL(tariff);
    const char* tend = std::strchr(tariff, '}');
    TEST_ASSERT_NOT_NULL(tend);
    const std::string tcomp(tariff, static_cast<std::size_t>(tend - tariff + 1));
    TEST_ASSERT_NOT_NULL(std::strstr(tcomp.c_str(), "Active tariff period"));
    TEST_ASSERT_NULL(std::strstr(tcomp.c_str(), "stat_cla"));
    TEST_ASSERT_NULL(std::strstr(tcomp.c_str(), "dev_cla"));
    TEST_ASSERT_NULL(std::strstr(tcomp.c_str(), "unit_of_meas"));
}

void test_availability_topic_once_at_root()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, p1pub::enabled_field_mask(cfg), 0, buf,
                                            sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_INT(1, count_key(buf, "\"availability_topic\""));
    TEST_ASSERT_EQUAL_INT(0, count_key(buf, "\"avty_t\""));
    const char* cmps = after_components(buf);
    TEST_ASSERT_NULL(std::strstr(cmps, "availability_topic"));
    TEST_ASSERT_NULL(std::strstr(cmps, "avty_t"));
}

void test_value_template_gated_on_format()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    const std::uint64_t mask = p1pub::enabled_field_mask(cfg);

    cfg.values().frmt_pyld = 3;
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, mask, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "val_tpl"));
    TEST_ASSERT_NULL(std::strstr(buf, "value_template"));

    cfg.values().frmt_pyld = 1;
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, mask, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "val_tpl"));

    cfg.values().frmt_pyld = 0;
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, mask, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NULL(std::strstr(buf, "val_tpl"));
    TEST_ASSERT_NULL(std::strstr(buf, "value_template"));
}

void test_default_catalogue_json_size()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    const std::size_t n =
        p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, p1pub::enabled_field_mask(cfg), 0, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(json_closed(buf));
    printf("default meter discovery %u bytes\n", static_cast<unsigned>(n));

    const std::size_t d =
        p1pub::dongle_discovery(cfg, "P1AABBCC", "3.00", kTestConfigUrl, buf, sizeof(buf));
    TEST_ASSERT_TRUE(d > 0);
    TEST_ASSERT_TRUE(json_closed(buf));
    printf("dongle discovery %u bytes\n", static_cast<unsigned>(d));
}

void test_all_fields_json_size()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    const p1cfg::Field* all = p1cfg::catalogue();
    TEST_ASSERT_EQUAL_UINT(50, p1cfg::catalogue_size());
    for (std::size_t i = 0; i < p1cfg::catalogue_size(); ++i) {
        cfg.set_field_on(all[i].id, true);
    }
    char buf[p1pub::kDiscoveryCap];
    const std::size_t n =
        p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, p1pub::enabled_field_mask(cfg), 0, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(json_closed(buf));
    printf("all-fields meter discovery %u bytes\n", static_cast<unsigned>(n));
}

void test_removal_form_platform_only()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    const p1cfg::Field* water = p1cfg::find_field("0-2:24.2.1");
    TEST_ASSERT_NOT_NULL(water);
    std::uint64_t stub = 0;
    const p1cfg::Field* all = p1cfg::catalogue();
    for (std::size_t i = 0; i < p1cfg::catalogue_size(); ++i) {
        if (std::strcmp(all[i].id, water->id) == 0) {
            stub = std::uint64_t{1} << i;
            break;
        }
    }
    TEST_ASSERT_TRUE(stub != 0);
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, 0, stub, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"utility_meter_water_consumption\":{\"p\":\"sensor\"}"));
    const char* p = std::strstr(buf, "\"utility_meter_water_consumption\":");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NULL(std::strstr(p, "unique_id"));
}

void test_legacy_topic_enumeration()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    std::vector<std::string> topics;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    const std::size_t n = p1pub::for_each_legacy_discovery_topic(
        cfg, "P10AB2C3", "P1AB2C3", collect, &topics);
    TEST_ASSERT_EQUAL_UINT(n, topics.size());
    TEST_ASSERT_EQUAL_UINT(50 + 12 + 12 + 1, n);
    bool saw_legacy_device = false;
    for (const std::string& t : topics) {
        if (t == "homeassistant/device/P1AB2C3/config") {
            saw_legacy_device = true;
        }
        TEST_ASSERT_FALSE(t == "homeassistant/device/P10AB2C3/config");
    }
    TEST_ASSERT_TRUE(saw_legacy_device);

    bool saw_new = false;
    bool saw_old = false;
    bool saw_meter = false;
    bool saw_switch = false;
    for (const std::string& t : topics) {
        if (t.find("P10AB2C3") != std::string::npos) {
            saw_new = true;
        }
        if (t.find("P1AB2C3_") != std::string::npos || t.find("P1AB2C3/") != std::string::npos) {
            saw_old = true;
        }
        if (t == "homeassistant/sensor/utility_meter_total_energy_consumed/config") {
            saw_meter = true;
        }
        if (t == "homeassistant/switch/P10AB2C3_reboot/config") {
            saw_switch = true;
        }
    }
    TEST_ASSERT_TRUE(saw_new);
    TEST_ASSERT_TRUE(saw_old);
    TEST_ASSERT_TRUE(saw_meter);
    TEST_ASSERT_TRUE(saw_switch);
}

void test_legacy_debug_topics_cover_2x_unpadded_id()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    const std::uint8_t mac[6] = {0x02, 0x00, 0x00, 0xA5, 0x0C, 0x3E};
    char id[p1mqtt::kDeviceIdCap];
    char legacy[p1mqtt::kDeviceIdCap];
    p1mqtt::device_id(mac, id, sizeof(id));
    p1mqtt::legacy_device_id(mac, legacy, sizeof(legacy));
    TEST_ASSERT_EQUAL_STRING("P1A50C3E", id);
    TEST_ASSERT_EQUAL_STRING("P1A5C3E", legacy);

    std::vector<std::string> topics;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    p1pub::for_each_legacy_discovery_topic(cfg, id, legacy, collect, &topics);

    const char* k23[] = {
        "homeassistant/sensor/P1A5C3E_reboots/config",
        "homeassistant/sensor/P1A5C3E_last_reset_reason_hw/config",
        "homeassistant/sensor/P1A5C3E_free_heap_size/config",
        "homeassistant/sensor/P1A5C3E_max_allocatable_block/config",
        "homeassistant/sensor/P1A5C3E_min_free_heap/config",
        "homeassistant/sensor/P1A5C3E_last_reset_reason_fw/config",
        "homeassistant/sensor/P1A5C3E_syslog/config",
        "homeassistant/sensor/P1A5C3E_ip/config",
        "homeassistant/sensor/P1A5C3E_firmware/config",
        "homeassistant/sensor/P1A5C3E_release_channel/config",
        "homeassistant/switch/P1A5C3E_reboot/config",
        "homeassistant/device/P1A5C3E/config",
    };
    for (const char* want : k23) {
        bool found = false;
        for (const std::string& t : topics) {
            if (t == want) {
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, want);
    }
    for (const std::string& t : topics) {
        TEST_ASSERT_FALSE(t == "homeassistant/device/P1A50C3E/config");
    }
}

void test_ha_status_inbound_topic()
{
    TEST_ASSERT_TRUE(p1mqtt::match_inbound("data/devices/utility_meter",
                                           "homeassistant/status") == p1mqtt::Inbound::HaStatus);
}

void test_identity_clear_ha_device()
{
    std::vector<std::string> topics;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    const std::size_t n = p1pub::identity_clear_topics(
        "utility_meter", "data/devices/utility_meter/", "example_meter",
        "data/devices/utility_meter/", collect, &topics);
    TEST_ASSERT_EQUAL_UINT(1, n);
    TEST_ASSERT_EQUAL_UINT(1, topics.size());
    TEST_ASSERT_EQUAL_STRING("homeassistant/device/utility_meter/config", topics[0].c_str());
}

void test_identity_clear_mqtt_prefix()
{
    std::vector<std::string> topics;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    const std::size_t n = p1pub::identity_clear_topics(
        "example_meter", "data/devices/utility_meter/", "example_meter", "data/devices/example_meter/",
        collect, &topics);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT(2, topics.size());
    TEST_ASSERT_EQUAL_STRING("homeassistant/device/example_meter/config", topics[0].c_str());
    TEST_ASSERT_EQUAL_STRING("data/devices/utility_meter", topics[1].c_str());
}

void test_identity_clear_unchanged_is_noop()
{
    std::vector<std::string> topics;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    TEST_ASSERT_EQUAL_UINT(0, p1pub::identity_clear_topics(
                                 "example_meter", "data/devices/example_meter/", "example_meter",
                                 "data/devices/example_meter/", collect, &topics));
    TEST_ASSERT_EQUAL_UINT(0, topics.size());
    TEST_ASSERT_EQUAL_UINT(0, p1pub::identity_clear_topics("", "", "example_meter",
                                                           "data/devices/example_meter/", collect,
                                                           &topics));
    TEST_ASSERT_EQUAL_UINT(0, topics.size());
}

void test_withdraw_ha_discovery_topics()
{
    // Hand-written from the device discovery format:
    //   homeassistant/device/<id>/config
    // Meter uses HA_DEVICE slug; dongle uses the station device id.
    std::vector<std::string> topics;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    const std::size_t n =
        p1pub::withdraw_ha_discovery("example_meter", "P10AB2C3", collect, &topics);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT(2, topics.size());
    TEST_ASSERT_EQUAL_STRING("homeassistant/device/example_meter/config", topics[0].c_str());
    TEST_ASSERT_EQUAL_STRING("homeassistant/device/P10AB2C3/config", topics[1].c_str());

    // Empty current identity via identity_clear_topics is not enough for a full
    // withdraw: it clears meter discovery + availability, not the dongle topic.
    topics.clear();
    const std::size_t via_clear = p1pub::identity_clear_topics(
        "example_meter", "data/devices/example_meter/", "", "", collect, &topics);
    TEST_ASSERT_EQUAL_UINT(2, via_clear);
    TEST_ASSERT_EQUAL_STRING("homeassistant/device/example_meter/config", topics[0].c_str());
    TEST_ASSERT_EQUAL_STRING("data/devices/example_meter", topics[1].c_str());
}

void test_clear_published_ha_identity_leaves_mig()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store s(mem);
    s.load();
    s.set_ha_migrated();
    s.set_published_ha_identity("example_meter", "data/devices/example_meter/");
    TEST_ASSERT_TRUE(mem.has("HA_MIG"));
    TEST_ASSERT_TRUE(mem.has("HA_SLUG"));
    TEST_ASSERT_TRUE(mem.has("HA_PFIX"));

    s.clear_published_ha_identity();
    TEST_ASSERT_FALSE(s.has_published_ha_identity());
    TEST_ASSERT_FALSE(mem.has("HA_SLUG"));
    TEST_ASSERT_FALSE(mem.has("HA_PFIX"));
    TEST_ASSERT_TRUE(mem.has("HA_MIG"));
    TEST_ASSERT_TRUE(s.ha_migrated());
}

void test_reenable_after_clear_is_first_publish()
{
    // After clear, has_published is false → sync_ha_identity's first-publish path
    // (identity_clear with empty last is a noop; discovery is published fresh).
    p1cfg::MemoryBackend mem;
    p1cfg::Store s(mem);
    s.load();
    s.values().ha_en = true;
    std::strcpy(s.values().ha_device, "Example meter");
    std::strcpy(s.values().mqtt_pfix, "data/devices/example_meter/");
    s.set_published_ha_identity("example_meter", "data/devices/example_meter/");
    s.clear_published_ha_identity();
    TEST_ASSERT_FALSE(s.has_published_ha_identity());

    std::vector<std::string> topics;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    TEST_ASSERT_EQUAL_UINT(0, p1pub::identity_clear_topics(
                                 "", "", "example_meter", "data/devices/example_meter/", collect,
                                 &topics));
    TEST_ASSERT_EQUAL_UINT(0, topics.size());

    char buf[p1pub::kDiscoveryCap];
    char topic[p1pub::kTopicMax];
    TEST_ASSERT_TRUE(p1pub::device_discovery_topic("example_meter", topic, sizeof(topic)) > 0);
    const std::uint64_t mask = p1pub::enabled_field_mask(s);
    const std::size_t n =
        p1pub::meter_discovery(s, "3.00", "http://192.0.2.30", mask, 0, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"components\""));
}

void test_published_identity_survives_reload()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store a(mem);
    a.load();
    TEST_ASSERT_FALSE(a.has_published_ha_identity());
    a.set_published_ha_identity("example_meter", "data/devices/example_meter/");
    TEST_ASSERT_TRUE(a.has_published_ha_identity());
    TEST_ASSERT_EQUAL_STRING("example_meter", a.published_ha_slug());
    TEST_ASSERT_EQUAL_STRING("data/devices/example_meter/", a.published_mqtt_pfix());

    p1cfg::Store b(mem);
    b.load();
    TEST_ASSERT_TRUE(b.has_published_ha_identity());
    TEST_ASSERT_EQUAL_STRING("example_meter", b.published_ha_slug());
    TEST_ASSERT_EQUAL_STRING("data/devices/example_meter/", b.published_mqtt_pfix());
}

void test_abbreviated_payload_keeps_unique_ids()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl, p1pub::enabled_field_mask(cfg), 0, buf,
                                            sizeof(buf)) > 0);
    TEST_ASSERT_TRUE(json_closed(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"components\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"cmps\":"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"device\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"dev\":"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"origin\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"o\":"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"availability_topic\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"avty_t\":"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"uniq_id\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"unique_id\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"~\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(buf, "\"stat_t\":\"data/devices/utility_meter/total_energy_consumed\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"uniq_id\":\"utility_meter_total_energy_consumed\""));

    const p1cfg::Field* all = p1cfg::catalogue();
    for (std::size_t i = 0; i < p1cfg::catalogue_size(); ++i) {
        if (!cfg.field_on(all[i].id)) {
            continue;
        }
        char uid[p1pub::kUniqueIdMax];
        TEST_ASSERT_TRUE(p1pub::meter_unique_id(cfg.values().ha_device, all[i].label, uid,
                                                sizeof(uid)) > 0);
        char needle[p1pub::kUniqueIdMax + 24];
        std::snprintf(needle, sizeof(needle), "\"uniq_id\":\"%s\"", uid);
        TEST_ASSERT_NOT_NULL(std::strstr(buf, needle));
    }

    TEST_ASSERT_TRUE(p1pub::dongle_discovery(cfg, "P1AABBCC", "3.00", kTestConfigUrl, buf,
                                            sizeof(buf)) > 0);
    TEST_ASSERT_TRUE(json_closed(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"uniq_id\":\"P1AABBCC_free_heap_size\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"stat_t\":\"sys/devices/P1AABBCC/free_heap_size\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"unit_of_meas\":\"kB\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"uniq_id\":\"P1AABBCC_release_channel\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"ent_cat\":\"diagnostic\""));
    TEST_ASSERT_NULL(std::strstr(buf, "\"unique_id\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"~\""));
}

void test_ha_announce_debug_without_telegram()
{
    const p1pub::HaAnnounce off = p1pub::ha_announce_now(false, false);
    TEST_ASSERT_FALSE(off.debug);
    TEST_ASSERT_FALSE(off.meter);

    const p1pub::HaAnnounce no_tg = p1pub::ha_announce_now(true, false);
    TEST_ASSERT_TRUE(no_tg.debug);
    TEST_ASSERT_FALSE(no_tg.meter);

    const p1pub::HaAnnounce online_no_tg = p1pub::ha_announce_now(true, false);
    TEST_ASSERT_TRUE(online_no_tg.debug);
    TEST_ASSERT_FALSE(online_no_tg.meter);

    const p1pub::HaAnnounce online_with = p1pub::ha_announce_now(true, true);
    TEST_ASSERT_TRUE(online_with.debug);
    TEST_ASSERT_TRUE(online_with.meter);

    const p1pub::HaAnnounce ha_off = p1pub::ha_announce_now(false, true);
    TEST_ASSERT_FALSE(ha_off.debug);
    TEST_ASSERT_FALSE(ha_off.meter);
}

void test_legacy_halves_split_meter_and_debug()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    std::vector<std::string> meter;
    std::vector<std::string> debug;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    p1pub::for_each_legacy_discovery_topic(cfg, "P10AB2C3", "P1AB2C3", collect, &meter,
                                           p1pub::LegacyDisc::Meter);
    p1pub::for_each_legacy_discovery_topic(cfg, "P10AB2C3", "P1AB2C3", collect, &debug,
                                           p1pub::LegacyDisc::Debug);
    TEST_ASSERT_EQUAL_UINT(50, meter.size());
    TEST_ASSERT_EQUAL_UINT(12 + 12 + 1, debug.size());
    bool saw_catalogue = false;
    for (const std::string& t : meter) {
        TEST_ASSERT_TRUE(t.find("homeassistant/sensor/") == 0);
        TEST_ASSERT_TRUE(t.find("P10AB2C3") == std::string::npos);
        TEST_ASSERT_TRUE(t.find("P1AB2C3") == std::string::npos);
        if (t == "homeassistant/sensor/utility_meter_total_energy_consumed/config") {
            saw_catalogue = true;
        }
    }
    TEST_ASSERT_TRUE(saw_catalogue);
    bool saw_debug = false;
    bool saw_legacy_device = false;
    for (const std::string& t : debug) {
        TEST_ASSERT_FALSE(t == "homeassistant/sensor/utility_meter_total_energy_consumed/config");
        if (t == "homeassistant/sensor/P10AB2C3_free_heap_size/config") {
            saw_debug = true;
        }
        if (t == "homeassistant/device/P1AB2C3/config") {
            saw_legacy_device = true;
        }
    }
    TEST_ASSERT_TRUE(saw_debug);
    TEST_ASSERT_TRUE(saw_legacy_device);
}

void test_debug_unique_id_reuses_2x_when_ids_match()
{
    // Desk-like MAC 02:00:00:aa:bb:cc — padded and unpadded ids are both P1AABBCC.
    const std::uint8_t mac[6] = {0x02, 0x00, 0x00, 0xAA, 0xBB, 0xCC};
    char id[p1mqtt::kDeviceIdCap];
    char legacy[p1mqtt::kDeviceIdCap];
    p1mqtt::device_id(mac, id, sizeof(id));
    p1mqtt::legacy_device_id(mac, legacy, sizeof(legacy));
    TEST_ASSERT_EQUAL_STRING("P1AABBCC", id);
    TEST_ASSERT_EQUAL_STRING("P1AABBCC", legacy);

    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::dongle_discovery(cfg, id, "3.03", kTestConfigUrl, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"uniq_id\":\"P1AABBCC_free_heap_size\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"uniq_id\":\"P1AABBCC_reboot\""));

    std::vector<std::string> topics;
    auto collect = [](std::string_view t, void* ctx) {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(t.data(), t.size());
    };
    p1pub::for_each_legacy_discovery_topic(cfg, id, legacy, collect, &topics,
                                           p1pub::LegacyDisc::Debug);
    bool saw_heap = false;
    bool saw_reboot = false;
    for (const std::string& t : topics) {
        if (t == "homeassistant/sensor/P1AABBCC_free_heap_size/config") {
            saw_heap = true;
        }
        if (t == "homeassistant/switch/P1AABBCC_reboot/config") {
            saw_reboot = true;
        }
    }
    TEST_ASSERT_TRUE(saw_heap);
    TEST_ASSERT_TRUE(saw_reboot);
}

void test_1phase_discovery_has_no_l2_l3()
{
    const char* candidates[] = {
#ifdef FIXTURES_DIR
        FIXTURES_DIR,
#endif
        "fixtures",
        "../fixtures",
        "../../fixtures",
        "../../../fixtures",
    };
    std::ifstream in;
    for (const char* dir : candidates) {
        in.open(std::string(dir) + "/belgian-1phase-derived.p1", std::ios::binary);
        if (in) {
            break;
        }
        in.clear();
    }
    TEST_ASSERT_TRUE(static_cast<bool>(in));
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>()};
    p1dsmr::Reader reader;
    const auto stats = reader.feed(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    TEST_ASSERT_EQUAL_UINT(1, stats.ok);

    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    TEST_ASSERT_EQUAL(p1cfg::JsonStatus::Ok,
                      cfg.apply_json(R"({"HA_DEVICE":"Example meter","MQTT_PFIX":"data/devices/example_meter/"})"));
    const std::uint64_t mask = p1pub::discovery_field_mask(reader.data(), cfg);
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.04", kTestConfigUrl, mask, 0, buf,
                                            sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "Voltage phase 1"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "Active power consumption L1"));
    TEST_ASSERT_NULL(std::strstr(buf, "phase 2"));
    TEST_ASSERT_NULL(std::strstr(buf, "phase 3"));
    TEST_ASSERT_NULL(std::strstr(buf, " L2"));
    TEST_ASSERT_NULL(std::strstr(buf, " L3"));
}

void test_ha_device_discovery_schema_shape()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", kTestConfigUrl,
                                            p1pub::enabled_field_mask(cfg), 0, buf,
                                            sizeof(buf)) > 0);
    TEST_ASSERT_TRUE(ha_device_payload_shape_ok(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"manufacturer\":\"plan-d.io\""));
    TEST_ASSERT_NOT_NULL(std::strstr(
        buf, "\"model\":\"P1 dongle for DSMR compatible utility meters\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"configuration_url\":\"http://192.0.2.10\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"sw_version\":\"3.00\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"identifiers\":[\"utility_meter\"]"));

    TEST_ASSERT_TRUE(
        p1pub::dongle_discovery(cfg, "P1AABBCC", "3.00", kTestConfigUrl, buf, sizeof(buf)) > 0);
    TEST_ASSERT_TRUE(ha_device_payload_shape_ok(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"model\":\"P1 dongle debug monitoring\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"identifiers\":[\"P1AABBCC\"]"));

    const char kBadTilde[] =
        "{\"device\":{\"name\":\"x\",\"identifiers\":[\"x\"],\"manufacturer\":\"m\","
        "\"model\":\"m\",\"configuration_url\":\"http://1.2.3.4\",\"sw_version\":\"1\"},"
        "\"origin\":{\"name\":\"o\"},\"components\":{\"a\":{\"p\":\"sensor\",\"uniq_id\":\"a\"}},"
        "\"~\":\"data/devices/x/\"}";
    TEST_ASSERT_FALSE(ha_device_payload_shape_ok(kBadTilde));
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_unique_id_matches_legacy);
    RUN_TEST(test_no_object_id);
    RUN_TEST(test_component_name_omits_device_name);
    RUN_TEST(test_state_class_from_device_class);
    RUN_TEST(test_meter_discovery_attributes_match_legacy);
    RUN_TEST(test_availability_topic_once_at_root);
    RUN_TEST(test_value_template_gated_on_format);
    RUN_TEST(test_default_catalogue_json_size);
    RUN_TEST(test_all_fields_json_size);
    RUN_TEST(test_removal_form_platform_only);
    RUN_TEST(test_legacy_topic_enumeration);
    RUN_TEST(test_legacy_debug_topics_cover_2x_unpadded_id);
    RUN_TEST(test_ha_status_inbound_topic);
    RUN_TEST(test_identity_clear_ha_device);
    RUN_TEST(test_identity_clear_mqtt_prefix);
    RUN_TEST(test_identity_clear_unchanged_is_noop);
    RUN_TEST(test_withdraw_ha_discovery_topics);
    RUN_TEST(test_clear_published_ha_identity_leaves_mig);
    RUN_TEST(test_reenable_after_clear_is_first_publish);
    RUN_TEST(test_published_identity_survives_reload);
    RUN_TEST(test_abbreviated_payload_keeps_unique_ids);
    RUN_TEST(test_ha_device_discovery_schema_shape);
    RUN_TEST(test_ha_announce_debug_without_telegram);
    RUN_TEST(test_legacy_halves_split_meter_and_debug);
    RUN_TEST(test_debug_unique_id_reuses_2x_when_ids_match);
    RUN_TEST(test_1phase_discovery_has_no_l2_l3);
    return UNITY_END();
}
