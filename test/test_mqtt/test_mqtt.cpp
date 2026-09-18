#include <P1Config.h>
#include <P1Mqtt.h>

#include <unity.h>

#include <cstdint>
#include <cstring>

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
    return UNITY_END();
}
