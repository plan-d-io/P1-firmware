#include <P1Button.h>

#include <unity.h>

#include <cstdint>

using p1btn::Event;
using p1btn::Helper;

static void press_release(Helper& h, std::uint32_t t0, std::uint32_t held, Event* last_while_down)
{
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(false, t0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, t0)));
    const Event mid = h.update(true, t0 + held);
    if (last_while_down != nullptr) {
        *last_while_down = mid;
    }
}

void test_hold_thresholds()
{
    {
        Helper h;
        Event mid = Event::Tap;
        press_release(h, 1000, 49, &mid);
        TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(mid));
        TEST_ASSERT_EQUAL(static_cast<int>(Event::None),
                          static_cast<int>(h.update(false, 1000 + 49)));
    }
    {
        Helper h;
        press_release(h, 1000, 50, nullptr);
        TEST_ASSERT_EQUAL(static_cast<int>(Event::Tap),
                          static_cast<int>(h.update(false, 1000 + 50)));
    }
    {
        Helper h;
        press_release(h, 1000, 1999, nullptr);
        TEST_ASSERT_EQUAL(static_cast<int>(Event::Tap),
                          static_cast<int>(h.update(false, 1000 + 1999)));
    }
    {
        Helper h;
        press_release(h, 1000, 2000, nullptr);
        TEST_ASSERT_EQUAL(static_cast<int>(Event::WifiReset),
                          static_cast<int>(h.update(false, 1000 + 2000)));
    }
    {
        Helper h;
        press_release(h, 1000, 4999, nullptr);
        TEST_ASSERT_EQUAL(static_cast<int>(Event::WifiReset),
                          static_cast<int>(h.update(false, 1000 + 4999)));
    }
    {
        Helper h;
        press_release(h, 1000, 5000, nullptr);
        TEST_ASSERT_EQUAL(static_cast<int>(Event::FactoryReset),
                          static_cast<int>(h.update(false, 1000 + 5000)));
    }
    {
        Helper h;
        press_release(h, 1000, 60000, nullptr);
        TEST_ASSERT_EQUAL(static_cast<int>(Event::FactoryReset),
                          static_cast<int>(h.update(false, 1000 + 60000)));
    }
}

void test_no_event_while_down()
{
    Helper h;
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(false, 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 1000)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 10000)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 60000)));
}

void test_stuck_at_boot_ignored()
{
    Helper h;
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 10000)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(false, 10000)));

    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 11000)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::Tap),
                      static_cast<int>(h.update(false, 11000 + 50)));
}

void test_two_taps()
{
    Helper h;
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(false, 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 10)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::Tap), static_cast<int>(h.update(false, 60)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, 80)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::Tap), static_cast<int>(h.update(false, 130)));
}

void test_millis_wrap()
{
    Helper h;
    const std::uint32_t t0 = 0xfffffff0u;
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(false, t0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h.update(true, t0)));
    // 0xfffffff0 + 80 wraps; unsigned elapsed is 80 → Tap.
    TEST_ASSERT_EQUAL(static_cast<int>(Event::Tap), static_cast<int>(h.update(false, 0x40u)));

    Helper h2;
    const std::uint32_t t1 = 0xfffff000u;
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h2.update(false, t1)));
    TEST_ASSERT_EQUAL(static_cast<int>(Event::None), static_cast<int>(h2.update(true, t1)));
    // 0xfffff000 + 5000 wraps to 0x388; unsigned elapsed is 5000.
    TEST_ASSERT_EQUAL(static_cast<int>(Event::FactoryReset),
                      static_cast<int>(h2.update(false, 0x388u)));
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_hold_thresholds);
    RUN_TEST(test_no_event_while_down);
    RUN_TEST(test_stuck_at_boot_ignored);
    RUN_TEST(test_two_taps);
    RUN_TEST(test_millis_wrap);
    return UNITY_END();
}
