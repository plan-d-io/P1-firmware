#include <P1Config.h>
#include <P1Mqtt.h>

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string_view>

namespace {

const std::uint8_t kMacLow[6] = {0x11, 0x22, 0x33, 0x0A, 0xB2, 0xC3};
const std::uint8_t kMacFull[6] = {0x11, 0x22, 0x33, 0x1A, 0xB2, 0xC3};

}  // namespace

void test_device_id_from_station_mac()
{
    const std::uint8_t mac[6] = {0x02, 0x00, 0x00, 0xAA, 0xBB, 0xCC};
    char id[p1mqtt::kDeviceIdCap];
    TEST_ASSERT_TRUE(p1mqtt::mac_usable(mac));
    p1mqtt::device_id(mac, id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING("P1AABBCC", id);
}

void test_all_zero_mac_is_invalid()
{
    const std::uint8_t z[6] = {};
    TEST_ASSERT_FALSE(p1mqtt::mac_usable(z));
    TEST_ASSERT_FALSE(p1mqtt::mac_usable(nullptr));

    char id[p1mqtt::kDeviceIdCap];
    std::memset(id, 'X', sizeof(id));
    p1mqtt::device_id(z, id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING("", id);

    char mqtt_id[p1cfg::kIdMax];
    mqtt_id[0] = '\0';
    TEST_ASSERT_FALSE(p1mqtt::migrate_mqtt_id(mqtt_id, sizeof(mqtt_id), z));
    TEST_ASSERT_EQUAL_STRING("", mqtt_id);
}

void test_device_id_pads_low_byte()
{
    char id[p1mqtt::kDeviceIdCap];
    char legacy[p1mqtt::kDeviceIdCap];

    p1mqtt::device_id(kMacLow, id, sizeof(id));
    p1mqtt::legacy_device_id(kMacLow, legacy, sizeof(legacy));
    TEST_ASSERT_EQUAL_STRING("P10AB2C3", id);
    TEST_ASSERT_EQUAL_STRING("P1AB2C3", legacy);
    TEST_ASSERT_EQUAL_UINT(8, std::strlen(id));
    TEST_ASSERT_EQUAL_UINT(7, std::strlen(legacy));

    p1mqtt::device_id(kMacFull, id, sizeof(id));
    p1mqtt::legacy_device_id(kMacFull, legacy, sizeof(legacy));
    TEST_ASSERT_EQUAL_STRING("P11AB2C3", id);
    TEST_ASSERT_EQUAL_STRING("P11AB2C3", legacy);
}

void test_mqtt_id_migrates_empty()
{
    char id[p1cfg::kIdMax];

    id[0] = '\0';
    TEST_ASSERT_TRUE(p1mqtt::migrate_mqtt_id(id, sizeof(id), kMacLow));
    TEST_ASSERT_EQUAL_STRING("P10AB2C3", id);

    id[0] = '\0';
    TEST_ASSERT_TRUE(p1mqtt::migrate_mqtt_id(id, sizeof(id), kMacFull));
    TEST_ASSERT_EQUAL_STRING("P11AB2C3", id);
}

void test_mqtt_id_migrates_legacy()
{
    char id[p1cfg::kIdMax];

    std::strcpy(id, "P1AB2C3");
    TEST_ASSERT_TRUE(p1mqtt::migrate_mqtt_id(id, sizeof(id), kMacLow));
    TEST_ASSERT_EQUAL_STRING("P10AB2C3", id);

    std::strcpy(id, "P11AB2C3");
    TEST_ASSERT_FALSE(p1mqtt::migrate_mqtt_id(id, sizeof(id), kMacFull));
    TEST_ASSERT_EQUAL_STRING("P11AB2C3", id);
}

void test_mqtt_id_leaves_user_choice()
{
    char id[p1cfg::kIdMax];
    std::strcpy(id, "custom-dongle");
    TEST_ASSERT_FALSE(p1mqtt::migrate_mqtt_id(id, sizeof(id), kMacLow));
    TEST_ASSERT_EQUAL_STRING("custom-dongle", id);

    std::strcpy(id, "P10AB2C3");
    TEST_ASSERT_FALSE(p1mqtt::migrate_mqtt_id(id, sizeof(id), kMacLow));
    TEST_ASSERT_EQUAL_STRING("P10AB2C3", id);
}

void test_broker_uri()
{
    char uri[p1mqtt::kUriMax];
    TEST_ASSERT_EQUAL_UINT(22, p1mqtt::broker_uri("192.0.2.20", 1883, uri, sizeof(uri)));
    TEST_ASSERT_EQUAL_STRING("mqtt://192.0.2.20:1883", uri);

    TEST_ASSERT_TRUE(p1mqtt::broker_uri("broker.example", 8883, uri, sizeof(uri)) > 0);
    TEST_ASSERT_EQUAL_STRING("mqtt://broker.example:8883", uri);

    TEST_ASSERT_EQUAL_UINT(0, p1mqtt::broker_uri("", 1883, uri, sizeof(uri)));
    TEST_ASSERT_EQUAL_STRING("", uri);
    TEST_ASSERT_EQUAL_UINT(0, p1mqtt::broker_uri("host", 0, uri, sizeof(uri)));
    TEST_ASSERT_EQUAL_UINT(0, p1mqtt::broker_uri("host", 65536u, uri, sizeof(uri)));

    char tiny[8];
    TEST_ASSERT_EQUAL_UINT(0, p1mqtt::broker_uri("192.0.2.20", 1883, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

void test_throttle_gate()
{
    TEST_ASSERT_TRUE(p1mqtt::throttle_due(0, 0, 0));
    TEST_ASSERT_TRUE(p1mqtt::throttle_due(0, 50, 0));
    TEST_ASSERT_TRUE(p1mqtt::throttle_due(0, 5000, 4000));

    TEST_ASSERT_FALSE(p1mqtt::throttle_due(10, 9999, 0));
    TEST_ASSERT_TRUE(p1mqtt::throttle_due(10, 10000, 0));
    TEST_ASSERT_TRUE(p1mqtt::throttle_due(10, 10001, 0));

    TEST_ASSERT_FALSE(p1mqtt::throttle_due(10, 19999, 10000));
    TEST_ASSERT_TRUE(p1mqtt::throttle_due(10, 20000, 10000));
    TEST_ASSERT_TRUE(p1mqtt::throttle_due(10, 25000, 10000));
}

void test_inbound_topics_match_dispatcher()
{
    const char* avail = "data/devices/utility_meter";
    char reboot[p1mqtt::kTopicBuf];
    char config[p1mqtt::kTopicBuf];

    TEST_ASSERT_TRUE(p1mqtt::inbound_topic(avail, p1mqtt::Inbound::Reboot, reboot,
                                           sizeof(reboot)) > 0);
    TEST_ASSERT_TRUE(p1mqtt::inbound_topic(avail, p1mqtt::Inbound::Config, config,
                                           sizeof(config)) > 0);
    TEST_ASSERT_EQUAL_STRING("data/devices/utility_meter/set/reboot", reboot);
    TEST_ASSERT_EQUAL_STRING("data/devices/utility_meter/set/config", config);

    TEST_ASSERT_TRUE(p1mqtt::match_inbound(avail, reboot) == p1mqtt::Inbound::Reboot);
    TEST_ASSERT_TRUE(p1mqtt::match_inbound(avail, config) == p1mqtt::Inbound::Config);

    TEST_ASSERT_TRUE(p1mqtt::match_inbound(avail, "set/devices/Utility meter/config") ==
                     p1mqtt::Inbound::None);
    TEST_ASSERT_TRUE(p1mqtt::match_inbound(avail, "data/devices/utility_meter/set/reboot/extra") ==
                     p1mqtt::Inbound::None);
}

void test_inbound_reassembly()
{
    const char* topic = "data/devices/utility_meter/set/config";
    const char payload[] = "ABCDEFGHIJabcdefghij0123456789";
    const int n = static_cast<int>(sizeof(payload) - 1);

    p1mqtt::InboundAssembler whole;
    TEST_ASSERT_TRUE(whole.feed(topic, payload, n, n, 0));
    char t[p1mqtt::kTopicBuf];
    char p[64];
    TEST_ASSERT_TRUE(whole.take(t, sizeof(t), p, sizeof(p)));
    TEST_ASSERT_EQUAL_STRING(topic, t);
    TEST_ASSERT_EQUAL_STRING(payload, p);

    p1mqtt::InboundAssembler split;
    TEST_ASSERT_FALSE(split.feed(topic, payload, 10, n, 0));
    TEST_ASSERT_FALSE(split.ready());
    TEST_ASSERT_FALSE(split.feed(std::string_view{}, payload + 10, 10, n, 10));
    TEST_ASSERT_TRUE(split.feed(std::string_view{}, payload + 20, n - 20, n, 20));
    TEST_ASSERT_TRUE(split.take(t, sizeof(t), p, sizeof(p)));
    TEST_ASSERT_EQUAL_STRING(topic, t);
    TEST_ASSERT_EQUAL_STRING(payload, p);
}

void test_inbound_oversized_dropped()
{
    p1mqtt::InboundAssembler a;
    char chunk[16]{};
    const int too_big = static_cast<int>(p1mqtt::InboundAssembler::kMax) + 1;
    TEST_ASSERT_FALSE(a.feed("t", chunk, 16, too_big, 0));
    TEST_ASSERT_FALSE(a.ready());
    TEST_ASSERT_TRUE(a.drops() >= 1u);

    char t[8];
    char p[8];
    TEST_ASSERT_FALSE(a.take(t, sizeof(t), p, sizeof(p)));

    TEST_ASSERT_FALSE(a.feed("t", chunk, 16, too_big, 16));
    TEST_ASSERT_FALSE(a.ready());
}

void test_reboot_command()
{
    TEST_ASSERT_TRUE(p1mqtt::reboot_command("{\"value\":\"true\"}"));
    TEST_ASSERT_TRUE(p1mqtt::reboot_command(" { \"value\" : \"true\" } "));
    TEST_ASSERT_FALSE(p1mqtt::reboot_command("{\"value\":\"false\"}"));
    TEST_ASSERT_FALSE(p1mqtt::reboot_command(""));
    TEST_ASSERT_FALSE(p1mqtt::reboot_command("not json"));
    TEST_ASSERT_FALSE(p1mqtt::reboot_command("{\"other\":\"true\"}"));
    TEST_ASSERT_FALSE(p1mqtt::reboot_command("{}"));
    TEST_ASSERT_FALSE(p1mqtt::reboot_command("{\"value\":true}"));
}

void test_config_snapshot_hides_password()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store s(mem);
    s.load();
    std::strcpy(s.values().mqtt_user, "alice");
    std::strcpy(s.values().mqtt_pass, "s3cret-pass");
    std::strcpy(s.values().wifi_ssid, "TestNet");
    std::strcpy(s.values().mqtt_host, "192.0.2.20");

    char buf[2048];
    TEST_ASSERT_TRUE(p1mqtt::config_snapshot(s, "P1AABBCC", "3.00", buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"MQTT_USER\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "alice"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"HOSTNAME\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "P1AABBCC"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"FW_VER\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"WIFI_SSID\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"MQTT_HOST\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"MQTT_PORT\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"MQTT_ID\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"MQTT_PFIX\""));
    TEST_ASSERT_NULL(std::strstr(buf, "MQTT_PASS"));
    TEST_ASSERT_NULL(std::strstr(buf, "WIFI_PASS"));
    TEST_ASSERT_NULL(std::strstr(buf, "s3cret-pass"));
}

void test_diag_interval_gate()
{
    TEST_ASSERT_FALSE(p1mqtt::interval_due(p1mqtt::kDiagIntervalMs, 59999, 0));
    TEST_ASSERT_TRUE(p1mqtt::interval_due(p1mqtt::kDiagIntervalMs, 60000, 0));
    TEST_ASSERT_TRUE(p1mqtt::interval_due(p1mqtt::kDiagIntervalMs, 60001, 0));
    TEST_ASSERT_FALSE(p1mqtt::interval_due(p1mqtt::kDiagIntervalMs, 119999, 60000));
    TEST_ASSERT_TRUE(p1mqtt::interval_due(p1mqtt::kDiagIntervalMs, 120000, 60000));
    TEST_ASSERT_TRUE(p1mqtt::interval_due(p1mqtt::kDiagIntervalMs, 180000, 60000));
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_device_id_from_station_mac);
    RUN_TEST(test_all_zero_mac_is_invalid);
    RUN_TEST(test_device_id_pads_low_byte);
    RUN_TEST(test_mqtt_id_migrates_empty);
    RUN_TEST(test_mqtt_id_migrates_legacy);
    RUN_TEST(test_mqtt_id_leaves_user_choice);
    RUN_TEST(test_broker_uri);
    RUN_TEST(test_throttle_gate);
    RUN_TEST(test_inbound_topics_match_dispatcher);
    RUN_TEST(test_inbound_reassembly);
    RUN_TEST(test_inbound_oversized_dropped);
    RUN_TEST(test_reboot_command);
    RUN_TEST(test_config_snapshot_hides_password);
    RUN_TEST(test_diag_interval_gate);
    return UNITY_END();
}
