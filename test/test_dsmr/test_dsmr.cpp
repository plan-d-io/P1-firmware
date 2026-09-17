#include <P1DSMR.h>

#include <unity.h>

#include <cstdint>
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

p1dsmr::FeedStats parse_all(p1dsmr::Reader& reader, const std::vector<std::uint8_t>& bytes)
{
    return reader.feed(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

int peak_count_from_raw(std::string_view raw)
{
    const auto pos = raw.find("0-0:98.1.0(");
    if (pos == std::string_view::npos) {
        return -1;
    }
    std::size_t i = pos + 11;  // strlen("0-0:98.1.0(")
    int n = 0;
    bool any = false;
    while (i < raw.size() && raw[i] >= '0' && raw[i] <= '9') {
        any = true;
        n = n * 10 + (raw[i] - '0');
        ++i;
    }
    return any ? n : -1;
}

struct FluviusFirst {
    std::int32_t energy_t1 = 0;
    std::int32_t power = 0;
    std::int32_t avg_demand = 0;
    std::int32_t voltage_l1 = 0;
    std::int32_t month_peak = 0;
    bool energy_t1_present = false;
    bool power_present = false;
    bool avg_demand_present = false;
    bool voltage_l1_present = false;
    bool month_peak_present = false;
};

FluviusFirst capture_fluvius_first(const p1dsmr::Data& d)
{
    FluviusFirst s;
    s.energy_t1_present = d.energy_delivered_tariff1_present;
    s.energy_t1 = d.energy_delivered_tariff1.int_val();
    s.power_present = d.power_delivered_present;
    s.power = d.power_delivered.int_val();
    s.avg_demand_present = d.active_energy_import_current_average_demand_present;
    s.avg_demand = d.active_energy_import_current_average_demand.int_val();
    s.voltage_l1_present = d.voltage_l1_present;
    s.voltage_l1 = d.voltage_l1.int_val();
    s.month_peak_present = d.active_energy_import_maximum_demand_running_month_present;
    s.month_peak = d.active_energy_import_maximum_demand_running_month.int_val();
    return s;
}

std::vector<FluviusFirst> parse_fluvius(std::span<const std::uint8_t> bytes)
{
    p1dsmr::Reader reader;
    std::vector<FluviusFirst> out;
    reader.feed(bytes, [&](const p1dsmr::Data& d) { out.push_back(capture_fluvius_first(d)); });
    return out;
}

}  // namespace

extern "C" void setUp() {}
extern "C" void tearDown() {}

void test_fluvius_ten_telegrams()
{
    const auto bytes = load_fixture("belgian-3phase-fluvius.p1");
    p1dsmr::Reader reader;
    const auto stats = parse_all(reader, bytes);
    TEST_ASSERT_EQUAL_UINT(10, stats.ok);
    TEST_ASSERT_EQUAL_UINT(0, stats.rejected);
}

void test_fluvius_first_values()
{
    const auto bytes = load_fixture("belgian-3phase-fluvius.p1");
    p1dsmr::Reader reader;
    FluviusFirst first;
    bool got = false;
    reader.feed(std::span<const std::uint8_t>(bytes.data(), bytes.size()),
                [&](const p1dsmr::Data& d) {
                    if (!got) {
                        first = capture_fluvius_first(d);
                        got = true;
                    }
                });

    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_TRUE(first.energy_t1_present);
    TEST_ASSERT_EQUAL_INT32(182077, first.energy_t1);
    TEST_ASSERT_TRUE(first.power_present);
    TEST_ASSERT_EQUAL_INT32(289, first.power);
    TEST_ASSERT_TRUE(first.avg_demand_present);
    TEST_ASSERT_EQUAL_INT32(260, first.avg_demand);
    TEST_ASSERT_TRUE(first.voltage_l1_present);
    TEST_ASSERT_EQUAL_INT32(242100, first.voltage_l1);
    TEST_ASSERT_TRUE(first.month_peak_present);
    TEST_ASSERT_EQUAL_INT32(2396, first.month_peak);
}

void test_mbus_gas_water()
{
    const auto bytes = load_fixture("belgian-mbus-gas-water-spec.p1");
    p1dsmr::Reader reader;
    const auto stats = parse_all(reader, bytes);
    TEST_ASSERT_EQUAL_UINT(1, stats.ok);
    TEST_ASSERT_TRUE(reader.has_telegram());

    const auto& d = reader.data();
    TEST_ASSERT_TRUE(d.gas_delivered_be_present);
    TEST_ASSERT_EQUAL_INT32(112384, d.gas_delivered_be.int_val());
    TEST_ASSERT_TRUE(d.water_delivered_present);
    TEST_ASSERT_EQUAL_INT32(872234, d.water_delivered.int_val());
    TEST_ASSERT_TRUE(d.gas_device_type_present);
    TEST_ASSERT_EQUAL_UINT16(3, d.gas_device_type);
    TEST_ASSERT_TRUE(d.water_device_type_present);
    TEST_ASSERT_EQUAL_UINT16(7, d.water_device_type);
}

void test_full_spec_unknown_codes_ignored()
{
    const auto bytes = load_fixture("belgian-full-spec-v21.p1");
    p1dsmr::Reader reader;
    const auto stats = parse_all(reader, bytes);
    TEST_ASSERT_EQUAL_UINT(1, stats.ok);
    TEST_ASSERT_EQUAL_UINT(0, stats.rejected);

    const auto& d = reader.data();
    TEST_ASSERT_TRUE(d.active_energy_import_current_average_demand_present);
    TEST_ASSERT_EQUAL_INT32(2351, d.active_energy_import_current_average_demand.int_val());
    TEST_ASSERT_TRUE(d.active_energy_import_maximum_demand_running_month_present);
    TEST_ASSERT_EQUAL_INT32(2589, d.active_energy_import_maximum_demand_running_month.int_val());
    TEST_ASSERT_TRUE(d.gas_delivered_be_present);
    TEST_ASSERT_EQUAL_INT32(112384, d.gas_delivered_be.int_val());

    // Water is on 0-2:24.2.3 in this fixture; stock water_delivered is 24.2.1.
    TEST_ASSERT_FALSE(d.water_delivered_present);
    // Dutch-only gas code must stay absent on a Belgian telegram.
    TEST_ASSERT_FALSE(d.gas_delivered_present);
}

void test_dutch_simulator()
{
    const auto bytes = load_fixture("dutch-3phase-esmr5-simulator.p1");
    p1dsmr::Reader reader;
    const auto stats = parse_all(reader, bytes);
    TEST_ASSERT_EQUAL_UINT(3, stats.ok);
    TEST_ASSERT_EQUAL_UINT(0, stats.rejected);
    TEST_ASSERT_TRUE(reader.has_telegram());

    const auto& d = reader.data();
    TEST_ASSERT_TRUE(d.gas_delivered_present);
    TEST_ASSERT_EQUAL_INT32(0, d.gas_delivered.int_val());
    TEST_ASSERT_FALSE(d.gas_delivered_be_present);
    TEST_ASSERT_FALSE(d.p1_version_be_present);
    TEST_ASSERT_FALSE(d.active_energy_import_current_average_demand_present);
    TEST_ASSERT_FALSE(d.active_energy_import_maximum_demand_running_month_present);
    TEST_ASSERT_FALSE(d.active_energy_import_maximum_demand_last_13_months_present);
    TEST_ASSERT_TRUE(d.p1_version_present);
}

void test_corrupt_crc_rejected()
{
    const auto bytes = load_fixture("corrupt-crc.p1");
    p1dsmr::Reader reader;
    const auto stats = parse_all(reader, bytes);
    TEST_ASSERT_EQUAL_UINT(0, stats.ok);
    TEST_ASSERT_TRUE(stats.rejected >= 1);
    TEST_ASSERT_FALSE(reader.has_telegram());
}

void test_fragmented_input()
{
    const auto bytes = load_fixture("belgian-3phase-fluvius.p1");
    const auto whole = parse_fluvius(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    TEST_ASSERT_EQUAL_UINT(10, whole.size());

    const std::size_t chunks[] = {1, 7, 64};
    for (const std::size_t n : chunks) {
        p1dsmr::Reader reader;
        std::vector<FluviusFirst> got;
        for (std::size_t i = 0; i < bytes.size(); i += n) {
            const std::size_t len = (i + n < bytes.size()) ? n : (bytes.size() - i);
            reader.feed(std::span<const std::uint8_t>(bytes.data() + i, len),
                        [&](const p1dsmr::Data& d) { got.push_back(capture_fluvius_first(d)); });
        }
        TEST_ASSERT_EQUAL_UINT(whole.size(), got.size());
        for (std::size_t i = 0; i < whole.size(); ++i) {
            TEST_ASSERT_EQUAL_INT32(whole[i].energy_t1, got[i].energy_t1);
            TEST_ASSERT_EQUAL_INT32(whole[i].power, got[i].power);
            TEST_ASSERT_EQUAL_INT32(whole[i].avg_demand, got[i].avg_demand);
            TEST_ASSERT_EQUAL_INT32(whole[i].voltage_l1, got[i].voltage_l1);
            TEST_ASSERT_EQUAL_INT32(whole[i].month_peak, got[i].month_peak);
        }
    }
}

void test_junk_before_telegram_resync()
{
    auto bytes = load_fixture("belgian-mbus-gas-water-spec.p1");
    const std::uint8_t junk[] = {'x', 'y', 'z', '\r', '\n', 0x00, 0xFF, '!'};
    bytes.insert(bytes.begin(), std::begin(junk), std::end(junk));

    p1dsmr::Reader reader;
    const auto stats = parse_all(reader, bytes);
    TEST_ASSERT_EQUAL_UINT(1, stats.ok);
    TEST_ASSERT_TRUE(reader.data().gas_delivered_be_present);
    TEST_ASSERT_EQUAL_INT32(112384, reader.data().gas_delivered_be.int_val());
}

void test_peak_history_sentinel_timestamp()
{
    const auto bytes = load_fixture("belgian-3phase-fluvius.p1");
    p1dsmr::Reader reader;
    bool saw = false;
    reader.feed(std::span<const std::uint8_t>(bytes.data(), bytes.size()),
                [&](const p1dsmr::Data& d) {
                    if (saw) {
                        return;
                    }
                    saw = true;
                    TEST_ASSERT_TRUE(d.active_energy_import_maximum_demand_last_13_months_present);
                    TEST_ASSERT_EQUAL_INT(3, peak_count_from_raw(reader.raw()));
                    TEST_ASSERT_NOT_EQUAL(std::string_view::npos,
                                          reader.raw().find("632525252525W"));
                });
    TEST_ASSERT_TRUE(saw);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_fluvius_ten_telegrams);
    RUN_TEST(test_fluvius_first_values);
    RUN_TEST(test_mbus_gas_water);
    RUN_TEST(test_full_spec_unknown_codes_ignored);
    RUN_TEST(test_dutch_simulator);
    RUN_TEST(test_corrupt_crc_rejected);
    RUN_TEST(test_fragmented_input);
    RUN_TEST(test_junk_before_telegram_resync);
    RUN_TEST(test_peak_history_sentinel_timestamp);
    return UNITY_END();
}
