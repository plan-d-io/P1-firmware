#include <P1Config.h>
#include <P1Mqtt.h>
#include <P1Publish.h>

#include <unity.h>

#include <cstring>

namespace {

const char kMeter[] = R"p1g({"device":{"name":"Fixture meter","identifiers":["fixture_meter"],"manufacturer":"plan-d.io","model":"P1 dongle for DSMR compatible utility meters","configuration_url":"http://192.0.2.10","sw_version":"3.04"},"origin":{"name":"P1 dongle","sw_version":"3.04","support_url":"https://github.com/plan-d-io/P1-dongle"},"availability_topic":"data/devices/fixture_meter","components":{"fixture_meter_total_energy_consumed":{"p":"sensor","uniq_id":"fixture_meter_total_energy_consumed","name":"Total energy consumed","stat_t":"data/devices/fixture_meter/total_energy_consumed","val_tpl":"{{ value_json.value }}","dev_cla":"energy","stat_cla":"total_increasing","unit_of_meas":"kWh"},"fixture_meter_total_energy_injected":{"p":"sensor","uniq_id":"fixture_meter_total_energy_injected","name":"Total energy injected","stat_t":"data/devices/fixture_meter/total_energy_injected","val_tpl":"{{ value_json.value }}","dev_cla":"energy","stat_cla":"total_increasing","unit_of_meas":"kWh"},"fixture_meter_total_active_power":{"p":"sensor","uniq_id":"fixture_meter_total_active_power","name":"Total active power","stat_t":"data/devices/fixture_meter/total_active_power","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_total_consumption_t1":{"p":"sensor","uniq_id":"fixture_meter_total_consumption_t1","name":"Total consumption T1","stat_t":"data/devices/fixture_meter/total_consumption_t1","val_tpl":"{{ value_json.value }}","dev_cla":"energy","stat_cla":"total_increasing","unit_of_meas":"kWh"},"fixture_meter_total_consumption_t2":{"p":"sensor","uniq_id":"fixture_meter_total_consumption_t2","name":"Total consumption T2","stat_t":"data/devices/fixture_meter/total_consumption_t2","val_tpl":"{{ value_json.value }}","dev_cla":"energy","stat_cla":"total_increasing","unit_of_meas":"kWh"},"fixture_meter_total_injection_t1":{"p":"sensor","uniq_id":"fixture_meter_total_injection_t1","name":"Total injection T1","stat_t":"data/devices/fixture_meter/total_injection_t1","val_tpl":"{{ value_json.value }}","dev_cla":"energy","stat_cla":"total_increasing","unit_of_meas":"kWh"},"fixture_meter_total_injection_t2":{"p":"sensor","uniq_id":"fixture_meter_total_injection_t2","name":"Total injection T2","stat_t":"data/devices/fixture_meter/total_injection_t2","val_tpl":"{{ value_json.value }}","dev_cla":"energy","stat_cla":"total_increasing","unit_of_meas":"kWh"},"fixture_meter_active_tariff_period":{"p":"sensor","uniq_id":"fixture_meter_active_tariff_period","name":"Active tariff period","stat_t":"data/devices/fixture_meter/active_tariff_period","val_tpl":"{{ value_json.value }}"},"fixture_meter_active_power_consumption":{"p":"sensor","uniq_id":"fixture_meter_active_power_consumption","name":"Active power consumption","stat_t":"data/devices/fixture_meter/active_power_consumption","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_active_power_injection":{"p":"sensor","uniq_id":"fixture_meter_active_power_injection","name":"Active power injection","stat_t":"data/devices/fixture_meter/active_power_injection","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_active_power_consumption_l1":{"p":"sensor","uniq_id":"fixture_meter_active_power_consumption_l1","name":"Active power consumption L1","stat_t":"data/devices/fixture_meter/active_power_consumption_l1","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_active_power_consumption_l2":{"p":"sensor","uniq_id":"fixture_meter_active_power_consumption_l2","name":"Active power consumption L2","stat_t":"data/devices/fixture_meter/active_power_consumption_l2","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_active_power_consumption_l3":{"p":"sensor","uniq_id":"fixture_meter_active_power_consumption_l3","name":"Active power consumption L3","stat_t":"data/devices/fixture_meter/active_power_consumption_l3","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_active_power_injection_l1":{"p":"sensor","uniq_id":"fixture_meter_active_power_injection_l1","name":"Active power injection L1","stat_t":"data/devices/fixture_meter/active_power_injection_l1","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_active_power_injection_l2":{"p":"sensor","uniq_id":"fixture_meter_active_power_injection_l2","name":"Active power injection L2","stat_t":"data/devices/fixture_meter/active_power_injection_l2","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_active_power_injection_l3":{"p":"sensor","uniq_id":"fixture_meter_active_power_injection_l3","name":"Active power injection L3","stat_t":"data/devices/fixture_meter/active_power_injection_l3","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_voltage_phase_1":{"p":"sensor","uniq_id":"fixture_meter_voltage_phase_1","name":"Voltage phase 1","stat_t":"data/devices/fixture_meter/voltage_phase_1","val_tpl":"{{ value_json.value }}","dev_cla":"voltage","stat_cla":"measurement","unit_of_meas":"V"},"fixture_meter_voltage_phase_2":{"p":"sensor","uniq_id":"fixture_meter_voltage_phase_2","name":"Voltage phase 2","stat_t":"data/devices/fixture_meter/voltage_phase_2","val_tpl":"{{ value_json.value }}","dev_cla":"voltage","stat_cla":"measurement","unit_of_meas":"V"},"fixture_meter_voltage_phase_3":{"p":"sensor","uniq_id":"fixture_meter_voltage_phase_3","name":"Voltage phase 3","stat_t":"data/devices/fixture_meter/voltage_phase_3","val_tpl":"{{ value_json.value }}","dev_cla":"voltage","stat_cla":"measurement","unit_of_meas":"V"},"fixture_meter_current_phase_1":{"p":"sensor","uniq_id":"fixture_meter_current_phase_1","name":"Current phase 1","stat_t":"data/devices/fixture_meter/current_phase_1","val_tpl":"{{ value_json.value }}","dev_cla":"current","stat_cla":"measurement","unit_of_meas":"A"},"fixture_meter_current_phase_2":{"p":"sensor","uniq_id":"fixture_meter_current_phase_2","name":"Current phase 2","stat_t":"data/devices/fixture_meter/current_phase_2","val_tpl":"{{ value_json.value }}","dev_cla":"current","stat_cla":"measurement","unit_of_meas":"A"},"fixture_meter_current_phase_3":{"p":"sensor","uniq_id":"fixture_meter_current_phase_3","name":"Current phase 3","stat_t":"data/devices/fixture_meter/current_phase_3","val_tpl":"{{ value_json.value }}","dev_cla":"current","stat_cla":"measurement","unit_of_meas":"A"},"fixture_meter_current_average_demand":{"p":"sensor","uniq_id":"fixture_meter_current_average_demand","name":"Current average demand","stat_t":"data/devices/fixture_meter/current_average_demand","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_maximum_demand_current_month":{"p":"sensor","uniq_id":"fixture_meter_maximum_demand_current_month","name":"Maximum demand current month","stat_t":"data/devices/fixture_meter/maximum_demand_current_month","val_tpl":"{{ value_json.value }}","dev_cla":"power","stat_cla":"measurement","unit_of_meas":"kW"},"fixture_meter_natural_gas_consumption":{"p":"sensor","uniq_id":"fixture_meter_natural_gas_consumption","name":"Natural gas consumption","stat_t":"data/devices/fixture_meter/natural_gas_consumption","val_tpl":"{{ value_json.value }}","dev_cla":"gas","stat_cla":"total_increasing","unit_of_meas":"m³"}}})p1g";

const char kDebug[] = R"p1g({"device":{"name":"P1AABBCC","identifiers":["P1AABBCC"],"manufacturer":"plan-d.io","model":"P1 dongle debug monitoring","configuration_url":"http://192.0.2.10","sw_version":"3.04"},"origin":{"name":"P1 dongle","sw_version":"3.04","support_url":"https://github.com/plan-d-io/P1-dongle"},"availability_topic":"data/devices/fixture_meter","components":{"P1AABBCC_free_heap_size":{"p":"sensor","uniq_id":"P1AABBCC_free_heap_size","name":"Free heap size","stat_t":"sys/devices/P1AABBCC/free_heap_size","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic","unit_of_meas":"kB"},"P1AABBCC_max_allocatable_block":{"p":"sensor","uniq_id":"P1AABBCC_max_allocatable_block","name":"Allocatable block size","stat_t":"sys/devices/P1AABBCC/max_allocatable_block","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic","unit_of_meas":"kB"},"P1AABBCC_min_free_heap":{"p":"sensor","uniq_id":"P1AABBCC_min_free_heap","name":"Lowest free heap size","stat_t":"sys/devices/P1AABBCC/min_free_heap","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic","unit_of_meas":"kB"},"P1AABBCC_ip":{"p":"sensor","uniq_id":"P1AABBCC_ip","name":"IP","stat_t":"sys/devices/P1AABBCC/ip","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic"},"P1AABBCC_firmware":{"p":"sensor","uniq_id":"P1AABBCC_firmware","name":"Firmware","stat_t":"sys/devices/P1AABBCC/firmware","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic"},"P1AABBCC_rssi":{"p":"sensor","uniq_id":"P1AABBCC_rssi","name":"RSSI","stat_t":"sys/devices/P1AABBCC/rssi","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic"},"P1AABBCC_reboots":{"p":"sensor","uniq_id":"P1AABBCC_reboots","name":"Reboots","stat_t":"sys/devices/P1AABBCC/reboots","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic"},"P1AABBCC_last_reset_reason_hw":{"p":"sensor","uniq_id":"P1AABBCC_last_reset_reason_hw","name":"Last reset reason (hardware)","stat_t":"sys/devices/P1AABBCC/last_reset_reason_hw","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic"},"P1AABBCC_last_reset_reason_fw":{"p":"sensor","uniq_id":"P1AABBCC_last_reset_reason_fw","name":"Last reset reason (firmware)","stat_t":"sys/devices/P1AABBCC/last_reset_reason_fw","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic"},"P1AABBCC_release_channel":{"p":"sensor","uniq_id":"P1AABBCC_release_channel","name":"Release channel","stat_t":"sys/devices/P1AABBCC/release_channel","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic"},"P1AABBCC_syslog":{"p":"sensor","uniq_id":"P1AABBCC_syslog","name":"System log","stat_t":"sys/devices/P1AABBCC/syslog","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic"},"P1AABBCC_reboot":{"p":"switch","uniq_id":"P1AABBCC_reboot","name":"Reboot","stat_t":"sys/devices/P1AABBCC/reboot","val_tpl":"{{ value_json.value }}","ent_cat":"diagnostic","cmd_t":"data/devices/fixture_meter/set/reboot","pl_on":"{\"value\":\"true\"}","pl_off":"{\"value\":\"false\"}","stat_on":"on","stat_off":"off"}}})p1g";

const char kConfig[] = R"p1g({"HOSTNAME":"P1AABBCC","FW_VER":"3.04","MQTT_EN":false,"MQTT_TLS":false,"MQTT_AUTH":false,"MQTT_PORT":1883,"MQTT_HOST":"","MQTT_ID":"","MQTT_USER":"","MQTT_PFIX":"data/devices/fixture_meter/","HA_EN":false,"HA_DEVICE":"Fixture meter","FRMT_PYLD":3,"UPL_THROTTLE":10,"REGION":"auto","WIFI_SSID":"","REBOOTS":0,"LAST_RESET":"","FIP_EN":false,"FIPADDR":"","FSUBN":"","FDEFGTW":"","FDNS1":"","FDNS2":"","EID_EN":false,"EIDCLAIM":"","UUID":"","REL_CHAN":"main","UPD_AUTO":true,"UPD_AUTOCHK":true,"EMAIL":"","PUSH_FULL":false,"TRG_TYPE":0,"TRG_INT":10,"RINT_SPIFFS":false})p1g";

const char kDiag[] = R"p1g({"friendly_name":"Free heap size","value":117,"entity":"P1AABBCC","sensorId":"P1AABBCC.free_heap_size","unit_of_measurement":"kB"})p1g";

}  // namespace

static void load_fixture(p1cfg::Store& cfg)
{
    cfg.load();
    TEST_ASSERT_EQUAL(p1cfg::JsonStatus::Ok, cfg.apply_json(
        R"({"HA_DEVICE":"Fixture meter","MQTT_PFIX":"data/devices/fixture_meter/"})"));
}

void test_dongle_payloads_match_recorded_literals()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    load_fixture(cfg);

    char buf[p1pub::kDiscoveryCap];
    const std::size_t meter = p1pub::meter_discovery(
        cfg, "3.04", "http://192.0.2.10", p1pub::enabled_field_mask(cfg), 0, buf, sizeof(buf),
        p1pub::kOriginName, p1pub::kMeterModel);
    TEST_ASSERT_EQUAL_UINT(sizeof(kMeter) - 1, meter);
    TEST_ASSERT_EQUAL_STRING(kMeter, buf);

    const std::size_t debug = p1pub::dongle_discovery(
        cfg, "P1AABBCC", "3.04", "http://192.0.2.10", buf, sizeof(buf), p1pub::kOriginName,
        p1pub::kDongleModel, nullptr, 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(kDebug) - 1, debug);
    TEST_ASSERT_EQUAL_STRING(kDebug, buf);

    char snap[4096];
    const std::size_t sn = p1mqtt::config_snapshot(cfg, "P1AABBCC", "3.04", snap, sizeof(snap));
    TEST_ASSERT_EQUAL_UINT(sizeof(kConfig) - 1, sn);
    TEST_ASSERT_EQUAL_STRING(kConfig, snap);

    char diag[256];
    const std::size_t dn = p1mqtt::diag_payload("Free heap size", "117", "P1AABBCC",
                                                "P1AABBCC.free_heap_size", diag, sizeof(diag), 0,
                                                "kB");
    TEST_ASSERT_EQUAL_UINT(sizeof(kDiag) - 1, dn);
    TEST_ASSERT_EQUAL_STRING(kDiag, diag);
}

void test_second_product_strings()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    load_fixture(cfg);
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(
                         cfg, "3.00", "http://192.0.2.10", p1pub::enabled_field_mask(cfg), 0, buf,
                         sizeof(buf), "P1 LoRa receiver",
                         "P1 LoRa receiver for DSMR compatible utility meters") > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"name\":\"P1 LoRa receiver\""));
    TEST_ASSERT_NOT_NULL(
        std::strstr(buf, "\"model\":\"P1 LoRa receiver for DSMR compatible utility meters\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"sw_version\":\"3.00\""));
    TEST_ASSERT_NULL(std::strstr(buf, "P1 dongle"));

    TEST_ASSERT_TRUE(p1pub::dongle_discovery(cfg, "P1AABBCC", "3.00", "http://192.0.2.10", buf,
                                            sizeof(buf), "P1 LoRa receiver",
                                            "P1 LoRa receiver debug monitoring", nullptr, 0) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"model\":\"P1 LoRa receiver debug monitoring\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"name\":\"P1 LoRa receiver\""));
    TEST_ASSERT_NULL(std::strstr(buf, "P1 dongle"));
}

void test_extra_diag_sensor_is_appended()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    load_fixture(cfg);
    const p1pub::DiagSpec extra[] = {
        {"link_rssi", "Link RSSI", "dBm", "signal_strength"},
    };
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::dongle_discovery(cfg, "P1AABBCC", "3.04", "http://192.0.2.10", buf,
                                            sizeof(buf), p1pub::kOriginName, p1pub::kDongleModel,
                                            extra, 1) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "P1AABBCC_link_rssi"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "sys/devices/P1AABBCC/link_rssi"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"name\":\"Link RSSI\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"unit_of_meas\":\"dBm\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"dev_cla\":\"signal_strength\""));
}

void test_empty_store_uses_product_identity()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.set_identity_defaults("LoRa receiver", "data/devices/lora_receiver/");
    cfg.load();
    TEST_ASSERT_EQUAL_STRING("LoRa receiver", cfg.values().ha_device);
    TEST_ASSERT_EQUAL_STRING("data/devices/lora_receiver/", cfg.values().mqtt_pfix);
    char json[p1cfg::kToJsonBufMin];
    TEST_ASSERT_TRUE(cfg.to_json(json, sizeof(json)) > 0);
    TEST_ASSERT_NULL(std::strstr(json, "utility_meter"));
    TEST_ASSERT_NULL(std::strstr(json, "Utility meter"));
}

bool reject_nope(char* value)
{
    return std::strcmp(value, "nope") != 0;
}

void test_product_secret_is_masked_until_reveal()
{
    char secret[40];
    const p1cfg::ExtraSetting extra[] = {{
        "DEMO_KEY",
        "Demo key",
        p1cfg::CfgType::Str,
        p1cfg::CFG_IN_API | p1cfg::CFG_SECRET | p1cfg::CFG_REBOOT,
        secret,
        sizeof(secret),
        "super-secret-value",
        "Extra",
        "Changing this reboots.",
        reject_nope,
    }};
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    TEST_ASSERT_TRUE(cfg.add_settings(extra, 1));
    cfg.set_reveal_key("DEMO_KEY");
    cfg.load();

    char json[p1cfg::kToJsonBufMin];
    const std::size_t n = cfg.to_json(json, sizeof(json));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(std::strstr(json, "super-secret-value"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"DEMO_KEY\":{"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"set\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"section\":\"Extra\""));

    char snap[4096];
    const std::size_t sn = p1mqtt::config_snapshot(cfg, "P1AABBCC", "3.04", snap, sizeof(snap));
    TEST_ASSERT_TRUE(sn > 0);
    TEST_ASSERT_NULL(std::strstr(snap, "super-secret-value"));
    TEST_ASSERT_NULL(std::strstr(snap, "DEMO_KEY"));

    char shown[40];
    TEST_ASSERT_TRUE(cfg.reveal("DEMO_KEY", shown, sizeof(shown)));
    TEST_ASSERT_EQUAL_STRING("super-secret-value", shown);
    TEST_ASSERT_FALSE(cfg.reveal("WIFI_PASS", shown, sizeof(shown)));
    TEST_ASSERT_EQUAL_STRING("", shown);
    TEST_ASSERT_FALSE(cfg.reveal("MQTT_PASS", shown, sizeof(shown)));

    TEST_ASSERT_EQUAL(p1cfg::JsonStatus::Invalid,
                      cfg.apply_json(R"({"DEMO_KEY":"nope"})"));
    TEST_ASSERT_EQUAL(p1cfg::JsonStatus::Ok, cfg.apply_json(R"({"DEMO_KEY":"kept-secret"})"));
    TEST_ASSERT_TRUE(cfg.consume_reboot_needed());
    TEST_ASSERT_TRUE(cfg.reveal("DEMO_KEY", shown, sizeof(shown)));
    TEST_ASSERT_EQUAL_STRING("kept-secret", shown);
    TEST_ASSERT_TRUE(cfg.to_json(json, sizeof(json)) > 0);
    TEST_ASSERT_NULL(std::strstr(json, "kept-secret"));
}

void test_support_url_argument()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    load_fixture(cfg);
    char buf[p1pub::kDiscoveryCap];
    TEST_ASSERT_TRUE(p1pub::meter_discovery(
                         cfg, "3.04", "http://192.0.2.10", p1pub::enabled_field_mask(cfg), 0, buf,
                         sizeof(buf), p1pub::kOriginName, p1pub::kMeterModel,
                         "https://github.com/plan-d-io/P1-firmware") > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "https://github.com/plan-d-io/P1-firmware"));
    TEST_ASSERT_NULL(std::strstr(buf, "https://github.com/plan-d-io/P1-dongle"));
}

void test_null_legacy_backend_is_not_read()
{
    p1cfg::MemoryBackend legacy;
    legacy.put_str("WIFI_SSID", "FromLegacy");
    legacy.put_str("MQTT_HOST", "192.0.2.20");

    p1cfg::MemoryBackend ours;
    p1cfg::Store closed(ours, nullptr);
    closed.load();
    TEST_ASSERT_FALSE(closed.legacy_import_ran());
    TEST_ASSERT_EQUAL_STRING("", closed.values().wifi_ssid);

    p1cfg::MemoryBackend ours2;
    p1cfg::Store opened(ours2, nullptr);
    opened.set_legacy(&legacy);
    opened.load();
    TEST_ASSERT_TRUE(opened.legacy_import_ran());
    TEST_ASSERT_EQUAL_STRING("FromLegacy", opened.values().wifi_ssid);
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_dongle_payloads_match_recorded_literals);
    RUN_TEST(test_second_product_strings);
    RUN_TEST(test_extra_diag_sensor_is_appended);
    RUN_TEST(test_null_legacy_backend_is_not_read);
    RUN_TEST(test_empty_store_uses_product_identity);
    RUN_TEST(test_product_secret_is_masked_until_reveal);
    RUN_TEST(test_support_url_argument);
    return UNITY_END();
}
