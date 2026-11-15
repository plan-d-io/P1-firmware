// P1 LoRa transmitter. Locked mode only: a three-phase test reading,
// paced to the steady interval, with ACKs listened for after each send.
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

#include <P1LoRa.h>
#include <P1LoRaRadio.h>

#include <esp_attr.h>
#include <esp_system.h>

#include <cstdlib>
#include <cstring>

namespace {

constexpr char kPairKey[] = P1_PAIR_KEY;
#ifdef P1_DESK_TEST
static_assert(sizeof(kPairKey) == 33, "desk pair key must be 32 hex characters");
#endif
constexpr int8_t kPowerDbm = P1_TX_DBM;
constexpr uint8_t kLockedStep = P1_LOCKED_STEP;
constexpr uint32_t kAckMarginMs = 100;
constexpr int kLed = 25;

const p1lora::RadioPins kPins{5, 19, 27, 18, 26, 23, 33};

p1lora::Radio g_radio(kPins);
p1lora::Duty g_duty(p1lora::profile(p1lora::ProfileId::A).duty_ms_per_hour);
p1lora::Key g_key;
p1lora::Optionals g_opt;
RTC_NOINIT_ATTR p1lora::DutyImage g_duty_image;

uint32_t g_duty_origin = 0;
uint8_t g_step = kLockedStep;
uint32_t g_seq = 0;
uint32_t g_gas = 0;
uint32_t g_last_send = 0;
bool g_sent_once = false;
uint32_t g_led_off = 0;
bool g_radio_up = false;
bool g_have_key = false;
uint32_t g_nag = 0;

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

p1lora::Reading make_reading(uint32_t seq)
{
    p1lora::Reading reading;
    reading.three_phase = true;
    reading.flags = static_cast<uint8_t>(p1lora::kFlagTariff2 | p1lora::kFlagGas | p1lora::kFlagPeak);
    reading.t1_wh = 182077u + seq;
    reading.t2_wh = 199084;
    reading.inj1_wh = 23;
    reading.inj2_wh = 134;
    reading.power_w = static_cast<uint16_t>(289 + (seq % 30u));
    reading.inject_w = 0;
    reading.demand_w = 260;
    reading.p_plus_w[0] = 75;
    reading.p_plus_w[1] = 15;
    reading.p_plus_w[2] = 198;
    reading.p_minus_w[0] = reading.p_minus_w[1] = reading.p_minus_w[2] = 0;
    reading.v_dv[0] = 2421;
    reading.v_dv[1] = 2417;
    reading.v_dv[2] = 2403;
    reading.i_ca[0] = 53;
    reading.i_ca[1] = 7;
    reading.i_ca[2] = 97;
    if ((seq % 20u) == 0) {
        g_gas = 1000u + seq / 20u;
    }
    reading.gas_dm3 = g_gas;
    reading.peak_time = 0xAC6DB4C0u;
    reading.peak_w = 2396;
    return reading;
}

bool take_ack(uint8_t counter, int8_t& snr_q)
{
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
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
            (void)profile;
            return true;
        }
    }
    return false;
}

void send_once()
{
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
    const p1lora::Reading reading = make_reading(g_seq);
    g_opt.observe(true, reading.gas_dm3, true, reading.peak_time, reading.peak_w);
    uint8_t frame[p1lora::kMaxFrame];
    size_t len = 0;
    const uint8_t counter = static_cast<uint8_t>(g_seq);
    if (p1lora::pack_reading(g_key, reading, counter, frame, sizeof frame, len) != p1lora::Status::Ok) {
        Serial.println("tx pack failed");
        return;
    }
    const uint32_t air_us = p1lora::airtime_us(p1lora::ProfileId::A, g_step, static_cast<uint16_t>(len));
    const uint32_t now = duty_clock();
    if (!g_duty.can_send(now, air_us)) {
        return;
    }
    uint32_t measured = 0;
    flash_led();
    if (!g_radio.send(frame, len, measured)) {
        Serial.printf("tx send failed %d\n", g_radio.last_status());
        return;
    }
    const uint32_t charge = measured > air_us ? measured : air_us;
    g_duty.record(duty_clock(), charge);
    remember_duty();
    g_opt.sent(counter, true, true);
    int8_t snr_q = 0;
    const bool acked = take_ack(counter, snr_q);
    if (acked) {
        g_opt.ack(counter);
    }
    Serial.printf("tx #%u step%u SF%u %uB air=%ums meas=%ums duty=", counter, g_step, profile.steps[g_step].sf,
                  static_cast<unsigned>(len), (air_us + 500u) / 1000u, (measured + 500u) / 1000u);
    print_pct(g_duty.charged_us(duty_clock()));
    Serial.print(acked ? " ack=ok snr_at_rx=" : " ack=none");
    if (acked) {
        print_quarters(snr_q);
    }
    Serial.printf(" gas=%s\n", g_opt.gas_pending() ? "pending" : "clear");
    g_last_send = millis();
    g_sent_once = true;
    ++g_seq;
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
    if (!g_have_key) {
        if (millis() - g_nag >= 1000) {
            Serial.println("no pair key set");
            g_nag = millis();
        }
        return;
    }
    if (!g_radio_up) {
        return;
    }
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
    const uint32_t air_us = p1lora::airtime_us(p1lora::ProfileId::A, g_step, 69);
    const uint32_t interval = static_cast<uint32_t>(p1lora::reading_interval_tenths(air_us, profile.duty_ms_per_hour)) * 100u;
    if (g_sent_once && static_cast<uint32_t>(millis() - g_last_send) < interval) {
        return;
    }
    if (!g_duty.can_send(duty_clock(), air_us)) {
        return;
    }
    send_once();
}
