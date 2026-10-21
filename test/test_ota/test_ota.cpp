#include <P1Ota.h>
#include <fw_version.h>

#include <unity.h>

#include <cstring>

void test_fw_ver_matches_num()
{
    unsigned parsed = 0;
    TEST_ASSERT_TRUE(p1ota::parse_display_version(p1fw::kFwVer, parsed));
    TEST_ASSERT_EQUAL_UINT(302u, parsed);
    TEST_ASSERT_EQUAL_UINT(302u, p1fw::kFwVerNum);
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
    RUN_TEST(test_storage_guard);
    return UNITY_END();
}
