#include <P1Config.h>

#include <unity.h>

#include <cstring>
#include <string>
#include <string_view>

namespace {

using p1cfg::Store;
using p1cfg::MemoryBackend;
using p1cfg::JsonStatus;
using p1cfg::RegionTag;
using p1cfg::MetricKind;

bool json_has(const char* json, const char* needle)
{
    return std::strstr(json, needle) != nullptr;
}

}  // namespace

void test_defaults_and_stored()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_FALSE(s.values().mqtt_en);
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);
    TEST_ASSERT_EQUAL_INT32(3, s.values().frmt_pyld);
    TEST_ASSERT_EQUAL_STRING("auto", s.values().region);
    TEST_ASSERT_EQUAL_STRING("data/devices/utility_meter/", s.values().mqtt_pfix);
    TEST_ASSERT_EQUAL_STRING("Utility meter", s.values().ha_device);

    mem.begin(false);
    mem.put_u32("MQTT_PORT", 8883);
    mem.put_bool("MQTT_EN", true);
    mem.end();

    Store s2(mem);
    s2.load();
    TEST_ASSERT_TRUE(s2.values().mqtt_en);
    TEST_ASSERT_EQUAL_UINT32(8883, s2.values().mqtt_port);
    TEST_ASSERT_EQUAL_INT32(3, s2.values().frmt_pyld);
}

void test_round_trip()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    s.values().mqtt_en = true;
    s.values().mqtt_port = 8883;
    s.values().frmt_pyld = 1;
    std::strcpy(s.values().mqtt_host, "broker.example");
    s.save();

    Store s2(mem);
    s2.load();
    TEST_ASSERT_TRUE(s2.values().mqtt_en);
    TEST_ASSERT_EQUAL_UINT32(8883, s2.values().mqtt_port);
    TEST_ASSERT_EQUAL_INT32(1, s2.values().frmt_pyld);
    TEST_ASSERT_EQUAL_STRING("broker.example", s2.values().mqtt_host);
}

void test_json_out_metadata()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    char buf[3072];
    const std::size_t n = s.to_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(json_has(buf, "\"MQTT_EN\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"type\":\"bool\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"label\":\"MQTT enabled\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"flags\":1"));
    TEST_ASSERT_TRUE(json_has(buf, "\"value\":false"));
    TEST_ASSERT_TRUE(json_has(buf, "\"MQTT_PORT\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"type\":\"u32\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"FRMT_PYLD\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"type\":\"i32\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"MQTT_HOST\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"type\":\"str\""));
    TEST_ASSERT_FALSE(json_has(buf, "\"CFG_VER\""));
}

void test_secrets_never_serialised()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    std::strcpy(s.values().mqtt_pass, "hunter2");
    char buf[3072];
    s.to_json(buf, sizeof(buf));
    TEST_ASSERT_FALSE(json_has(buf, "hunter2"));
    TEST_ASSERT_TRUE(json_has(buf, "\"MQTT_PASS\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"set\":true"));
    TEST_ASSERT_TRUE(json_has(buf, "\"flags\":3"));

    s.values().mqtt_pass[0] = '\0';
    s.to_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(json_has(buf, "\"set\":false"));
}

void test_json_in_atomic()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();

    TEST_ASSERT_EQUAL(JsonStatus::UnknownKey,
                      s.apply_json(R"({"MQTT_EN":true,"NOPE":1})"));
    TEST_ASSERT_FALSE(s.values().mqtt_en);

    TEST_ASSERT_EQUAL(JsonStatus::WrongType,
                      s.apply_json(R"({"MQTT_EN":true,"MQTT_PORT":"nope"})"));
    TEST_ASSERT_FALSE(s.values().mqtt_en);
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);

    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"FRMT_PYLD":9})"));
    TEST_ASSERT_EQUAL_INT32(3, s.values().frmt_pyld);

    TEST_ASSERT_EQUAL(JsonStatus::Ok,
                      s.apply_json(R"({"MQTT_EN":true,"MQTT_PORT":8883})"));
    TEST_ASSERT_TRUE(s.values().mqtt_en);
    TEST_ASSERT_EQUAL_UINT32(8883, s.values().mqtt_port);
}

void test_field_catalogue_overrides()
{
    TEST_ASSERT_NOT_NULL(p1cfg::find_field("A-0:0.0.1"));
    TEST_ASSERT_TRUE(p1cfg::find_field("A-0:0.0.1")->default_on);
    TEST_ASSERT_EQUAL_STRING("GridElectricityImport",
                             p1cfg::find_field("A-0:0.0.1")->metric);
    TEST_ASSERT_EQUAL(MetricKind::Cumulative, p1cfg::find_field("A-0:0.0.1")->kind);

    const p1cfg::Field* t1 = p1cfg::find_field("1-0:1.8.1");
    TEST_ASSERT_NOT_NULL(t1);
    TEST_ASSERT_EQUAL_STRING("", t1->metric);
    TEST_ASSERT_EQUAL(MetricKind::Cumulative, t1->kind);

    const p1cfg::Field* l1 = p1cfg::find_field("1-0:21.7.0");
    TEST_ASSERT_NOT_NULL(l1);
    TEST_ASSERT_TRUE(l1->default_on);
    TEST_ASSERT_NULL(l1->metric);
    TEST_ASSERT_EQUAL(MetricKind::None, l1->kind);

    TEST_ASSERT_NULL(p1cfg::find_field("0-0:98.1.0"));

    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_TRUE(s.field_on("1-0:1.8.1"));
    TEST_ASSERT_FALSE(s.field_on("0-0:1.0.0"));

    s.set_field_on("1-0:1.8.1", false);
    TEST_ASSERT_FALSE(s.field_on("1-0:1.8.1"));
    s.save();

    Store s2(mem);
    s2.load();
    TEST_ASSERT_FALSE(s2.field_on("1-0:1.8.1"));
    TEST_ASSERT_TRUE(s2.field_on("1-0:1.8.2"));

    s2.set_field_on("1-0:1.8.1", true);
    s2.save();
    Store s3(mem);
    s3.load();
    TEST_ASSERT_TRUE(s3.field_on("1-0:1.8.1"));

    s3.set_field_on("no-such-field", true);
    TEST_ASSERT_FALSE(s3.field_on("no-such-field"));
}

void test_region_filter()
{
    const p1cfg::Field* be = p1cfg::find_field("0-0:96.1.4");
    const p1cfg::Field* nl = p1cfg::find_field("1-3:0.2.8");
    const p1cfg::Field* common = p1cfg::find_field("1-0:1.8.1");
    TEST_ASSERT_NOT_NULL(be);
    TEST_ASSERT_NOT_NULL(nl);
    TEST_ASSERT_NOT_NULL(common);

    TEST_ASSERT_TRUE(p1cfg::field_in_region(*be, RegionTag::Be));
    TEST_ASSERT_FALSE(p1cfg::field_in_region(*be, RegionTag::Nl));
    TEST_ASSERT_TRUE(p1cfg::field_in_region(*nl, RegionTag::Nl));
    TEST_ASSERT_FALSE(p1cfg::field_in_region(*nl, RegionTag::Be));
    TEST_ASSERT_TRUE(p1cfg::field_in_region(*common, RegionTag::Be));
    TEST_ASSERT_TRUE(p1cfg::field_in_region(*common, RegionTag::Nl));
    TEST_ASSERT_TRUE(p1cfg::field_in_region(*be, RegionTag::Auto));
    TEST_ASSERT_TRUE(p1cfg::field_in_region(*nl, RegionTag::Auto));

    std::size_t be_n = 0;
    std::size_t nl_n = 0;
    std::size_t other_in_be = 0;
    std::size_t other_in_nl = 0;
    const p1cfg::Field* all = p1cfg::catalogue();
    for (std::size_t i = 0; i < p1cfg::catalogue_size(); ++i) {
        if (p1cfg::field_in_region(all[i], RegionTag::Be)) {
            ++be_n;
            if (all[i].region == RegionTag::Nl) {
                ++other_in_be;
            }
        }
        if (p1cfg::field_in_region(all[i], RegionTag::Nl)) {
            ++nl_n;
            if (all[i].region == RegionTag::Be) {
                ++other_in_nl;
            }
        }
    }
    TEST_ASSERT_TRUE(be_n > 0);
    TEST_ASSERT_TRUE(nl_n > 0);
    TEST_ASSERT_EQUAL_UINT(0, other_in_be);
    TEST_ASSERT_EQUAL_UINT(0, other_in_nl);
}

void test_cfg_ver_migrates_once()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_TRUE(s.migrated());
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s.values().cfg_ver);

    Store s2(mem);
    s2.load();
    TEST_ASSERT_FALSE(s2.migrated());
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s2.values().cfg_ver);
}

void test_mqtt_port_range()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();

    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"MQTT_PORT":0})"));
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);

    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"MQTT_PORT":999999})"));
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"MQTT_PORT":1})"));
    TEST_ASSERT_EQUAL_UINT32(1, s.values().mqtt_port);

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"MQTT_PORT":65535})"));
    TEST_ASSERT_EQUAL_UINT32(65535, s.values().mqtt_port);
}

void test_json_rejects_overlong_string()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    std::strcpy(s.values().mqtt_host, "keep.example");

    const std::string too_long(p1cfg::kHostMax, 'a');  // 64 chars, cap is 64 with NUL
    const std::string json = std::string("{\"MQTT_HOST\":\"") + too_long + "\"}";
    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(json));
    TEST_ASSERT_EQUAL_STRING("keep.example", s.values().mqtt_host);

    const std::string fits(p1cfg::kHostMax - 1, 'b');
    const std::string ok = std::string("{\"MQTT_HOST\":\"") + fits + "\"}";
    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(ok));
    TEST_ASSERT_EQUAL_STRING(fits.c_str(), s.values().mqtt_host);

    const std::string id_too_long(p1cfg::kIdMax, 'x');
    const std::string id_json = std::string("{\"MQTT_ID\":\"") + id_too_long + "\"}";
    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(id_json));
    TEST_ASSERT_EQUAL_STRING("", s.values().mqtt_id);
}

void test_json_rejected_batch_leaves_string()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    std::strcpy(s.values().mqtt_host, "keep.me");

    TEST_ASSERT_EQUAL(JsonStatus::Invalid,
                      s.apply_json(R"({"MQTT_HOST":"new.host","FRMT_PYLD":9})"));
    TEST_ASSERT_EQUAL_STRING("keep.me", s.values().mqtt_host);
    TEST_ASSERT_EQUAL_INT32(3, s.values().frmt_pyld);
}

void test_json_number_bounded_by_view()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();

    const char full[] = "{\"MQTT_PORT\":65535}";
    TEST_ASSERT_TRUE(std::string_view(full, 17) == "{\"MQTT_PORT\":6553");
    std::string_view truncated(full, 17);
    TEST_ASSERT_EQUAL(JsonStatus::Parse, s.apply_json(truncated));
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(std::string_view(full)));
    TEST_ASSERT_EQUAL_UINT32(65535, s.values().mqtt_port);
}

void test_override_buffer_holds_all_fields()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();

    const p1cfg::Field* all = p1cfg::catalogue();
    const std::size_t n = p1cfg::catalogue_size();
    for (std::size_t i = 0; i < n; ++i) {
        s.set_field_on(all[i].id, !all[i].default_on);
    }
    s.save();

    Store s2(mem);
    s2.load();
    for (std::size_t i = 0; i < n; ++i) {
        TEST_ASSERT_EQUAL(static_cast<int>(!all[i].default_on),
                          static_cast<int>(s2.field_on(all[i].id)));
    }
}

void test_memory_backend_refuses_overlong_string()
{
    MemoryBackend mem;
    mem.begin(false);
    char too_big[p1cfg::kOverrideBufCap + 8];
    std::memset(too_big, 'x', sizeof(too_big) - 1);
    too_big[sizeof(too_big) - 1] = '\0';
    mem.put_str("FLD_OVR", too_big);
    TEST_ASSERT_FALSE(mem.has("FLD_OVR"));

    char fits[16];
    std::strcpy(fits, "1-0:1.8.1=0");
    mem.put_str("FLD_OVR", fits);
    TEST_ASSERT_TRUE(mem.has("FLD_OVR"));
    mem.end();
}

void test_to_json_overflow_is_zero()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();

    char tiny[8];
    std::memset(tiny, 'Z', sizeof(tiny));
    TEST_ASSERT_EQUAL_UINT(0, s.to_json(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL('\0', tiny[0]);

    char buf[3072];
    const std::size_t n = s.to_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n >= 2);
    TEST_ASSERT_EQUAL_CHAR('{', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('}', buf[n - 1]);
}

void test_upl_throttle_migrates()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_TRUE(s.migrated());
    TEST_ASSERT_EQUAL_UINT32(2, s.values().cfg_ver);
    TEST_ASSERT_EQUAL_UINT32(10, s.values().upl_throttle);

    MemoryBackend old;
    old.begin(false);
    old.put_u32("CFG_VER", 1);
    old.put_u32("MQTT_PORT", 1883);
    old.end();

    Store s2(old);
    s2.load();
    TEST_ASSERT_TRUE(s2.migrated());
    TEST_ASSERT_EQUAL_UINT32(2, s2.values().cfg_ver);
    TEST_ASSERT_EQUAL_UINT32(10, s2.values().upl_throttle);
    TEST_ASSERT_EQUAL_UINT32(1883, s2.values().mqtt_port);

    Store s3(old);
    s3.load();
    TEST_ASSERT_FALSE(s3.migrated());
    TEST_ASSERT_EQUAL_UINT32(10, s3.values().upl_throttle);
}

void test_water_equipment_id_not_catalogued()
{
    TEST_ASSERT_NULL(p1cfg::find_field("0-2:96.1.1"));
    TEST_ASSERT_NOT_NULL(p1cfg::find_field("0-2:24.2.1"));
    TEST_ASSERT_NOT_NULL(p1cfg::find_field("0-1:96.1.1"));
}

void test_fixture_region_tags_unchanged()
{
    const char* ids[] = {"0-0:17.0.0", "0-0:96.3.10", "1-0:31.4.0", "0-1:24.4.0",
                         "0-2:24.1.0", "0-2:24.2.1"};
    for (const char* id : ids) {
        const p1cfg::Field* f = p1cfg::find_field(id);
        TEST_ASSERT_NOT_NULL(f);
        TEST_ASSERT_EQUAL(RegionTag::Be, f->region);
    }
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_and_stored);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_json_out_metadata);
    RUN_TEST(test_secrets_never_serialised);
    RUN_TEST(test_json_in_atomic);
    RUN_TEST(test_field_catalogue_overrides);
    RUN_TEST(test_region_filter);
    RUN_TEST(test_cfg_ver_migrates_once);
    RUN_TEST(test_mqtt_port_range);
    RUN_TEST(test_json_rejects_overlong_string);
    RUN_TEST(test_json_rejected_batch_leaves_string);
    RUN_TEST(test_json_number_bounded_by_view);
    RUN_TEST(test_override_buffer_holds_all_fields);
    RUN_TEST(test_memory_backend_refuses_overlong_string);
    RUN_TEST(test_to_json_overflow_is_zero);
    RUN_TEST(test_upl_throttle_migrates);
    RUN_TEST(test_water_equipment_id_not_catalogued);
    RUN_TEST(test_fixture_region_tags_unchanged);
    return UNITY_END();
}
