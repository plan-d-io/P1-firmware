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
    TEST_ASSERT_EQUAL_STRING("", s.values().wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("", s.values().wifi_pass);
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s.values().cfg_ver);
    TEST_ASSERT_EQUAL_UINT(35, s.setting_count());
    TEST_ASSERT_EQUAL_UINT32(0, s.values().reboots);
    TEST_ASSERT_EQUAL_STRING("", s.values().last_reset);
    TEST_ASSERT_EQUAL_UINT32(0, s.values().log_mark);
    TEST_ASSERT_FALSE(s.values().fip_en);
    TEST_ASSERT_EQUAL_STRING("", s.values().fipaddr);
    TEST_ASSERT_EQUAL_STRING("", s.values().fdns2);
    TEST_ASSERT_FALSE(s.values().eid_en);
    TEST_ASSERT_EQUAL_STRING("", s.values().uuid);
    TEST_ASSERT_EQUAL_STRING("main", s.values().rel_chan);
    TEST_ASSERT_TRUE(s.values().upd_auto);
    TEST_ASSERT_TRUE(s.values().upd_autochk);
    TEST_ASSERT_FALSE(s.values().push_full);
    TEST_ASSERT_FALSE(s.values().rint_spiffs);
    TEST_ASSERT_EQUAL_STRING("", s.values().email);
    TEST_ASSERT_NOT_NULL(s.find_setting("FIP_EN"));
    TEST_ASSERT_NOT_NULL(s.find_setting("PUSH_FULL"));
    TEST_ASSERT_NOT_NULL(s.find_setting("RINT_SPIFFS"));
    TEST_ASSERT_NOT_NULL(s.find_setting("EMAIL"));
    TEST_ASSERT_EQUAL(p1cfg::CFG_IN_API, s.find_setting("EMAIL")->flags & p1cfg::CFG_IN_API);
    TEST_ASSERT_TRUE((s.find_setting("WIFI_SSID")->flags & p1cfg::CFG_REBOOT) != 0);
    TEST_ASSERT_TRUE((s.find_setting("WIFI_PASS")->flags & p1cfg::CFG_REBOOT) != 0);
    TEST_ASSERT_TRUE((s.find_setting("FIP_EN")->flags & p1cfg::CFG_REBOOT) != 0);

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
    char buf[p1cfg::kToJsonBufMin];
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
    TEST_ASSERT_TRUE(json_has(buf, "\"WIFI_SSID\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"WIFI_PASS\""));
    TEST_ASSERT_FALSE(json_has(buf, "\"CFG_VER\""));
}

void test_secrets_never_serialised()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    std::strcpy(s.values().mqtt_pass, "hunter2");
    char buf[p1cfg::kToJsonBufMin];
    TEST_ASSERT_TRUE(s.to_json(buf, sizeof(buf)) > 0);
    TEST_ASSERT_FALSE(json_has(buf, "hunter2"));
    TEST_ASSERT_TRUE(json_has(buf, "\"MQTT_PASS\""));
    TEST_ASSERT_TRUE(json_has(buf, "\"set\":true"));
    TEST_ASSERT_TRUE(json_has(buf, "\"flags\":3"));

    s.values().mqtt_pass[0] = '\0';
    s.to_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(json_has(buf, "\"set\":false"));

    std::strcpy(s.values().wifi_pass, "not-in-json");
    s.to_json(buf, sizeof(buf));
    TEST_ASSERT_FALSE(json_has(buf, "not-in-json"));
    TEST_ASSERT_TRUE(json_has(buf, "\"WIFI_PASS\""));
    TEST_ASSERT_FALSE(json_has(buf, "\"EID_PROVKEY\""));
    TEST_ASSERT_FALSE(json_has(buf, "\"EID_PROVSEC\""));
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

    char buf[p1cfg::kToJsonBufMin];
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
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s.values().cfg_ver);
    TEST_ASSERT_EQUAL_UINT32(10, s.values().upl_throttle);

    MemoryBackend old;
    old.begin(false);
    old.put_u32("CFG_VER", 1);
    old.put_u32("MQTT_PORT", 1883);
    old.end();

    Store s2(old);
    s2.load();
    TEST_ASSERT_TRUE(s2.migrated());
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s2.values().cfg_ver);
    TEST_ASSERT_EQUAL_UINT32(10, s2.values().upl_throttle);
    TEST_ASSERT_EQUAL_UINT32(1883, s2.values().mqtt_port);

    Store s3(old);
    s3.load();
    TEST_ASSERT_FALSE(s3.migrated());
    TEST_ASSERT_EQUAL_UINT32(10, s3.values().upl_throttle);
}

void test_wifi_settings_migrate()
{
    MemoryBackend old;
    old.begin(false);
    old.put_u32("CFG_VER", 2);
    old.put_u32("MQTT_PORT", 1883);
    old.end();

    Store s(old);
    s.load();
    TEST_ASSERT_TRUE(s.migrated());
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s.values().cfg_ver);
    TEST_ASSERT_EQUAL_STRING("", s.values().wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("", s.values().wifi_pass);
    TEST_ASSERT_NOT_NULL(s.find_setting("WIFI_SSID"));
    TEST_ASSERT_NOT_NULL(s.find_setting("WIFI_PASS"));
    TEST_ASSERT_EQUAL(p1cfg::CFG_IN_API | p1cfg::CFG_SECRET | p1cfg::CFG_REBOOT,
                      static_cast<int>(s.find_setting("WIFI_PASS")->flags));
    TEST_ASSERT_EQUAL(p1cfg::CFG_IN_API | p1cfg::CFG_REBOOT,
                      static_cast<int>(s.find_setting("WIFI_SSID")->flags));
}

void test_boot_facts_migrate()
{
    MemoryBackend old;
    old.begin(false);
    old.put_u32("CFG_VER", 3);
    old.put_u32("MQTT_PORT", 1883);
    old.end();

    Store s(old);
    s.load();
    TEST_ASSERT_TRUE(s.migrated());
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s.values().cfg_ver);
    TEST_ASSERT_EQUAL_UINT32(0, s.values().reboots);
    TEST_ASSERT_EQUAL_STRING("", s.values().last_reset);
    TEST_ASSERT_NOT_NULL(s.find_setting("REBOOTS"));
    TEST_ASSERT_NOT_NULL(s.find_setting("LAST_RESET"));
}

void test_log_mark_migrate_hidden()
{
    MemoryBackend old;
    old.begin(false);
    old.put_u32("CFG_VER", 4);
    old.put_u32("REBOOTS", 3);
    old.end();

    Store s(old);
    s.load();
    TEST_ASSERT_TRUE(s.migrated());
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s.values().cfg_ver);
    TEST_ASSERT_EQUAL_UINT32(0, s.values().log_mark);
    TEST_ASSERT_EQUAL_UINT32(3, s.values().reboots);
    TEST_ASSERT_NOT_NULL(s.find_setting("LOG_MARK"));

    char buf[p1cfg::kToJsonBufMin];
    TEST_ASSERT_TRUE(s.to_json(buf, sizeof(buf)) > 0);
    TEST_ASSERT_FALSE(json_has(buf, "\"LOG_MARK\""));
    TEST_ASSERT_FALSE(json_has(buf, "\"CFG_VER\""));
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

void test_schema6_migrates_new_keys()
{
    MemoryBackend mem;
    mem.begin(false);
    mem.put_u32("CFG_VER", 5);
    mem.put_str("WIFI_SSID", "TestNet");
    mem.put_u32("REBOOTS", 7);
    mem.end();

    Store s(mem);
    s.load();
    TEST_ASSERT_TRUE(s.migrated());
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s.values().cfg_ver);
    TEST_ASSERT_EQUAL_STRING("TestNet", s.values().wifi_ssid);
    TEST_ASSERT_EQUAL_UINT32(7, s.values().reboots);
    TEST_ASSERT_FALSE(s.values().fip_en);
    TEST_ASSERT_EQUAL_STRING("main", s.values().rel_chan);
    TEST_ASSERT_TRUE(s.values().upd_auto);
    TEST_ASSERT_FALSE(s.values().push_full);
    TEST_ASSERT_TRUE(mem.has("FIP_EN"));
    TEST_ASSERT_TRUE(mem.has("REL_CHAN"));
    TEST_ASSERT_TRUE(mem.has("PUSH_FULL"));
    TEST_ASSERT_TRUE(mem.has("UUID"));
}

void test_eid_provisioning_keys_removed()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_NULL(s.find_setting("EID_PROVKEY"));
    TEST_ASSERT_NULL(s.find_setting("EID_PROVSEC"));
    TEST_ASSERT_EQUAL(JsonStatus::UnknownKey, s.apply_json(R"({"EID_PROVKEY":"x"})"));
    TEST_ASSERT_EQUAL(JsonStatus::UnknownKey, s.apply_json(R"({"EID_PROVSEC":"x"})"));

    MemoryBackend old;
    old.begin(false);
    old.put_u32("CFG_VER", 7);
    old.put_str("EID_PROVKEY", "old-key");
    old.put_str("EID_PROVSEC", "old-sec");
    old.put_str("WIFI_SSID", "TestNet");
    old.end();
    Store s2(old);
    s2.load();
    TEST_ASSERT_TRUE(s2.migrated());
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s2.values().cfg_ver);
    TEST_ASSERT_FALSE(old.has("EID_PROVKEY"));
    TEST_ASSERT_FALSE(old.has("EID_PROVSEC"));
    TEST_ASSERT_EQUAL_STRING("TestNet", s2.values().wifi_ssid);
}

void test_ip_and_rel_chan_validation()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();

    TEST_ASSERT_TRUE(p1cfg::ip_ok("192.0.2.40"));
    TEST_ASSERT_FALSE(p1cfg::ip_ok("192.0.2"));
    TEST_ASSERT_FALSE(p1cfg::ip_ok("192.0.2.256"));
    TEST_ASSERT_FALSE(p1cfg::ip_ok("not.an.ip"));
    TEST_ASSERT_FALSE(p1cfg::ip_ok(""));

    // FIP_EN false ignores address fields, however malformed.
    TEST_ASSERT_EQUAL(JsonStatus::Ok,
                      s.apply_json(R"({"FIP_EN":false,"FIPADDR":"not.an.ip","FSUBN":"x"})"));
    TEST_ASSERT_FALSE(s.values().fip_en);
    TEST_ASSERT_EQUAL_STRING("not.an.ip", s.values().fipaddr);

    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"REL_CHAN":"alpha"})"));
    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"REL_CHAN":"develop"})"));
    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"REL_CHAN":"V2"})"));
    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"REL_CHAN":"main"})"));
    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"REL_CHAN":"beta"})"));
    TEST_ASSERT_EQUAL_STRING("main", s.values().rel_chan);
}

void test_rel_chan_alpha_v2_migrate_to_develop()
{
    MemoryBackend mem;
    mem.begin(false);
    mem.put_u32("CFG_VER", 6);
    mem.put_str("REL_CHAN", "alpha");
    mem.end();
    Store s(mem);
    s.load();
    TEST_ASSERT_TRUE(s.migrated());
    TEST_ASSERT_EQUAL_STRING("develop", s.values().rel_chan);
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s.values().cfg_ver);

    MemoryBackend mem2;
    mem2.begin(false);
    mem2.put_u32("CFG_VER", 6);
    mem2.put_str("REL_CHAN", "V2");
    mem2.end();
    Store s2(mem2);
    s2.load();
    TEST_ASSERT_TRUE(s2.migrated());
    TEST_ASSERT_EQUAL_STRING("develop", s2.values().rel_chan);
    TEST_ASSERT_EQUAL_UINT32(p1cfg::kSchemaVersion, s2.values().cfg_ver);
}

void test_rel_chan_change_flag()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_FALSE(s.consume_rel_chan_changed());

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"UPL_THROTTLE":5})"));
    TEST_ASSERT_FALSE(s.consume_rel_chan_changed());

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"REL_CHAN":"develop"})"));
    TEST_ASSERT_TRUE(s.consume_rel_chan_changed());
    TEST_ASSERT_FALSE(s.consume_rel_chan_changed());

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"REL_CHAN":"develop"})"));
    TEST_ASSERT_FALSE(s.consume_rel_chan_changed());

    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"REL_CHAN":"nope"})"));
    TEST_ASSERT_FALSE(s.consume_rel_chan_changed());
    TEST_ASSERT_EQUAL_STRING("develop", s.values().rel_chan);
}

void test_cfg_reboot_changed()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    std::strcpy(s.values().wifi_ssid, "TestNet");
    std::strcpy(s.values().wifi_pass, "secret");
    s.capture_boot_network();
    TEST_ASSERT_FALSE(s.network_pending());
    TEST_ASSERT_FALSE(s.consume_reboot_needed());

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"MQTT_HOST":"192.0.2.20"})"));
    TEST_ASSERT_FALSE(s.consume_reboot_needed());
    TEST_ASSERT_FALSE(s.network_pending());

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"WIFI_PASS":"other"})"));
    TEST_ASSERT_TRUE(s.consume_reboot_needed());
    TEST_ASSERT_FALSE(s.consume_reboot_needed());
    TEST_ASSERT_TRUE(s.network_pending());

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"WIFI_SSID":"TestNet"})"));
    TEST_ASSERT_FALSE(s.consume_reboot_needed());
    TEST_ASSERT_TRUE(s.network_pending());

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"WIFI_PASS":"secret"})"));
    TEST_ASSERT_TRUE(s.consume_reboot_needed());
    TEST_ASSERT_FALSE(s.network_pending());
}

void test_static_ip_subnet_rules()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();

    TEST_ASSERT_TRUE(p1cfg::mask_contiguous(0xFFFFFF00u));
    TEST_ASSERT_TRUE(p1cfg::mask_contiguous(0xFFFF0000u));
    TEST_ASSERT_TRUE(p1cfg::mask_contiguous(0xFFFFFF80u));
    TEST_ASSERT_FALSE(p1cfg::mask_contiguous(0xFF00FF00u));
    TEST_ASSERT_FALSE(p1cfg::mask_contiguous(0u));

    TEST_ASSERT_TRUE(
        p1cfg::static_ip_consistent("192.0.2.40", "255.255.255.0", "192.0.2.1"));
    TEST_ASSERT_FALSE(
        p1cfg::static_ip_consistent("10.99.0.50", "255.255.255.0", "192.0.2.1"));
    TEST_ASSERT_FALSE(
        p1cfg::static_ip_consistent("192.0.2.0", "255.255.255.0", "192.0.2.1"));
    TEST_ASSERT_FALSE(
        p1cfg::static_ip_consistent("192.0.2.255", "255.255.255.0", "192.0.2.1"));

    const char* good =
        R"({"FIP_EN":true,"FIPADDR":"192.0.2.40","FSUBN":"255.255.255.0","FDEFGTW":"192.0.2.1","FDNS1":"192.0.2.1","FDNS2":""})";
    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(good));

    const char* wrong_subnet =
        R"({"FIP_EN":true,"FIPADDR":"10.99.0.50","FSUBN":"255.255.255.0","FDEFGTW":"192.0.2.1","FDNS1":"192.0.2.1"})";
    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(wrong_subnet));
    TEST_ASSERT_EQUAL_STRING("192.0.2.40", s.values().fipaddr);

    TEST_ASSERT_EQUAL(
        JsonStatus::Invalid,
        s.apply_json(
            R"({"FIP_EN":true,"FIPADDR":"192.0.2.40","FSUBN":"255.0.255.0","FDEFGTW":"192.0.2.1","FDNS1":"1.1.1.1"})"));
    TEST_ASSERT_EQUAL(
        JsonStatus::Invalid,
        s.apply_json(
            R"({"FIP_EN":true,"FIPADDR":"192.0.2.0","FSUBN":"255.255.255.0","FDEFGTW":"192.0.2.1","FDNS1":"1.1.1.1"})"));
    TEST_ASSERT_EQUAL(
        JsonStatus::Invalid,
        s.apply_json(
            R"({"FIP_EN":true,"FIPADDR":"192.0.2.255","FSUBN":"255.255.255.0","FDEFGTW":"192.0.2.1","FDNS1":"1.1.1.1"})"));

    TEST_ASSERT_EQUAL(
        JsonStatus::Ok,
        s.apply_json(
            R"({"FIP_EN":true,"FIPADDR":"192.0.2.40","FSUBN":"255.255.255.128","FDEFGTW":"192.0.2.1","FDNS1":"192.0.2.1"})"));
}

void test_uuid_generated_once()
{
    const std::uint8_t rnd_a[4] = {0x23, 0x11, 0x1d, 0x41};
    const std::uint8_t rnd_b[4] = {0xff, 0xee, 0xdd, 0xcc};
    char uuid[p1cfg::kUuidMax]{};
    TEST_ASSERT_TRUE(p1cfg::ensure_uuid(uuid, sizeof(uuid), rnd_a));
    TEST_ASSERT_EQUAL_STRING("P123111d41", uuid);
    TEST_ASSERT_FALSE(p1cfg::ensure_uuid(uuid, sizeof(uuid), rnd_b));
    TEST_ASSERT_EQUAL_STRING("P123111d41", uuid);
}

void test_device_id_and_claim_code()
{
    const std::uint8_t rnd[4] = {0x23, 0x11, 0x1d, 0x41};
    char uuid[p1cfg::kUuidMax]{};
    char claim[p1cfg::kEidClaimMax]{};
    TEST_ASSERT_TRUE(p1cfg::ensure_uuid(uuid, sizeof(uuid), rnd));
    TEST_ASSERT_TRUE(p1cfg::ensure_claim_code(uuid, claim, sizeof(claim)));
    TEST_ASSERT_EQUAL_STRING("P123111d41", uuid);
    TEST_ASSERT_EQUAL_STRING("23111d41", claim);
    TEST_ASSERT_FALSE(p1cfg::ensure_claim_code(uuid, claim, sizeof(claim)));

    char kept_uuid[p1cfg::kUuidMax]{};
    char kept_claim[p1cfg::kEidClaimMax]{};
    std::strcpy(kept_uuid, "P123111d41");
    std::strcpy(kept_claim, "23111d41");
    TEST_ASSERT_FALSE(p1cfg::ensure_uuid(kept_uuid, sizeof(kept_uuid), rnd));
    TEST_ASSERT_FALSE(p1cfg::ensure_claim_code(kept_uuid, kept_claim, sizeof(kept_claim)));
    TEST_ASSERT_EQUAL_STRING("P123111d41", kept_uuid);
    TEST_ASSERT_EQUAL_STRING("23111d41", kept_claim);

    char long_uuid[p1cfg::kUuidMax]{};
    char derived[p1cfg::kEidClaimMax]{};
    std::strcpy(long_uuid, "P16512d5689ff6529d");
    TEST_ASSERT_TRUE(p1cfg::ensure_claim_code(long_uuid, derived, sizeof(derived)));
    TEST_ASSERT_EQUAL_STRING("6512d5689ff6529d", derived);

    MemoryBackend mem;
    Store s(mem);
    s.load();
    std::strcpy(s.values().uuid, "P123111d41");
    std::strcpy(s.values().eidclaim, "23111d41");
    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"UUID":"P1deadbeef"})"));
    TEST_ASSERT_EQUAL_STRING("P123111d41", s.values().uuid);
    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"EIDCLAIM":"other"})"));
    TEST_ASSERT_EQUAL_STRING("23111d41", s.values().eidclaim);
}

void test_to_json_fits_max_strings()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    auto fill = [](char* dst, std::size_t cap, char c) {
        for (std::size_t i = 0; i + 1 < cap; ++i) {
            dst[i] = c;
        }
        dst[cap - 1] = '\0';
    };
    fill(s.values().mqtt_host, sizeof(s.values().mqtt_host), 'h');
    fill(s.values().mqtt_id, sizeof(s.values().mqtt_id), 'i');
    fill(s.values().mqtt_user, sizeof(s.values().mqtt_user), 'u');
    fill(s.values().mqtt_pfix, sizeof(s.values().mqtt_pfix), 'p');
    fill(s.values().ha_device, sizeof(s.values().ha_device), 'd');
    fill(s.values().region, sizeof(s.values().region), 'r');
    // region must stay valid for realism; overwrite with legal value
    std::strcpy(s.values().region, "auto");
    fill(s.values().wifi_ssid, sizeof(s.values().wifi_ssid), 'w');
    fill(s.values().last_reset, sizeof(s.values().last_reset), 'x');
    std::strcpy(s.values().fipaddr, "255.255.255.255");
    std::strcpy(s.values().fsubn, "255.255.255.255");
    std::strcpy(s.values().fdefgtw, "255.255.255.255");
    std::strcpy(s.values().fdns1, "255.255.255.255");
    std::strcpy(s.values().fdns2, "255.255.255.255");
    fill(s.values().eidclaim, sizeof(s.values().eidclaim), 'c');
    fill(s.values().uuid, sizeof(s.values().uuid), 'z');
    std::strcpy(s.values().rel_chan, "develop");
    fill(s.values().email, sizeof(s.values().email), 'e');

    char buf[p1cfg::kToJsonBufMin];
    const std::size_t n = s.to_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_TRUE(n <= p1cfg::kToJsonBufMin);
}

void test_config_api_apply_semantics()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();

    // HTTP POST /config rejects bodies above this cap (server_esp.cpp).
    constexpr std::size_t kMax = 2048;
    TEST_ASSERT_TRUE(kMax + 1 > kMax);

    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"UPL_THROTTLE":15})"));
    s.save();
    TEST_ASSERT_EQUAL_UINT32(15, s.values().upl_throttle);

    Store s2(mem);
    s2.load();
    TEST_ASSERT_EQUAL_UINT32(15, s2.values().upl_throttle);

    const auto before = s2.values().upl_throttle;
    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s2.apply_json(R"({"REL_CHAN":"nope"})"));
    TEST_ASSERT_EQUAL_STRING("main", s2.values().rel_chan);
    TEST_ASSERT_EQUAL_UINT32(before, s2.values().upl_throttle);
    TEST_ASSERT_EQUAL_STRING("Invalid", p1cfg::json_status_name(JsonStatus::Invalid));
}

void test_apply_json_reports_bad_key()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    char bad[24];

    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"MQTT_PORT":0})", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("MQTT_PORT", bad);
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);

    TEST_ASSERT_EQUAL(JsonStatus::Invalid, s.apply_json(R"({"FRMT_PYLD":9})", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("FRMT_PYLD", bad);

    TEST_ASSERT_EQUAL(JsonStatus::UnknownKey, s.apply_json(R"({"NOPE":1})", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("NOPE", bad);

    TEST_ASSERT_EQUAL(JsonStatus::WrongType,
                      s.apply_json(R"({"MQTT_PORT":"x"})", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("MQTT_PORT", bad);

    TEST_ASSERT_EQUAL(
        JsonStatus::Invalid,
        s.apply_json(
            R"({"FIP_EN":true,"FIPADDR":"10.99.0.50","FSUBN":"255.255.255.0","FDEFGTW":"192.0.2.1","FDNS1":"192.0.2.1"})",
            bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("FIPADDR", bad);

    TEST_ASSERT_EQUAL(
        JsonStatus::Invalid,
        s.apply_json(
            R"({"FIP_EN":true,"FIPADDR":"192.0.2.40","FSUBN":"not.a.mask","FDEFGTW":"192.0.2.1","FDNS1":"1.1.1.1"})",
            bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("FSUBN", bad);

    bad[0] = 'x';
    TEST_ASSERT_EQUAL(JsonStatus::Ok, s.apply_json(R"({"MQTT_PORT":1883})", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("", bad);
}

void test_reset_wifi_clears_creds_keeps_rest()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_EQUAL(JsonStatus::Ok,
                      s.apply_json(R"({
      "WIFI_SSID":"DeskNet","WIFI_PASS":"secretwifi",
      "FIP_EN":true,"FIPADDR":"192.0.2.30","FSUBN":"255.255.255.0",
      "FDEFGTW":"192.0.2.1","FDNS1":"1.1.1.1","FDNS2":"8.8.8.8",
      "MQTT_EN":true,"MQTT_HOST":"192.0.2.20","MQTT_PORT":1883,
      "MQTT_PFIX":"data/devices/example_meter/",
      "HA_EN":true,"HA_DEVICE":"Example meter","UPL_THROTTLE":15
    })"));
    s.save();

    s.reset_wifi();

    TEST_ASSERT_EQUAL_STRING("", s.values().wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("", s.values().wifi_pass);
    TEST_ASSERT_FALSE(s.values().fip_en);
    TEST_ASSERT_EQUAL_STRING("192.0.2.30", s.values().fipaddr);
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", s.values().fsubn);
    TEST_ASSERT_EQUAL_STRING("192.0.2.1", s.values().fdefgtw);
    TEST_ASSERT_EQUAL_STRING("1.1.1.1", s.values().fdns1);
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", s.values().fdns2);
    TEST_ASSERT_TRUE(s.values().mqtt_en);
    TEST_ASSERT_EQUAL_STRING("192.0.2.20", s.values().mqtt_host);
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);
    TEST_ASSERT_EQUAL_STRING("data/devices/example_meter/", s.values().mqtt_pfix);
    TEST_ASSERT_TRUE(s.values().ha_en);
    TEST_ASSERT_EQUAL_STRING("Example meter", s.values().ha_device);
    TEST_ASSERT_EQUAL_UINT32(15, s.values().upl_throttle);
}

void test_factory_reset_defaults_and_clears_ha_keys()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_EQUAL(JsonStatus::Ok,
                      s.apply_json(R"({
      "WIFI_SSID":"DeskNet","WIFI_PASS":"secretwifi",
      "MQTT_EN":true,"MQTT_HOST":"broker.example","MQTT_PORT":8883,
      "MQTT_PFIX":"data/devices/example_meter/",
      "HA_EN":true,"HA_DEVICE":"Example meter","UPL_THROTTLE":15,
      "EMAIL":"desk@example.com","PUSH_FULL":true
    })"));
    s.set_field_on("1-0:1.8.1", false);
    s.save();
    s.set_ha_migrated();
    s.set_published_ha_identity("example_meter", "data/devices/example_meter/");
    s.set_ota_note("app1", 301);
    s.set_ota_failed(301);
    TEST_ASSERT_TRUE(s.ha_migrated());
    TEST_ASSERT_TRUE(s.has_published_ha_identity());
    TEST_ASSERT_TRUE(s.has_ota_note());
    TEST_ASSERT_TRUE(s.has_ota_failed());
    TEST_ASSERT_TRUE(mem.has("HA_MIG"));
    TEST_ASSERT_TRUE(mem.has("HA_SLUG"));
    TEST_ASSERT_TRUE(mem.has("HA_PFIX"));
    TEST_ASSERT_TRUE(mem.has("OTA_NPART"));
    TEST_ASSERT_TRUE(mem.has("OTA_FAIL"));

    s.factory_reset();

    TEST_ASSERT_FALSE(s.values().mqtt_en);
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);
    TEST_ASSERT_EQUAL_STRING("", s.values().mqtt_host);
    TEST_ASSERT_EQUAL_STRING("data/devices/utility_meter/", s.values().mqtt_pfix);
    TEST_ASSERT_FALSE(s.values().ha_en);
    TEST_ASSERT_EQUAL_STRING("Utility meter", s.values().ha_device);
    TEST_ASSERT_EQUAL_UINT32(10, s.values().upl_throttle);
    TEST_ASSERT_EQUAL_STRING("", s.values().wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("", s.values().wifi_pass);
    TEST_ASSERT_EQUAL_STRING("", s.values().email);
    TEST_ASSERT_FALSE(s.values().push_full);
    TEST_ASSERT_TRUE(s.values().upd_auto);
    TEST_ASSERT_TRUE(s.values().upd_autochk);
    TEST_ASSERT_EQUAL_STRING("auto", s.values().region);
    TEST_ASSERT_EQUAL_STRING("main", s.values().rel_chan);
    TEST_ASSERT_EQUAL_INT32(3, s.values().frmt_pyld);
    // Overrides cleared → catalogue default_on.
    TEST_ASSERT_TRUE(s.field_on("1-0:1.8.1"));
    TEST_ASSERT_FALSE(s.ha_migrated());
    TEST_ASSERT_FALSE(s.has_published_ha_identity());
    TEST_ASSERT_EQUAL_STRING("", s.published_ha_slug());
    TEST_ASSERT_EQUAL_STRING("", s.published_mqtt_pfix());
    TEST_ASSERT_FALSE(mem.has("HA_MIG"));
    TEST_ASSERT_FALSE(mem.has("HA_SLUG"));
    TEST_ASSERT_FALSE(mem.has("HA_PFIX"));
    TEST_ASSERT_FALSE(s.has_ota_note());
    TEST_ASSERT_FALSE(s.has_ota_failed());
    TEST_ASSERT_FALSE(mem.has("OTA_NPART"));
    TEST_ASSERT_FALSE(mem.has("OTA_NVER"));
    TEST_ASSERT_FALSE(mem.has("OTA_FAIL"));
}

void test_selectable_field_count()
{
    // Literal from the catalogue: 50 entries, 9 identifiers/type codes off.
    TEST_ASSERT_EQUAL_UINT(50, p1cfg::catalogue_size());
    std::size_t n = 0;
    for (std::size_t i = 0; i < p1cfg::catalogue_size(); ++i) {
        if (p1cfg::catalogue()[i].selectable) {
            ++n;
        }
    }
    TEST_ASSERT_EQUAL_UINT(41, n);
    TEST_ASSERT_FALSE(p1cfg::find_field("identification")->selectable);
    TEST_ASSERT_FALSE(p1cfg::find_field("0-0:1.0.0")->selectable);
    TEST_ASSERT_TRUE(p1cfg::find_field("A-0:0.0.1")->selectable);
    TEST_ASSERT_TRUE(p1cfg::find_field("0-2:24.2.1")->selectable);
}

void test_apply_fields_json()
{
    MemoryBackend mem;
    Store s(mem);
    s.load();
    TEST_ASSERT_TRUE(s.field_on("1-0:1.8.1"));
    TEST_ASSERT_FALSE(s.field_on("0-2:24.2.1"));

    TEST_ASSERT_EQUAL(JsonStatus::Ok,
                      s.apply_fields_json(R"({"1-0:1.8.1":false,"0-2:24.2.1":true})"));
    TEST_ASSERT_FALSE(s.field_on("1-0:1.8.1"));
    TEST_ASSERT_TRUE(s.field_on("0-2:24.2.1"));
    s.save();

    char bad[24];
    TEST_ASSERT_EQUAL(JsonStatus::UnknownKey,
                      s.apply_fields_json(R"({"no-such":true})", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("no-such", bad);
    TEST_ASSERT_FALSE(s.field_on("1-0:1.8.1"));
    TEST_ASSERT_TRUE(s.field_on("0-2:24.2.1"));

    TEST_ASSERT_EQUAL(JsonStatus::UnknownKey,
                      s.apply_fields_json(R"({"identification":true})", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("identification", bad);
    TEST_ASSERT_FALSE(s.field_on("1-0:1.8.1"));

    TEST_ASSERT_EQUAL(JsonStatus::WrongType,
                      s.apply_fields_json(R"({"1-0:1.8.2":"yes"})", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("1-0:1.8.2", bad);
    TEST_ASSERT_TRUE(s.field_on("1-0:1.8.2"));
}

void fill_full_2x(MemoryBackend& L, bool beta, bool alpha, bool v2)
{
    L.begin(false);
    L.put_str("WIFI_SSID", "TestNet");
    L.put_str("WIFI_PASSWD", "testpass1");
    L.put_bool("FIP_EN", true);
    L.put_u32("FIPADDR", 0x1E0200C0);
    L.put_u32("FSUBN", 0x00FFFFFF);
    L.put_u32("FDEFGTW", 0x010200C0);
    L.put_u32("FDNS1", 0x010200C0);
    L.put_u32("FDNS2", 0);
    L.put_bool("MQTT_EN", true);
    L.put_bool("MQTT_TLS", false);
    L.put_bool("MQTT_AUTH", false);
    L.put_u32("MQTT_PORT", 1883);
    L.put_str("MQTT_HOST", "192.0.2.20");
    L.put_str("MQTT_ID", "P1AABBCC");
    L.put_str("MQTT_USER", "");
    L.put_str("MQTT_PFIX", "data/devices/example_meter/");
    L.put_str("MQTT_PASS", "secret");
    L.put_bool("HA_EN", true);
    L.put_str("HA_DEVICE", "Example meter");
    L.put_i32("FRMT_PYLD", 3);
    L.put_u32("UPL_THROTTLE", 10);
    L.put_bool("PUSH_FULL", false);
    L.put_bool("UPD_AUTO", false);
    L.put_bool("UPD_AUTOCHK", false);
    L.put_bool("EID_EN", false);
    L.put_str("EIDCLAIM", "claim");
    L.put_str("EID_PROVKEY", "pkey");
    L.put_str("EID_PROVSEC", "psec");
    L.put_str("UUID", "P16512d5689ff6529d");
    L.put_str("EMAIL", "desk@example.com");
    L.put_u32("reboots", 42);
    L.put_bool("BETA_FLT", beta);
    L.put_bool("ALPHA_FLT", alpha);
    L.put_bool("V2_FLT", v2);
    L.end();
}

void test_format_arduino_ipv4()
{
    char buf[16];
    TEST_ASSERT_TRUE(p1cfg::format_arduino_ipv4(0x1E0200C0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("192.0.2.30", buf);
    TEST_ASSERT_TRUE(p1cfg::format_arduino_ipv4(0x00FFFFFF, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", buf);
    TEST_ASSERT_TRUE(p1cfg::format_arduino_ipv4(0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_legacy_import_full_2x()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, true, false, false);
    const std::size_t writes = legacy.write_count();
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_TRUE(s.legacy_import_ran());
    TEST_ASSERT_FALSE(s.legacy_import_again());
    TEST_ASSERT_TRUE(s.import_pending());
    TEST_ASSERT_TRUE(s.legacy_imported() > 0);
    TEST_ASSERT_EQUAL_STRING("TestNet", s.values().wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("testpass1", s.values().wifi_pass);
    TEST_ASSERT_TRUE(s.values().fip_en);
    TEST_ASSERT_EQUAL_STRING("192.0.2.30", s.values().fipaddr);
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", s.values().fsubn);
    TEST_ASSERT_EQUAL_STRING("192.0.2.1", s.values().fdefgtw);
    TEST_ASSERT_EQUAL_STRING("192.0.2.1", s.values().fdns1);
    TEST_ASSERT_EQUAL_STRING("", s.values().fdns2);
    TEST_ASSERT_TRUE(s.values().mqtt_en);
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);
    TEST_ASSERT_EQUAL_STRING("192.0.2.20", s.values().mqtt_host);
    TEST_ASSERT_EQUAL_STRING("data/devices/example_meter/", s.values().mqtt_pfix);
    TEST_ASSERT_EQUAL_STRING("secret", s.values().mqtt_pass);
    TEST_ASSERT_TRUE(s.values().ha_en);
    TEST_ASSERT_EQUAL_STRING("Example meter", s.values().ha_device);
    TEST_ASSERT_EQUAL_UINT32(42, s.values().reboots);
    TEST_ASSERT_EQUAL_STRING("develop", s.values().rel_chan);
    TEST_ASSERT_FALSE(s.values().upd_auto);
    TEST_ASSERT_EQUAL_STRING("desk@example.com", s.values().email);
    TEST_ASSERT_EQUAL_STRING("claim", s.values().eidclaim);
    TEST_ASSERT_EQUAL_STRING("P16512d5689ff6529d", s.values().uuid);
    TEST_ASSERT_FALSE(ours.has("EID_PROVKEY"));
    TEST_ASSERT_FALSE(ours.has("EID_PROVSEC"));
    TEST_ASSERT_EQUAL(writes, legacy.write_count());
}

void test_legacy_import_channel_flags()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, false, false, false);
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_EQUAL_STRING("main", s.values().rel_chan);

    MemoryBackend ours_a;
    MemoryBackend legacy_a;
    fill_full_2x(legacy_a, false, true, false);
    Store sa(ours_a, &legacy_a);
    sa.load();
    TEST_ASSERT_EQUAL_STRING("develop", sa.values().rel_chan);

    MemoryBackend ours_v;
    MemoryBackend legacy_v;
    fill_full_2x(legacy_v, false, false, true);
    Store sv(ours_v, &legacy_v);
    sv.load();
    TEST_ASSERT_EQUAL_STRING("develop", sv.values().rel_chan);
}

void test_legacy_import_1x_keeps_defaults()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    legacy.begin(false);
    legacy.put_str("WIFI_SSID", "TestNet");
    legacy.put_str("WIFI_PASSWD", "testpass1");
    legacy.put_bool("MQTT_EN", true);
    legacy.put_str("MQTT_HOST", "192.0.2.20");
    legacy.put_u32("MQTT_PORT", 1883);
    legacy.end();
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_TRUE(s.legacy_import_ran());
    TEST_ASSERT_EQUAL_STRING("TestNet", s.values().wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("testpass1", s.values().wifi_pass);
    TEST_ASSERT_TRUE(s.values().mqtt_en);
    TEST_ASSERT_EQUAL_STRING("192.0.2.20", s.values().mqtt_host);
    TEST_ASSERT_EQUAL_STRING("data/devices/utility_meter/", s.values().mqtt_pfix);
    TEST_ASSERT_EQUAL_STRING("Utility meter", s.values().ha_device);
    TEST_ASSERT_FALSE(s.values().fip_en);
    TEST_ASSERT_EQUAL_STRING("", s.values().fipaddr);
    TEST_ASSERT_EQUAL_STRING("main", s.values().rel_chan);
}

void test_legacy_import_wrong_type_skipped()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, true, false, false);
    legacy.begin(false);
    legacy.put_i32("MQTT_PORT", 1883);
    legacy.end();
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_EQUAL_UINT32(1883, s.values().mqtt_port);
    TEST_ASSERT_EQUAL_STRING("TestNet", s.values().wifi_ssid);
    bool saw_port = false;
    for (std::size_t i = 0; i < s.legacy_skip_count(); ++i) {
        if (std::strcmp(s.legacy_skip_key(i), "MQTT_PORT") == 0) {
            saw_port = true;
        }
    }
    TEST_ASSERT_TRUE(saw_port);
}

void test_legacy_import_overlong_host_skipped()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, true, false, false);
    char long_host[80];
    std::memset(long_host, 'x', 70);
    long_host[70] = '\0';
    legacy.begin(false);
    legacy.put_str("MQTT_HOST", long_host);
    legacy.end();
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_EQUAL_STRING("", s.values().mqtt_host);
    TEST_ASSERT_EQUAL_STRING("TestNet", s.values().wifi_ssid);
    bool saw = false;
    for (std::size_t i = 0; i < s.legacy_skip_count(); ++i) {
        if (std::strcmp(s.legacy_skip_key(i), "MQTT_HOST") == 0) {
            saw = true;
        }
    }
    TEST_ASSERT_TRUE(saw);
}

void test_legacy_import_fip_en_without_address()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, false, false, false);
    legacy.begin(false);
    legacy.put_u32("FIPADDR", 0);
    legacy.end();
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_FALSE(s.values().fip_en);
    TEST_ASSERT_EQUAL_STRING("", s.values().fipaddr);
}

void test_legacy_import_skipped_when_cfg_ver_present()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, true, false, false);
    ours.begin(false);
    ours.put_u32("CFG_VER", p1cfg::kSchemaVersion);
    ours.put_str("WIFI_SSID", "Already");
    ours.end();
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_FALSE(s.legacy_import_ran());
    TEST_ASSERT_EQUAL_STRING("Already", s.values().wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("main", s.values().rel_chan);
}

void test_legacy_namespace_unchanged_after_import()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, true, false, false);
    const std::size_t writes = legacy.write_count();
    const std::size_t keys = legacy.key_count();
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_TRUE(s.legacy_import_ran());
    TEST_ASSERT_EQUAL(writes, legacy.write_count());
    TEST_ASSERT_EQUAL(keys, legacy.key_count());
    TEST_ASSERT_TRUE(legacy.has("WIFI_PASSWD"));
    TEST_ASSERT_TRUE(legacy.has("BETA_FLT"));
}

void test_factory_reset_clears_legacy_namespace()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, true, false, false);
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_TRUE(s.legacy_import_ran());
    TEST_ASSERT_TRUE(s.import_pending());
    TEST_ASSERT_TRUE(legacy.has("WIFI_SSID"));
    s.factory_reset();
    TEST_ASSERT_FALSE(s.import_pending());
    TEST_ASSERT_EQUAL_UINT(0, legacy.key_count());
    TEST_ASSERT_FALSE(legacy.has("WIFI_SSID"));

    Store s2(ours, &legacy);
    s2.load();
    TEST_ASSERT_FALSE(s2.legacy_import_ran());
    TEST_ASSERT_FALSE(s2.import_pending());
    TEST_ASSERT_EQUAL_STRING("", s2.values().wifi_ssid);
}

void test_legacy_reimport_while_pending()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, true, false, false);
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_TRUE(s.import_pending());
    s.values().region[0] = 'b';
    s.values().region[1] = 'e';
    s.values().region[2] = '\0';
    s.save();
    legacy.begin(false);
    legacy.put_str("WIFI_PASSWD", "newpass");
    legacy.end();
    Store s2(ours, &legacy);
    s2.load();
    TEST_ASSERT_TRUE(s2.legacy_import_ran());
    TEST_ASSERT_TRUE(s2.legacy_import_again());
    TEST_ASSERT_EQUAL_STRING("newpass", s2.values().wifi_pass);
    TEST_ASSERT_EQUAL_STRING("be", s2.values().region);
}

void test_legacy_no_import_when_mark_cleared()
{
    MemoryBackend ours;
    MemoryBackend legacy;
    fill_full_2x(legacy, true, false, false);
    Store s(ours, &legacy);
    s.load();
    TEST_ASSERT_TRUE(s.import_pending());
    std::snprintf(s.values().wifi_ssid, sizeof(s.values().wifi_ssid), "KeepMe");
    s.save();
    s.clear_import_pending();
    TEST_ASSERT_FALSE(s.import_pending());
    Store s2(ours, &legacy);
    s2.load();
    TEST_ASSERT_FALSE(s2.legacy_import_ran());
    TEST_ASSERT_FALSE(s2.import_pending());
    TEST_ASSERT_EQUAL_STRING("KeepMe", s2.values().wifi_ssid);
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
    RUN_TEST(test_wifi_settings_migrate);
    RUN_TEST(test_boot_facts_migrate);
    RUN_TEST(test_log_mark_migrate_hidden);
    RUN_TEST(test_water_equipment_id_not_catalogued);
    RUN_TEST(test_fixture_region_tags_unchanged);
    RUN_TEST(test_schema6_migrates_new_keys);
    RUN_TEST(test_eid_provisioning_keys_removed);
    RUN_TEST(test_ip_and_rel_chan_validation);
    RUN_TEST(test_rel_chan_alpha_v2_migrate_to_develop);
    RUN_TEST(test_rel_chan_change_flag);
    RUN_TEST(test_cfg_reboot_changed);
    RUN_TEST(test_static_ip_subnet_rules);
    RUN_TEST(test_uuid_generated_once);
    RUN_TEST(test_device_id_and_claim_code);
    RUN_TEST(test_to_json_fits_max_strings);
    RUN_TEST(test_config_api_apply_semantics);
    RUN_TEST(test_apply_json_reports_bad_key);
    RUN_TEST(test_reset_wifi_clears_creds_keeps_rest);
    RUN_TEST(test_factory_reset_defaults_and_clears_ha_keys);
    RUN_TEST(test_selectable_field_count);
    RUN_TEST(test_apply_fields_json);
    RUN_TEST(test_format_arduino_ipv4);
    RUN_TEST(test_legacy_import_full_2x);
    RUN_TEST(test_legacy_import_channel_flags);
    RUN_TEST(test_legacy_import_1x_keeps_defaults);
    RUN_TEST(test_legacy_import_wrong_type_skipped);
    RUN_TEST(test_legacy_import_overlong_host_skipped);
    RUN_TEST(test_legacy_import_fip_en_without_address);
    RUN_TEST(test_legacy_import_skipped_when_cfg_ver_present);
    RUN_TEST(test_legacy_namespace_unchanged_after_import);
    RUN_TEST(test_factory_reset_clears_legacy_namespace);
    RUN_TEST(test_legacy_reimport_while_pending);
    RUN_TEST(test_legacy_no_import_when_mark_cleared);
    return UNITY_END();
}
