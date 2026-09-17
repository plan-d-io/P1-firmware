#include <Arduino.h>
#include <P1Config.h>
#include <P1DSMR.h>

#include <cstdarg>
#include <cstdio>
#include <span>

// Pins from archive/P1-dongle/P1-dongle.ino:
//   HWSERIAL.begin(115200, SERIAL_8N1, 21, 22);
//   TRIGGER 25, HIGH for continuous telegrams.
static constexpr int kP1Rx = 21;
static constexpr int kP1Tx = 22;
static constexpr int kDataRequest = 25;

static p1dsmr::Reader g_reader;
static p1cfg::NvsBackend g_nvs;
static p1cfg::Store g_cfg{g_nvs};
static unsigned g_ok = 0;
static unsigned g_rejected = 0;
static volatile unsigned g_ovf = 0;
static volatile unsigned g_full = 0;
static volatile unsigned g_frm = 0;
static char g_line[256];

static size_t append(size_t n, const char* fmt, ...)
{
    if (n >= sizeof(g_line)) {
        return sizeof(g_line);
    }
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(g_line + n, sizeof(g_line) - n, fmt, ap);
    va_end(ap);
    if (w < 0) {
        return n;
    }
    return n + static_cast<size_t>(w);
}

static size_t append_sv(size_t n, const char* key, bool present, std::string_view v)
{
    if (!present) {
        return n;
    }
    return append(n, " %s=%.*s", key, static_cast<int>(v.size()), v.data());
}

static size_t append_f(size_t n, const char* key, bool present, const dsmr_parser::FixedValue& v,
                      const char* fmt)
{
    if (!present) {
        return n;
    }
    n = append(n, " %s=", key);
    return append(n, fmt, static_cast<double>(v.val()));
}

static void print_telegram(const p1dsmr::Data& d)
{
    size_t n = append(0, "T");
    n = append_sv(n, "ts", d.timestamp_present, d.timestamp);
    n = append_f(n, "t1", d.energy_delivered_tariff1_present, d.energy_delivered_tariff1, "%.3f");
    n = append_f(n, "t2", d.energy_delivered_tariff2_present, d.energy_delivered_tariff2, "%.3f");
    n = append_f(n, "r1", d.energy_returned_tariff1_present, d.energy_returned_tariff1, "%.3f");
    n = append_f(n, "r2", d.energy_returned_tariff2_present, d.energy_returned_tariff2, "%.3f");
    n = append_sv(n, "tariff", d.electricity_tariff_present, d.electricity_tariff);
    n = append_f(n, "p", d.power_delivered_present, d.power_delivered, "%.3f");
    n = append_f(n, "pr", d.power_returned_present, d.power_returned, "%.3f");
    n = append_f(n, "l1", d.power_delivered_l1_present, d.power_delivered_l1, "%.3f");
    n = append_f(n, "l2", d.power_delivered_l2_present, d.power_delivered_l2, "%.3f");
    n = append_f(n, "l3", d.power_delivered_l3_present, d.power_delivered_l3, "%.3f");
    n = append_f(n, "v1", d.voltage_l1_present, d.voltage_l1, "%.1f");
    n = append_f(n, "v2", d.voltage_l2_present, d.voltage_l2, "%.1f");
    n = append_f(n, "v3", d.voltage_l3_present, d.voltage_l3, "%.1f");
    n = append_f(n, "i1", d.current_l1_present, d.current_l1, "%.2f");
    n = append_f(n, "i2", d.current_l2_present, d.current_l2, "%.2f");
    n = append_f(n, "i3", d.current_l3_present, d.current_l3, "%.2f");
    if (d.gas_delivered_be_present) {
        n = append_f(n, "gas", true, d.gas_delivered_be, "%.3f");
    } else {
        n = append_f(n, "gas", d.gas_delivered_present, d.gas_delivered, "%.3f");
    }
    if (n >= sizeof(g_line)) {
        n = sizeof(g_line) - 1;
    }
    g_line[n++] = '\n';
    Serial.write(reinterpret_cast<const uint8_t*>(g_line), n);
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

void setup()
{
    Serial.begin(115200);

    pinMode(kDataRequest, OUTPUT);
    digitalWrite(kDataRequest, HIGH);

    const size_t rx = Serial1.setRxBufferSize(1024);
    Serial1.begin(115200, SERIAL_8N1, kP1Rx, kP1Tx);
    Serial1.onReceiveError(on_uart_err);

    Serial.printf("P1 UART rx=%u\n", static_cast<unsigned>(rx));

    g_cfg.load();
    Serial.printf("P1Config ver=%u port=%u\n",
                  static_cast<unsigned>(g_cfg.values().cfg_ver),
                  static_cast<unsigned>(g_cfg.values().mqtt_port));
    if (g_cfg.values().mqtt_id[0] == '\0') {
        std::snprintf(g_cfg.values().mqtt_id, sizeof(g_cfg.values().mqtt_id), "p1cfg");
        g_cfg.save();
        Serial.println("P1Config probe saved MQTT_ID=p1cfg");
    } else {
        Serial.printf("P1Config MQTT_ID=%s\n", g_cfg.values().mqtt_id);
    }
}

void loop()
{
    uint8_t buf[64];
    const size_t n = Serial1.read(buf, sizeof(buf));

    if (n > 0) {
        const unsigned prev = g_ok + g_rejected;
        const auto stats =
            g_reader.feed(std::span<const uint8_t>(buf, n), print_telegram);
        g_ok += static_cast<unsigned>(stats.ok);
        g_rejected += static_cast<unsigned>(stats.rejected);

        const unsigned now = g_ok + g_rejected;
        for (unsigned k = prev + 1; k <= now; ++k) {
            if (k % 10 == 0) {
                Serial.printf("S ok=%u rejected=%u heap=%u ovf=%u full=%u frm=%u\n",
                              g_ok, g_rejected,
                              static_cast<unsigned>(ESP.getFreeHeap()),
                              static_cast<unsigned>(g_ovf),
                              static_cast<unsigned>(g_full),
                              static_cast<unsigned>(g_frm));
            }
        }
    }

    yield();
}
