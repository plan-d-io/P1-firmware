#include <P1Config.h>
#include <P1LoRa.h>
#include <link.h>
#include <P1Mqtt.h>
#include <from_telegram.h>
#include <to_data.h>
#include <P1DSMR.h>

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
        std::ifstream probe(std::string(dir) + "/belgian-3phase-fluvius.p1", std::ios::binary);
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
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(in), path.c_str());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

p1lora::Reading first_reading(const char* fixture)
{
    const auto bytes = load_fixture(fixture);
    p1dsmr::Reader reader;
    p1lora::Reading reading;
    bool have = false;
    reader.feed(std::span<const std::uint8_t>(bytes.data(), bytes.size()),
                [&](const p1dsmr::Data& d) {
                    if (!have) {
                        reading = p1lora::from_telegram(d);
                        have = true;
                    }
                });
    TEST_ASSERT_TRUE(have);
    return reading;
}

int unhex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<std::uint8_t> parse_hex(const char* hex)
{
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; hex[i] != 0; i += 2) {
        out.push_back(static_cast<std::uint8_t>((unhex(hex[i]) << 4) | unhex(hex[i + 1])));
    }
    return out;
}

p1lora::Key sample_key()
{
    p1lora::Key key;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::KeyStatus::Ok),
                      static_cast<int>(p1lora::parse_key("000102030405060708090a0b0c0d0e0f", 32, key)));
    return key;
}

p1lora::Reading three_phase_reading()
{
    p1lora::Reading r;
    r.three_phase = true;
    r.flags = 0x09;
    r.t1_wh = 182077;
    r.t2_wh = 199084;
    r.inj1_wh = 23;
    r.inj2_wh = 134;
    r.power_w = 289;
    r.inject_w = 0;
    r.demand_w = 260;
    r.p_plus_w[0] = 75;
    r.p_plus_w[1] = 15;
    r.p_plus_w[2] = 198;
    r.p_minus_w[0] = r.p_minus_w[1] = r.p_minus_w[2] = 0;
    r.v_dv[0] = 2421;
    r.v_dv[1] = 2417;
    r.v_dv[2] = 2403;
    r.i_ca[0] = 53;
    r.i_ca[1] = 7;
    r.i_ca[2] = 97;
    r.peak_time = 0xAC6DB4C0u;
    r.peak_w = 2396;
    return r;
}

void expect_reading(const p1lora::Reading& r, const p1lora::Reading& e)
{
    TEST_ASSERT_EQUAL(e.three_phase, r.three_phase);
    TEST_ASSERT_EQUAL_UINT8(e.flags, r.flags);
    TEST_ASSERT_EQUAL_UINT32(e.t1_wh, r.t1_wh);
    TEST_ASSERT_EQUAL_UINT32(e.t2_wh, r.t2_wh);
    TEST_ASSERT_EQUAL_UINT32(e.inj1_wh, r.inj1_wh);
    TEST_ASSERT_EQUAL_UINT32(e.inj2_wh, r.inj2_wh);
    TEST_ASSERT_EQUAL_UINT16(e.power_w, r.power_w);
    TEST_ASSERT_EQUAL_UINT16(e.inject_w, r.inject_w);
    TEST_ASSERT_EQUAL_UINT16(e.demand_w, r.demand_w);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_UINT16(e.p_plus_w[i], r.p_plus_w[i]);
        TEST_ASSERT_EQUAL_UINT16(e.p_minus_w[i], r.p_minus_w[i]);
        TEST_ASSERT_EQUAL_UINT16(e.v_dv[i], r.v_dv[i]);
        TEST_ASSERT_EQUAL_UINT16(e.i_ca[i], r.i_ca[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(e.gas_dm3, r.gas_dm3);
    TEST_ASSERT_EQUAL_UINT32(e.peak_time, r.peak_time);
    TEST_ASSERT_EQUAL_UINT16(e.peak_w, r.peak_w);
}

}  // namespace

void test_profiles()
{
    const p1lora::Profile& a = p1lora::profile(p1lora::ProfileId::A);
    TEST_ASSERT_EQUAL_UINT32(869525000u, a.hz);
    TEST_ASSERT_EQUAL_INT(17, a.max_dbm);
    TEST_ASSERT_EQUAL_UINT32(360000u, a.duty_ms_per_hour);
    TEST_ASSERT_EQUAL_UINT8(6, a.step_count);
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_UINT8(12 - i, a.steps[i].sf);
        TEST_ASSERT_EQUAL_UINT32(125000u, a.steps[i].bw_hz);
    }

    const p1lora::Profile& b = p1lora::profile(p1lora::ProfileId::B);
    TEST_ASSERT_EQUAL_UINT32(868300000u, b.hz);
    TEST_ASSERT_EQUAL_INT(13, b.max_dbm);
    TEST_ASSERT_EQUAL_UINT32(36000u, b.duty_ms_per_hour);
    TEST_ASSERT_EQUAL_UINT8(7, b.step_count);
    TEST_ASSERT_EQUAL_UINT8(12, b.steps[0].sf);
    TEST_ASSERT_EQUAL_UINT32(125000u, b.steps[0].bw_hz);
    const std::uint8_t sf_b[] = {12, 11, 10, 9, 8, 7};
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_UINT8(sf_b[i], b.steps[i + 1].sf);
        TEST_ASSERT_EQUAL_UINT32(250000u, b.steps[i + 1].bw_hz);
    }
    TEST_ASSERT_EQUAL_UINT8(0x2B, a.sync_word);
    TEST_ASSERT_EQUAL_UINT8(0x2B, b.sync_word);
}

void test_airtime_table()
{
    const std::uint16_t lens[] = {37, 53, 69, 8, 54};
    const std::uint16_t ms[5][6] = {
        {1974, 1069, 494, 267, 144, 82},
        {2466, 1315, 616, 329, 185, 103},
        {2957, 1642, 739, 411, 226, 128},
        {991, 496, 248, 124, 72, 36},
        {2466, 1315, 616, 349, 185, 103},
    };
    for (int row = 0; row < 5; ++row) {
        for (int step = 0; step < 6; ++step) {
            const std::uint32_t us = p1lora::airtime_us(p1lora::ProfileId::A, static_cast<std::uint8_t>(step), lens[row]);
            TEST_ASSERT_EQUAL_UINT32(ms[row][step], (us + 500u) / 1000u);
        }
    }
    const std::uint32_t b12 = p1lora::airtime_us(12, 250000u, 53);
    const std::uint32_t b7 = p1lora::airtime_us(7, 250000u, 53);
    TEST_ASSERT_EQUAL_UINT32(1233u, (b12 + 500u) / 1000u);
    TEST_ASSERT_EQUAL_UINT32(51u, (b7 + 500u) / 1000u);
}

void test_reading_interval()
{
    const std::uint16_t tenths[] = {247, 131, 62, 33, 18, 10};
    for (int step = 0; step < 6; ++step) {
        const std::uint32_t us = p1lora::airtime_us(p1lora::ProfileId::A, static_cast<std::uint8_t>(step), 53);
        const std::uint16_t got = p1lora::reading_interval_tenths(us, 360000u);
        TEST_ASSERT_EQUAL_UINT16(tenths[step], got);
    }
}

void test_key()
{
    p1lora::Key key = sample_key();
    const auto aes = parse_hex("129b22b630b655e9913d8eb87cec060c7df76789b66b76b0e482207e9dcbe9d1");
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(key.aes, aes.data(), 32));
    TEST_ASSERT_EQUAL_UINT8(0x6b, key.net[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0b, key.net[1]);

    p1lora::Key upper;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::KeyStatus::Ok),
                      static_cast<int>(p1lora::parse_key("000102030405060708090A0B0C0D0E0F", 32, upper)));
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(key.aes, upper.aes, 32));
    TEST_ASSERT_EQUAL_UINT8(key.net[0], upper.net[0]);
    TEST_ASSERT_EQUAL_UINT8(key.net[1], upper.net[1]);

    p1lora::Key junk;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::KeyStatus::Rejected),
                      static_cast<int>(p1lora::parse_key("000102030405060708090a0b0c0d0e0", 31, junk)));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::KeyStatus::Rejected),
                      static_cast<int>(p1lora::parse_key("000102030405060708090a0b0c0d0e0f00", 34, junk)));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::KeyStatus::Rejected),
                      static_cast<int>(p1lora::parse_key("000102030405060708090a0b0c0d0e0g", 32, junk)));
}

void test_three_phase_frame()
{
    const p1lora::Key key = sample_key();
    const p1lora::Reading reading = three_phase_reading();
    std::uint8_t plain[80];
    const std::size_t n = p1lora::write_reading_plain(reading, plain, sizeof plain);
    const auto expect_plain = parse_hex(
        "093dc70200ac09030017000000860000002101000004014b000f00c600000000000000"
        "750971096309350007006100c0b46dac5c09");
    TEST_ASSERT_EQUAL_UINT(expect_plain.size(), n);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(plain, expect_plain.data(), n));

    std::uint8_t header[5];
    std::memcpy(header, parse_hex("6b0b420107").data(), 5);
    std::uint8_t iv[16];
    p1lora::frame_iv(key, header, iv);
    const auto expect_iv = parse_hex("e29af0cceef99a41745e357b9b3dc0e7");
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(iv, expect_iv.data(), 16));

    std::uint8_t frame[80];
    std::size_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_reading(key, reading, 7, frame, sizeof frame, len)));
    const auto expect = parse_hex(
        "6b0b420107b5355ddfabc47f48d508d3983886377837abc24666595f24fc78a68bfc5a"
        "2151c3132c0b1a87d09b97505f7328d1f7e3356d67c18e6815f755b860b04704c37c");
    TEST_ASSERT_EQUAL_UINT(expect.size(), len);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(frame, expect.data(), len));

    const p1lora::Decoded back = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok), static_cast<int>(back.status));
    TEST_ASSERT_EQUAL_UINT8(7, back.counter);
    expect_reading(back.reading, reading);

    p1lora::Key other = key;
    other.aes[31] ^= 0x01;
    const p1lora::Decoded bad = p1lora::decode(other, frame, len);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::KeyCheck), static_cast<int>(bad.status));
}

void test_single_phase_frame()
{
    const p1lora::Key key = sample_key();
    p1lora::Reading r;
    r.three_phase = false;
    r.flags = 0x01;
    r.t1_wh = 182077;
    r.t2_wh = 199084;
    r.inj1_wh = 23;
    r.inj2_wh = 134;
    r.power_w = 289;
    r.inject_w = 0;
    r.demand_w = 260;
    r.v_dv[0] = 2421;
    r.i_ca[0] = 53;

    std::uint8_t plain[40];
    const std::size_t n = p1lora::write_reading_plain(r, plain, sizeof plain);
    const auto expect_plain = parse_hex("013dc70200ac090300170000008600000021010000040175093500");
    TEST_ASSERT_EQUAL_UINT(expect_plain.size(), n);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(plain, expect_plain.data(), n));

    std::uint8_t frame[80];
    std::size_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_reading(key, r, 0, frame, sizeof frame, len)));
    const auto expect = parse_hex("6b0b4101006efa99e7fd18418d92dc8c6d4c6fac18f7fc40e68e95ee311f042fac0ec20721");
    TEST_ASSERT_EQUAL_UINT(expect.size(), len);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(frame, expect.data(), len));

    const p1lora::Decoded back = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok), static_cast<int>(back.status));
    expect_reading(back.reading, r);
}

void test_control_frames()
{
    const p1lora::Key key = sample_key();
    std::uint8_t frame[80];
    std::size_t len = 0;

    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_probe(key, 0, 32, frame, sizeof frame, len)));
    TEST_ASSERT_EQUAL_UINT(37, len);
    TEST_ASSERT_EQUAL_UINT8(0x04, frame[2]);
    for (std::size_t i = 5; i < len; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, frame[i]);
    }
    p1lora::Decoded d = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Kind::Probe), static_cast<int>(d.kind));
    TEST_ASSERT_EQUAL_UINT8(32, d.probe_len);

    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_probe(key, 1, 48, frame, sizeof frame, len)));
    TEST_ASSERT_EQUAL_UINT(53, len);
    d = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL_UINT8(48, d.probe_len);

    p1lora::Ack ack;
    ack.counter = 7;
    ack.flags = 0x01;
    ack.snr = 0x10;
    ack.command = true;
    ack.profile_bit = 1;
    ack.mode_bit = 1;
    ack.step = 5;
    ack.fallback = 3;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_ack(key, 9, ack, frame, sizeof frame, len)));
    const auto expect_ack = parse_hex("6b0b8101090703101703");
    TEST_ASSERT_EQUAL_UINT(expect_ack.size(), len);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(frame, expect_ack.data(), len));
    d = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok), static_cast<int>(d.status));
    TEST_ASSERT_EQUAL_UINT8(7, d.ack.counter);
    TEST_ASSERT_TRUE(d.ack.command);
    TEST_ASSERT_EQUAL_UINT8(1, d.ack.profile_bit);
    TEST_ASSERT_EQUAL_UINT8(1, d.ack.mode_bit);
    TEST_ASSERT_EQUAL_UINT8(5, d.ack.step);
    TEST_ASSERT_EQUAL_UINT8(3, d.ack.fallback);
    TEST_ASSERT_EQUAL_INT(0x10, d.ack.snr);

    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_offer(key, 3, 4, frame, sizeof frame, len)));
    const auto expect_offer = parse_hex("6b0b82010304");
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(frame, expect_offer.data(), len));
    d = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Kind::Offer), static_cast<int>(d.kind));
    TEST_ASSERT_EQUAL_UINT8(4, d.step);

    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_confirm(key, 3, 4, frame, sizeof frame, len)));
    frame[2] = 0x83;
    d = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Kind::Confirm), static_cast<int>(d.kind));
    TEST_ASSERT_EQUAL_UINT8(4, d.step);
}

void test_rejections()
{
    const p1lora::Key key = sample_key();
    auto frame = parse_hex("6b0b420107b5355ddfabc47f48d508d3983886377837abc24666595f24fc78a68bfc5a"
                           "2151c3132c0b1a87d09b97505f7328d1f7e3356d67c18e6815f755b860b04704c37c");
    frame[0] = 0x00;
    p1lora::Decoded d = p1lora::decode(key, frame.data(), frame.size());
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::WrongNetwork), static_cast<int>(d.status));

    frame[0] = 0x6b;
    frame[2] = 0x06;
    d = p1lora::decode(key, frame.data(), frame.size());
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::UnknownType), static_cast<int>(d.status));

    frame[2] = 0x42;
    frame[3] = 2;
    d = p1lora::decode(key, frame.data(), frame.size());
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::UnknownVersion), static_cast<int>(d.status));

    std::uint8_t ack[9] = {0x6b, 0x0b, 0x81, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00};
    d = p1lora::decode(key, ack, sizeof ack);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::WrongLength), static_cast<int>(d.status));
}

void test_converter()
{
    const p1lora::Reading mbus = first_reading("belgian-mbus-gas-water-spec.p1");
    TEST_ASSERT_TRUE(mbus.three_phase);
    TEST_ASSERT_EQUAL_UINT8(0, mbus.flags & p1lora::kFlagTariff2);
    TEST_ASSERT_EQUAL_UINT8(p1lora::kFlagGas, mbus.flags & p1lora::kFlagGas);
    TEST_ASSERT_EQUAL_UINT8(0, mbus.flags & p1lora::kFlagWater);
    TEST_ASSERT_EQUAL_UINT8(0, mbus.flags & p1lora::kFlagPeak);
    TEST_ASSERT_EQUAL_UINT32(112384u, mbus.gas_dm3);
    TEST_ASSERT_EQUAL_UINT16(p1lora::kAbsent16, mbus.demand_w);

    const p1lora::Reading dutch = first_reading("dutch-3phase-esmr5-simulator.p1");
    TEST_ASSERT_TRUE(dutch.three_phase);
    TEST_ASSERT_EQUAL_UINT16(20700, dutch.power_w);
    TEST_ASSERT_EQUAL_UINT16(3000, dutch.i_ca[0]);
    TEST_ASSERT_EQUAL_UINT16(3000, dutch.i_ca[1]);
    TEST_ASSERT_EQUAL_UINT16(3000, dutch.i_ca[2]);
    TEST_ASSERT_EQUAL_UINT16(p1lora::kAbsent16, dutch.demand_w);
    TEST_ASSERT_EQUAL_UINT8(p1lora::kFlagGas, dutch.flags & p1lora::kFlagGas);
    TEST_ASSERT_EQUAL_UINT32(0u, dutch.gas_dm3);
    TEST_ASSERT_EQUAL_UINT8(0, dutch.flags & p1lora::kFlagPeak);

    TEST_ASSERT_FALSE(first_reading("belgian-1phase-derived.p1").three_phase);
    TEST_ASSERT_TRUE(first_reading("belgian-3phase-fluvius.p1").three_phase);
}

void expect_fixed(const char* name, bool src_on, std::int32_t src, bool got_on, std::int32_t got)
{
    if (!src_on) {
        TEST_ASSERT_FALSE_MESSAGE(got_on, name);
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(got_on, name);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(src, got, name);
}

void round_trip_one(const char* fixture)
{
    const auto bytes = load_fixture(fixture);
    p1dsmr::Reader reader;
    bool have = false;
    reader.feed(std::span<const std::uint8_t>(bytes.data(), bytes.size()), [&](const p1dsmr::Data& src) {
        if (have) {
            return;
        }
        have = true;
        const p1lora::Reading reading = p1lora::from_telegram(src);
        p1dsmr::Data back;
        p1lora::to_data(reading, back, nullptr, nullptr);

        TEST_ASSERT_FALSE(back.timestamp_present);
        TEST_ASSERT_FALSE(back.identification_present);
        TEST_ASSERT_FALSE(back.equipment_id_present);
        TEST_ASSERT_FALSE(back.water_delivered_present);
        TEST_ASSERT_FALSE(back.gas_delivered_present);

        expect_fixed("t1", src.energy_delivered_tariff1_present, src.energy_delivered_tariff1.int_val(),
                     back.energy_delivered_tariff1_present, back.energy_delivered_tariff1.int_val());
        expect_fixed("t2", src.energy_delivered_tariff2_present, src.energy_delivered_tariff2.int_val(),
                     back.energy_delivered_tariff2_present, back.energy_delivered_tariff2.int_val());
        expect_fixed("inj1", src.energy_returned_tariff1_present, src.energy_returned_tariff1.int_val(),
                     back.energy_returned_tariff1_present, back.energy_returned_tariff1.int_val());
        expect_fixed("inj2", src.energy_returned_tariff2_present, src.energy_returned_tariff2.int_val(),
                     back.energy_returned_tariff2_present, back.energy_returned_tariff2.int_val());
        expect_fixed("p", src.power_delivered_present, src.power_delivered.int_val(),
                     back.power_delivered_present, back.power_delivered.int_val());
        expect_fixed("inj", src.power_returned_present, src.power_returned.int_val(),
                     back.power_returned_present, back.power_returned.int_val());
        expect_fixed("dem", src.active_energy_import_current_average_demand_present,
                     src.active_energy_import_current_average_demand.int_val(),
                     back.active_energy_import_current_average_demand_present,
                     back.active_energy_import_current_average_demand.int_val());
        expect_fixed("v1", src.voltage_l1_present, src.voltage_l1.int_val(), back.voltage_l1_present,
                     back.voltage_l1.int_val());
        expect_fixed("v2", src.voltage_l2_present, src.voltage_l2.int_val(), back.voltage_l2_present,
                     back.voltage_l2.int_val());
        expect_fixed("v3", src.voltage_l3_present, src.voltage_l3.int_val(), back.voltage_l3_present,
                     back.voltage_l3.int_val());
        expect_fixed("i1", src.current_l1_present, src.current_l1.int_val(), back.current_l1_present,
                     back.current_l1.int_val());
        expect_fixed("i2", src.current_l2_present, src.current_l2.int_val(), back.current_l2_present,
                     back.current_l2.int_val());
        expect_fixed("i3", src.current_l3_present, src.current_l3.int_val(), back.current_l3_present,
                     back.current_l3.int_val());
        // Three-phase power is carried per phase. A single-phase frame has no
        // phase power, so L1 is filled from the total and L2/L3 stay absent.
        const auto carried16 = [&](const char* name, std::uint16_t raw, int scale, bool got_on,
                                   std::int32_t got) {
            const bool on = raw != p1lora::kAbsent16;
            expect_fixed(name, on, on ? static_cast<std::int32_t>(raw) * scale : 0, got_on, got);
        };
        if (reading.three_phase) {
            carried16("p1", reading.p_plus_w[0], 1, back.power_delivered_l1_present,
                      back.power_delivered_l1.int_val());
            carried16("p2", reading.p_plus_w[1], 1, back.power_delivered_l2_present,
                      back.power_delivered_l2.int_val());
            carried16("p3", reading.p_plus_w[2], 1, back.power_delivered_l3_present,
                      back.power_delivered_l3.int_val());
            carried16("q1", reading.p_minus_w[0], 1, back.power_returned_l1_present,
                      back.power_returned_l1.int_val());
            carried16("q2", reading.p_minus_w[1], 1, back.power_returned_l2_present,
                      back.power_returned_l2.int_val());
            carried16("q3", reading.p_minus_w[2], 1, back.power_returned_l3_present,
                      back.power_returned_l3.int_val());
        } else {
            carried16("p1", reading.power_w, 1, back.power_delivered_l1_present,
                      back.power_delivered_l1.int_val());
            carried16("q1", reading.inject_w, 1, back.power_returned_l1_present,
                      back.power_returned_l1.int_val());
            TEST_ASSERT_FALSE(back.power_delivered_l2_present);
            TEST_ASSERT_FALSE(back.power_delivered_l3_present);
            TEST_ASSERT_FALSE(back.power_returned_l2_present);
            TEST_ASSERT_FALSE(back.power_returned_l3_present);
        }

        if (src.gas_delivered_be_present) {
            TEST_ASSERT_TRUE(back.gas_delivered_be_present);
            TEST_ASSERT_EQUAL_INT32(src.gas_delivered_be.int_val(), back.gas_delivered_be.int_val());
        } else {
            TEST_ASSERT_FALSE(back.gas_delivered_be_present);
        }
        if (src.active_energy_import_maximum_demand_running_month_present) {
            TEST_ASSERT_TRUE(back.active_energy_import_maximum_demand_running_month_present);
            TEST_ASSERT_EQUAL_INT32(src.active_energy_import_maximum_demand_running_month.int_val(),
                                    back.active_energy_import_maximum_demand_running_month.int_val());
            TEST_ASSERT_EQUAL_UINT32(
                static_cast<unsigned>(src.active_energy_import_maximum_demand_running_month.timestamp.size()),
                static_cast<unsigned>(back.active_energy_import_maximum_demand_running_month.timestamp.size()));
            TEST_ASSERT_EQUAL_MEMORY(
                src.active_energy_import_maximum_demand_running_month.timestamp.data(),
                back.active_energy_import_maximum_demand_running_month.timestamp.data(),
                src.active_energy_import_maximum_demand_running_month.timestamp.size());
        } else {
            TEST_ASSERT_FALSE(back.active_energy_import_maximum_demand_running_month_present);
        }
        if (src.electricity_tariff_present) {
            const int want = p1lora::tariff_number(src.electricity_tariff);
            TEST_ASSERT_TRUE(back.electricity_tariff_present);
            TEST_ASSERT_EQUAL_INT(want == 2 ? 2 : 1, p1lora::tariff_number(back.electricity_tariff));
        }
    });
    TEST_ASSERT_TRUE(have);
}

void test_reading_round_trip()
{
    round_trip_one("belgian-3phase-fluvius.p1");
    round_trip_one("belgian-mbus-gas-water-spec.p1");
    round_trip_one("belgian-1phase-derived.p1");
}

void test_reused_data_drops_average_demand()
{
    p1dsmr::Data data;
    p1lora::to_data(first_reading("belgian-3phase-fluvius.p1"), data, nullptr, nullptr);
    TEST_ASSERT_TRUE(data.active_energy_import_current_average_demand_present);
    data.equipment_id_present = true;
    data.equipment_id = "stale";

    p1lora::to_data(first_reading("dutch-3phase-esmr5-simulator.p1"), data, nullptr, nullptr);
    TEST_ASSERT_FALSE(data.active_energy_import_current_average_demand_present);
    TEST_ASSERT_FALSE(data.equipment_id_present);
}

void test_single_phase_clears_reused_data()
{
    p1dsmr::Data data;
    p1lora::to_data(three_phase_reading(), data, nullptr, nullptr);
    TEST_ASSERT_TRUE(data.power_delivered_l2_present);
    TEST_ASSERT_TRUE(data.voltage_l3_present);

    p1lora::Reading one;
    one.three_phase = false;
    one.power_w = 2300;
    one.inject_w = 0;
    one.v_dv[0] = 2300;
    one.i_ca[0] = 1000;
    p1lora::to_data(one, data, nullptr, nullptr);
    TEST_ASSERT_TRUE(data.power_delivered_l1_present);
    TEST_ASSERT_EQUAL_INT32(2300, data.power_delivered_l1.int_val());
    TEST_ASSERT_TRUE(data.power_returned_l1_present);
    TEST_ASSERT_EQUAL_INT32(0, data.power_returned_l1.int_val());
    TEST_ASSERT_FALSE(data.power_delivered_l2_present);
    TEST_ASSERT_FALSE(data.power_delivered_l3_present);
    TEST_ASSERT_FALSE(data.power_returned_l2_present);
    TEST_ASSERT_FALSE(data.power_returned_l3_present);
    TEST_ASSERT_FALSE(data.voltage_l2_present);
    TEST_ASSERT_FALSE(data.voltage_l3_present);
    TEST_ASSERT_FALSE(data.current_l2_present);
    TEST_ASSERT_FALSE(data.current_l3_present);
}

void test_duty()
{
    auto run = [](std::uint32_t budget_ms, int min_first_hour) {
        p1lora::Duty duty(budget_ms);
        const std::uint32_t air_us = 2466u * 1000u;
        const std::uint32_t end = 3u * 3600u * 1000u;
        std::vector<std::uint32_t> sends;
        std::uint32_t now = 0;
        int first_hour = 0;
        while (now < end) {
            if (duty.can_send(now, air_us)) {
                duty.record(now, air_us);
                sends.push_back(now);
                if (now < 3600000u) {
                    ++first_hour;
                }
                now += 2466u;
            } else {
                const std::uint32_t w = duty.wait_ms(now, air_us);
                TEST_ASSERT_TRUE(w > 0);
                TEST_ASSERT_TRUE(now + w > now);
                now += w;
            }
        }
        TEST_ASSERT_TRUE(first_hour >= min_first_hour);
        for (std::uint32_t t : sends) {
            std::uint32_t sum_after = 0;
            std::uint32_t sum_before = 0;
            for (std::uint32_t s : sends) {
                if (s >= t && s - t < 3600000u) {
                    sum_after += 2466u;
                }
                if (t >= s && t - s < 3600000u) {
                    sum_before += 2466u;
                }
            }
            TEST_ASSERT_TRUE(sum_after <= budget_ms);
            TEST_ASSERT_TRUE(sum_before <= budget_ms);
        }
    };
    run(360000u, 140);
    run(36000u, 13);

    p1lora::Duty duty(360000u);
    const std::uint32_t air_us = 2466u * 1000u;
    std::uint32_t now = 0;
    while (duty.can_send(now, air_us)) {
        duty.record(now, air_us);
        now += 2466u;
    }
    TEST_ASSERT_FALSE(duty.can_send(now, air_us));
    now += 3600000u;
    TEST_ASSERT_TRUE(duty.can_send(now, air_us));
}

void test_power_clamp()
{
    TEST_ASSERT_EQUAL_INT(17, p1lora::clamp_tx_power(p1lora::ProfileId::A, 20));
    TEST_ASSERT_EQUAL_INT(17, p1lora::clamp_tx_power(p1lora::ProfileId::A, 17));
    TEST_ASSERT_EQUAL_INT(2, p1lora::clamp_tx_power(p1lora::ProfileId::A, 2));
    TEST_ASSERT_EQUAL_INT(13, p1lora::clamp_tx_power(p1lora::ProfileId::B, 14));
    TEST_ASSERT_EQUAL_INT(13, p1lora::clamp_tx_power(p1lora::ProfileId::B, 13));
    TEST_ASSERT_EQUAL_INT(2, p1lora::clamp_tx_power(p1lora::ProfileId::B, 2));
}

void test_duty_image()
{
    p1lora::Duty duty(360000u);
    duty.record(0, 1000000u);
    duty.record(1000u, 2000000u);
    TEST_ASSERT_EQUAL_UINT32(3000000u, duty.charged_us(1000u));

    p1lora::DutyImage image{};
    duty.save(image, 5000u);
    TEST_ASSERT_EQUAL_UINT32(p1lora::kDutyMagic, image.magic);
    TEST_ASSERT_EQUAL_UINT32(5000u, image.now_ms);

    p1lora::Duty restored(360000u);
    TEST_ASSERT_TRUE(restored.restore(image));
    TEST_ASSERT_EQUAL_UINT32(3000000u, restored.charged_us(5000u));
    TEST_ASSERT_EQUAL_UINT32(0u, restored.charged_us(3660000u));

    p1lora::DutyImage broken = image;
    broken.checksum ^= 1u;
    p1lora::Duty rejected(360000u);
    TEST_ASSERT_FALSE(rejected.restore(broken));
    TEST_ASSERT_EQUAL_UINT32(0u, rejected.charged_us(5000u));

    broken = image;
    broken.magic = 0;
    TEST_ASSERT_FALSE(rejected.restore(broken));

    p1lora::Duty other_budget(36000u);
    TEST_ASSERT_FALSE(other_budget.restore(image));
    TEST_ASSERT_EQUAL_UINT32(1468u, sizeof(p1lora::DutyImage));
}

std::uint32_t steady_interval_ms(std::uint32_t budget_ms, std::uint32_t air_us)
{
    const std::uint64_t target = static_cast<std::uint64_t>(budget_ms) * 1000ull * 95ull / 100ull;
    const std::uint64_t raw =
        (static_cast<std::uint64_t>(air_us) * 3600000ull + target - 1ull) / target;
    return raw < 1000ull ? 1000u : static_cast<std::uint32_t>(raw);
}

void assert_under_budget(const std::deque<std::pair<std::uint32_t, std::uint32_t>>& log,
                         std::uint32_t budget_us)
{
    std::uint64_t sum = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < log.size(); ++i) {
        sum += log[i].second;
        const std::uint32_t now = log[i].first;
        const std::uint32_t start = now >= 3600000u ? now - 3600000u : 0u;
        while (begin < i && log[begin].first <= start) {
            sum -= log[begin].second;
            ++begin;
        }
        TEST_ASSERT_TRUE(sum <= budget_us);
    }
}

void test_pace_sf8()
{
    const std::uint32_t air = p1lora::airtime_us(p1lora::ProfileId::A, 4, 53);
    const std::uint32_t expect = steady_interval_ms(360000u, air);
    TEST_ASSERT_EQUAL_UINT32(1946u, expect);

    p1lora::Duty duty(360000u);
    TEST_ASSERT_EQUAL_UINT32(1946u, duty.next_send_ms(0, air));
    std::uint32_t now = 0;
    int steady = 0;
    int sends = 0;
    std::uint32_t gap_min = 0xFFFFFFFFu;
    std::uint32_t gap_max = 0;
    while (now < 2u * 3600000u && sends < 8000) {
        const std::uint32_t next = duty.next_send_ms(now, air);
        TEST_ASSERT_TRUE(next > now);
        TEST_ASSERT_TRUE(duty.can_send(next, air));
        const std::uint32_t gap = next - now;
        if (sends > 0 && next >= 3600000u) {
            if (gap < gap_min) {
                gap_min = gap;
            }
            if (gap > gap_max) {
                gap_max = gap;
            }
            ++steady;
        } else if (sends > 0) {
            TEST_ASSERT_TRUE(gap <= expect * 2u);
            TEST_ASSERT_TRUE(gap >= 1000u);
        }
        duty.record(next, air);
        now = next;
        ++sends;
    }
    TEST_ASSERT_TRUE(steady > 100);
    // 1946 ms is the 95 % line. The extra 10 s slot still inside the window
    // stretches a few gaps by one frame's worth of the over-count.
    TEST_ASSERT_EQUAL_UINT32(1946u, gap_min);
    TEST_ASSERT_EQUAL_UINT32(1951u, gap_max);
}

void test_pace_burst()
{
    const std::uint32_t air = p1lora::airtime_us(p1lora::ProfileId::A, 4, 53);
    const std::uint32_t burst = p1lora::airtime_us(p1lora::ProfileId::A, 0, 53);
    const std::uint32_t expect = steady_interval_ms(360000u, air);
    p1lora::Duty duty(360000u);
    std::deque<std::pair<std::uint32_t, std::uint32_t>> log;
    std::uint32_t now = 0;
    int burst_left = 0;
    bool burst_done = false;
    int after = 0;
    bool recovered = false;
    while (now < 3u * 3600000u && log.size() < 8000u) {
        std::uint32_t use = air;
        const std::uint32_t next = duty.next_send_ms(now, air);
        if (!burst_done && next >= 1800000u) {
            burst_left = 3;
            burst_done = true;
        }
        if (burst_left > 0) {
            use = burst;
            --burst_left;
        }
        TEST_ASSERT_TRUE(next > now);
        const std::uint32_t gap = next - now;
        TEST_ASSERT_TRUE(gap <= expect * 2u);
        TEST_ASSERT_TRUE(duty.can_send(next, use));
        duty.record(next, use);
        log.emplace_back(next, use);
        if (next > 1900000u) {
            if (gap == expect) {
                ++after;
                if (after >= 30) {
                    recovered = true;
                }
            } else {
                after = 0;
            }
        }
        now = next;
    }
    assert_under_budget(log, 360000000u);
    TEST_ASSERT_TRUE(recovered);

    p1lora::Duty tight(36000u);
    const std::uint32_t slow = p1lora::airtime_us(p1lora::ProfileId::B, 0, 53);
    const std::uint32_t slow_gap = steady_interval_ms(36000u, slow);
    std::deque<std::pair<std::uint32_t, std::uint32_t>> slow_log;
    now = 0;
    while (now < 2u * 3600000u && slow_log.size() < 200u) {
        const std::uint32_t next = tight.next_send_ms(now, slow);
        TEST_ASSERT_TRUE(tight.can_send(next, slow));
        const std::uint32_t gap = next - now;
        TEST_ASSERT_TRUE(gap <= slow_gap * 2u);
        tight.record(next, slow);
        slow_log.emplace_back(next, slow);
        now = next;
    }
    assert_under_budget(slow_log, 36000000u);
}

void test_link_image()
{
    p1lora::LinkSettings in;
    in.mode = p1lora::LinkMode::Locked;
    in.profile = p1lora::ProfileId::B;
    in.locked_step = 2;
    in.last_good_step = 1;
    in.fallback_min = 30;
    in.saved = true;
    p1lora::LinkImage image{};
    TEST_ASSERT_TRUE(p1lora::link_image_store(in, image));
    TEST_ASSERT_EQUAL_UINT32(16u, sizeof(image));
    TEST_ASSERT_EQUAL_UINT32(p1lora::kLinkMagic, image.magic);
    p1lora::LinkSettings out;
    TEST_ASSERT_TRUE(p1lora::link_image_load(image, out));
    TEST_ASSERT_TRUE(out.saved);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(p1lora::LinkMode::Locked),
                            static_cast<std::uint8_t>(out.mode));
    TEST_ASSERT_EQUAL_UINT8(2, out.locked_step);
    TEST_ASSERT_EQUAL_UINT16(30, out.fallback_min);
    image.checksum ^= 1u;
    TEST_ASSERT_FALSE(p1lora::link_image_load(image, out));
}

void test_counter_gap()
{
    p1lora::Gap g = p1lora::counter_gap(10, 11);
    TEST_ASSERT_FALSE(g.restart);
    TEST_ASSERT_EQUAL_UINT8(0, g.loss);
    g = p1lora::counter_gap(10, 14);
    TEST_ASSERT_FALSE(g.restart);
    TEST_ASSERT_EQUAL_UINT8(3, g.loss);
    g = p1lora::counter_gap(254, 1);
    TEST_ASSERT_FALSE(g.restart);
    TEST_ASSERT_EQUAL_UINT8(2, g.loss);
    g = p1lora::counter_gap(10, 0);
    TEST_ASSERT_TRUE(g.restart);
    g = p1lora::counter_gap(200, 0);
    TEST_ASSERT_FALSE(g.restart);
    TEST_ASSERT_EQUAL_UINT8(55, g.loss);
}

void test_status_frame()
{
    const p1lora::Key key = sample_key();
    p1lora::MeterStatus meter;
    meter.reason = 2;
    meter.since_s = 600;
    meter.rejected = 0;
    std::uint8_t frame[16];
    std::size_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_status(key, 3, meter, frame, sizeof frame, len)));
    const auto expect = parse_hex("6b0b05010302580200");
    TEST_ASSERT_EQUAL_UINT(expect.size(), len);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(frame, expect.data(), len));

    const p1lora::Decoded back = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok), static_cast<int>(back.status));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Kind::Status), static_cast<int>(back.kind));
    TEST_ASSERT_EQUAL_UINT8(0x05, back.type);
    TEST_ASSERT_EQUAL_UINT8(3, back.counter);
    TEST_ASSERT_EQUAL_UINT8(2, back.meter.reason);
    TEST_ASSERT_EQUAL_UINT16(600, back.meter.since_s);
    TEST_ASSERT_EQUAL_UINT8(0, back.meter.rejected);
    TEST_ASSERT_FALSE(back.meter.accepted);

    p1lora::MeterStatus flagged = meter;
    flagged.accepted = true;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_status(key, 3, flagged, frame, sizeof frame, len)));
    TEST_ASSERT_EQUAL_UINT8(0x82, frame[5]);
    const p1lora::Decoded flagged_back = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL_UINT8(2, flagged_back.meter.reason);
    TEST_ASSERT_TRUE(flagged_back.meter.accepted);

    meter.since_s = 0xFFFE;
    meter.rejected = 0xFF;
    meter.reason = 3;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_status(key, 4, meter, frame, sizeof frame, len)));
    const p1lora::Decoded sat = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL_UINT8(3, sat.meter.reason);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, sat.meter.since_s);
    TEST_ASSERT_EQUAL_UINT8(0xFF, sat.meter.rejected);

    meter.since_s = 0xFFFF;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_status(key, 5, meter, frame, sizeof frame, len)));
    const p1lora::Decoded never = p1lora::decode(key, frame, len);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, never.meter.since_s);

    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::WrongLength),
                      static_cast<int>(p1lora::decode(key, frame, 8).status));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::WrongLength),
                      static_cast<int>(p1lora::decode(key, frame, 10).status));
}

void test_status_interval()
{
    // 9-byte SF12/125 airtime is 991232 us.
    // Profile A duty is 10%: 4 * 991232 us / 0.1 = 39.649 s, which rounds up to
    // 40 s and is then held at the 60 s floor.
    // Profile B duty is 1%: 4 * 991232 us / 0.01 = 396.493 s, which rounds up to 397 s.
    for (std::uint8_t step = 0; step < 6; ++step) {
        TEST_ASSERT_EQUAL_UINT32(60u, p1lora::status_interval_s(p1lora::ProfileId::A, step));
    }
    TEST_ASSERT_EQUAL_UINT32(397u, p1lora::status_interval_s(p1lora::ProfileId::B, 0));
}

void test_stale_gate()
{
    p1lora::MeterGate boot;
    p1lora::SendPlan plan = boot.plan(0, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Status), static_cast<int>(plan.choice));
    TEST_ASSERT_EQUAL_UINT8(1, plan.meter.reason);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, plan.meter.since_s);
    boot.status_sent(0);
    plan = boot.plan(59999u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Wait), static_cast<int>(plan.choice));
    plan = boot.plan(60000u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Status), static_cast<int>(plan.choice));
    TEST_ASSERT_EQUAL_UINT8(1, plan.meter.reason);

    p1lora::MeterGate fresh;
    fresh.note_valid(1000u);
    plan = fresh.plan(10999u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Reading), static_cast<int>(plan.choice));
    plan = fresh.plan(11000u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Status), static_cast<int>(plan.choice));
    TEST_ASSERT_EQUAL_UINT8(2, plan.meter.reason);
    TEST_ASSERT_EQUAL_UINT16(10, plan.meter.since_s);
    fresh.status_sent(11000u);
    plan = fresh.plan(70999u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Wait), static_cast<int>(plan.choice));
    plan = fresh.plan(71000u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Status), static_cast<int>(plan.choice));

    p1lora::MeterGate rejected;
    rejected.note_valid(0);
    rejected.note_reject(5000u);
    plan = rejected.plan(10000u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Status), static_cast<int>(plan.choice));
    TEST_ASSERT_EQUAL_UINT8(3, plan.meter.reason);
    TEST_ASSERT_EQUAL_UINT8(1, plan.meter.rejected);
    TEST_ASSERT_EQUAL_UINT16(10, plan.meter.since_s);
    plan = rejected.plan(15000u, 60000u);
    TEST_ASSERT_EQUAL_UINT8(2, plan.meter.reason);
    TEST_ASSERT_EQUAL_UINT8(1, plan.meter.rejected);

    p1lora::MeterGate flooded;
    for (int i = 0; i < 300; ++i) {
        flooded.note_reject(1000u);
    }
    plan = flooded.plan(1000u, 60000u);
    TEST_ASSERT_EQUAL_UINT8(3, plan.meter.reason);
    TEST_ASSERT_EQUAL_UINT8(0xFF, plan.meter.rejected);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, plan.meter.since_s);

    p1lora::MeterGate old;
    old.note_valid(0);
    plan = old.plan(65535000u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Status), static_cast<int>(plan.choice));
    TEST_ASSERT_EQUAL_UINT8(2, plan.meter.reason);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, plan.meter.since_s);

    p1lora::MeterGate again;
    again.note_valid(0);
    plan = again.plan(10000u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Status), static_cast<int>(plan.choice));
    again.status_sent(10000u);
    again.note_valid(20000u);
    plan = again.plan(29999u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Reading), static_cast<int>(plan.choice));
    plan = again.plan(30000u, 60000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::SendChoice::Status), static_cast<int>(plan.choice));
    TEST_ASSERT_EQUAL_UINT8(2, plan.meter.reason);
}

void test_optionals()
{
    p1lora::Optionals opt;
    opt.observe(true, 100, false, 0, 0);
    TEST_ASSERT_TRUE(opt.gas_pending());
    opt.sent(1, true, false);
    opt.sent(2, true, false);
    opt.sent(3, true, false);
    TEST_ASSERT_TRUE(opt.gas_pending());
    opt.ack(2);
    TEST_ASSERT_FALSE(opt.gas_pending());

    p1lora::Optionals changed;
    changed.observe(true, 100, false, 0, 0);
    changed.sent(2, true, false);
    changed.observe(true, 101, false, 0, 0);
    changed.ack(2);
    TEST_ASSERT_TRUE(changed.gas_pending());

    p1lora::Optionals missed;
    missed.observe(true, 100, false, 0, 0);
    missed.sent(9, false, false);
    missed.ack(9);
    TEST_ASSERT_TRUE(missed.gas_pending());
}

void test_self_test()
{
    TEST_ASSERT_NULL(p1lora::crypto_self_test());
}

void test_pair_key_text_and_masking()
{
    const std::uint8_t rnd[16] = {0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
                                  0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19};
    char formatted[33];
    p1lora::format_pair_key(rnd, formatted, sizeof(formatted));
    TEST_ASSERT_EQUAL_UINT(32, std::strlen(formatted));
    TEST_ASSERT_EQUAL_STRING("0a0b0c0d0e0f10111213141516171819", formatted);
    for (const char c : std::string_view(formatted)) {
        const bool lower = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        TEST_ASSERT_TRUE(lower);
    }

    char mixed[] = "0A0B0C0D0E0F10111213141516171819";
    TEST_ASSERT_TRUE(p1lora::normalise_pair_key(mixed));
    TEST_ASSERT_EQUAL_STRING(formatted, mixed);
    char empty[] = "";
    TEST_ASSERT_TRUE(p1lora::normalise_pair_key(empty));
    char bad[] = "nope";
    TEST_ASSERT_FALSE(p1lora::normalise_pair_key(bad));
    char odd[] = "0a0b0c0d0e0f1011121314151617181g";
    TEST_ASSERT_FALSE(p1lora::normalise_pair_key(odd));

    char secret[33];
    const p1cfg::ExtraSetting extra[] = {{
        "LORA_KEY",
        "Pair key",
        p1cfg::CfgType::Str,
        p1cfg::CFG_IN_API | p1cfg::CFG_SECRET | p1cfg::CFG_REBOOT,
        secret,
        sizeof(secret),
        formatted,
        "LoRa",
        "The transmitter must be reflashed with the new key.",
        p1lora::normalise_pair_key,
    }};
    p1cfg::MemoryBackend mem;
    p1cfg::Store cfg(mem);
    TEST_ASSERT_TRUE(cfg.add_settings(extra, 1));
    cfg.set_reveal_key("LORA_KEY");
    cfg.load();

    char json[p1cfg::kToJsonBufMin];
    TEST_ASSERT_TRUE(cfg.to_json(json, sizeof(json)) > 0);
    TEST_ASSERT_NULL(std::strstr(json, formatted));
    TEST_ASSERT_NOT_NULL(std::strstr(json, "\"set\":true"));

    char snap[4096];
    TEST_ASSERT_TRUE(p1mqtt::config_snapshot(cfg, "P1AABBCC", "3.00", snap, sizeof(snap)) > 0);
    TEST_ASSERT_NULL(std::strstr(snap, formatted));
    TEST_ASSERT_NULL(std::strstr(snap, "LORA_KEY"));

    char shown[40];
    TEST_ASSERT_TRUE(cfg.reveal("LORA_KEY", shown, sizeof(shown)));
    TEST_ASSERT_EQUAL_STRING(formatted, shown);
    TEST_ASSERT_EQUAL(p1cfg::JsonStatus::Invalid, cfg.apply_json(R"({"LORA_KEY":"nope"})"));
    TEST_ASSERT_EQUAL(p1cfg::JsonStatus::Ok,
                      cfg.apply_json(R"({"LORA_KEY":"000102030405060708090A0B0C0D0E0F"})"));
    TEST_ASSERT_TRUE(cfg.reveal("LORA_KEY", shown, sizeof(shown)));
    TEST_ASSERT_EQUAL_STRING("000102030405060708090a0b0c0d0e0f", shown);
    TEST_ASSERT_TRUE(cfg.to_json(json, sizeof(json)) > 0);
    TEST_ASSERT_NULL(std::strstr(json, "000102030405060708090a0b0c0d0e0f"));
}

void test_link_offer_snr();
void test_link_first_boot();
void test_link_snr_steps();
void test_link_loss_step_down();
void test_link_silence_search();
void test_link_step_up();
void test_link_offer_reverts();
void test_link_no_split();
void test_link_locked();
void test_link_commands();
void test_link_resume();
void test_link_status_accept();
void test_link_duty_windows();
void test_link_loss_burst();
void test_link_confirm_quota();
void test_link_candidate_median();

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_profiles);
    RUN_TEST(test_airtime_table);
    RUN_TEST(test_reading_interval);
    RUN_TEST(test_key);
    RUN_TEST(test_three_phase_frame);
    RUN_TEST(test_single_phase_frame);
    RUN_TEST(test_control_frames);
    RUN_TEST(test_rejections);
    RUN_TEST(test_converter);
    RUN_TEST(test_reading_round_trip);
    RUN_TEST(test_reused_data_drops_average_demand);
    RUN_TEST(test_single_phase_clears_reused_data);
    RUN_TEST(test_duty);
    RUN_TEST(test_power_clamp);
    RUN_TEST(test_duty_image);
    RUN_TEST(test_pace_sf8);
    RUN_TEST(test_pace_burst);
    RUN_TEST(test_link_image);
    RUN_TEST(test_counter_gap);
    RUN_TEST(test_status_frame);
    RUN_TEST(test_status_interval);
    RUN_TEST(test_stale_gate);
    RUN_TEST(test_optionals);
    RUN_TEST(test_self_test);
    RUN_TEST(test_pair_key_text_and_masking);
    RUN_TEST(test_link_offer_snr);
    RUN_TEST(test_link_first_boot);
    RUN_TEST(test_link_snr_steps);
    RUN_TEST(test_link_loss_step_down);
    RUN_TEST(test_link_silence_search);
    RUN_TEST(test_link_step_up);
    RUN_TEST(test_link_offer_reverts);
    RUN_TEST(test_link_no_split);
    RUN_TEST(test_link_locked);
    RUN_TEST(test_link_commands);
    RUN_TEST(test_link_resume);
    RUN_TEST(test_link_status_accept);
    RUN_TEST(test_link_duty_windows);
    RUN_TEST(test_link_loss_burst);
    RUN_TEST(test_link_confirm_quota);
    RUN_TEST(test_link_candidate_median);
    return UNITY_END();
}
