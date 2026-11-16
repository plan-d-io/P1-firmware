// P1 LoRa transmitter. Locked mode: read DSMR on GPIO 34 and send the latest
// values, paced to the steady interval, with ACKs listened for after each send.
// A telegram older than 10 s is not sent; a small status frame goes instead.
//
// User settings. An empty pair key does not transmit.
// Desk builds inject the key and the power with PLATFORMIO_BUILD_FLAGS.

#ifndef P1_PAIR_KEY
#define P1_PAIR_KEY ""
#endif
#ifndef P1_TX_DBM
#define P1_TX_DBM 17
#endif
#ifndef P1_LOCKED_STEP
#define P1_LOCKED_STEP 3
#endif

#include <Arduino.h>

#include <P1DSMR.h>
#include <P1LoRa.h>
#include <P1LoRaRadio.h>
#include <from_telegram.h>

#include <esp_attr.h>
#include <esp_system.h>
#include <soc/uart_reg.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {

constexpr char kPairKey[] = P1_PAIR_KEY;
#ifdef P1_DESK_TEST
static_assert(sizeof(kPairKey) == 33, "desk pair key must be 32 hex characters");
#endif
constexpr int8_t kPowerDbm = P1_TX_DBM;
constexpr uint8_t kLockedStep = P1_LOCKED_STEP;
constexpr uint32_t kAckMarginMs = 100;
constexpr uint32_t kSummaryMs = 60000;
constexpr uint32_t kSilenceMs = 10000;
constexpr int kLed = 25;
// Input-only. No internal pull: the interface board's 2.2 kΩ to 3.3 V is the pull-up.
constexpr int kP1Rx = 34;
constexpr int kP1Tx = -1;

const p1lora::RadioPins kPins{5, 19, 27, 18, 26, 23, 33};

p1lora::Radio g_radio(kPins);
p1lora::Duty g_duty(p1lora::profile(p1lora::ProfileId::A).duty_ms_per_hour);
p1lora::Key g_key;
p1lora::Optionals g_opt;
p1lora::MeterGate g_gate;
p1dsmr::Reader g_reader;
p1lora::Reading g_reading;
RTC_NOINIT_ATTR p1lora::DutyImage g_duty_image;

uint32_t g_duty_origin = 0;
uint8_t g_step = kLockedStep;
uint32_t g_seq = 0;
uint32_t g_led_off = 0;
bool g_radio_up = false;
bool g_have_key = false;
bool g_have_reading = false;
bool g_was_fresh = false;
bool g_silence_on = false;
uint32_t g_nag = 0;
uint32_t g_summary = 0;
uint32_t g_silence_ms = 0;
uint32_t g_next_reading_ms = 0;
uint16_t g_pace_len = 69;
uint32_t g_reading_gap_ms = 0;

uint32_t g_ok = 0;
uint32_t g_rejected = 0;
uint32_t g_skipped = 0;
uint32_t g_readings_sent = 0;
uint32_t g_status_sent = 0;
uint32_t g_stale_skips = 0;
volatile uint32_t g_ovf = 0;
volatile uint32_t g_full = 0;
volatile uint32_t g_frm = 0;
char g_reason[p1dsmr::Reader::kReasonMax] = {};

uint32_t duty_clock()
{
    const uint32_t ms = millis();
    if (g_duty_origin > 0xFFFFFFFFu - ms) {
        return 0xFFFFFFFFu;
    }
    return ms + g_duty_origin;
}

void print_pct(uint32_t used_us)
{
    const uint32_t budget = p1lora::profile(p1lora::ProfileId::A).duty_ms_per_hour * 1000u;
    const uint32_t tenths =
        budget == 0 ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(used_us) * 1000ull) / budget);
    Serial.printf("%u.%u%%", tenths / 10u, tenths % 10u);
}

void print_quarters(int8_t quarters)
{
    int v = quarters;
    const char* sign = "";
    if (v < 0) {
        sign = "-";
        v = -v;
    }
    Serial.printf("%s%d.%02d", sign, v / 4, (v % 4) * 25);
}

const char* reset_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
        return "task-watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_BROWNOUT:
        return "brownout";
    default:
        return "other";
    }
}

void load_duty()
{
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool power = reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
    Serial.printf("duty ");
    if (!power && g_duty.restore(g_duty_image)) {
        g_duty_origin = g_duty_image.now_ms;
        Serial.print("restored ");
        print_pct(g_duty.charged_us(duty_clock()));
        Serial.printf(" (%s)\n", reset_name(reason));
        return;
    }
    Serial.printf("empty (%s)\n", reset_name(reason));
}

void remember_duty()
{
    g_duty.save(g_duty_image, duty_clock());
}

void flash_led()
{
    digitalWrite(kLed, HIGH);
    g_led_off = millis() + 40;
}

void poll_led()
{
    if (g_led_off != 0 && static_cast<int32_t>(millis() - g_led_off) >= 0) {
        digitalWrite(kLed, LOW);
        g_led_off = 0;
    }
}

void recompute_gap()
{
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
    const uint32_t air = p1lora::airtime_us(p1lora::ProfileId::A, g_step, g_pace_len);
    g_reading_gap_ms = static_cast<uint32_t>(p1lora::reading_interval_tenths(air, profile.duty_ms_per_hour)) * 100u;
}

int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void apply_step(uint8_t step)
{
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
    if (step >= profile.step_count || !g_radio.configure(profile, step, kPowerDbm)) {
        Serial.println("step rejected");
        return;
    }
    g_step = step;
    recompute_gap();
    Serial.printf("step %u SF%u\n", step, profile.steps[step].sf);
}

#ifdef P1_DESK_TEST
void handle_line(char* line)
{
    if (std::strncmp(line, "step ", 5) == 0) {
        apply_step(static_cast<uint8_t>(atoi(line + 5)));
        return;
    }
    if (std::strcmp(line, "restart") == 0) {
        remember_duty();
        Serial.println("restart");
        Serial.flush();
        ESP.restart();
    }
    if (std::strncmp(line, "sync ", 5) == 0) {
        const char* hex = line + 5;
        if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
            hex += 2;
        }
        const int hi = hex_nibble(hex[0]);
        int value = hi;
        if (hex[1] != '\0') {
            const int lo = hex_nibble(hex[1]);
            if (hi < 0 || lo < 0 || hex[2] != '\0') {
                Serial.println("sync rejected");
                return;
            }
            value = (hi << 4) | lo;
        } else if (hi < 0) {
            Serial.println("sync rejected");
            return;
        }
        if (!g_radio.set_sync_word(static_cast<uint8_t>(value))) {
            Serial.println("sync rejected");
            return;
        }
        Serial.printf("sync 0x%02X\n", value);
    }
}

void poll_commands()
{
    static char line[24];
    static size_t used = 0;
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (used + 1 < sizeof line) {
                line[used++] = c;
            }
            continue;
        }
        line[used] = '\0';
        used = 0;
        handle_line(line);
    }
}
#else
void poll_commands() {}
#endif

void keep_reason(std::string_view reason)
{
    std::size_t n = reason.size();
    if (n >= sizeof g_reason) {
        n = sizeof g_reason - 1;
    }
    if (n == 0) {
        g_reason[0] = '\0';
        return;
    }
    std::memcpy(g_reason, reason.data(), n);
    g_reason[n] = '\0';
}

void on_uart_err(hardwareSerial_error_t err)
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

void on_telegram(const p1dsmr::Data& data)
{
    g_reading = p1lora::from_telegram(data);
    g_have_reading = true;
    const bool gas = (g_reading.flags & p1lora::kFlagGas) != 0;
    const bool peak = (g_reading.flags & p1lora::kFlagPeak) != 0;
    g_opt.observe(gas, g_reading.gas_dm3, peak, g_reading.peak_time, g_reading.peak_w);
    g_gate.note_valid(millis());
}

void drain_p1()
{
    std::uint8_t buf[256];
    std::size_t drained = 0;
    while (Serial1.available() > 0 && drained < p1dsmr::Reader::kBufferSize) {
        std::size_t want = sizeof buf;
        const std::size_t room = p1dsmr::Reader::kBufferSize - drained;
        if (want > room) {
            want = room;
        }
        const size_t n = Serial1.read(buf, want);
        if (n == 0) {
            break;
        }
        drained += n;
        for (size_t i = 0; i < n; ++i) {
            const auto st = g_reader.feed(buf[i]);
            if (st == p1dsmr::Reader::Status::Ok) {
                ++g_ok;
                g_skipped += static_cast<uint32_t>(g_reader.last_skipped_count());
                on_telegram(g_reader.data());
            } else if (st == p1dsmr::Reader::Status::Rejected) {
                ++g_rejected;
                keep_reason(g_reader.last_reject_reason());
                g_gate.note_reject(millis());
            }
        }
    }
}

bool reading_due(uint32_t now)
{
    return g_next_reading_ms == 0 || static_cast<int32_t>(now - g_next_reading_ms) >= 0;
}

void arm_reading(uint32_t now)
{
    g_next_reading_ms = now + g_reading_gap_ms;
}

struct AirResult {
    bool acked = false;
    int8_t snr_q = 0;
    uint32_t air_us = 0;
    uint32_t measured = 0;
};

bool take_ack(uint8_t counter, int8_t& snr_q)
{
    const uint32_t ack_us = p1lora::airtime_us(p1lora::ProfileId::A, g_step, 8);
    const uint32_t window_ms = (ack_us + 500u) / 1000u + kAckMarginMs;
    if (!g_radio.listen()) {
        return false;
    }
    const uint32_t started = millis();
    uint8_t buf[255];
    while (static_cast<uint32_t>(millis() - started) < window_ms) {
        poll_commands();
        poll_led();
        drain_p1();
        size_t len = 0;
        float rssi = 0;
        float snr = 0;
        const p1lora::Rx got = g_radio.poll(buf, sizeof buf, len, rssi, snr);
        if (got != p1lora::Rx::Packet) {
            continue;
        }
        const p1lora::Decoded decoded = p1lora::decode(g_key, buf, len);
        if (decoded.status == p1lora::Status::Ok && decoded.kind == p1lora::Kind::Ack &&
            decoded.ack.counter == counter) {
            snr_q = decoded.ack.snr;
            return true;
        }
    }
    return false;
}

bool radio_send(const uint8_t* frame, size_t len, AirResult& out)
{
    out.air_us = p1lora::airtime_us(p1lora::ProfileId::A, g_step, static_cast<uint16_t>(len));
    if (!g_duty.can_send(duty_clock(), out.air_us)) {
        return false;
    }
    flash_led();
    if (!g_radio.send(frame, len, out.measured)) {
        Serial.printf("tx send failed %d\n", g_radio.last_status());
        return false;
    }
    const uint32_t charge = out.measured > out.air_us ? out.measured : out.air_us;
    g_duty.record(duty_clock(), charge);
    remember_duty();
    return true;
}

void print_air(uint8_t counter, size_t len, const AirResult& air, bool acked_known)
{
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
    Serial.printf("tx #%u step%u SF%u %uB air=%ums meas=%ums duty=", counter, g_step, profile.steps[g_step].sf,
                  static_cast<unsigned>(len), (air.air_us + 500u) / 1000u, (air.measured + 500u) / 1000u);
    print_pct(g_duty.charged_us(duty_clock()));
    if (!acked_known) {
        return;
    }
    Serial.print(air.acked ? " ack=ok snr_at_rx=" : " ack=none");
    if (air.acked) {
        print_quarters(air.snr_q);
    }
}

void print_u32(const char* name, uint32_t value, bool absent)
{
    Serial.print(' ');
    Serial.print(name);
    Serial.print('=');
    if (absent) {
        Serial.print('-');
    } else {
        Serial.print(value);
    }
}

void print_u16(const char* name, uint16_t value)
{
    print_u32(name, value, value == p1lora::kAbsent16);
}

void print_sent_values(const p1lora::Reading& reading)
{
    print_u32("t1", reading.t1_wh, reading.t1_wh == p1lora::kAbsent32);
    print_u32("t2", reading.t2_wh, reading.t2_wh == p1lora::kAbsent32);
    print_u32("inj1", reading.inj1_wh, reading.inj1_wh == p1lora::kAbsent32);
    print_u32("inj2", reading.inj2_wh, reading.inj2_wh == p1lora::kAbsent32);
    print_u16("p", reading.power_w);
    print_u16("inj", reading.inject_w);
    print_u16("dem", reading.demand_w);
    print_u16("v1", reading.v_dv[0]);
    print_u16("v2", reading.v_dv[1]);
    print_u16("v3", reading.v_dv[2]);
    print_u16("i1", reading.i_ca[0]);
    print_u16("i2", reading.i_ca[1]);
    print_u16("i3", reading.i_ca[2]);
    if ((reading.flags & p1lora::kFlagGas) != 0) {
        print_u32("gas", reading.gas_dm3, false);
    } else {
        Serial.print(" gas=-");
    }
    if ((reading.flags & p1lora::kFlagPeak) != 0) {
        print_u16("peak", reading.peak_w);
        print_u32("peak_t", reading.peak_time, false);
    } else {
        Serial.print(" peak=-");
    }
    Serial.printf(" tariff=%u", (reading.flags & p1lora::kFlagTariff2) != 0 ? 2u : 1u);
}

void send_reading()
{
    p1lora::Reading reading = g_reading;
    const bool gas = g_opt.gas_pending() && (reading.flags & p1lora::kFlagGas) != 0;
    const bool peak = g_opt.peak_pending() && (reading.flags & p1lora::kFlagPeak) != 0;
    if (!gas) {
        reading.flags = static_cast<uint8_t>(reading.flags & ~p1lora::kFlagGas);
    }
    if (!peak) {
        reading.flags = static_cast<uint8_t>(reading.flags & ~p1lora::kFlagPeak);
    }
    uint8_t frame[p1lora::kMaxFrame];
    size_t len = 0;
    const uint8_t counter = static_cast<uint8_t>(g_seq);
    if (p1lora::pack_reading(g_key, reading, counter, frame, sizeof frame, len) != p1lora::Status::Ok) {
        Serial.println("tx pack failed");
        return;
    }
    AirResult air;
    if (!radio_send(frame, len, air)) {
        return;
    }
    // Record the generation before the ACK window. A telegram that arrives
    // while listening must not be the one this frame is credited with.
    g_opt.sent(counter, gas, peak);
    air.acked = take_ack(counter, air.snr_q);
    if (air.acked) {
        g_opt.ack(counter);
    }
    g_pace_len = static_cast<uint16_t>(len);
    recompute_gap();
    arm_reading(millis());
    ++g_readings_sent;
    ++g_seq;
    print_air(counter, len, air, true);
    Serial.printf(" gas=%s peak=%s", g_opt.gas_pending() ? "pending" : "clear",
                  g_opt.peak_pending() ? "pending" : "clear");
    print_sent_values(reading);
    Serial.println();
}

void send_status(const p1lora::MeterStatus& meter)
{
    uint8_t frame[16];
    size_t len = 0;
    const uint8_t counter = static_cast<uint8_t>(g_seq);
    if (p1lora::pack_status(g_key, counter, meter, frame, sizeof frame, len) != p1lora::Status::Ok) {
        Serial.println("tx pack failed");
        return;
    }
    AirResult air;
    if (!radio_send(frame, len, air)) {
        return;
    }
    // Clear the reported reject count before the ACK window, so a reject
    // that arrives while listening is counted on the next status frame.
    g_gate.status_sent(millis());
    air.acked = take_ack(counter, air.snr_q);
    ++g_status_sent;
    ++g_seq;
    print_air(counter, len, air, true);
    Serial.print(" status reason=");
    Serial.print(meter.reason);
    Serial.print(" since=");
    if (meter.since_s == p1lora::kSinceNever) {
        Serial.print("never");
    } else {
        Serial.print(meter.since_s);
        Serial.print('s');
    }
    Serial.printf(" rejected=%u\n", meter.rejected);
}

void print_summary()
{
    Serial.printf("tx summary ok=%u rejected=%u reason=%s skipped=%u ovf=%u full=%u frm=%u readings=%u status=%u stale_skips=%u\n",
                  static_cast<unsigned>(g_ok), static_cast<unsigned>(g_rejected), g_reason[0] == '\0' ? "-" : g_reason,
                  static_cast<unsigned>(g_skipped), static_cast<unsigned>(g_ovf), static_cast<unsigned>(g_full),
                  static_cast<unsigned>(g_frm), static_cast<unsigned>(g_readings_sent), static_cast<unsigned>(g_status_sent),
                  static_cast<unsigned>(g_stale_skips));
}

void note_silence(uint32_t now, const p1lora::SendPlan& plan)
{
    if (plan.choice == p1lora::SendChoice::Reading) {
        g_silence_on = false;
        return;
    }
    if (g_silence_on && static_cast<uint32_t>(now - g_silence_ms) < kSilenceMs) {
        return;
    }
    Serial.print("no telegram for ");
    if (plan.meter.since_s == p1lora::kSinceNever) {
        Serial.println("never");
    } else {
        Serial.printf("%u s\n", static_cast<unsigned>(plan.meter.since_s));
    }
    g_silence_ms = now;
    g_silence_on = true;
}

void start_p1_uart()
{
    const size_t rx = Serial1.setRxBufferSize(p1dsmr::Reader::kBufferSize);
    Serial1.begin(115200, SERIAL_8N1, kP1Rx, kP1Tx);
    Serial1.onReceiveError(on_uart_err);
    // Same clock as the dongle (src/main.cpp): the 3.x core selects REF_TICK
    // at 115200, which runs fast. UART_TICK_REF_ALWAYS_ON selects APB, and the
    // divider is APB / 115200. GPIO 34 has no internal pull; do not enable one.
    // Serial1 is UART1, so these are the UART1 registers the dongle writes.
    REG_SET_BIT(UART_CONF0_REG(1), UART_TICK_REF_ALWAYS_ON);
    {
        const uint32_t clk = static_cast<uint32_t>(getApbFrequency());
        const uint32_t clkdiv = (clk << 4) / 115200u;
        REG_WRITE(UART_CLKDIV_REG(1), ((clkdiv & 0xFu) << UART_CLKDIV_FRAG_S) | (clkdiv >> 4));
    }
    Serial1.setRxTimeout(3);
    Serial.printf("p1 uart GPIO %d rx-only 115200 8N1 buf=%u apb\n", kP1Rx, static_cast<unsigned>(rx));
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    pinMode(kLed, OUTPUT);
    digitalWrite(kLed, LOW);
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
    Serial.println("lora-transmitter");
    Serial.printf("step %u SF%u power %d dBm sync 0x%02X\n", kLockedStep, profile.steps[kLockedStep].sf, kPowerDbm,
                  profile.sync_word);
    load_duty();
    g_summary = millis();
    recompute_gap();
    start_p1_uart();
    g_have_key = p1lora::parse_key(kPairKey, sizeof kPairKey - 1, g_key) == p1lora::KeyStatus::Ok;
    if (!g_have_key) {
        Serial.println("no pair key set");
        return;
    }
    if (kLockedStep >= profile.step_count || !g_radio.begin(profile, kLockedStep, kPowerDbm)) {
        Serial.printf("radio begin failed %d\n", g_radio.last_status());
        return;
    }
    g_step = kLockedStep;
    g_radio_up = true;
}

void loop()
{
    poll_led();
    poll_commands();
    drain_p1();
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - g_summary) >= kSummaryMs) {
        print_summary();
        g_summary = now;
    }
    if (!g_have_key) {
        if (now - g_nag >= 1000) {
            Serial.println("no pair key set");
            g_nag = now;
        }
        return;
    }
    if (!g_radio_up) {
        return;
    }
    const uint32_t status_ms = p1lora::status_interval_s(p1lora::ProfileId::A, g_step) * 1000u;
    const p1lora::SendPlan plan = g_gate.plan(now, status_ms);
    note_silence(now, plan);
    const bool fresh = plan.choice == p1lora::SendChoice::Reading && g_have_reading;
    if (fresh && !g_was_fresh) {
        g_next_reading_ms = 0;
    }
    g_was_fresh = fresh;
    if (fresh) {
        if (reading_due(now)) {
            send_reading();
        }
        return;
    }
    if (reading_due(now)) {
        ++g_stale_skips;
        arm_reading(now);
    }
    if (plan.choice == p1lora::SendChoice::Status) {
        send_status(plan.meter);
    }
}
