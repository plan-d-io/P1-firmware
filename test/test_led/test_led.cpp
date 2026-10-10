#include <P1Led.h>

#include <unity.h>

#include <cstring>

using p1led::Appearance;
using p1led::Colour;
using p1led::Condition;
using p1led::Facts;
using p1led::Pattern;
using p1led::Player;

static void expect_pattern(Pattern p, const bool* expected, std::size_t n)
{
    TEST_ASSERT_EQUAL_UINT(n, p1led::pattern_len(p));
    for (std::size_t cycle = 0; cycle < 3; ++cycle) {
        for (std::size_t i = 0; i < n; ++i) {
            TEST_ASSERT_EQUAL(expected[i], p1led::pattern_on(p, cycle * n + i));
        }
    }
}

void test_pattern_sequences()
{
    // On/off literals from the LED tables.
    const bool steady[] = {true, true, true, true, true, true};
    const bool dbl[] = {true, false, true, false, false, false};
    const bool slow[] = {true, true, true, false, false, false};
    const bool fast[] = {true, false, true, false, true, false};
    const bool triple[] = {true, false, true, false, true, false, false, false};

    expect_pattern(Pattern::Steady, steady, 6);
    expect_pattern(Pattern::DoubleBlink, dbl, 6);
    expect_pattern(Pattern::SlowBlink, slow, 6);
    expect_pattern(Pattern::FastBlink, fast, 6);
    expect_pattern(Pattern::TripleBlink, triple, 8);
}

static Facts base_ok()
{
    Facts f{};
    f.sta_connected = true;
    f.mqtt_en = true;
    f.mqtt_connected = true;
    f.ms_since_meter = 0;
    return f;
}

void test_priority_rows()
{
    {
        Facts f = base_ok();
        f.busy = true;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::Busy), static_cast<int>(a.condition));
        TEST_ASSERT_EQUAL(static_cast<int>(Colour::Red), static_cast<int>(a.colour));
        TEST_ASSERT_EQUAL(static_cast<int>(Pattern::Steady), static_cast<int>(a.pattern));
    }
    {
        Facts f = base_ok();
        f.have_button_press = true;
        f.ms_since_button = 0;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::UploadWindow), static_cast<int>(a.condition));
        TEST_ASSERT_EQUAL(static_cast<int>(Pattern::TripleBlink), static_cast<int>(a.pattern));
        TEST_ASSERT_EQUAL(static_cast<int>(Colour::Green), static_cast<int>(a.colour));
    }
    {
        Facts f = base_ok();
        f.fs_fault = true;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::CriticalFault), static_cast<int>(a.condition));
        TEST_ASSERT_EQUAL(static_cast<int>(Pattern::FastBlink), static_cast<int>(a.pattern));
    }
    {
        Facts f = base_ok();
        f.ms_since_meter = 30000;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::NoMeter), static_cast<int>(a.condition));
        TEST_ASSERT_EQUAL(static_cast<int>(Pattern::SlowBlink), static_cast<int>(a.pattern));
    }
    {
        Facts f = base_ok();
        f.mqtt_connected = false;
        f.ms_mqtt_down = 30000;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::ConnectionProblem),
                          static_cast<int>(a.condition));
        TEST_ASSERT_EQUAL(static_cast<int>(Pattern::DoubleBlink), static_cast<int>(a.pattern));
    }
    {
        Facts f = base_ok();
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));
        TEST_ASSERT_EQUAL(static_cast<int>(Pattern::Steady), static_cast<int>(a.pattern));
        TEST_ASSERT_EQUAL(static_cast<int>(Colour::Green), static_cast<int>(a.colour));
    }
}

void test_priority_combinations()
{
    // Busy beats upload window.
    {
        Facts f = base_ok();
        f.busy = true;
        f.have_button_press = true;
        f.ms_since_button = 0;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::Busy), static_cast<int>(a.condition));
        TEST_ASSERT_EQUAL(static_cast<int>(Colour::Red), static_cast<int>(a.colour));
    }
    // Upload window beats critical fault.
    {
        Facts f = base_ok();
        f.have_button_press = true;
        f.ms_since_button = 1000;
        f.fs_fault = true;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::UploadWindow), static_cast<int>(a.condition));
    }
    // Critical fault beats no meter.
    {
        Facts f = base_ok();
        f.fs_fault = true;
        f.ms_since_meter = 60000;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::CriticalFault), static_cast<int>(a.condition));
    }
    // No meter beats connection problem.
    {
        Facts f = base_ok();
        f.ms_since_meter = 30000;
        f.mqtt_connected = false;
        f.ms_mqtt_down = 60000;
        const Appearance a = p1led::decide(f);
        TEST_ASSERT_EQUAL(static_cast<int>(Condition::NoMeter), static_cast<int>(a.condition));
    }
}

void test_mode_colour()
{
    Facts f = base_ok();
    f.ms_since_meter = 30000;
    f.ap_up = false;
    Appearance a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Colour::Green), static_cast<int>(a.colour));

    f.ap_up = true;
    f.sta_connected = false;
    a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Colour::Blue), static_cast<int>(a.colour));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::SlowBlink), static_cast<int>(a.pattern));
}

void test_fresh_dongle_steady_blue()
{
    Facts f{};
    f.ap_up = true;
    f.wifi_ssid_stored = false;
    f.sta_connected = false;
    f.ms_since_meter = 0;
    const Appearance a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));
    TEST_ASSERT_EQUAL(static_cast<int>(Colour::Blue), static_cast<int>(a.colour));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::Steady), static_cast<int>(a.pattern));
}

void test_mqtt_thresholds()
{
    Facts f = base_ok();
    f.mqtt_connected = false;

    f.ms_mqtt_down = 29000;
    Appearance a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::Steady), static_cast<int>(a.pattern));

    f.ms_mqtt_down = 30000;
    a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::ConnectionProblem), static_cast<int>(a.condition));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::DoubleBlink), static_cast<int>(a.pattern));

    f.mqtt_en = false;
    f.ms_mqtt_down = 3600000;
    a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::Steady), static_cast<int>(a.pattern));
}

void test_meter_thresholds()
{
    Facts f = base_ok();

    f.ms_since_meter = 29000;
    Appearance a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));

    f.ms_since_meter = 30000;
    a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::NoMeter), static_cast<int>(a.condition));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::SlowBlink), static_cast<int>(a.pattern));

    f.ms_since_meter = 29000;
    a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));
}

void test_upload_window_edge()
{
    Facts f = base_ok();
    f.have_button_press = true;

    f.ms_since_button = 59000;
    Appearance a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::UploadWindow), static_cast<int>(a.condition));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::TripleBlink), static_cast<int>(a.pattern));

    f.ms_since_button = 60000;
    a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::Steady), static_cast<int>(a.pattern));
}

void test_player_restarts_on_change()
{
    Player p;
    p.set(Colour::Green, Pattern::Steady);
    TEST_ASSERT_TRUE(p.tick(1000));
    TEST_ASSERT_EQUAL_UINT(0, p.step());
    TEST_ASSERT_TRUE(p.on());

    TEST_ASSERT_TRUE(p.tick(1000 + 300));
    TEST_ASSERT_EQUAL_UINT(1, p.step());

    p.set(Colour::Blue, Pattern::DoubleBlink);
    TEST_ASSERT_EQUAL_UINT(0, p.step());
    TEST_ASSERT_TRUE(p.on());
    TEST_ASSERT_EQUAL(static_cast<int>(Colour::Blue), static_cast<int>(p.colour()));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::DoubleBlink), static_cast<int>(p.pattern()));
}

void test_millis_wrap_elapsed()
{
    // Unsigned subtraction: now wrapped past a past timestamp.
    const std::uint32_t past = 0xFFFFFFF0u;
    const std::uint32_t now = 0x00000020u;
    const std::uint32_t elapsed = now - past;
    TEST_ASSERT_EQUAL_UINT(0x30u, elapsed);

    Facts f = base_ok();
    f.ms_since_meter = elapsed;
    Appearance a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));

    f.ms_since_meter = 30000;
    // Simulate wrap-safe "30 s ago" using the same unsigned form the caller uses.
    const std::uint32_t last = now - 30000u;
    f.ms_since_meter = now - last;
    a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::NoMeter), static_cast<int>(a.condition));
}

void test_ap_fallback_is_conn_problem()
{
    Facts f{};
    f.ap_up = true;
    f.wifi_ssid_stored = true;
    f.sta_connected = false;
    f.ms_since_meter = 0;
    const Appearance a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::ConnectionProblem), static_cast<int>(a.condition));
    TEST_ASSERT_EQUAL(static_cast<int>(Colour::Blue), static_cast<int>(a.colour));
    TEST_ASSERT_EQUAL(static_cast<int>(Pattern::DoubleBlink), static_cast<int>(a.pattern));
}

void test_https_pause_not_conn_problem()
{
    Facts f = base_ok();
    f.mqtt_connected = false;
    f.mqtt_https_paused = true;
    f.ms_mqtt_down = 60000;
    const Appearance a = p1led::decide(f);
    TEST_ASSERT_EQUAL(static_cast<int>(Condition::Ok), static_cast<int>(a.condition));
}

void test_shared_meter_constant()
{
    TEST_ASSERT_EQUAL_UINT(30000u, p1led::kMeterStaleMs);
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_pattern_sequences);
    RUN_TEST(test_priority_rows);
    RUN_TEST(test_priority_combinations);
    RUN_TEST(test_mode_colour);
    RUN_TEST(test_fresh_dongle_steady_blue);
    RUN_TEST(test_mqtt_thresholds);
    RUN_TEST(test_meter_thresholds);
    RUN_TEST(test_upload_window_edge);
    RUN_TEST(test_player_restarts_on_change);
    RUN_TEST(test_millis_wrap_elapsed);
    RUN_TEST(test_ap_fallback_is_conn_problem);
    RUN_TEST(test_https_pause_not_conn_problem);
    RUN_TEST(test_shared_meter_constant);
    return UNITY_END();
}
