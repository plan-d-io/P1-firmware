#include <P1Mqtt.h>
#include <P1Syslog.h>

#include <unity.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t g_fake_unix = 0;
std::uint32_t g_fake_uptime = 0;
std::vector<std::string> g_serial;
std::vector<std::string> g_mqtt_topics;
std::vector<std::string> g_mqtt_payloads;
bool g_mqtt_ok = true;

std::uint64_t fake_unix()
{
    return g_fake_unix;
}

std::uint32_t fake_uptime()
{
    return g_fake_uptime;
}

void on_serial(std::string_view line, void* /*ctx*/)
{
    g_serial.emplace_back(line.data(), line.size());
}

bool on_publish(std::string_view topic, std::string_view payload, void* /*ctx*/)
{
    g_mqtt_topics.emplace_back(topic.data(), topic.size());
    g_mqtt_payloads.emplace_back(payload.data(), payload.size());
    return g_mqtt_ok;
}

void reset_sinks()
{
    g_serial.clear();
    g_mqtt_topics.clear();
    g_mqtt_payloads.clear();
    g_mqtt_ok = true;
}

}  // namespace

void test_line_format_each_level()
{
    char line[p1log::kLineMax];
    // 2026-09-20 14:32:05 UTC
    const std::uint64_t ts = 1789914725ull;
    TEST_ASSERT_TRUE(p1log::format_human_line(p1log::Level::Debug, "d", ts, line, sizeof(line)) > 0);
    TEST_ASSERT_EQUAL_STRING("2026-09-20T14:32:05Z DEBUG d", line);
    TEST_ASSERT_TRUE(p1log::format_human_line(p1log::Level::Info, "i", ts, line, sizeof(line)) > 0);
    TEST_ASSERT_EQUAL_STRING("2026-09-20T14:32:05Z INFO i", line);
    TEST_ASSERT_TRUE(p1log::format_human_line(p1log::Level::Warn, "w", ts, line, sizeof(line)) > 0);
    TEST_ASSERT_EQUAL_STRING("2026-09-20T14:32:05Z WARN w", line);
    TEST_ASSERT_TRUE(p1log::format_human_line(p1log::Level::Error, "e", ts, line, sizeof(line)) > 0);
    TEST_ASSERT_EQUAL_STRING("2026-09-20T14:32:05Z ERROR e", line);
}

void test_pre_clock_uptime_stamp()
{
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 8;  // before 2020 → not valid
    g_fake_uptime = 8;
    TEST_ASSERT_FALSE(p1log::time_valid());
    TEST_ASSERT_EQUAL_UINT64(8, p1log::unix_now());
    char line[p1log::kLineMax];
    TEST_ASSERT_TRUE(
        p1log::format_human_line(p1log::Level::Info, "Mounting filesystem", p1log::unix_now(),
                                 line, sizeof(line)) > 0);
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:08Z INFO Mounting filesystem", line);
}

void test_repeats_collapse()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(true);

    log.log(p1log::Level::Warn, "same");
    log.log(p1log::Level::Warn, "same");
    log.log(p1log::Level::Warn, "same");
    log.log(p1log::Level::Warn, "other");

    // serial: same, (no repeats yet), then on other: "repeated 2 times", then other
    TEST_ASSERT_TRUE(g_serial.size() >= 3);
    TEST_ASSERT_TRUE(std::strstr(g_serial[0].c_str(), "WARN same") != nullptr);
    bool saw_repeat = false;
    bool saw_other = false;
    for (const auto& s : g_serial) {
        if (s.find("Previous message repeated 2 times") != std::string::npos) {
            saw_repeat = true;
        }
        if (s.find("WARN other") != std::string::npos) {
            saw_other = true;
        }
    }
    TEST_ASSERT_TRUE(saw_repeat);
    TEST_ASSERT_TRUE(saw_other);
}

void test_differing_level_not_repeat()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(true);

    log.log(p1log::Level::Info, "same");
    log.log(p1log::Level::Warn, "same");
    TEST_ASSERT_EQUAL_UINT(2, g_serial.size());
    TEST_ASSERT_TRUE(g_serial[0].find("INFO same") != std::string::npos);
    TEST_ASSERT_TRUE(g_serial[1].find("WARN same") != std::string::npos);
}

void test_rotation_thresholds()
{
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);
    reset_sinks();

    unsigned i = 0;
    while (fs.size(p1log::kActivePath) + 80 < p1log::kFileCap) {
        char msg[48];
        std::snprintf(msg, sizeof(msg), "pad line %u xxxxxxxxxxxxxxxx", i++);
        log.log(p1log::Level::Info, msg);
        if (i > 2000) {
            break;
        }
    }
    const std::size_t under = fs.size(p1log::kActivePath);
    TEST_ASSERT_TRUE(under < p1log::kFileCap);
    TEST_ASSERT_EQUAL_UINT(0, log.rotations());
    TEST_ASSERT_EQUAL_UINT(0, fs.size(p1log::kPrevPath));

    // Cross the cap → rotate.
    log.log(p1log::Level::Info, "this line forces rotation across 32KB boundary xx");
    TEST_ASSERT_EQUAL_UINT(1, log.rotations());
    TEST_ASSERT_TRUE(fs.size(p1log::kPrevPath) > 0);
    TEST_ASSERT_TRUE(fs.size(p1log::kPrevPath) <= p1log::kFileCap);
    TEST_ASSERT_TRUE(fs.size(p1log::kActivePath) < p1log::kFileCap);
    TEST_ASSERT_TRUE(fs.size(p1log::kActivePath) > 0);

    // Previous holds what was active before the rotating write.
    TEST_ASSERT_TRUE(under == fs.size(p1log::kPrevPath) ||
                     fs.size(p1log::kPrevPath) >= under - 200);
}

void test_replay_order_and_cap()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    log.log(p1log::Level::Info, "one");
    log.log(p1log::Level::Info, "two");
    log.log(p1log::Level::Info, "three");
    TEST_ASSERT_EQUAL_UINT(0, g_mqtt_topics.size());

    log.set_mqtt_ready(true);
    const std::size_t n = log.replay_undelivered();
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT(3, g_mqtt_topics.size());
    for (const auto& t : g_mqtt_topics) {
        TEST_ASSERT_EQUAL_STRING("sys/devices/P1AABBCC/syslog", t.c_str());
    }
    TEST_ASSERT_TRUE(g_mqtt_payloads[0].find("INFO one") != std::string::npos);
    TEST_ASSERT_TRUE(g_mqtt_payloads[1].find("INFO two") != std::string::npos);
    TEST_ASSERT_TRUE(g_mqtt_payloads[2].find("INFO three") != std::string::npos);

    // Second replay sends nothing — flags are set.
    g_mqtt_topics.clear();
    g_mqtt_payloads.clear();
    TEST_ASSERT_EQUAL_UINT(0, log.replay_undelivered());
    TEST_ASSERT_EQUAL_UINT(0, g_mqtt_topics.size());
}

void test_replay_cap_holds()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    for (unsigned i = 0; i < p1log::kReplayCap + 40; ++i) {
        char msg[32];
        std::snprintf(msg, sizeof(msg), "line %u", i);
        log.log(p1log::Level::Info, msg);
    }
    log.set_mqtt_ready(true);
    const std::size_t n = log.replay_undelivered();
    TEST_ASSERT_EQUAL_UINT(p1log::kReplayCap, n);
    TEST_ASSERT_EQUAL_UINT(p1log::kReplayCap, g_mqtt_topics.size());
}

void test_replay_and_publish_share_topic_fn()
{
    char a[64];
    char b[64];
    TEST_ASSERT_TRUE(p1log::syslog_topic("P1AABBCC", a, sizeof(a)) > 0);
    TEST_ASSERT_TRUE(p1mqtt::sys_topic("P1AABBCC", "syslog", b, sizeof(b)) > 0);
    TEST_ASSERT_EQUAL_STRING(a, b);
    TEST_ASSERT_EQUAL_STRING("sys/devices/P1AABBCC/syslog", a);
}

void test_comma_zero_does_not_corrupt_flag()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    log.log(p1log::Level::Info, "value was \", 0\" in payload");
    const char* raw = fs.contents(p1log::kActivePath);
    TEST_ASSERT_NOT_NULL(std::strstr(raw, "\", 0\""));
    // Delivered flag is after the record separator, last before newline.
    const char* sep = std::strchr(raw, p1log::kFlagSep);
    TEST_ASSERT_NOT_NULL(sep);
    TEST_ASSERT_EQUAL_CHAR('0', sep[1]);

    log.set_mqtt_ready(true);
    TEST_ASSERT_EQUAL_UINT(1, log.replay_undelivered());
    const char* after = fs.contents(p1log::kActivePath);
    const char* sep2 = std::strchr(after, p1log::kFlagSep);
    TEST_ASSERT_NOT_NULL(sep2);
    TEST_ASSERT_EQUAL_CHAR('1', sep2[1]);
    // Message text still contains ", 0".
    TEST_ASSERT_NOT_NULL(std::strstr(after, "\", 0\""));
}

void test_hw_reset_strings()
{
    TEST_ASSERT_EQUAL_STRING("POWERON_RESET", p1log::hw_reset_reason_string(1));
    TEST_ASSERT_EQUAL_STRING("SW_CPU_RESET", p1log::hw_reset_reason_string(3));
    TEST_ASSERT_EQUAL_STRING("NO_MEAN", p1log::hw_reset_reason_string(0));
}

extern "C" void setUp()
{
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    g_fake_uptime = 0;
    reset_sinks();
}

extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_line_format_each_level);
    RUN_TEST(test_pre_clock_uptime_stamp);
    RUN_TEST(test_repeats_collapse);
    RUN_TEST(test_differing_level_not_repeat);
    RUN_TEST(test_rotation_thresholds);
    RUN_TEST(test_replay_order_and_cap);
    RUN_TEST(test_replay_cap_holds);
    RUN_TEST(test_replay_and_publish_share_topic_fn);
    RUN_TEST(test_comma_zero_does_not_corrupt_flag);
    RUN_TEST(test_hw_reset_strings);
    return UNITY_END();
}
