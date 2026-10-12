#include <P1DSMR.h>
#include <P1Eid.h>

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
            crc = (crc & 1u) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001)
                             : static_cast<std::uint16_t>(crc >> 1);
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

constexpr char kFakeAuth[] =
    "SharedAccessSignature sr=https%3a%2f%2fexample.servicebus.windows.net%2fq"
    "&sig=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA%3d&se=1700000000&skn=DeviceAccessKey";
constexpr char kFakeTwin[] = "abcd1234-5678-90ab-cdef-111122223333";

}  // namespace

void test_hello_body()
{
    char buf[p1eid::kHelloBodyMax];
    const std::size_t n =
        p1eid::hello_body(buf, sizeof(buf), "23111d41", "P123111d41", "3.00", "192.0.2.30");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"claimCode\":\"23111d41\",\"deviceId\":\"P123111d41\","
        "\"deviceName\":\"P1 dongle by Plan-D\",\"firmwareVersion\":\"3.00\","
        "\"ipAddress\":\"192.0.2.30\",\"lcoalDeviceUrl\":\"http://192.0.2.30\"}",
        buf);
}

void test_hello_body_max()
{
    char claim[p1eid::kClaimCodeMax];
    std::memset(claim, 'c', sizeof(claim) - 1);
    claim[sizeof(claim) - 1] = '\0';
    char uuid[20];
    std::memset(uuid, 'u', sizeof(uuid) - 1);
    uuid[sizeof(uuid) - 1] = '\0';
    char buf[p1eid::kHelloBodyMax];
    const std::size_t n =
        p1eid::hello_body(buf, sizeof(buf), claim, uuid, "3.00", "255.255.255.255");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "lcoalDeviceUrl"));
}

void test_unclaimed_reply()
{
    const char* json =
        "{\"claimCode\":\"23111d41\","
        "\"claimUrl\":\"https://app.energyid.eu/integrations/"
        "d91a1a41-e628-4dd7-8c84-64995b3b72fd/new?code=23111d41\","
        "\"exp\":1791841228}";
    p1eid::HelloReply r{};
    TEST_ASSERT_TRUE(p1eid::parse_hello_reply(json, r));
    TEST_ASSERT_EQUAL(static_cast<int>(p1eid::ReplyKind::Unclaimed), static_cast<int>(r.kind));
    TEST_ASSERT_EQUAL_STRING("23111d41", r.claim_code);
    TEST_ASSERT_TRUE(std::strstr(r.claim_url, "code=23111d41") != nullptr);
    TEST_ASSERT_EQUAL_INT64(1791841228, r.exp);
    TEST_ASSERT_EQUAL_STRING("", r.webhook_url);
}

void test_claimed_reply()
{
    char json[p1eid::kReplyMax];
    std::snprintf(json, sizeof(json),
                  "{\"webhookUrl\":\"https://sbns-energyid-prod.servicebus.windows.net/"
                  "sbq-pland-p1/messages\",\"headers\":{\"authorization\":\"%s\","
                  "\"x-twin-id\":\"%s\"},\"recordNumber\":\"EA-00000000\","
                  "\"recordName\":\"Plan D\",\"webhookPolicy\":{\"allowedInterval\":\"PT5M\","
                  "\"uploadInterval\":300}}",
                  kFakeAuth, kFakeTwin);
    p1eid::HelloReply r{};
    TEST_ASSERT_TRUE(p1eid::parse_hello_reply(json, r));
    TEST_ASSERT_EQUAL(static_cast<int>(p1eid::ReplyKind::Claimed), static_cast<int>(r.kind));
    TEST_ASSERT_EQUAL_STRING(
        "https://sbns-energyid-prod.servicebus.windows.net/sbq-pland-p1/messages", r.webhook_url);
    TEST_ASSERT_EQUAL_STRING(kFakeAuth, r.authorization);
    TEST_ASSERT_EQUAL_STRING(kFakeTwin, r.twin_id);
    TEST_ASSERT_EQUAL_STRING("Plan D", r.record_name);
    TEST_ASSERT_EQUAL_STRING("EA-00000000", r.record_number);
    TEST_ASSERT_EQUAL_UINT32(300, r.interval_s);
    TEST_ASSERT_EQUAL_INT64(1700000000, r.token_expiry);
}

void test_intervals()
{
    TEST_ASSERT_EQUAL_UINT32(300, p1eid::parse_iso_interval("PT5M"));
    TEST_ASSERT_EQUAL_UINT32(3600, p1eid::parse_iso_interval("PT1H"));
    TEST_ASSERT_EQUAL_UINT32(86400, p1eid::parse_iso_interval("P1D"));
    TEST_ASSERT_EQUAL_UINT32(10, p1eid::parse_iso_interval("PT10S"));
    TEST_ASSERT_EQUAL_UINT32(300, p1eid::choose_interval(false, 0, "PT5M"));
    TEST_ASSERT_EQUAL_UINT32(3600, p1eid::choose_interval(false, 0, "PT1H"));
    TEST_ASSERT_EQUAL_UINT32(86400, p1eid::choose_interval(false, 0, "P1D"));
    TEST_ASSERT_EQUAL_UINT32(3600, p1eid::choose_interval(true, 300, "PT1H"));
    TEST_ASSERT_EQUAL_UINT32(60, p1eid::choose_interval(false, 0, "PT10S"));
    TEST_ASSERT_EQUAL_UINT32(3600, p1eid::choose_interval(false, 0, ""));
}

void test_malformed_replies()
{
    p1eid::HelloReply r{};
    TEST_ASSERT_FALSE(p1eid::parse_hello_reply("{", r));
    TEST_ASSERT_EQUAL(static_cast<int>(p1eid::ReplyKind::Error), static_cast<int>(r.kind));
    TEST_ASSERT_FALSE(p1eid::parse_hello_reply("{\"claimCode\":}", r));
    TEST_ASSERT_FALSE(p1eid::parse_hello_reply("{\"claimCode\":\"abc", r));
    TEST_ASSERT_FALSE(p1eid::parse_hello_reply("[]", r));
    TEST_ASSERT_FALSE(p1eid::parse_hello_reply("{\"webhookUrl\":\"http://x\"}", r));
}

void test_batch_electricity_only()
{
    p1dsmr::Reader reader;
    TEST_ASSERT_TRUE(parse_one(
        reader, seal("/TEST\n\n1-0:1.8.1(000016.950*kWh)\n1-0:1.8.2(000000.000*kWh)\n"
                     "1-0:2.8.1(000000.000*kWh)\n1-0:2.8.2(000000.000*kWh)\n"
                     "1-0:1.7.0(20.700*kW)\n1-0:2.7.0(00.000*kW)\n"
                     "1-0:32.7.0(230.0*V)\n1-0:31.7.0(030.00*A)\n"
                     "0-0:1.0.0(241015205308S)\n!")));
    char buf[p1eid::kBatchMax];
    const std::size_t n = p1eid::upload_batch(reader.data(), buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"t1\":16.95"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"t2\":0"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"pwr\":20.70"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"volt1\":230"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"cur1\":30"));
    TEST_ASSERT_NULL(std::strstr(buf, "high-pp"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"g\""));
    TEST_ASSERT_NULL(std::strstr(buf, "\"w\""));
}

void test_batch_all_fields()
{
    p1dsmr::Reader reader;
    const auto bytes = load_fixture("belgian-full-spec-v21.p1");
    TEST_ASSERT_TRUE(reader.feed(std::span<const std::uint8_t>(bytes.data(), bytes.size())).ok >= 1);
    TEST_ASSERT_TRUE(reader.has_telegram());
    char buf[p1eid::kBatchMax];
    const std::size_t n = p1eid::upload_batch(reader.data(), buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"t1\":"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"high-pp\":"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"g\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "\"w\":"));
}

void test_batch_number_format()
{
    p1dsmr::Reader reader;
    TEST_ASSERT_TRUE(parse_one(
        reader, seal("/TEST\n\n1-0:1.8.1(000005.000*kWh)\n1-0:1.7.0(00.464*kW)\n"
                     "0-0:1.0.0(230101000000W)\n!")));
    char buf[p1eid::kBatchMax];
    TEST_ASSERT_TRUE(p1eid::upload_batch(reader.data(), buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"t1\":5"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"pwr\":0.46"));
}

void test_batch_max_fits()
{
    p1dsmr::Reader reader;
    TEST_ASSERT_TRUE(parse_one(
        reader, seal("/TEST\n\n"
                     "1-0:1.8.1(999999.999*kWh)\n1-0:1.8.2(999999.999*kWh)\n"
                     "1-0:2.8.1(999999.999*kWh)\n1-0:2.8.2(999999.999*kWh)\n"
                     "1-0:1.7.0(99.999*kW)\n1-0:2.7.0(99.999*kW)\n"
                     "1-0:1.4.0(99.999*kW)\n1-0:1.6.0(230101000000W)(99.999*kW)\n"
                     "1-0:32.7.0(999.9*V)\n1-0:31.7.0(999.99*A)\n"
                     "0-1:24.2.3(230101000000W)(999999.999*m3)\n"
                     "0-2:24.2.1(230101000000W)(999999.999*m3)\n"
                     "0-0:1.0.0(230101000000W)\n!")));
    char buf[p1eid::kBatchMax];
    const std::size_t n = p1eid::upload_batch(reader.data(), buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "high-pp"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"g\":"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"w\":"));
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_hello_body);
    RUN_TEST(test_hello_body_max);
    RUN_TEST(test_unclaimed_reply);
    RUN_TEST(test_claimed_reply);
    RUN_TEST(test_intervals);
    RUN_TEST(test_malformed_replies);
    RUN_TEST(test_batch_electricity_only);
    RUN_TEST(test_batch_all_fields);
    RUN_TEST(test_batch_number_format);
    RUN_TEST(test_batch_max_fits);
    return UNITY_END();
}
