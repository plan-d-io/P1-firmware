#include <P1Ota.h>
#include <fw_version.h>

#include <unity.h>

#include <cstring>

void test_fw_ver_matches_num()
{
    unsigned parsed = 0;
    TEST_ASSERT_TRUE(p1ota::parse_display_version(p1fw::kFwVer, parsed));
    TEST_ASSERT_EQUAL_UINT(304u, parsed);
    TEST_ASSERT_EQUAL_UINT(304u, p1fw::kFwVerNum);
    TEST_ASSERT_EQUAL_UINT(parsed, p1fw::kFwVerNum);
}

void test_parse_version_file()
{
    unsigned v = 0;
    TEST_ASSERT_TRUE(p1ota::parse_version("301", v));
    TEST_ASSERT_EQUAL_UINT(301u, v);

    TEST_ASSERT_TRUE(p1ota::parse_version("301\n", v));
    TEST_ASSERT_EQUAL_UINT(301u, v);

    TEST_ASSERT_TRUE(p1ota::parse_version(" 301 ", v));
    TEST_ASSERT_EQUAL_UINT(301u, v);

    TEST_ASSERT_TRUE(p1ota::parse_version("223", v));
    TEST_ASSERT_EQUAL_UINT(223u, v);

    TEST_ASSERT_FALSE(p1ota::parse_version("", v));
    TEST_ASSERT_FALSE(p1ota::parse_version("3.01", v));
    TEST_ASSERT_FALSE(p1ota::parse_version("abc", v));
    TEST_ASSERT_FALSE(p1ota::parse_version("-1", v));
    TEST_ASSERT_FALSE(p1ota::parse_version("0", v));
    TEST_ASSERT_FALSE(p1ota::parse_version("3.00", v));
}

void test_channel_urls()
{
    char url[p1ota::kUrlMax];

    TEST_ASSERT_TRUE(p1ota::version_url("main", url, sizeof(url)) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "https://raw.githubusercontent.com/plan-d-io/P1-dongle/main/version", url);
    TEST_ASSERT_TRUE(p1ota::firmware_url("main", url, sizeof(url)) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "https://raw.githubusercontent.com/plan-d-io/P1-dongle/main/p1-dongle.bin", url);

    TEST_ASSERT_TRUE(p1ota::version_url("develop", url, sizeof(url)) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "https://raw.githubusercontent.com/plan-d-io/P1-dongle/develop/version", url);
    TEST_ASSERT_TRUE(p1ota::firmware_url("develop", url, sizeof(url)) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "https://raw.githubusercontent.com/plan-d-io/P1-dongle/develop/p1-dongle.bin", url);

    TEST_ASSERT_EQUAL_UINT(0, p1ota::version_url("alpha", url, sizeof(url)));
    TEST_ASSERT_EQUAL_UINT(0, p1ota::firmware_url("alpha", url, sizeof(url)));
    TEST_ASSERT_EQUAL_UINT(0, p1ota::version_url("V2", url, sizeof(url)));
    TEST_ASSERT_EQUAL_UINT(0, p1ota::firmware_url("V2", url, sizeof(url)));
    TEST_ASSERT_EQUAL_UINT(0, p1ota::version_url("beta", url, sizeof(url)));
    TEST_ASSERT_EQUAL_UINT(0, p1ota::firmware_url("nope", url, sizeof(url)));
    TEST_ASSERT_NULL(p1ota::channel_path("alpha"));
    TEST_ASSERT_NULL(p1ota::channel_path("V2"));
    TEST_ASSERT_NULL(p1ota::channel_path("beta"));
}

void test_should_install()
{
    TEST_ASSERT_TRUE(p1ota::should_install(301, 300, 0, true));
    TEST_ASSERT_FALSE(p1ota::should_install(301, 300, 301, true));
    TEST_ASSERT_TRUE(p1ota::should_install(301, 300, 301, false));
    TEST_ASSERT_TRUE(p1ota::should_install(302, 300, 301, true));
    TEST_ASSERT_FALSE(p1ota::should_install(300, 300, 0, true));
    TEST_ASSERT_FALSE(p1ota::should_install(223, 300, 0, true));
    TEST_ASSERT_FALSE(p1ota::should_install(301, 301, 0, false));
}

void test_should_install_skip_expires()
{
    const std::uint32_t skip = p1ota::kAutoFailSkipS;
    TEST_ASSERT_EQUAL_UINT(23u * 3600u, skip);
    TEST_ASSERT_FALSE(p1ota::should_install(305, 304, 305, true, 3u * 60u));
    TEST_ASSERT_FALSE(p1ota::should_install(305, 304, 305, true, skip - 1u));
    TEST_ASSERT_TRUE(p1ota::should_install(305, 304, 305, true, skip));
    TEST_ASSERT_TRUE(p1ota::should_install(305, 304, 305, true, skip + 60u));
    TEST_ASSERT_TRUE(p1ota::should_install(306, 304, 305, true, 0));
    TEST_ASSERT_TRUE(p1ota::should_install(305, 304, 305, false, 0));
}

void test_fail_elapsed_fake_clock()
{
    TEST_ASSERT_EQUAL_UINT(180u, p1ota::fail_elapsed_s(0, 0, false, 0, 180000u));
    TEST_ASSERT_EQUAL_UINT(p1ota::kAutoFailSkipS,
                           p1ota::fail_elapsed_s(1700000000u, 0, true,
                                                 1700000000u + p1ota::kAutoFailSkipS, 0));
    TEST_ASSERT_EQUAL_UINT(p1ota::kAutoFailSkipS,
                           p1ota::fail_elapsed_s(1700000000u, 50000u, true,
                                                 1700000000u + p1ota::kAutoFailSkipS, 1000u));
    TEST_ASSERT_EQUAL_UINT(5u, p1ota::fail_elapsed_s(0, 0, false, 0, 5000u));
    TEST_ASSERT_EQUAL_UINT(1699999820u, p1ota::fail_unix_stamp(1700000000u, 180000u, 0));
}

void test_format_fail_times()
{
    char buf[32];
    TEST_ASSERT_TRUE(p1ota::format_unix_utc(1577836800u, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("2020-01-01 00:00:00 UTC", buf);
    TEST_ASSERT_TRUE(p1ota::format_boot_age(23u * 3600u, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("boot+23h", buf);
    TEST_ASSERT_TRUE(p1ota::format_boot_age(180u, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("boot+3m", buf);
    TEST_ASSERT_TRUE(p1ota::format_boot_age(12u, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("boot+12s", buf);
}

void test_storage_guard()
{
    TEST_ASSERT_FALSE(p1ota::storage_on_trial_should_rollback(false, true, true));
    TEST_ASSERT_FALSE(p1ota::storage_on_trial_should_rollback(false, false, false));
    TEST_ASSERT_FALSE(p1ota::storage_on_trial_should_rollback(false, false, true));
    TEST_ASSERT_FALSE(p1ota::storage_on_trial_should_rollback(false, true, false));
    TEST_ASSERT_TRUE(p1ota::storage_format_allowed(false));

    TEST_ASSERT_FALSE(p1ota::storage_on_trial_should_rollback(true, true, true));
    TEST_ASSERT_FALSE(p1ota::storage_format_allowed(true));
    TEST_ASSERT_TRUE(p1ota::storage_on_trial_should_rollback(true, false, true));
    TEST_ASSERT_TRUE(p1ota::storage_on_trial_should_rollback(true, true, false));
    TEST_ASSERT_TRUE(p1ota::storage_on_trial_should_rollback(true, false, false));
}

void test_format_version()
{
    char buf[16];
    TEST_ASSERT_EQUAL_UINT(4, p1ota::format_version(300, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("3.00", buf);
    TEST_ASSERT_EQUAL_UINT(4, p1ota::format_version(301, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("3.01", buf);
    TEST_ASSERT_EQUAL_UINT(4, p1ota::format_version(223, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("2.23", buf);
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_fw_ver_matches_num);
    RUN_TEST(test_parse_version_file);
    RUN_TEST(test_channel_urls);
    RUN_TEST(test_format_version);
    RUN_TEST(test_should_install);
    RUN_TEST(test_should_install_skip_expires);
    RUN_TEST(test_fail_elapsed_fake_clock);
    RUN_TEST(test_format_fail_times);
    RUN_TEST(test_storage_guard);
    return UNITY_END();
}
