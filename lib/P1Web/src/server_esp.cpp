#if defined(ARDUINO)

#include "P1Web.h"

#include <cstdlib>
#include <cstring>
#include <fcntl.h>

#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

namespace p1web {
namespace {

Server* g_server = nullptr;

esp_err_t send_gzip(httpd_req_t* req, const char* type, const std::uint8_t* data, std::size_t len)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", kCacheControl);
    return httpd_resp_send(req, reinterpret_cast<const char*>(data), static_cast<ssize_t>(len));
}

esp_err_t send_html(httpd_req_t* req, const char* html, std::size_t len)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, static_cast<ssize_t>(len));
}

esp_err_t send_json(httpd_req_t* req, const char* json, std::size_t len)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, static_cast<ssize_t>(len));
}

esp_err_t dispatch(httpd_req_t* req)
{
    if (g_server == nullptr) {
        return ESP_FAIL;
    }
    const Method method = (req->method == HTTP_POST) ? Method::Post : Method::Get;
    const Route route = resolve(g_server->mode_for_handlers(), method, req->uri);

    switch (route) {
    case Route::Style:
        return send_gzip(req, "text/css", assets::kStyleCssGz, assets::kStyleCssGzLen);
    case Route::Restarting:
        return send_gzip(req, "text/html", assets::kRestartingHtmlGz, assets::kRestartingHtmlGzLen);
    case Route::Rebooting:
        return send_gzip(req, "text/html", assets::kRebootingHtmlGz, assets::kRebootingHtmlGzLen);
    case Route::ConfigPage:
        return send_gzip(req, "text/html", assets::kConfigHtmlGz, assets::kConfigHtmlGzLen);
    case Route::Setup: {
        char* page = static_cast<char*>(malloc(kHtmlBuf));
        if (page == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        ScanCache empty{};
        const ScanCache* scan = g_server->scan_ptr();
        const std::size_t n =
            render_setup_page(scan != nullptr ? *scan : empty, page, kHtmlBuf);
        esp_err_t err = ESP_FAIL;
        if (n == 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "setup render");
        } else {
            err = send_html(req, page, n);
        }
        free(page);
        return err;
    }
    case Route::Status: {
        char* page = static_cast<char*>(malloc(kHtmlBuf));
        if (page == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        StatusView blank{};
        const StatusView* st = g_server->status_ptr();
        const std::size_t n = render_status_page(st != nullptr ? *st : blank, page, kHtmlBuf);
        esp_err_t err = ESP_FAIL;
        if (n == 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status render");
        } else {
            err = send_html(req, page, n);
        }
        free(page);
        return err;
    }
    case Route::ApiStatus: {
        char* json = static_cast<char*>(malloc(kApiStatusMax));
        if (json == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        StatusView blank{};
        const StatusView* st = g_server->status_ptr();
        const std::size_t n = render_api_status(st != nullptr ? *st : blank, json, kApiStatusMax);
        if (n == 0) {
            free(json);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status json");
            return ESP_FAIL;
        }
        const esp_err_t err = send_json(req, json, n);
        free(json);
        return err;
    }
    case Route::ApiData: {
        char* json = static_cast<char*>(malloc(kApiDataMax));
        if (json == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        ReadingsView blank{};
        const ReadingsView* rd = g_server->readings_ptr();
        const std::size_t n = render_api_data(rd != nullptr ? *rd : blank, json, kApiDataMax);
        if (n == 0) {
            free(json);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "data json");
            return ESP_FAIL;
        }
        const esp_err_t err = send_json(req, json, n);
        free(json);
        return err;
    }
    case Route::ApiFields: {
        p1cfg::Store* store = g_server->store_ptr();
        if (store == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no store");
            return ESP_FAIL;
        }
        char* json = static_cast<char*>(malloc(kApiFieldsMax));
        if (json == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        const std::size_t n = render_api_fields(*store, json, kApiFieldsMax);
        if (n == 0) {
            free(json);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fields json");
            return ESP_FAIL;
        }
        const esp_err_t err = send_json(req, json, n);
        free(json);
        return err;
    }
    case Route::FieldsPost: {
        p1cfg::Store* store = g_server->store_ptr();
        if (store == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no store");
            return ESP_FAIL;
        }
        if (req->content_len <= 0 ||
            static_cast<std::size_t>(req->content_len) > kConfigBodyMax) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "TooLarge");
            return ESP_FAIL;
        }
        char* body = static_cast<char*>(malloc(static_cast<std::size_t>(req->content_len) + 1));
        if (body == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        int got = 0;
        while (got < req->content_len) {
            const int r = httpd_req_recv(req, body + got, req->content_len - got);
            if (r <= 0) {
                free(body);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
                return ESP_FAIL;
            }
            got += r;
        }
        body[got] = '\0';
        char bad_key[p1cfg::kMaxFieldId + 1]{};
        const p1cfg::JsonStatus st = store->apply_fields_json(
            std::string_view(body, static_cast<std::size_t>(got)), bad_key, sizeof(bad_key));
        free(body);
        if (st != p1cfg::JsonStatus::Ok) {
            char err[kConfigErrMax];
            format_config_error(st, bad_key, err, sizeof(err));
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err);
            return ESP_FAIL;
        }
        store->save();
        g_server->actions()->republish_config = true;
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Ok", 2);
    }
    case Route::LogPage:
        return send_gzip(req, "text/html", assets::kLogHtmlGz, assets::kLogHtmlGzLen);
    case Route::Syslog:
    case Route::Syslog0: {
        p1log::FileStore* fs = g_server->files_ptr();
        const char* path =
            (route == Route::Syslog) ? p1log::kActivePath : p1log::kPrevPath;
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        if (fs == nullptr) {
            return httpd_resp_send(req, "", 0);
        }
        const std::size_t total = fs->size(path);
        char chunk[kSyslogChunk];
        std::size_t off = 0;
        while (off < total) {
            const std::size_t want =
                (total - off) < sizeof(chunk) ? (total - off) : sizeof(chunk);
            const std::size_t got = fs->read(path, off, chunk, want);
            if (got == 0) {
                break;
            }
            if (httpd_resp_send_chunk(req, chunk, static_cast<ssize_t>(got)) != ESP_OK) {
                httpd_resp_sendstr_chunk(req, nullptr);
                return ESP_FAIL;
            }
            off += got;
        }
        return httpd_resp_send_chunk(req, nullptr, 0);
    }
    case Route::Reboot: {
        g_server->actions()->reboot = true;
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Ok", 2);
    }
    case Route::ResetWifi: {
        g_server->actions()->reset_wifi = true;
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Ok", 2);
    }
    case Route::ResetFactory: {
        g_server->actions()->reset_factory = true;
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Ok", 2);
    }
    case Route::ConfigGet: {
        p1cfg::Store* store = g_server->store_ptr();
        if (store == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no store");
            return ESP_FAIL;
        }
        char* json = static_cast<char*>(malloc(kConfigJsonMax));
        if (json == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        const std::size_t n = store->to_json(json, kConfigJsonMax);
        if (n == 0) {
            free(json);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json overflow");
            return ESP_FAIL;
        }
        const esp_err_t err = send_json(req, json, n);
        free(json);
        return err;
    }
    case Route::ConfigPost: {
        p1cfg::Store* store = g_server->store_ptr();
        if (store == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no store");
            return ESP_FAIL;
        }
        if (req->content_len <= 0 ||
            static_cast<std::size_t>(req->content_len) > kConfigBodyMax) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "TooLarge");
            return ESP_FAIL;
        }
        char* body = static_cast<char*>(malloc(static_cast<std::size_t>(req->content_len) + 1));
        if (body == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        int got = 0;
        while (got < req->content_len) {
            const int r = httpd_req_recv(req, body + got, req->content_len - got);
            if (r <= 0) {
                free(body);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
                return ESP_FAIL;
            }
            got += r;
        }
        body[got] = '\0';
        char bad_key[24]{};
        // apply_json is in-memory only; save() touches NVS. Both are safe from the
        // HTTP task. MQTT republish is deferred to the loop via Actions.
        const p1cfg::JsonStatus st = store->apply_json(
            std::string_view(body, static_cast<std::size_t>(got)), bad_key, sizeof(bad_key));
        free(body);
        if (st != p1cfg::JsonStatus::Ok) {
            char err[kConfigErrMax];
            format_config_error(st, bad_key, err, sizeof(err));
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err);
            return ESP_FAIL;
        }
        store->save();
        g_server->actions()->republish_config = true;
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Ok", 2);
    }
    case Route::Save: {
        if (req->content_len <= 0 || req->content_len > 512) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");
            return ESP_FAIL;
        }
        char body[513];
        int got = 0;
        while (got < req->content_len) {
            const int r = httpd_req_recv(req, body + got, req->content_len - got);
            if (r <= 0) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
                return ESP_FAIL;
            }
            got += r;
        }
        body[got] = '\0';
        Actions* act = g_server->actions();
        if (!parse_save_form(body, act->wifi_ssid, sizeof(act->wifi_ssid), act->wifi_pass,
                             sizeof(act->wifi_pass))) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
            return ESP_FAIL;
        }
        act->save_wifi = true;
        act->reboot = true;
        return send_gzip(req, "text/html", assets::kRestartingHtmlGz, assets::kRestartingHtmlGzLen);
    }
    case Route::NotFound:
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    return ESP_FAIL;
}

esp_err_t on_404(httpd_req_t* req, httpd_err_code_t /*err*/)
{
    if (g_server != nullptr && g_server->mode_for_handlers() == Mode::Ap) {
        char* page = static_cast<char*>(malloc(kHtmlBuf));
        if (page == nullptr) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        ScanCache empty{};
        const ScanCache* scan = g_server->scan_ptr();
        const std::size_t n =
            render_setup_page(scan != nullptr ? *scan : empty, page, kHtmlBuf);
        esp_err_t err = ESP_FAIL;
        if (n == 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "setup render");
        } else {
            err = send_html(req, page, n);
        }
        free(page);
        return err;
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_FAIL;
}

void register_one(httpd_handle_t handle, const char* uri, httpd_method_t method)
{
    httpd_uri_t u{};
    u.uri = uri;
    u.method = method;
    u.handler = dispatch;
    httpd_register_uri_handler(handle, &u);
}

}  // namespace

Mode Server::mode_for_handlers() const
{
    return mode_;
}
const ScanCache* Server::scan_ptr() const
{
    return scan_;
}
const StatusView* Server::status_ptr() const
{
    return status_;
}

const ReadingsView* Server::readings_ptr() const
{
    return readings_;
}

p1cfg::Store* Server::store_ptr() const
{
    return store_;
}

p1log::FileStore* Server::files_ptr() const
{
    return files_;
}

bool Server::begin(Mode mode)
{
    if (running_) {
        set_mode(mode);
        return true;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 32;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    httpd_handle_t handle = nullptr;
    if (httpd_start(&handle, &cfg) != ESP_OK) {
        return false;
    }
    handle_ = handle;
    g_server = this;
    mode_ = mode;

    register_one(handle, "/", HTTP_GET);
    register_one(handle, "/index.html", HTTP_GET);
    register_one(handle, "/setup", HTTP_GET);
    register_one(handle, "/setup.html", HTTP_GET);
    register_one(handle, "/status", HTTP_GET);
    register_one(handle, "/status.html", HTTP_GET);
    register_one(handle, "/style.css", HTTP_GET);
    register_one(handle, "/restarting", HTTP_GET);
    register_one(handle, "/restarting.html", HTTP_GET);
    register_one(handle, "/rebooting", HTTP_GET);
    register_one(handle, "/rebooting.html", HTTP_GET);
    register_one(handle, "/generate_204", HTTP_GET);
    register_one(handle, "/hotspot-detect.html", HTTP_GET);
    register_one(handle, "/save", HTTP_POST);
    register_one(handle, "/config", HTTP_GET);
    register_one(handle, "/config", HTTP_POST);
    register_one(handle, "/api/status", HTTP_GET);
    register_one(handle, "/api/data", HTTP_GET);
    register_one(handle, "/api/fields", HTTP_GET);
    register_one(handle, "/fields", HTTP_POST);
    register_one(handle, "/syslog", HTTP_GET);
    register_one(handle, "/syslog0", HTTP_GET);
    register_one(handle, "/log", HTTP_GET);
    register_one(handle, "/log.html", HTTP_GET);
    register_one(handle, "/reboot", HTTP_POST);
    register_one(handle, "/reset/wifi", HTTP_POST);
    register_one(handle, "/reset/factory", HTTP_POST);
    httpd_register_err_handler(handle, HTTPD_404_NOT_FOUND, on_404);

    running_ = true;
    return true;
}

void Server::end()
{
    if (!running_) {
        return;
    }
    httpd_stop(static_cast<httpd_handle_t>(handle_));
    handle_ = nullptr;
    running_ = false;
    if (g_server == this) {
        g_server = nullptr;
    }
}

void Server::set_mode(Mode mode)
{
    mode_ = mode;
}

void Server::set_scan(const ScanCache* scan)
{
    scan_ = scan;
}

void Server::set_status(const StatusView* status)
{
    status_ = status;
}

void Server::set_readings(const ReadingsView* readings)
{
    readings_ = readings;
}

void Server::set_store(p1cfg::Store* store)
{
    store_ = store;
}

void Server::set_files(p1log::FileStore* files)
{
    files_ = files;
}

Actions* Server::actions()
{
    return &actions_;
}

bool CaptiveDns::begin(std::uint32_t ap_ip_host_order)
{
    end();
    ip_ = ap_ip_host_order;
    sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock_ < 0) {
        return false;
    }
    int yes = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        end();
        return false;
    }
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
    return true;
}

void CaptiveDns::end()
{
    if (sock_ >= 0) {
        closesocket(sock_);
        sock_ = -1;
    }
}

void CaptiveDns::loop()
{
    if (sock_ < 0) {
        return;
    }
    std::uint8_t buf[512];
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    const int n = recvfrom(sock_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromlen);
    if (n < 12) {
        return;
    }
    std::uint8_t resp[512];
    if (static_cast<std::size_t>(n) + 16 > sizeof(resp)) {
        return;
    }
    std::memcpy(resp, buf, static_cast<std::size_t>(n));
    resp[2] = 0x81;
    resp[3] = 0x80;
    resp[6] = 0;
    resp[7] = 1;
    resp[8] = 0;
    resp[9] = 0;
    resp[10] = 0;
    resp[11] = 0;
    std::size_t off = static_cast<std::size_t>(n);
    resp[off++] = 0xc0;
    resp[off++] = 0x0c;
    resp[off++] = 0x00;
    resp[off++] = 0x01;
    resp[off++] = 0x00;
    resp[off++] = 0x01;
    resp[off++] = 0x00;
    resp[off++] = 0x00;
    resp[off++] = 0x00;
    resp[off++] = 30;
    resp[off++] = 0x00;
    resp[off++] = 0x04;
    const std::uint32_t be = htonl(ip_);
    std::memcpy(resp + off, &be, 4);
    off += 4;
    sendto(sock_, resp, off, 0, reinterpret_cast<sockaddr*>(&from), fromlen);
}

}  // namespace p1web

#endif  // ARDUINO
