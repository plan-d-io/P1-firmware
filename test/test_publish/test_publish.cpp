#include <P1Config.h>
#include <P1DSMR.h>
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

std::string fixtures_dir()
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
    for (const char* dir : candidates) {
        std::ifstream probe(std::string(dir) + "/belgian-3phase-fluvius.p1",
                            std::ios::binary);
        if (probe) {
            return dir;
        }
    }
    return "fixtures";
}

std::vector<std::uint8_t> load_fixture(const char* name)
{
    const std::string path = fixtures_dir() + "/" + name;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        printf("cannot open fixture %s\n", path.c_str());
        TEST_FAIL_MESSAGE("fixture missing");
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string seal(std::string body)
{
    if (body.empty() || body.back() != '!') {
        body.push_back('!');
    }
    std::uint16_t crc = 0;
    for (unsigned char b : body) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 1u) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001) : static_cast<std::uint16_t>(crc >> 1);
        }
    }
    char hex[5];
    std::snprintf(hex, sizeof(hex), "%04X", crc);
    body += hex;
    body += '\n';
    return body;
}

bool parse_one(p1dsmr::Reader& reader, std::string_view bytes)
{
    const auto stats = reader.feed(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    return stats.ok >= 1 && reader.has_telegram();
}

struct Hit {
    std::string topic;
    std::string payload;
};

std::vector<Hit> collect(const p1dsmr::Data& data, const p1cfg::Store& cfg)
{
    p1pub::Builder b;
    std::vector<Hit> out;
    b.emit(data, cfg, [&](std::string_view t, std::string_view p) {
        out.push_back(Hit{std::string(t), std::string(p)});
    });
    return out;
}

const Hit* find_topic(const std::vector<Hit>& hits, const char* topic)
{
    for (const Hit& h : hits) {
        if (h.topic == topic) {
            return &h;
        }
    }
    return nullptr;
}

bool has_topic(const std::vector<Hit>& hits, const char* topic)
{
    return find_topic(hits, topic) != nullptr;
}

const p1dsmr::Data* first_of(p1dsmr::Reader& reader, const char* fixture)
{
    const auto bytes = load_fixture(fixture);
    for (std::uint8_t b : bytes) {
        if (reader.feed(b) == p1dsmr::Reader::Status::Ok) {
            return &reader.data();
        }
    }
    return nullptr;
}

}  // namespace

extern "C" void setUp() {}
extern "C" void tearDown() {}

void test_topic_contract()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();

    const char* ids[] = {
        "A-0:0.0.1",
        "A-0:0.0.2",
        "A-0:0.0.3",
        "1-0:1.8.1",
        "1-0:1.7.0",
        "1-0:1.4.0",
        "1-0:1.6.0",
        "1-0:32.7.0",
        "1-0:71.7.0",
        "0-1:24.2.3",
        "0-2:24.2.1",
    };
    const char* want[] = {
        "data/devices/utility_meter/total_energy_consumed",
        "data/devices/utility_meter/total_energy_injected",
        "data/devices/utility_meter/total_active_power",
        "data/devices/utility_meter/total_consumption_t1",
        "data/devices/utility_meter/active_power_consumption",
        "data/devices/utility_meter/current_average_demand",
        "data/devices/utility_meter/maximum_demand_current_month",
        "data/devices/utility_meter/voltage_phase_1",
        "data/devices/utility_meter/current_phase_3",
        "data/devices/utility_meter/natural_gas_consumption",
        "data/devices/utility_meter/water_consumption",
    };
    char buf[p1pub::kTopicMax];
    for (std::size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        const p1cfg::Field* f = p1cfg::find_field(ids[i]);
        TEST_ASSERT_NOT_NULL(f);
        TEST_ASSERT_TRUE(p1pub::reading_topic(cfg, *f, buf, sizeof(buf)) > 0);
        TEST_ASSERT_EQUAL_STRING(want[i], buf);
    }

    TEST_ASSERT_TRUE(p1pub::availability_topic(cfg, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("data/devices/utility_meter", buf);
}

void test_payload_formats()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();

    p1dsmr::Reader flu;
    TEST_ASSERT_NOT_NULL(first_of(flu, "belgian-3phase-fluvius.p1"));
    p1dsmr::Reader mbus;
    TEST_ASSERT_NOT_NULL(first_of(mbus, "belgian-mbus-gas-water-spec.p1"));

    const char* t1 = "data/devices/utility_meter/total_consumption_t1";
    const char* tariff = "data/devices/utility_meter/active_tariff_period";
    const char* total = "data/devices/utility_meter/total_energy_consumed";
    const char* gas = "data/devices/utility_meter/natural_gas_consumption";
    const char* l1 = "data/devices/utility_meter/active_power_consumption_l1";

    cfg.set_field_on("0-0:96.14.0", true);

    cfg.values().frmt_pyld = 0;
    auto f0 = collect(flu.data(), cfg);
    TEST_ASSERT_NOT_NULL(find_topic(f0, t1));
    TEST_ASSERT_EQUAL_STRING("182.077", find_topic(f0, t1)->payload.c_str());
    TEST_ASSERT_EQUAL_STRING("2", find_topic(f0, tariff)->payload.c_str());
    TEST_ASSERT_EQUAL_STRING("381.161", find_topic(f0, total)->payload.c_str());
    auto g0 = collect(mbus.data(), cfg);
    TEST_ASSERT_EQUAL_STRING("112.384", find_topic(g0, gas)->payload.c_str());

    cfg.values().frmt_pyld = 1;
    auto f1 = collect(flu.data(), cfg);
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":182.077,\"unit\":\"kWh\",\"timestamp\":1692463399}",
        find_topic(f1, t1)->payload.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"value\":2,\"timestamp\":1692463399}",
                             find_topic(f1, tariff)->payload.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":381.161,\"unit\":\"kWh\",\"timestamp\":1692463399}",
        find_topic(f1, total)->payload.c_str());
    auto g1 = collect(mbus.data(), cfg);
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":112.384,\"unit\":\"m\xC2\xB3\",\"timestamp\":1589283958}",
        find_topic(g1, gas)->payload.c_str());

    cfg.values().frmt_pyld = 2;
    auto f2 = collect(flu.data(), cfg);
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":182.077,\"unit\":\"kWh\",\"timestamp\":1692463399,"
        "\"friendly_name\":\"Total consumption T1\","
        "\"sensorId\":\"utility_meter.total_consumption_t1\"}",
        find_topic(f2, t1)->payload.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":2,\"timestamp\":1692463399,"
        "\"friendly_name\":\"Active tariff period\","
        "\"sensorId\":\"utility_meter.active_tariff_period\"}",
        find_topic(f2, tariff)->payload.c_str());

    cfg.values().frmt_pyld = 3;
    auto f3 = collect(flu.data(), cfg);
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":182.077,\"unit\":\"kWh\",\"timestamp\":1692463399,"
        "\"friendly_name\":\"Total consumption T1\","
        "\"sensorId\":\"utility_meter.total_consumption_t1\","
        "\"entity\":\"Utility meter\",\"metricKind\":\"cumulative\"}",
        find_topic(f3, t1)->payload.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":381.161,\"unit\":\"kWh\",\"timestamp\":1692463399,"
        "\"friendly_name\":\"Total energy consumed\","
        "\"sensorId\":\"utility_meter.total_energy_consumed\","
        "\"entity\":\"Utility meter\",\"metric\":\"GridElectricityImport\","
        "\"metricKind\":\"cumulative\"}",
        find_topic(f3, total)->payload.c_str());
    auto g3 = collect(mbus.data(), cfg);
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":112.384,\"unit\":\"m\xC2\xB3\",\"timestamp\":1589283958,"
        "\"friendly_name\":\"Natural gas consumption\","
        "\"sensorId\":\"utility_meter.natural_gas_consumption\","
        "\"entity\":\"Utility meter\",\"metric\":\"naturalGasImport\","
        "\"metricKind\":\"cumulative\"}",
        find_topic(g3, gas)->payload.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":0.075,\"unit\":\"kW\",\"timestamp\":1692463399,"
        "\"friendly_name\":\"Active power consumption L1\","
        "\"sensorId\":\"utility_meter.active_power_consumption_l1\","
        "\"entity\":\"Utility meter\"}",
        find_topic(f3, l1)->payload.c_str());
}

void test_precision()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    cfg.values().frmt_pyld = 0;

    p1dsmr::Reader r1;
    const std::string sum = seal(
        "/TEST\n\n1-0:1.8.1(000000.100*kWh)\n1-0:1.8.2(000000.200*kWh)\n"
        "0-0:1.0.0(230101000000W)\n!");
    TEST_ASSERT_TRUE(parse_one(r1, sum));
    auto h = collect(r1.data(), cfg);
    TEST_ASSERT_EQUAL_STRING("0.3", find_topic(h, "data/devices/utility_meter/total_energy_consumed")
                                        ->payload.c_str());
    TEST_ASSERT_EQUAL_STRING("0.1", find_topic(h, "data/devices/utility_meter/total_consumption_t1")
                                        ->payload.c_str());

    p1dsmr::Reader r2;
    const std::string whole = seal("/TEST\n\n1-0:1.8.1(000005.000*kWh)\n0-0:1.0.0(230101000000W)\n!");
    TEST_ASSERT_TRUE(parse_one(r2, whole));
    auto h2 = collect(r2.data(), cfg);
    TEST_ASSERT_EQUAL_STRING("5", find_topic(h2, "data/devices/utility_meter/total_consumption_t1")
                                      ->payload.c_str());

    p1dsmr::Reader flu;
    TEST_ASSERT_NOT_NULL(first_of(flu, "belgian-3phase-fluvius.p1"));
    auto h3 = collect(flu.data(), cfg);
    TEST_ASSERT_EQUAL_STRING("182.077",
                             find_topic(h3, "data/devices/utility_meter/total_consumption_t1")
                                 ->payload.c_str());
}

void test_computed_totals()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    cfg.values().frmt_pyld = 0;

    p1dsmr::Reader both;
    TEST_ASSERT_TRUE(parse_one(
        both, seal("/TEST\n\n1-0:1.8.1(000000.100*kWh)\n1-0:1.8.2(000000.200*kWh)\n"
                   "1-0:2.8.1(000001.000*kWh)\n1-0:2.8.2(000002.000*kWh)\n"
                   "1-0:1.7.0(00.000*kW)\n1-0:2.7.0(01.500*kW)\n"
                   "0-0:1.0.0(230101000000W)\n!")));
    auto h = collect(both.data(), cfg);
    TEST_ASSERT_EQUAL_STRING("0.3", find_topic(h, "data/devices/utility_meter/total_energy_consumed")
                                        ->payload.c_str());
    TEST_ASSERT_EQUAL_STRING("3", find_topic(h, "data/devices/utility_meter/total_energy_injected")
                                      ->payload.c_str());
    TEST_ASSERT_EQUAL_STRING("-1.5", find_topic(h, "data/devices/utility_meter/total_active_power")
                                         ->payload.c_str());

    p1dsmr::Reader half;
    TEST_ASSERT_TRUE(parse_one(
        half, seal("/TEST\n\n1-0:1.8.1(000000.100*kWh)\n0-0:1.0.0(230101000000W)\n!")));
    auto h2 = collect(half.data(), cfg);
    TEST_ASSERT_NULL(find_topic(h2, "data/devices/utility_meter/total_energy_consumed"));
    TEST_ASSERT_NOT_NULL(find_topic(h2, "data/devices/utility_meter/total_consumption_t1"));
    TEST_ASSERT_NULL(find_topic(h2, "data/devices/utility_meter/total_active_power"));
}

void test_field_off_emits_nothing()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    p1dsmr::Reader flu;
    TEST_ASSERT_NOT_NULL(first_of(flu, "belgian-3phase-fluvius.p1"));

    auto before = collect(flu.data(), cfg);
    TEST_ASSERT_NOT_NULL(find_topic(before, "data/devices/utility_meter/total_consumption_t1"));

    cfg.set_field_on("1-0:1.8.1", false);
    auto after = collect(flu.data(), cfg);
    TEST_ASSERT_NULL(find_topic(after, "data/devices/utility_meter/total_consumption_t1"));
    TEST_ASSERT_NOT_NULL(find_topic(after, "data/devices/utility_meter/total_consumption_t2"));
}

void test_cofy_metric_keys()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    cfg.values().frmt_pyld = 3;
    p1dsmr::Reader flu;
    TEST_ASSERT_NOT_NULL(first_of(flu, "belgian-3phase-fluvius.p1"));
    auto hits = collect(flu.data(), cfg);

    const Hit* empty_metric = find_topic(hits, "data/devices/utility_meter/total_consumption_t1");
    TEST_ASSERT_NOT_NULL(empty_metric);
    TEST_ASSERT_TRUE(empty_metric->payload.find("\"metricKind\":\"cumulative\"") != std::string::npos);
    TEST_ASSERT_TRUE(empty_metric->payload.find("\"metric\"") == std::string::npos);

    const Hit* none = find_topic(hits, "data/devices/utility_meter/active_power_consumption_l1");
    TEST_ASSERT_NOT_NULL(none);
    TEST_ASSERT_TRUE(none->payload.find("\"metric\"") == std::string::npos);
    TEST_ASSERT_TRUE(none->payload.find("\"metricKind\"") == std::string::npos);

    const Hit* named = find_topic(hits, "data/devices/utility_meter/total_energy_consumed");
    TEST_ASSERT_NOT_NULL(named);
    TEST_ASSERT_TRUE(named->payload.find("\"metric\":\"GridElectricityImport\"") != std::string::npos);
    TEST_ASSERT_TRUE(named->payload.find("\"metricKind\":\"cumulative\"") != std::string::npos);
}

void test_fluvius_topic_set()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    p1dsmr::Reader flu;
    TEST_ASSERT_NOT_NULL(first_of(flu, "belgian-3phase-fluvius.p1"));
    auto hits = collect(flu.data(), cfg);

    const char* want[] = {
        "data/devices/utility_meter/total_energy_consumed",
        "data/devices/utility_meter/total_energy_injected",
        "data/devices/utility_meter/total_active_power",
        "data/devices/utility_meter/total_consumption_t1",
        "data/devices/utility_meter/total_consumption_t2",
        "data/devices/utility_meter/total_injection_t1",
        "data/devices/utility_meter/total_injection_t2",
        "data/devices/utility_meter/active_tariff_period",
        "data/devices/utility_meter/active_power_consumption",
        "data/devices/utility_meter/active_power_injection",
        "data/devices/utility_meter/active_power_consumption_l1",
        "data/devices/utility_meter/active_power_consumption_l2",
        "data/devices/utility_meter/active_power_consumption_l3",
        "data/devices/utility_meter/active_power_injection_l1",
        "data/devices/utility_meter/active_power_injection_l2",
        "data/devices/utility_meter/active_power_injection_l3",
        "data/devices/utility_meter/voltage_phase_1",
        "data/devices/utility_meter/voltage_phase_2",
        "data/devices/utility_meter/voltage_phase_3",
        "data/devices/utility_meter/current_phase_1",
        "data/devices/utility_meter/current_phase_2",
        "data/devices/utility_meter/current_phase_3",
        "data/devices/utility_meter/current_average_demand",
        "data/devices/utility_meter/maximum_demand_current_month",
    };
    TEST_ASSERT_EQUAL_UINT(sizeof(want) / sizeof(want[0]), hits.size());
    for (const char* t : want) {
        TEST_ASSERT_TRUE(has_topic(hits, t));
    }
    TEST_ASSERT_FALSE(has_topic(hits, "data/devices/utility_meter/natural_gas_consumption"));
}

void test_tariff_is_unsigned_integer()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    cfg.set_field_on("0-0:96.14.0", true);

    const char* topic = "data/devices/utility_meter/active_tariff_period";

    p1dsmr::Reader flu;
    TEST_ASSERT_NOT_NULL(first_of(flu, "belgian-3phase-fluvius.p1"));

    cfg.values().frmt_pyld = 0;
    auto f0 = collect(flu.data(), cfg);
    TEST_ASSERT_NOT_NULL(find_topic(f0, topic));
    TEST_ASSERT_EQUAL_STRING("2", find_topic(f0, topic)->payload.c_str());

    cfg.values().frmt_pyld = 1;
    auto f1 = collect(flu.data(), cfg);
    TEST_ASSERT_EQUAL_STRING("{\"value\":2,\"timestamp\":1692463399}",
                             find_topic(f1, topic)->payload.c_str());

    p1dsmr::Reader junk;
    TEST_ASSERT_TRUE(parse_one(
        junk, seal("/TEST\n\n0-0:96.14.0(XXXX)\n0-0:1.0.0(230101000000W)\n!")));
    cfg.values().frmt_pyld = 0;
    auto skipped = collect(junk.data(), cfg);
    TEST_ASSERT_NULL(find_topic(skipped, topic));
}

void test_gas_water_unit_is_cubic_metre_utf8()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    cfg.values().frmt_pyld = 1;
    cfg.set_field_on("0-2:24.2.1", true);

    p1dsmr::Reader mbus;
    TEST_ASSERT_NOT_NULL(first_of(mbus, "belgian-mbus-gas-water-spec.p1"));
    auto hits = collect(mbus.data(), cfg);

    const Hit* gas = find_topic(hits, "data/devices/utility_meter/natural_gas_consumption");
    TEST_ASSERT_NOT_NULL(gas);
    const char want_gas[] = "{\"value\":112.384,\"unit\":\"m\xC2\xB3\",\"timestamp\":1589283958}";
    TEST_ASSERT_EQUAL_UINT(sizeof(want_gas) - 1, gas->payload.size());
    TEST_ASSERT_EQUAL_STRING(want_gas, gas->payload.c_str());
    const auto unit_at = gas->payload.find("\"unit\":\"");
    TEST_ASSERT_TRUE(unit_at != std::string::npos);
    TEST_ASSERT_EQUAL_UINT('m', static_cast<unsigned char>(gas->payload[unit_at + 8]));
    TEST_ASSERT_EQUAL_UINT(0xC2, static_cast<unsigned char>(gas->payload[unit_at + 9]));
    TEST_ASSERT_EQUAL_UINT(0xB3, static_cast<unsigned char>(gas->payload[unit_at + 10]));

    const Hit* water = find_topic(hits, "data/devices/utility_meter/water_consumption");
    TEST_ASSERT_NOT_NULL(water);
    const char want_water[] = "{\"value\":872.234,\"unit\":\"m\xC2\xB3\",\"timestamp\":1589283958}";
    TEST_ASSERT_EQUAL_STRING(want_water, water->payload.c_str());
}

void test_maximum_demand_uses_capture_time()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    cfg.values().frmt_pyld = 1;

    p1dsmr::Reader flu;
    TEST_ASSERT_NOT_NULL(first_of(flu, "belgian-3phase-fluvius.p1"));
    auto hits = collect(flu.data(), cfg);

    const Hit* peak =
        find_topic(hits, "data/devices/utility_meter/maximum_demand_current_month");
    TEST_ASSERT_NOT_NULL(peak);
    TEST_ASSERT_EQUAL_STRING("{\"value\":2.396,\"unit\":\"kW\",\"timestamp\":1692064800}",
                             peak->payload.c_str());

    const Hit* t1 = find_topic(hits, "data/devices/utility_meter/total_consumption_t1");
    TEST_ASSERT_NOT_NULL(t1);
    TEST_ASSERT_TRUE(t1->payload.find("\"timestamp\":1692463399") != std::string::npos);
}

void test_builder_payload_bound()
{
    TEST_ASSERT_EQUAL_UINT(3072, p1pub::kPayloadMax);
    TEST_ASSERT_TRUE(sizeof(p1pub::Builder) >= p1pub::kTopicMax + p1pub::kPayloadMax);
    TEST_ASSERT_TRUE(sizeof(p1pub::Builder) < 4096);

    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    cfg.set_field_on("0-0:96.13.0", true);
    cfg.values().frmt_pyld = 3;

    std::string body = "/TEST\n\n0-0:96.13.0(";
    body.append(2048, 'A');
    body += ")\n0-0:1.0.0(230101000000W)\n!";
    p1dsmr::Reader r;
    TEST_ASSERT_TRUE(parse_one(r, seal(body)));
    auto hits = collect(r.data(), cfg);
    const Hit* msg = find_topic(hits, "data/devices/utility_meter/text_message");
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(msg->payload.size() > 2048);
    TEST_ASSERT_TRUE(msg->payload.size() < p1pub::kPayloadMax);
    TEST_ASSERT_TRUE(msg->payload.find("{\"value\":\"") == 0);
    TEST_ASSERT_TRUE(msg->payload.find(std::string(2048, 'A')) != std::string::npos);
}

void test_dutch_topic_set()
{
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    cfg.load();
    p1dsmr::Reader nl;
    TEST_ASSERT_NOT_NULL(first_of(nl, "dutch-3phase-esmr5-simulator.p1"));
    auto hits = collect(nl.data(), cfg);

    const char* want[] = {
        "data/devices/utility_meter/total_energy_consumed",
        "data/devices/utility_meter/total_energy_injected",
        "data/devices/utility_meter/total_active_power",
        "data/devices/utility_meter/total_consumption_t1",
        "data/devices/utility_meter/total_consumption_t2",
        "data/devices/utility_meter/total_injection_t1",
        "data/devices/utility_meter/total_injection_t2",
        "data/devices/utility_meter/active_tariff_period",
        "data/devices/utility_meter/active_power_consumption",
        "data/devices/utility_meter/active_power_injection",
        "data/devices/utility_meter/active_power_consumption_l1",
        "data/devices/utility_meter/active_power_consumption_l2",
        "data/devices/utility_meter/active_power_consumption_l3",
        "data/devices/utility_meter/active_power_injection_l1",
        "data/devices/utility_meter/active_power_injection_l2",
        "data/devices/utility_meter/active_power_injection_l3",
        "data/devices/utility_meter/voltage_phase_1",
        "data/devices/utility_meter/voltage_phase_2",
        "data/devices/utility_meter/voltage_phase_3",
        "data/devices/utility_meter/current_phase_1",
        "data/devices/utility_meter/current_phase_2",
        "data/devices/utility_meter/current_phase_3",
        "data/devices/utility_meter/natural_gas_consumption",
    };
    TEST_ASSERT_EQUAL_UINT(sizeof(want) / sizeof(want[0]), hits.size());
    for (const char* t : want) {
        TEST_ASSERT_TRUE(has_topic(hits, t));
    }
    TEST_ASSERT_FALSE(has_topic(hits, "data/devices/utility_meter/current_average_demand"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_topic_contract);
    RUN_TEST(test_payload_formats);
    RUN_TEST(test_precision);
    RUN_TEST(test_computed_totals);
    RUN_TEST(test_field_off_emits_nothing);
    RUN_TEST(test_cofy_metric_keys);
    RUN_TEST(test_fluvius_topic_set);
    RUN_TEST(test_dutch_topic_set);
    RUN_TEST(test_tariff_is_unsigned_integer);
    RUN_TEST(test_gas_water_unit_is_cubic_metre_utf8);
    RUN_TEST(test_maximum_demand_uses_capture_time);
    RUN_TEST(test_builder_payload_bound);
    return UNITY_END();
}
