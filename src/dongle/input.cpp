// P1 input and Atom board for the dongle. The shared application calls this
// through p1app::Product. The UART setup is the 2.23 match; do not change it.

#include <Arduino.h>
#include <P1DSMR.h>
#include <P1Publish.h>
#include <P1Syslog.h>
#include <P1Web.h>
#include <fw_version.h>
#include <esp32-hal-rgb-led.h>
#include <p1_data_request.h>
#include <driver/gpio.h>
#include <soc/io_mux_reg.h>
#include <soc/uart_reg.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

#include "../app/product.h"

// Pins from archive/P1-dongle/P1-dongle.ino:
//   HWSERIAL.begin(115200, SERIAL_8N1, 21, 22);
//   TRIGGER 25, HIGH for continuous telegrams.
// Face button: M5Atom.h Button Btn = Button(39, true, 10) — GPIO 39, active low.
static constexpr int kP1Rx = 21;
static constexpr int kP1Tx = 22;
static constexpr int kDataRequest = 25;
static constexpr int kBtn = 39;
// Single SK6812 on the Atom Lite.
static constexpr int kLedPin = 27;

static p1req::Line g_req;
static p1dsmr::Reader g_reader;

static unsigned g_ok = 0;
static unsigned g_rejected = 0;
static unsigned g_skipped = 0;
static bool g_tele_good_stretch = true;
static std::uint32_t g_tele_summary_ms = 0;
static unsigned g_win_ok = 0;
static unsigned g_win_rej = 0;
static unsigned g_win_skip = 0;
static char g_tele_last_reason[p1dsmr::Reader::kReasonMax]{};
static std::uint32_t g_last_telegram_ms = 0;
static bool g_hold_p1 = false;  // desk: pause telegram handling without unplugging
static volatile unsigned g_ovf = 0;
static volatile unsigned g_full = 0;
static volatile unsigned g_frm = 0;

struct TelegramKeep {
    char bytes[p1dsmr::Reader::kBufferSize]{};
    std::size_t len = 0;
    char reason[p1dsmr::Reader::kReasonMax]{};
    char when[24]{};
};
static TelegramKeep g_keep_rej{};
static TelegramKeep g_keep_skip{};

static p1app::Product::OnTelegram g_on_telegram = nullptr;
static p1app::Product::LogFn g_log = nullptr;
static const std::uint32_t* g_loop_max_ms = nullptr;

static void log_msg(p1log::Level level, const char* msg)
{
    if (g_log != nullptr && msg != nullptr) {
        g_log(level, msg);
    }
}

static void deliver(const p1dsmr::Data& data)
{
    if (g_hold_p1) {
        return;
    }
    if (g_on_telegram != nullptr) {
        g_on_telegram(data, g_reader.raw());
    }
}

static void keep_telegram(TelegramKeep& dst, std::string_view raw, std::string_view reason)
{
    dst.len = (raw.size() < sizeof(dst.bytes)) ? raw.size() : sizeof(dst.bytes);
    if (dst.len > 0 && raw.data() != nullptr) {
        std::memcpy(dst.bytes, raw.data(), dst.len);
    }
    const std::size_t rn = (reason.size() < sizeof(dst.reason) - 1) ? reason.size()
                                                                     : sizeof(dst.reason) - 1;
    std::memcpy(dst.reason, reason.data(), rn);
    dst.reason[rn] = '\0';
    p1log::format_iso_utc(p1log::unix_now(), dst.when, sizeof(dst.when));
}

static p1web::TelegramDump dump_rejected()
{
    if (g_keep_rej.len == 0) {
        return {};
    }
    return {g_keep_rej.bytes, g_keep_rej.len, g_keep_rej.reason, g_keep_rej.when};
}

static p1web::TelegramDump dump_skipped()
{
    if (g_keep_skip.len == 0) {
        return {};
    }
    return {g_keep_skip.bytes, g_keep_skip.len, g_keep_skip.reason, g_keep_skip.when};
}

static void set_tele_reason(std::string_view reason)
{
    const std::size_t n = (reason.size() < sizeof(g_tele_last_reason) - 1)
                              ? reason.size()
                              : sizeof(g_tele_last_reason) - 1;
    std::memcpy(g_tele_last_reason, reason.data(), n);
    g_tele_last_reason[n] = '\0';
}

static void note_tele_problem(const char* kind, std::string_view reason)
{
    set_tele_reason(reason);
    if (!g_tele_good_stretch) {
        return;
    }
    g_tele_good_stretch = false;
    g_tele_summary_ms = millis();
    char msg[192];
    std::snprintf(msg, sizeof(msg), "%s: %.*s", kind, static_cast<int>(reason.size()),
                  reason.data());
    log_msg(p1log::Level::Warn, msg);
}

static void service_telegram_log()
{
    if (g_tele_good_stretch) {
        return;
    }
    const std::uint32_t now = millis();
    if (static_cast<std::int32_t>(now - g_tele_summary_ms) < 60000) {
        return;
    }
    g_tele_summary_ms = now;
    if (g_win_rej == 0 && g_win_skip == 0) {
        g_tele_good_stretch = true;
        g_win_ok = 0;
        return;
    }
    char last[44];
    const std::size_t n = std::strlen(g_tele_last_reason);
    const std::size_t cut = (n < sizeof(last) - 1) ? n : sizeof(last) - 1;
    std::memcpy(last, g_tele_last_reason, cut);
    last[cut] = '\0';
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "Telegrams last minute: ok=%u rejected=%u skipped lines=%u; last: %s; "
                  "uart ovf=%u full=%u frm=%u",
                  g_win_ok, g_win_rej, g_win_skip, last, static_cast<unsigned>(g_ovf),
                  static_cast<unsigned>(g_full), static_cast<unsigned>(g_frm));
    log_msg(p1log::Level::Warn, msg);
    if (g_loop_max_ms != nullptr) {
        char loopmsg[48];
        std::snprintf(loopmsg, sizeof(loopmsg), "loop max=%u ms",
                      static_cast<unsigned>(*g_loop_max_ms));
        log_msg(p1log::Level::Debug, loopmsg);
    }
    g_win_ok = 0;
    g_win_rej = 0;
    g_win_skip = 0;
}

static void apply_uart_bytes(std::span<const std::uint8_t> bytes)
{
    const unsigned prev = g_ok + g_rejected;
    const auto stats = g_reader.feed(bytes, deliver);
    g_ok += static_cast<unsigned>(stats.ok);
    g_rejected += static_cast<unsigned>(stats.rejected);
    g_skipped += static_cast<unsigned>(stats.skipped);
    g_win_ok += static_cast<unsigned>(stats.ok);
    g_win_rej += static_cast<unsigned>(stats.rejected);
    g_win_skip += static_cast<unsigned>(stats.skipped);
    if (stats.skipped > 0) {
        keep_telegram(g_keep_skip, g_reader.last_with_skip(), g_reader.last_skipped_line());
        note_tele_problem("Telegram skipped line", g_reader.last_skipped_line());
    }
    if (stats.rejected > 0) {
        keep_telegram(g_keep_rej, g_reader.last_rejected(), g_reader.last_reject_reason());
        note_tele_problem("Telegram rejected", g_reader.last_reject_reason());
    }

    const unsigned now = g_ok + g_rejected;
    if (now > prev) {
        g_req.on_telegram(millis());
    }
    for (unsigned k = prev + 1; k <= now; ++k) {
        if (k % 10 == 0) {
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                          "S ok=%u rejected=%u heap=%u ovf=%u full=%u frm=%u", g_ok, g_rejected,
                          static_cast<unsigned>(ESP.getFreeHeap()),
                          static_cast<unsigned>(g_ovf), static_cast<unsigned>(g_full),
                          static_cast<unsigned>(g_frm));
            log_msg(p1log::Level::Debug, msg);
        }
    }
}

static void source_request(std::uint32_t trg_type, std::uint32_t trg_int, bool force)
{
    static std::uint32_t applied_type = 0xFFFFFFFFu;
    static std::uint32_t applied_int = 0xFFFFFFFFu;
    if (force || trg_type != applied_type || trg_int != applied_int) {
        g_req.configure(trg_type, trg_int, millis());
        applied_type = trg_type;
        applied_int = trg_int;
    }
    g_req.tick(millis());
    digitalWrite(kDataRequest, g_req.high ? HIGH : LOW);
}

// Increments only — this runs on uart_event_task (~2 KB stack).
static void on_uart_err(hardwareSerial_error_t err)
{
    switch (err) {
    case UART_FIFO_OVF_ERROR:
        g_ovf = g_ovf + 1;
        break;
    case UART_BUFFER_FULL_ERROR:
        g_full = g_full + 1;
        break;
    case UART_FRAME_ERROR:
        g_frm = g_frm + 1;
        break;
    default:
        break;
    }
}

static void board_begin()
{
    // GPIO 39 is input-only on ESP32-PICO-D4 — no internal pull-up. The Atom
    // board provides an external pull-up; pressed = LOW (M5Atom Button invert).
    pinMode(kBtn, INPUT);
}

static void led_write(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    rgbLedWrite(static_cast<std::uint8_t>(kLedPin), r, g, b);
}

static bool button_down()
{
    return digitalRead(kBtn) == LOW;
}

static void source_begin(p1app::Product::OnTelegram on_telegram, p1app::Product::LogFn log,
                         const std::uint32_t* loop_max_ms, p1app::Product::UartBringup& uart)
{
    g_on_telegram = on_telegram;
    g_log = log;
    g_loop_max_ms = loop_max_ms;

    pinMode(kDataRequest, OUTPUT);

    uart.heap_before = static_cast<unsigned>(ESP.getFreeHeap());
#if defined(P1_TEST_OLD_UART)
    const size_t rx = Serial1.setRxBufferSize(1024);
#else
    const size_t rx = Serial1.setRxBufferSize(p1dsmr::Reader::kBufferSize);
#endif
    uart.heap_after = static_cast<unsigned>(ESP.getFreeHeap());
    uart.rx_buf = static_cast<unsigned>(rx);
    Serial1.begin(115200, SERIAL_8N1, kP1Rx, kP1Tx);
    Serial1.onReceiveError(on_uart_err);
    // Match 2.23 (Arduino-esp32 2.0.14) on this chip: GPIO 21 as GPIO
    // function with pull-up, UART1 clocked from APB at 115201 baud, RX
    // timeout 3 symbols. Measured against a 2.0.14 sketch on the desk unit.
    PIN_FUNC_SELECT(IO_MUX_GPIO21_REG, PIN_FUNC_GPIO);
    PIN_INPUT_ENABLE(IO_MUX_GPIO21_REG);
    gpio_pullup_en(static_cast<gpio_num_t>(kP1Rx));
    gpio_pulldown_dis(static_cast<gpio_num_t>(kP1Rx));
    REG_SET_BIT(UART_CONF0_REG(1), UART_TICK_REF_ALWAYS_ON);
    {
        const std::uint32_t clk = static_cast<std::uint32_t>(getApbFrequency());
        const std::uint32_t clkdiv = (clk << 4) / 115200u;
        REG_WRITE(UART_CLKDIV_REG(1),
                  ((clkdiv & 0xFu) << UART_CLKDIV_FRAG_S) | (clkdiv >> 4));
    }
    Serial1.setRxTimeout(3);
}

static void source_drain()
{
#if defined(P1_TEST_OLD_UART)
    std::uint8_t buf[64];
    const size_t n = Serial1.read(buf, sizeof(buf));
    if (n > 0) {
        apply_uart_bytes(std::span<const std::uint8_t>(buf, n));
    }
#else
    std::uint8_t buf[256];
    std::size_t drained = 0;
    while (Serial1.available() > 0 && drained < p1dsmr::Reader::kBufferSize) {
        std::size_t want = sizeof(buf);
        const std::size_t room = p1dsmr::Reader::kBufferSize - drained;
        if (want > room) {
            want = room;
        }
        const size_t n = Serial1.read(buf, want);
        if (n == 0) {
            break;
        }
        drained += n;
        apply_uart_bytes(std::span<const std::uint8_t>(buf, n));
    }
#endif
    service_telegram_log();
}

static void note_fresh(std::uint32_t ms)
{
    g_last_telegram_ms = ms;
}

static void meter(p1app::Product::MeterSnap& out)
{
    out.ok = g_ok;
    out.rejected = g_rejected;
    out.skipped = g_skipped;
    out.ovf = static_cast<unsigned>(g_ovf);
    out.full = static_cast<unsigned>(g_full);
    out.frm = static_cast<unsigned>(g_frm);
    out.last_ms = g_last_telegram_ms;
    out.has_rejected = g_keep_rej.len > 0;
    out.has_skipped = g_keep_skip.len > 0;
}

static std::size_t no_diagnostics(p1app::Product::DiagPoint*, std::size_t)
{
    return 0;
}

static std::size_t no_status_lines(p1app::Product::StatusLine*, std::size_t)
{
    return 0;
}

static bool serial_cmd(char* line)
{
    if (std::memcmp(line, "hold ", 5) == 0) {
        // Desk helper: drop parsed telegrams so the meter indicator can go false
        // without unplugging (DATA_REQUEST is ignored by some sources).
        if (std::strcmp(line + 5, "on") == 0) {
            g_hold_p1 = true;
            Serial.println("hold on");
        } else if (std::strcmp(line + 5, "off") == 0) {
            g_hold_p1 = false;
            Serial.println("hold off");
        } else {
            Serial.println("usage: hold on|off");
        }
        return true;
    }
    if (std::memcmp(line, "req ", 4) == 0) {
        // Desk helper: drop DATA_REQUEST to stop meter telegrams (Fluvius continuous mode).
        if (std::strcmp(line + 4, "on") == 0) {
            digitalWrite(kDataRequest, HIGH);
            Serial.println("req on");
        } else if (std::strcmp(line + 4, "off") == 0) {
            digitalWrite(kDataRequest, LOW);
            Serial.println("req off");
        } else {
            Serial.println("usage: req on|off");
        }
        return true;
    }
    return false;
}

static const p1app::Product kProduct = {
    "P1 dongle",
    true,
    true,
    true,
    true,
    p1web::kApSsid,
    p1fw::kFwVer,
    p1fw::kFwVerNum,
    p1pub::kOriginName,
    p1pub::kMeterModel,
    p1pub::kDongleModel,
    no_diagnostics,
    no_status_lines,
    board_begin,
    led_write,
    button_down,
    source_begin,
    source_request,
    source_drain,
    note_fresh,
    meter,
    dump_rejected,
    dump_skipped,
    serial_cmd,
};

const p1app::Product& dongle_product()
{
    return kProduct;
}
