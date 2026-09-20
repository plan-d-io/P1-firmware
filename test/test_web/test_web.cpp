#include <P1Web.h>

#include <unity.h>

#include <cstdio>
#include <cstring>

void test_route_ap_fallback()
{
    TEST_ASSERT_EQUAL(p1web::Route::Setup, p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/"));
    TEST_ASSERT_EQUAL(p1web::Route::Setup,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/setup"));
    TEST_ASSERT_EQUAL(p1web::Route::Setup,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/generate_204"));
    TEST_ASSERT_EQUAL(p1web::Route::Setup,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/anything/else"));
    TEST_ASSERT_EQUAL(p1web::Route::Style,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/style.css"));
    TEST_ASSERT_EQUAL(p1web::Route::Save,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Post, "/save"));
}

void test_route_station()
{
    TEST_ASSERT_EQUAL(p1web::Route::Status,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/"));
    TEST_ASSERT_EQUAL(p1web::Route::Status,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/status"));
    TEST_ASSERT_EQUAL(p1web::Route::NotFound,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/nope"));
    TEST_ASSERT_EQUAL(p1web::Route::Style,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/style.css"));
}

void test_gzip_blobs()
{
    TEST_ASSERT_TRUE(p1web::assets::kStyleCssGzLen > 2);
    TEST_ASSERT_EQUAL_HEX8(0x1f, p1web::assets::kStyleCssGz[0]);
    TEST_ASSERT_EQUAL_HEX8(0x8b, p1web::assets::kStyleCssGz[1]);
    TEST_ASSERT_TRUE(p1web::assets::kStyleCssRawLen > p1web::assets::kStyleCssGzLen);

    TEST_ASSERT_TRUE(p1web::assets::kRestartingHtmlGzLen > 2);
    TEST_ASSERT_EQUAL_HEX8(0x1f, p1web::assets::kRestartingHtmlGz[0]);
    TEST_ASSERT_EQUAL_HEX8(0x8b, p1web::assets::kRestartingHtmlGz[1]);
}

void test_status_render()
{
    p1web::StatusView v{};
    v.device_id = "P1AABBCC";
    v.fw_ver = "3.00";
    v.ip = "192.0.2.10";
    v.uptime = "12s";
    v.heap = "169000";
    v.rssi = "-66";
    v.mqtt = "Connected";
    v.telegrams = "ok=10 rejected=1";
    char page[p1web::kHtmlBuf];
    TEST_ASSERT_TRUE(p1web::render_status_page(v, page, sizeof(page)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(page, "P1AABBCC"));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "3.00"));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "192.0.2.10"));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "Connected"));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "ok=10 rejected=1"));
}

void test_provision_machine()
{
    p1web::ProvisionMachine m;
    p1web::ProvInput in{};

    // No credentials → AP.
    in.have_ssid = false;
    in.sta_connected = false;
    in.now_ms = 0;
    auto out = m.tick(in);
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);

    // Credentials, connecting.
    in.have_ssid = true;
    in.now_ms = 1000;
    out = m.tick(in);
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Connecting, out.phase);
    TEST_ASSERT_FALSE(out.want_ap);
    TEST_ASSERT_TRUE(out.want_sta);

    // Still within 60 s.
    in.now_ms = 1000 + p1web::kConnectTimeoutMs - 1;
    out = m.tick(in);
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Connecting, out.phase);
    TEST_ASSERT_FALSE(out.want_ap);

    // Timeout → AP (while still wanting STA).
    in.now_ms = 1000 + p1web::kConnectTimeoutMs;
    out = m.tick(in);
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_TRUE(out.want_sta);

    // Connected → station, AP down.
    in.sta_connected = true;
    in.now_ms = 1000 + p1web::kConnectTimeoutMs + 5000;
    out = m.tick(in);
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Station, out.phase);
    TEST_ASSERT_FALSE(out.want_ap);
    TEST_ASSERT_TRUE(out.want_sta);
}

void test_save_form_and_json()
{
    char ssid[33];
    char pass[64];
    TEST_ASSERT_TRUE(p1web::parse_save_form("ssid=TestNet&pass=secret%21", ssid, sizeof(ssid), pass,
                                            sizeof(pass)));
    TEST_ASSERT_EQUAL_STRING("TestNet", ssid);
    TEST_ASSERT_EQUAL_STRING("secret!", pass);

    TEST_ASSERT_TRUE(p1web::parse_save_form("ssid=Ignore&ssid_other=Hidden&pass=", ssid,
                                            sizeof(ssid), pass, sizeof(pass)));
    TEST_ASSERT_EQUAL_STRING("Hidden", ssid);

    char json[160];
    TEST_ASSERT_TRUE(p1web::wifi_creds_json("TestNet", "a\"b", json, sizeof(json)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"WIFI_SSID\":\"TestNet\",\"WIFI_PASS\":\"a\\\"b\"}", json);
}

void test_scan_sort()
{
    p1web::ScanCache c{};
    c.count = 3;
    std::snprintf(c.entries[0].ssid, sizeof(c.entries[0].ssid), "weak");
    c.entries[0].rssi = -90;
    std::snprintf(c.entries[1].ssid, sizeof(c.entries[1].ssid), "TestNet");
    c.entries[1].rssi = -50;
    std::snprintf(c.entries[2].ssid, sizeof(c.entries[2].ssid), "mid");
    c.entries[2].rssi = -70;
    p1web::sort_scan_strongest_first(c);
    TEST_ASSERT_EQUAL_STRING("TestNet", c.entries[0].ssid);
    TEST_ASSERT_EQUAL_STRING("mid", c.entries[1].ssid);
    TEST_ASSERT_EQUAL_STRING("weak", c.entries[2].ssid);
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_route_ap_fallback);
    RUN_TEST(test_route_station);
    RUN_TEST(test_gzip_blobs);
    RUN_TEST(test_status_render);
    RUN_TEST(test_provision_machine);
    RUN_TEST(test_save_form_and_json);
    RUN_TEST(test_scan_sort);
    return UNITY_END();
}
