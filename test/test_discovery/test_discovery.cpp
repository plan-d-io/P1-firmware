#include <P1Config.h>
#include <P1Mqtt.h>
#include <P1Publish.h>

#include <unity.h>

#include <cstdint>
#include <cstring>
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
    return p != nullptr ? p : json;
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
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", mask, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NULL(std::strstr(buf, "object_id"));
    TEST_ASSERT_NULL(std::strstr(buf, "obj_id"));

    TEST_ASSERT_TRUE(p1pub::dongle_discovery(cfg, "P1AABBCC", "3.00", buf, sizeof(buf)) > 0);
    TEST_ASSERT_NULL(std::strstr(buf, "object_id"));
}

void test_component_name_omits_device_name()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", p1pub::enabled_field_mask(cfg), 0, buf,
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
}

void test_availability_topic_once_at_root()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", p1pub::enabled_field_mask(cfg), 0, buf,
                                            sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_INT(1, count_key(buf, "\"availability_topic\""));
    const char* cmps = after_components(buf);
    TEST_ASSERT_NULL(std::strstr(cmps, "availability_topic"));
}

void test_value_template_gated_on_format()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    const std::uint64_t mask = p1pub::enabled_field_mask(cfg);

    cfg.values().frmt_pyld = 3;
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", mask, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "value_template"));

    cfg.values().frmt_pyld = 1;
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", mask, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "value_template"));

    cfg.values().frmt_pyld = 0;
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", mask, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NULL(std::strstr(buf, "value_template"));
}

void test_default_catalogue_json_size()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    char buf[p1pub::kDiscoveryCap];
    const std::size_t n =
        p1pub::meter_discovery(cfg, "3.00", p1pub::enabled_field_mask(cfg), 0, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(json_closed(buf));
    printf("default meter discovery %u bytes\n", static_cast<unsigned>(n));

    const std::size_t d =
        p1pub::dongle_discovery(cfg, "P1AABBCC", "3.00", buf, sizeof(buf));
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
        p1pub::meter_discovery(cfg, "3.00", p1pub::enabled_field_mask(cfg), 0, buf, sizeof(buf));
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
    TEST_ASSERT_TRUE(p1pub::meter_discovery(cfg, "3.00", 0, stub, buf, sizeof(buf)) > 0);
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
    TEST_ASSERT_EQUAL_UINT(50 + 12 + 12, n);

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

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_unique_id_matches_legacy);
    RUN_TEST(test_no_object_id);
    RUN_TEST(test_component_name_omits_device_name);
    RUN_TEST(test_state_class_from_device_class);
    RUN_TEST(test_availability_topic_once_at_root);
    RUN_TEST(test_value_template_gated_on_format);
    RUN_TEST(test_default_catalogue_json_size);
    RUN_TEST(test_all_fields_json_size);
    RUN_TEST(test_removal_form_platform_only);
    RUN_TEST(test_legacy_topic_enumeration);
    RUN_TEST(test_ha_status_inbound_topic);
    RUN_TEST(test_identity_clear_ha_device);
    RUN_TEST(test_identity_clear_mqtt_prefix);
    RUN_TEST(test_identity_clear_unchanged_is_noop);
    RUN_TEST(test_published_identity_survives_reload);
    return UNITY_END();
}
