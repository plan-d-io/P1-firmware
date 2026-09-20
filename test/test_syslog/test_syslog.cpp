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
std::uint32_t g_persisted_mark = 0;
unsigned g_persist_calls = 0;

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

void on_persist(std::uint32_t mark, void* /*ctx*/)
{
    g_persisted_mark = mark;
    ++g_persist_calls;
}

void reset_sinks()
{
    g_serial.clear();
    g_mqtt_topics.clear();
    g_mqtt_payloads.clear();
    g_mqtt_ok = true;
    g_persisted_mark = 0;
    g_persist_calls = 0;
}

// Drain paced replay: one call per "loop".
std::size_t drain_replay(p1log::Logger& log, unsigned max_steps = 64)
{
    std::size_t total = 0;
    for (unsigned i = 0; i < max_steps; ++i) {
        const std::size_t n = log.replay_undelivered();
        if (n == 0 && !log.replay_pending()) {
            break;
        }
        total += n;
    }
    return total;
}

}  // namespace

void test_line_format_each_level()
{
    char line[p1log::kLineMax];
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
    g_fake_unix = 8;
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

void test_rotation_resets_watermark()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    log.set_mark_persist(&on_persist, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    unsigned i = 0;
    while (fs.size(p1log::kActivePath) + 80 < p1log::kFileCap) {
        char msg[48];
        std::snprintf(msg, sizeof(msg), "pad line %u xxxxxxxxxxxxxxxx", i++);
        log.log(p1log::Level::Info, msg);
        if (i > 2000) {
            break;
        }
    }
    // Pretend some of the file was already delivered.
    log.set_log_mark(100);
    g_persisted_mark = 100;

    log.log(p1log::Level::Info, "this line forces rotation across 32KB boundary xx");
    TEST_ASSERT_EQUAL_UINT(1, log.rotations());
    TEST_ASSERT_EQUAL_UINT32(0, log.log_mark());
    TEST_ASSERT_EQUAL_UINT32(0, g_persisted_mark);
}

void test_watermark_advances_and_persists()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    log.set_mark_persist(&on_persist, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    log.log(p1log::Level::Info, "one");
    log.log(p1log::Level::Info, "two");
    log.log(p1log::Level::Info, "three");
    TEST_ASSERT_EQUAL_UINT32(0, log.log_mark());
    TEST_ASSERT_EQUAL_UINT(0, g_mqtt_topics.size());

    log.set_mqtt_ready(true);
    TEST_ASSERT_EQUAL_UINT(1, log.replay_undelivered());
    TEST_ASSERT_TRUE(log.log_mark() > 0);
    TEST_ASSERT_EQUAL_UINT32(log.log_mark(), g_persisted_mark);
    const std::uint32_t after1 = log.log_mark();

    TEST_ASSERT_EQUAL_UINT(1, log.replay_undelivered());
    TEST_ASSERT_TRUE(log.log_mark() > after1);
    TEST_ASSERT_EQUAL_UINT(1, log.replay_undelivered());
    TEST_ASSERT_FALSE(log.replay_pending());
    TEST_ASSERT_EQUAL_UINT(3, g_mqtt_topics.size());
    TEST_ASSERT_TRUE(g_mqtt_payloads[0].find("INFO one") != std::string::npos);
    TEST_ASSERT_TRUE(g_mqtt_payloads[1].find("INFO two") != std::string::npos);
    TEST_ASSERT_TRUE(g_mqtt_payloads[2].find("INFO three") != std::string::npos);
}

void test_restart_resumes_watermark()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    log.set_mark_persist(&on_persist, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    log.log(p1log::Level::Info, "one");
    log.log(p1log::Level::Info, "two");
    log.log(p1log::Level::Info, "three");

    log.set_mqtt_ready(true);
    TEST_ASSERT_EQUAL_UINT(1, log.replay_undelivered());
    const std::uint32_t saved = g_persisted_mark;

    // Simulate restart: new logger, same file, restore watermark.
    reset_sinks();
    p1log::Logger log2(fs, &on_publish, nullptr, &on_serial, nullptr);
    log2.set_mark_persist(&on_persist, nullptr);
    log2.set_device_id("P1AABBCC");
    log2.set_log_mark(saved);
    log2.set_mqtt_ready(true);

    drain_replay(log2);
    TEST_ASSERT_EQUAL_UINT(2, g_mqtt_topics.size());
    TEST_ASSERT_TRUE(g_mqtt_payloads[0].find("INFO two") != std::string::npos);
    TEST_ASSERT_TRUE(g_mqtt_payloads[1].find("INFO three") != std::string::npos);
}

void test_cap_twenty_with_skip()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    log.set_mark_persist(&on_persist, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    for (unsigned i = 0; i < 20; ++i) {
        char msg[32];
        std::snprintf(msg, sizeof(msg), "line %u", i);
        log.log(p1log::Level::Info, msg);
    }
    log.set_mqtt_ready(true);
    const std::size_t n = drain_replay(log);
    // 16 replayed + 1 skip WARN (also published).
    TEST_ASSERT_EQUAL_UINT(17, n);
    TEST_ASSERT_EQUAL_UINT(17, g_mqtt_topics.size());
    TEST_ASSERT_FALSE(log.replay_pending());
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(fs.size(p1log::kActivePath)),
                             log.log_mark());

    bool saw_skip = false;
    for (const auto& p : g_mqtt_payloads) {
        if (p.find("4 earlier lines not replayed, see log file") != std::string::npos) {
            saw_skip = true;
        }
    }
    TEST_ASSERT_TRUE(saw_skip);
    // Skip line is also in the file.
    TEST_ASSERT_NOT_NULL(std::strstr(fs.contents(p1log::kActivePath),
                                     "4 earlier lines not replayed, see log file"));
}

void test_cap_exactly_sixteen_no_skip()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    log.set_mark_persist(&on_persist, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    for (unsigned i = 0; i < 16; ++i) {
        char msg[32];
        std::snprintf(msg, sizeof(msg), "line %u", i);
        log.log(p1log::Level::Info, msg);
    }
    log.set_mqtt_ready(true);
    const std::size_t n = drain_replay(log);
    TEST_ASSERT_EQUAL_UINT(16, n);
    TEST_ASSERT_EQUAL_UINT(16, g_mqtt_topics.size());
    for (const auto& p : g_mqtt_payloads) {
        TEST_ASSERT_TRUE(p.find("not replayed") == std::string::npos);
    }
    TEST_ASSERT_FALSE(log.replay_pending());
}

void test_crash_mid_replay_resumes()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    log.set_mark_persist(&on_persist, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    for (unsigned i = 0; i < 16; ++i) {
        char msg[32];
        std::snprintf(msg, sizeof(msg), "line %u", i);
        log.log(p1log::Level::Info, msg);
    }
    log.set_mqtt_ready(true);
    for (unsigned i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_UINT(1, log.replay_undelivered());
    }
    const std::uint32_t saved = g_persisted_mark;

    reset_sinks();
    p1log::Logger log2(fs, &on_publish, nullptr, &on_serial, nullptr);
    log2.set_mark_persist(&on_persist, nullptr);
    log2.set_device_id("P1AABBCC");
    log2.set_log_mark(saved);
    log2.set_mqtt_ready(true);
    drain_replay(log2);

    TEST_ASSERT_EQUAL_UINT(11, g_mqtt_topics.size());
    TEST_ASSERT_TRUE(g_mqtt_payloads[0].find("line 5") != std::string::npos);
    TEST_ASSERT_TRUE(g_mqtt_payloads[10].find("line 15") != std::string::npos);
}

void test_file_is_plain_text()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    g_fake_unix = 1789914725ull;
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);

    log.log(p1log::Level::Info, "value was \", 0\" mid");
    const char* raw = fs.contents(p1log::kActivePath);
    TEST_ASSERT_NOT_NULL(std::strstr(raw, "\", 0\""));
    // Records are plain "<human>\n" — no delivered-flag trailer.
    bool flag_trailer = false;
    for (const char* p = raw; *p != '\0'; ++p) {
        if (p[0] == '\x1e' && (p[1] == '0' || p[1] == '1') && p[2] == '\n') {
            flag_trailer = true;
            break;
        }
    }
    TEST_ASSERT_FALSE(flag_trailer);

    // A raw 0x1e in the message must not break replay.
    fs.clear();
    reset_sinks();
    p1log::Logger log2(fs, &on_publish, nullptr, &on_serial, nullptr);
    log2.set_device_id("P1AABBCC");
    log2.set_mqtt_ready(false);
    char msg[48];
    std::snprintf(msg, sizeof(msg), "stray%cbyte and ok", '\x1e');
    log2.log(p1log::Level::Info, msg);
    log2.set_mqtt_ready(true);
    drain_replay(log2);
    TEST_ASSERT_EQUAL_UINT(1, g_mqtt_topics.size());
    TEST_ASSERT_TRUE(g_mqtt_payloads[0].find("stray") != std::string::npos);
}

void test_replay_payload_timestamps_differ()
{
    reset_sinks();
    p1log::MemoryFileStore fs;
    p1log::Logger log(fs, &on_publish, nullptr, &on_serial, nullptr);
    p1log::set_clock(&fake_unix, &fake_uptime);
    // Event time while logging.
    g_fake_unix = 1789914725ull;  // 2026-09-20T14:32:05Z
    log.set_device_id("P1AABBCC");
    log.set_mqtt_ready(false);
    log.log(p1log::Level::Error, "Lost connection to MQTT broker");

    // Transport time at replay — later.
    g_fake_unix = 1789914725ull + 3600ull;
    log.set_mqtt_ready(true);
    TEST_ASSERT_EQUAL_UINT(1, log.replay_undelivered());
    TEST_ASSERT_EQUAL_UINT(1, g_mqtt_payloads.size());
    const std::string& p = g_mqtt_payloads[0];
    TEST_ASSERT_TRUE(p.find("2026-09-20T14:32:05Z ERROR Lost connection") != std::string::npos);
    TEST_ASSERT_TRUE(p.find("\"timestamp\":1789918325") != std::string::npos);
    // One message, not a batch.
    TEST_ASSERT_TRUE(p.find("[") == std::string::npos);
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
    RUN_TEST(test_rotation_resets_watermark);
    RUN_TEST(test_watermark_advances_and_persists);
    RUN_TEST(test_restart_resumes_watermark);
    RUN_TEST(test_cap_twenty_with_skip);
    RUN_TEST(test_cap_exactly_sixteen_no_skip);
    RUN_TEST(test_crash_mid_replay_resumes);
    RUN_TEST(test_file_is_plain_text);
    RUN_TEST(test_replay_payload_timestamps_differ);
    RUN_TEST(test_replay_and_publish_share_topic_fn);
    RUN_TEST(test_hw_reset_strings);
    return UNITY_END();
}
