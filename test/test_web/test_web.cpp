#include <P1Web.h>
#include <P1Config.h>

#include <unity.h>

#include <cstdio>
#include <cstring>

void test_route_ap_fallback()
{
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPage,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPage,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/index.html"));
    TEST_ASSERT_EQUAL(p1web::Route::RedirectHome,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/setup"));
    TEST_ASSERT_EQUAL(p1web::Route::RedirectHome,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/setup.html"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPage,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/generate_204"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPage,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/hotspot-detect.html"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPage,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/anything/else"));
    TEST_ASSERT_EQUAL(p1web::Route::Style,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/style.css"));
    TEST_ASSERT_EQUAL(p1web::Route::UpdatePage,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/update"));
    TEST_ASSERT_EQUAL(p1web::Route::UpdateUpload,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Post, "/update"));
    TEST_ASSERT_EQUAL(p1web::Route::UpdateWindow,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/update/window"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigGet,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/config"));
    TEST_ASSERT_EQUAL(p1web::Route::ConfigPost,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Post, "/config"));
    TEST_ASSERT_EQUAL(p1web::Route::ApiStatus,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/api/status"));
    TEST_ASSERT_EQUAL(p1web::Route::ApiData,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/api/data"));
    TEST_ASSERT_EQUAL(p1web::Route::ApiFields,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/api/fields"));
    TEST_ASSERT_EQUAL(p1web::Route::ApiNetworks,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/api/networks"));
    TEST_ASSERT_EQUAL(p1web::Route::FieldsPost,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Post, "/fields"));
    TEST_ASSERT_EQUAL(p1web::Route::Status,
                      p1web::resolve(p1web::Mode::Ap, p1web::Method::Get, "/status"));
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
    TEST_ASSERT_EQUAL(p1web::Route::OtaCheck,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Post, "/update/check"));
    TEST_ASSERT_EQUAL(p1web::Route::OtaInstall,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Post, "/update/install"));
    TEST_ASSERT_EQUAL(p1web::Route::UpdatePage,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/update"));
    TEST_ASSERT_EQUAL(p1web::Route::UpdateUpload,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Post, "/update"));
    TEST_ASSERT_EQUAL(p1web::Route::UpdateWindow,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/update/window"));
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
    TEST_ASSERT_EQUAL(p1web::Route::ApiNetworks,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/api/networks"));
    TEST_ASSERT_EQUAL(p1web::Route::RedirectHome,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/setup"));
    TEST_ASSERT_EQUAL(p1web::Route::RedirectHome,
                      p1web::resolve(p1web::Mode::Station, p1web::Method::Get, "/setup.html"));
}

void test_gzip_blobs()
{
    TEST_ASSERT_TRUE(p1web::assets::kStyleCssGzLen > 2);
    TEST_ASSERT_EQUAL_HEX8(0x1f, p1web::assets::kStyleCssGz[0]);
    TEST_ASSERT_EQUAL_HEX8(0x8b, p1web::assets::kStyleCssGz[1]);
    TEST_ASSERT_TRUE(p1web::assets::kStyleCssRawLen > p1web::assets::kStyleCssGzLen);

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

    TEST_ASSERT_TRUE(p1web::assets::kUpdateHtmlGzLen > 2);
    TEST_ASSERT_EQUAL_HEX8(0x1f, p1web::assets::kUpdateHtmlGz[0]);
    TEST_ASSERT_EQUAL_HEX8(0x8b, p1web::assets::kUpdateHtmlGz[1]);
    TEST_ASSERT_TRUE(p1web::assets::kUpdateHtmlRawLen > p1web::assets::kUpdateHtmlGzLen);
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
    v.telegrams = "ok=10 rejected=1 skipped=2";
    char page[p1web::kHtmlBuf];
    TEST_ASSERT_TRUE(p1web::render_status_page(v, page, sizeof(page)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(page, "P1AABBCC"));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "3.00"));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "192.0.2.10"));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "Connected"));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "ok=10 rejected=1 skipped=2"));
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
    v.telegrams_skipped = 2;
    char buf[p1web::kApiStatusMax];
    const std::size_t n = p1web::render_api_status(v, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"device_id\":\"P1AABBCC\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"mqtt\":\"Connected\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"mqtt_err\":\"\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"upd_checked\":false"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"upd_available\":false"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"upd_found\":0"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"upd_channel\":\"\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"upd_fail\":\"\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"net_pending\":false"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"wifi_fail\":\"\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"wifi\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"mqtt_en\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"telegrams_ok\":10"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"telegrams_skipped\":2"));
    TEST_ASSERT_NULL(std::strstr(buf, "password"));
    TEST_ASSERT_NULL(std::strstr(buf, "secret"));
}

void test_api_status_json_max()
{
    char device_id[p1cfg::kIdMax];
    char fw_ver[8];
    char ip[p1cfg::kIpMax];
    char uptime[24];
    char heap[16];
    char rssi[12];
    char mqtt[16];
    char mqtt_err[48];
    char telegrams[72];
    char upd_channel[p1cfg::kRelChanMax];
    char upd_fail[64];
    char wifi_fail[p1web::kWifiFailMax];
    std::memset(device_id, 'A', sizeof(device_id) - 1);
    device_id[sizeof(device_id) - 1] = '\0';
    std::memset(fw_ver, '9', sizeof(fw_ver) - 1);
    fw_ver[sizeof(fw_ver) - 1] = '\0';
    std::memset(ip, '1', sizeof(ip) - 1);
    ip[sizeof(ip) - 1] = '\0';
    std::memset(uptime, '8', sizeof(uptime) - 1);
    uptime[sizeof(uptime) - 1] = '\0';
    std::memset(heap, '7', sizeof(heap) - 1);
    heap[sizeof(heap) - 1] = '\0';
    std::memset(rssi, '6', sizeof(rssi) - 1);
    rssi[sizeof(rssi) - 1] = '\0';
    std::memset(mqtt, 'C', sizeof(mqtt) - 1);
    mqtt[sizeof(mqtt) - 1] = '\0';
    std::memset(mqtt_err, 'E', sizeof(mqtt_err) - 1);
    mqtt_err[sizeof(mqtt_err) - 1] = '\0';
    std::memset(telegrams, 'T', sizeof(telegrams) - 1);
    telegrams[sizeof(telegrams) - 1] = '\0';
    std::memset(upd_channel, 'm', sizeof(upd_channel) - 1);
    upd_channel[sizeof(upd_channel) - 1] = '\0';
    std::memset(upd_fail, 'U', sizeof(upd_fail) - 1);
    upd_fail[sizeof(upd_fail) - 1] = '\0';
    std::memset(wifi_fail, 'W', sizeof(wifi_fail) - 1);
    wifi_fail[sizeof(wifi_fail) - 1] = '\0';

    p1web::StatusView v{};
    v.device_id = device_id;
    v.fw_ver = fw_ver;
    v.ip = ip;
    v.uptime = uptime;
    v.heap = heap;
    v.rssi = rssi;
    v.mqtt = mqtt;
    v.mqtt_err = mqtt_err;
    v.telegrams = telegrams;
    v.wifi = true;
    v.meter = true;
    v.ha_en = true;
    v.mqtt_en = true;
    v.telegrams_ok = 4294967295u;
    v.telegrams_rejected = 4294967295u;
    v.telegrams_skipped = 4294967295u;
    v.upd_checked = true;
    v.upd_available = true;
    v.upd_found = 4294967295u;
    v.upd_channel = upd_channel;
    v.upd_fail = upd_fail;
    v.net_pending = true;
    v.wifi_fail = wifi_fail;
    char eid[] = "waiting";
    char eid_claim[p1cfg::kEidClaimMax];
    char eid_record[64];
    char eid_error[64];
    std::memset(eid_claim, 'C', sizeof(eid_claim) - 1);
    eid_claim[sizeof(eid_claim) - 1] = '\0';
    std::memset(eid_record, 'R', sizeof(eid_record) - 1);
    eid_record[sizeof(eid_record) - 1] = '\0';
    std::memset(eid_error, 'E', sizeof(eid_error) - 1);
    eid_error[sizeof(eid_error) - 1] = '\0';
    v.eid = eid;
    v.eid_claim = eid_claim;
    v.eid_record = eid_record;
    v.eid_interval = 4294967295u;
    v.eid_last_upload = 4294967295u;
    v.eid_error = eid_error;

    char buf[p1web::kApiStatusMax];
    const std::size_t n = p1web::render_api_status(v, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
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

static p1web::ProvInput prov_in(bool have_ssid, bool sta, bool seen, std::uint8_t clients,
                               std::uint32_t now_ms)
{
    p1web::ProvInput in{};
    in.have_ssid = have_ssid;
    in.sta_connected = sta;
    in.ssid_seen = seen;
    in.ap_clients = clients;
    in.now_ms = now_ms;
    return in;
}

void test_provision_never_connects_stays_in_setup()
{
    p1web::ProvisionMachine m;
    auto out = m.tick(prov_in(true, false, false, 0, 0));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Connecting, out.phase);
    TEST_ASSERT_FALSE(out.want_ap);
    TEST_ASSERT_TRUE(out.want_sta);

    out = m.tick(prov_in(true, false, false, 0, 59999));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Connecting, out.phase);
    TEST_ASSERT_FALSE(out.want_ap);
    TEST_ASSERT_TRUE(out.want_sta);

    out = m.tick(prov_in(true, false, false, 0, 60000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);
    TEST_ASSERT_EQUAL(p1web::ProvEvent::ConnectFailed, out.event);

    out = m.tick(prov_in(true, false, false, 0, 60001));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);

    out = m.tick(prov_in(true, false, false, 0, 90000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);

    out = m.tick(prov_in(true, false, false, 0, 179999));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);
}

void test_provision_idle_while_ssid_not_seen()
{
    p1web::ProvisionMachine m;
    m.tick(prov_in(true, false, false, 0, 0));
    m.tick(prov_in(true, false, false, 0, 60000));
    auto out = m.tick(prov_in(true, false, false, 0, 180000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);
    TEST_ASSERT_EQUAL(p1web::ProvEvent::None, out.event);
    out = m.tick(prov_in(true, false, false, 0, 360000));
    TEST_ASSERT_FALSE(out.want_sta);
}

void test_provision_retry_after_120s_when_ssid_seen()
{
    p1web::ProvisionMachine m;
    m.tick(prov_in(true, false, false, 0, 0));
    m.tick(prov_in(true, false, true, 0, 60000));

    auto out = m.tick(prov_in(true, false, true, 0, 179999));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);

    out = m.tick(prov_in(true, false, true, 0, 180000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_TRUE(out.want_sta);
    TEST_ASSERT_EQUAL(p1web::ProvEvent::RetryStart, out.event);
}

void test_provision_failed_attempt_then_wait_120s()
{
    p1web::ProvisionMachine m;
    m.tick(prov_in(true, false, false, 0, 0));
    m.tick(prov_in(true, false, true, 0, 60000));
    auto out = m.tick(prov_in(true, false, true, 0, 180000));
    TEST_ASSERT_TRUE(out.want_sta);

    out = m.tick(prov_in(true, false, true, 0, 199999));
    TEST_ASSERT_TRUE(out.want_sta);
    TEST_ASSERT_TRUE(out.want_ap);

    out = m.tick(prov_in(true, false, true, 0, 200000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);
    TEST_ASSERT_EQUAL(p1web::ProvEvent::RetryFailed, out.event);

    out = m.tick(prov_in(true, false, true, 0, 319999));
    TEST_ASSERT_FALSE(out.want_sta);

    out = m.tick(prov_in(true, false, true, 0, 320000));
    TEST_ASSERT_TRUE(out.want_sta);
    TEST_ASSERT_EQUAL(p1web::ProvEvent::RetryStart, out.event);
}

void test_provision_retry_connects()
{
    p1web::ProvisionMachine m;
    m.tick(prov_in(true, false, false, 0, 0));
    m.tick(prov_in(true, false, true, 0, 60000));
    m.tick(prov_in(true, false, true, 0, 180000));

    auto out = m.tick(prov_in(true, true, true, 0, 185000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Station, out.phase);
    TEST_ASSERT_FALSE(out.want_ap);
    TEST_ASSERT_TRUE(out.want_sta);
    TEST_ASSERT_EQUAL(p1web::ProvEvent::Rejoined, out.event);
}

void test_provision_no_retry_while_client_connected()
{
    p1web::ProvisionMachine m;
    m.tick(prov_in(true, false, false, 0, 0));
    m.tick(prov_in(true, false, true, 1, 60000));
    auto out = m.tick(prov_in(true, false, true, 1, 180000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);
    out = m.tick(prov_in(true, false, true, 1, 400000));
    TEST_ASSERT_FALSE(out.want_sta);
}

void test_provision_no_ssid_never_attempts()
{
    p1web::ProvisionMachine m;
    auto out = m.tick(prov_in(false, false, false, 0, 0));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);
    out = m.tick(prov_in(false, false, true, 0, 180000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_FALSE(out.want_sta);
}

void test_provision_drop_retries_then_setup()
{
    p1web::ProvisionMachine m;
    auto out = m.tick(prov_in(true, true, false, 0, 0));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Station, out.phase);

    out = m.tick(prov_in(true, false, false, 0, 10000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Connecting, out.phase);
    TEST_ASSERT_FALSE(out.want_ap);
    TEST_ASSERT_TRUE(out.want_sta);

    out = m.tick(prov_in(true, false, false, 0, 69999));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::Connecting, out.phase);

    out = m.tick(prov_in(true, false, false, 0, 70000));
    TEST_ASSERT_EQUAL(p1web::ProvPhase::NeedAp, out.phase);
    TEST_ASSERT_TRUE(out.want_ap);
    TEST_ASSERT_FALSE(out.want_sta);
    TEST_ASSERT_EQUAL(p1web::ProvEvent::ConnectFailed, out.event);
}

void test_api_networks_json_max()
{
    p1web::ScanCache c{};
    c.scanning = false;
    c.ready = true;
    c.count = p1web::kMaxScanResults;
    for (std::size_t i = 0; i < c.count; ++i) {
        for (std::size_t j = 0; j + 1 < p1web::kSsidMax; ++j) {
            c.entries[i].ssid[j] = '"';
        }
        c.entries[i].ssid[p1web::kSsidMax - 1] = '\0';
        c.entries[i].rssi = -2147483647 - 1;
    }
    char buf[p1web::kApiNetworksMax];
    const std::size_t n = p1web::render_api_networks(c, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"scanning\":false"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\\\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"rssi\":"));
}

void test_status_page_full_size()
{
    char device_id[9];
    char ip[p1cfg::kIpMax];
    char uptime[24];
    char heap[16];
    char rssi[12];
    char telegrams[72];
    std::memset(device_id, 'P', sizeof(device_id) - 1);
    device_id[sizeof(device_id) - 1] = '\0';
    std::memset(ip, '1', sizeof(ip) - 1);
    ip[sizeof(ip) - 1] = '\0';
    std::memset(uptime, '8', sizeof(uptime) - 1);
    uptime[sizeof(uptime) - 1] = '\0';
    std::memset(heap, '7', sizeof(heap) - 1);
    heap[sizeof(heap) - 1] = '\0';
    std::memset(rssi, '6', sizeof(rssi) - 1);
    rssi[sizeof(rssi) - 1] = '\0';
    std::memset(telegrams, 'T', sizeof(telegrams) - 1);
    telegrams[sizeof(telegrams) - 1] = '\0';

    p1web::StatusView v{};
    v.device_id = device_id;
    v.fw_ver = "99.99";
    v.ip = ip;
    v.uptime = uptime;
    v.heap = heap;
    v.rssi = rssi;
    v.mqtt = "Unsupported";
    v.telegrams = telegrams;
    char page[p1web::kHtmlBuf];
    const std::size_t n = p1web::render_status_page(v, page, sizeof(page));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(page));
    TEST_ASSERT_NOT_NULL(std::strstr(page, "Unsupported"));
}

void test_upload_gates()
{
    using p1web::UploadRefuse;
    const std::size_t max_image = 1966080;

    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::WindowClosed),
                      static_cast<int>(p1web::check_upload_start(false, true, false, 0, max_image)));
    TEST_ASSERT_EQUAL_STRING("Press the button on the dongle first.",
                             p1web::upload_refuse_reason(UploadRefuse::WindowClosed));

    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::Busy),
                      static_cast<int>(p1web::check_upload_start(true, true, false, 0, max_image)));
    TEST_ASSERT_EQUAL_STRING("An update is already in progress.",
                             p1web::upload_refuse_reason(UploadRefuse::Busy));

    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::NoFile),
                      static_cast<int>(p1web::check_upload_start(true, false, false, 100, max_image)));
    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::NoFile),
                      static_cast<int>(p1web::check_upload_start(true, false, true, 0, max_image)));
    TEST_ASSERT_EQUAL_STRING("No file received.",
                             p1web::upload_refuse_reason(UploadRefuse::NoFile));

    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::TooLarge),
                      static_cast<int>(p1web::check_upload_start(true, false, true, max_image + 1,
                                                                max_image)));
    TEST_ASSERT_EQUAL_STRING("File is too large to be firmware.",
                             p1web::upload_refuse_reason(UploadRefuse::TooLarge));

    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::Ok),
                      static_cast<int>(p1web::check_upload_start(true, false, true, 100, max_image)));

    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::Ok),
                      static_cast<int>(p1web::check_firmware_magic(0xE9)));
    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::NotFirmware),
                      static_cast<int>(p1web::check_firmware_magic(0x00)));
    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::NotFirmware),
                      static_cast<int>(p1web::check_firmware_magic(0x89)));
    TEST_ASSERT_EQUAL(static_cast<int>(UploadRefuse::NotFirmware),
                      static_cast<int>(p1web::check_firmware_magic(0x7B)));
    TEST_ASSERT_EQUAL_STRING("Not a firmware image.",
                             p1web::upload_refuse_reason(UploadRefuse::NotFirmware));
}

void test_update_window_json_worst()
{
    char buf[p1web::kUpdateWindowJsonMax];
    const std::size_t n =
        p1web::render_update_window(true, 4294967295u, true, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"open\":true,\"left_s\":4294967295,\"busy\":true}", buf);

    TEST_ASSERT_TRUE(p1web::render_update_window(false, 42, false, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"open\":false,\"left_s\":0,\"busy\":false}", buf);
}

void test_wifi_ssid_in_save()
{
    // Loaded SSID empty, user leaves the placeholder: MQTT_HOST-only save
    // must not include WIFI_SSID (or a blank WIFI_PASS for a network change).
    TEST_ASSERT_FALSE(p1web::wifi_ssid_in_save("", ""));
    TEST_ASSERT_FALSE(p1web::wifi_ssid_in_save("TestNet", "TestNet"));
    TEST_ASSERT_TRUE(p1web::wifi_ssid_in_save("", "TestNet"));
    TEST_ASSERT_TRUE(p1web::wifi_ssid_in_save("TestNet", "OtherNet"));
}

void test_wifi_join_reason_text()
{
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::Password, p1web::classify_wifi_reason(202));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::Password, p1web::classify_wifi_reason(15));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::Password, p1web::classify_wifi_reason(204));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::Password, p1web::classify_wifi_reason(23));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::Password, p1web::classify_wifi_reason(2));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::NotFound, p1web::classify_wifi_reason(201));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::NotFound, p1web::classify_wifi_reason(210));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::NotFound, p1web::classify_wifi_reason(211));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::Other, p1web::classify_wifi_reason(205));
    TEST_ASSERT_EQUAL(p1web::WifiJoinFail::Other, p1web::classify_wifi_reason(1));

    char buf[160];
    TEST_ASSERT_TRUE(p1web::format_wifi_join_log("TestNet", 202, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("WiFi join failed for TestNet: password not accepted", buf);
    TEST_ASSERT_TRUE(p1web::format_wifi_join_log("TestNet", 201, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("WiFi join failed for TestNet: network not found", buf);
    TEST_ASSERT_TRUE(p1web::format_wifi_join_log("TestNet", 205, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("WiFi join failed for TestNet: no response from the network [205]", buf);

    TEST_ASSERT_TRUE(p1web::format_wifi_join_page("TestNet", 202, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("Could not join \"TestNet\": the password was not accepted.", buf);
    TEST_ASSERT_TRUE(p1web::format_wifi_join_page("TestNet", 201, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("Could not join \"TestNet\": the network was not found.", buf);
    TEST_ASSERT_TRUE(p1web::format_wifi_join_page("TestNet", 205, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("Could not join \"TestNet\": no response from the network.", buf);
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
    RUN_TEST(test_api_status_json_max);
    RUN_TEST(test_api_data_json_max);
    RUN_TEST(test_api_fields_json_max);
    RUN_TEST(test_format_config_error);
    RUN_TEST(test_provision_never_connects_stays_in_setup);
    RUN_TEST(test_provision_idle_while_ssid_not_seen);
    RUN_TEST(test_provision_retry_after_120s_when_ssid_seen);
    RUN_TEST(test_provision_failed_attempt_then_wait_120s);
    RUN_TEST(test_provision_retry_connects);
    RUN_TEST(test_provision_no_retry_while_client_connected);
    RUN_TEST(test_provision_no_ssid_never_attempts);
    RUN_TEST(test_provision_drop_retries_then_setup);
    RUN_TEST(test_api_networks_json_max);
    RUN_TEST(test_status_page_full_size);
    RUN_TEST(test_upload_gates);
    RUN_TEST(test_update_window_json_worst);
    RUN_TEST(test_scan_sort);
    RUN_TEST(test_wifi_ssid_in_save);
    RUN_TEST(test_wifi_join_reason_text);
    return UNITY_END();
}
