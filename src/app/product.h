#pragma once

// What one product gives the shared application. The product fills a static
// instance; the application keeps a pointer. Nothing here is allocated.

#include <P1DSMR.h>
#include <P1Syslog.h>
#include <P1Web.h>

#include <cstdint>
#include <string_view>

namespace p1app {

struct Product {
    const char* name;

    // All true on the dongle, so behaviour is unchanged. A later product
    // turns individual ones off.
    bool ota;
    bool energieid;
    bool ha_2x_migration;
    bool legacy_nvs_import;

    // Setup AP, WiFi hostname and mDNS name. Dongle: "p1dongle".
    const char* ap_ssid;
    const char* fw_ver;
    unsigned fw_ver_num;
    const char* ha_origin;
    const char* ha_meter_model;
    const char* ha_debug_model;

    // Extra debug-device sensors. The dongle returns 0.
    struct DiagPoint {
        const char* id;
        const char* name;
        const char* unit;
        const char* dev_class;
        const char* value_json;
    };
    std::size_t (*diagnostics)(DiagPoint* out, std::size_t cap);

    // Extra status-page lines. The dongle returns 0 and keeps its UART fields
    // in MeterSnap. A radio fills link facts here.
    struct StatusLine {
        char label[24];
        char value[64];
    };
    std::size_t (*status_lines)(StatusLine* out, std::size_t cap);

    void (*board_begin)();
    void (*led_write)(std::uint8_t r, std::uint8_t g, std::uint8_t b);
    bool (*button_down)();

    struct UartBringup {
        unsigned rx_buf;
        unsigned heap_before;
        unsigned heap_after;
    };

    using OnTelegram = void (*)(const p1dsmr::Data& data, std::string_view raw);
    using LogFn = void (*)(p1log::Level level, const char* msg);

    // on_telegram, log and loop_max_ms are the application's. The input
    // calls them; it does not own WiFi, MQTT or the log store.
    void (*source_begin)(OnTelegram on_telegram, LogFn log, const std::uint32_t* loop_max_ms,
                         UartBringup& uart);
    void (*source_request)(std::uint32_t trg_type, std::uint32_t trg_int, bool force);
    void (*source_drain)();
    void (*note_fresh)(std::uint32_t ms);

    struct MeterSnap {
        unsigned ok;
        unsigned rejected;
        unsigned skipped;
        unsigned ovf;
        unsigned full;
        unsigned frm;
        std::uint32_t last_ms;
        bool has_rejected;
        bool has_skipped;
    };
    void (*meter)(MeterSnap& out);

    p1web::TelegramDump (*dump_rejected)();
    p1web::TelegramDump (*dump_skipped)();

    // Product serial commands. True when the line was handled.
    bool (*serial_cmd)(char* line);
};

}  // namespace p1app
