#include <P1Web.h>
#include <P1Config.h>

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
    TEST_ASSERT_EQUAL(p1web::Route::ConfigGet,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/config"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPost,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Post, "/config"));
}

void test_route_station()
{
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPage,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPage,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/index.html"));
    TEST_ASSERT_EQUAL(p1web::Route::Status,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/status"));
    TEST_ASSERT_EQUAL(p1web::Route::Rebooting,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/rebooting"));
    TEST_ASSERT_EQUAL(p1web::Route::Rebooting,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/rebooting.html"));
    TEST_ASSERT_EQUAL(p1web::Route::ApiStatus,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/api/status"));
    TEST_ASSERT_EQUAL(p1web::Route::ApiData,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/api/data"));
    TEST_ASSERT_EQUAL(p1web::Route::ApiFields,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/api/fields"));
    TEST_ASSERT_EQUAL(p1web::Route::FieldsPost,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Post, "/fields"));
    TEST_ASSERT_EQUAL(p1web::Route::Syslog,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/syslog"));
    TEST_ASSERT_EQUAL(p1web::Route::Syslog0,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/syslog0"));
    TEST_ASSERT_EQUAL(p1web::Route::LogPage,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/log"));
    TEST_ASSERT_EQUAL(p1web::Route::Reboot,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Post, "/reboot"));
    TEST_ASSERT_EQUAL(p1web::Route::NotFound,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/reboot"));
    TEST_ASSERT_EQUAL(p1web::Route::ResetWifi,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Post, "/reset/wifi"));
    TEST_ASSERT_EQUAL(p1web::Route::ResetFactory,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Post, "/reset/factory"));
    TEST_ASSERT_EQUAL(p1web::Route::NotFound,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/reset/factory"));
    TEST_ASSERT_EQUAL(p1web::Route::Rebooting,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get,
                                     "/rebooting?reset=wifi"));
    TEST_ASSERT_EQUAL(p1web::Route::NotFound,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/nope"));
    TEST_ASSERT_EQUAL(p1web::Route::Style,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/style.css"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigGet,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/config"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPost,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Post, "/config"));
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

    TEST_ASSERT_TRUE(p1web::assets::kConfigHtmlGzLen > 2);
    TEST_ASSERT_EQUAL_HEX8(0x1f, p1web::assets::kConfigHtmlGz[0]);
    TEST_ASSERT_EQUAL_HEX8(0x8b, p1web::assets::kConfigHtmlGz[1]);
    TEST_ASSERT_TRUE(p1web::assets::kConfigHtmlRawLen > p1web::assets::kConfigHtmlGzLen);

    TEST_ASSERT_TRUE(p1web::assets::kRebootingHtmlGzLen > 2);
    TEST_ASSERT_EQUAL_HEX8(0x1f, p1web::assets::kRebootingHtmlGz[0]);
    TEST_ASSERT_EQUAL_HEX8(0x8b, p1web::assets::kRebootingHtmlGz[1]);

    TEST_ASSERT_TRUE(p1web::assets::kLogHtmlGzLen > 2);
    TEST_ASSERT_EQUAL_HEX8(0x1f, p1web::assets::kLogHtmlGz[0]);
    TEST_ASSERT_EQUAL_HEX8(0x8b, p1web::assets::kLogHtmlGz[1]);
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

void test_api_status_json()
{
    p1web::StatusView v{};
    v.device_id = "P1AABBCC";
    v.fw_ver = "3.00";
    v.ip = "192.0.2.10";
    v.uptime = "12 s";
    v.heap = "169000";
    v.rssi = "-66";
    v.mqtt = "Connected";
    v.wifi = true;
    v.meter = true;
    v.ha_en = true;
    v.mqtt_en = true;
    v.telegrams_ok = 10;
    v.telegrams_rejected = 1;
    char buf[p1web::kApiStatusMax];
    const std::size_t n = p1web::render_api_status(v, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"device_id\":\"P1AABBCC\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"mqtt\":\"Connected\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"mqtt_err\":\"\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"wifi\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"mqtt_en\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"telegrams_ok\":10"));
    TEST_ASSERT_NULL(std::strstr(buf, "password"));
    TEST_ASSERT_NULL(std::strstr(buf, "secret"));
}

void test_api_data_json_max()
{
    p1web::FieldReading fields[p1cfg::kMaxFields]{};
    const std::size_t nf = p1cfg::catalogue_size();
    TEST_ASSERT_TRUE(nf <= p1cfg::kMaxFields);
    for (std::size_t i = 0; i < nf; ++i) {
        // Max-length display strings.
        for (std::size_t j = 0; j + 1 < p1web::kReadingValueMax; ++j) {
            fields[i].value[j] = '9';
        }
        fields[i].value[p1web::kReadingValueMax - 1] = '\0';
        std::snprintf(fields[i].unit, sizeof(fields[i].unit), "kWh");
    }
    p1web::ReadingsView r{fields, nf};
    char buf[p1web::kApiDataMax];
    const std::size_t n = p1web::render_api_data(r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_EQUAL('[', buf[0]);
    TEST_ASSERT_EQUAL(']', buf[n - 1]);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"id\":"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"label\":"));
    TEST_ASSERT_NULL(std::strstr(buf, "WIFI_PASS"));
    TEST_ASSERT_NULL(std::strstr(buf, "MQTT_PASS"));
}

void test_api_fields_json_max()
{
    // Worst-case constant: N = kMaxFields, each entry 104 B → 6721.
    TEST_ASSERT_EQUAL_UINT(6721, p1web::kApiFieldsMax);
    p1cfg::MemoryBackend mem;
    p1cfg::Store s(mem);
    s.load();
    char buf[p1web::kApiFieldsMax];
    const std::size_t n = p1web::render_api_fields(s, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_EQUAL('[', buf[0]);
    TEST_ASSERT_EQUAL(']', buf[n - 1]);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"1-0:1.8.1\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"region\":\""));
    TEST_ASSERT_NULL(std::strstr(buf, "identification"));
    // Synthetic fill: every catalogue slot as a max-size entry must fit.
    std::size_t need = 2;
    for (std::size_t i = 0; i < p1cfg::kMaxFields; ++i) {
        if (i > 0) {
            need += 1;
        }
        need += p1web::kApiFieldsEntryMax;
    }
    TEST_ASSERT_TRUE(p1web::kApiFieldsMax >= need);
}

void test_format_config_error()
{
    char err[p1web::kConfigErrMax];
    TEST_ASSERT_TRUE(p1web::format_config_error(p1cfg::JsonStatus::Invalid, "MQTT_PORT", err,
                                                sizeof(err)) > 0);
    TEST_ASSERT_EQUAL_STRING("Invalid:MQTT_PORT", err);
    TEST_ASSERT_TRUE(
        p1web::format_config_error(p1cfg::JsonStatus::Parse, "", err, sizeof(err)) > 0);
    TEST_ASSERT_EQUAL_STRING("Parse", err);
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
    RUN_TEST(test_api_status_json);
    RUN_TEST(test_api_data_json_max);
    RUN_TEST(test_api_fields_json_max);
    RUN_TEST(test_format_config_error);
    RUN_TEST(test_provision_machine);
    RUN_TEST(test_save_form_and_json);
    RUN_TEST(test_scan_sort);
    return UNITY_END();
}
