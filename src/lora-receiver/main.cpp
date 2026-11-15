// P1 LoRa receiver. Locked mode only: decode a reading and ACK it
// unless this radio's own duty budget would be exceeded.
//
// User settings. An empty pair key does not transmit (and so sends no ACK).
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
constexpr int kLed = 25;
constexpr uint32_t kSummaryMs = 60000;

const p1lora::RadioPins kPins{5, 19, 27, 18, 26, 23, 33};

p1lora::Radio g_radio(kPins);
p1lora::Duty g_duty(p1lora::profile(p1lora::ProfileId::A).duty_ms_per_hour);
p1lora::Key g_key;
RTC_NOINIT_ATTR p1lora::DutyImage g_duty_image;

uint32_t g_duty_origin = 0;
uint8_t g_step = kLockedStep;
uint8_t g_tx_counter = 0;
bool g_have_last = false;
uint8_t g_last_counter = 0;
uint32_t g_frames = 0;
uint32_t g_lost = 0;
uint32_t g_network = 0;
uint32_t g_key_fail = 0;
uint32_t g_length = 0;
uint32_t g_crc = 0;
uint32_t g_type = 0;
uint32_t g_version = 0;
uint32_t g_ack_sent = 0;
uint32_t g_ack_skip = 0;
uint32_t g_led_off = 0;
uint32_t g_summary = 0;
uint32_t g_nag = 0;
bool g_radio_up = false;
bool g_have_key = false;

uint32_t duty_clock()
{
    const uint32_t ms = millis();
    if (g_duty_origin > 0xFFFFFFFFu - ms) {
        return 0xFFFFFFFFu;
    }
    return ms + g_duty_origin;
}

void print_pct_of(uint32_t used_us, uint32_t budget_us)
{
    const uint32_t tenths =
        budget_us == 0 ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(used_us) * 1000ull) / budget_us);
    Serial.printf("%u.%u%%", tenths / 10u, tenths % 10u);
}

void print_duty()
{
    const uint32_t budget = p1lora::profile(p1lora::ProfileId::A).duty_ms_per_hour * 1000u;
    print_pct_of(g_duty.charged_us(duty_clock()), budget);
}

void print_loss_pct()
{
    const uint32_t total = g_frames + g_lost;
    const uint32_t tenths = total == 0 ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(g_lost) * 1000ull) / total);
    Serial.printf("%u.%u%%", tenths / 10u, tenths % 10u);
}

void print_quarters(float snr_db)
{
    int q = static_cast<int>(snr_db * 4.0f + (snr_db >= 0 ? 0.5f : -0.5f));
    if (q > 127) {
        q = 127;
    }
    if (q < -128) {
        q = -128;
    }
    int v = q;
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
        print_duty();
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

void print_summary()
{
    Serial.printf("rx summary frames=%u loss=", g_frames);
    print_loss_pct();
    Serial.printf(" network=%u key=%u length=%u crc=%u type=%u version=%u ack_sent=%u ack_skip=%u duty=", g_network,
                  g_key_fail, g_length, g_crc, g_type, g_version, g_ack_sent, g_ack_skip);
    print_duty();
    Serial.println();
}

void apply_step(uint8_t step)
{
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
    if (step >= profile.step_count || !g_radio.configure(profile, step, kPowerDbm) || !g_radio.listen()) {
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

int8_t snr_quarters(float snr_db)
{
    int q = static_cast<int>(snr_db * 4.0f + (snr_db >= 0 ? 0.5f : -0.5f));
    if (q > 127) {
        q = 127;
    }
    if (q < -128) {
        q = -128;
    }
    return static_cast<int8_t>(q);
}

bool send_ack(uint8_t frame_counter, float snr_db)
{
    const uint32_t air_us = p1lora::airtime_us(p1lora::ProfileId::A, g_step, 8);
    if (!g_duty.can_send(duty_clock(), air_us)) {
        ++g_ack_skip;
        return false;
    }
    p1lora::Ack ack;
    ack.counter = frame_counter;
    ack.flags = 0;
    ack.snr = snr_quarters(snr_db);
    ack.command = false;
    uint8_t frame[16];
    size_t len = 0;
    if (p1lora::pack_ack(g_key, g_tx_counter, ack, frame, sizeof frame, len) != p1lora::Status::Ok) {
        ++g_ack_skip;
        return false;
    }
    uint32_t measured = 0;
    if (!g_radio.send(frame, len, measured)) {
        Serial.printf("ack send failed %d\n", g_radio.last_status());
        g_radio.listen();
        ++g_ack_skip;
        return false;
    }
    const uint32_t charge = measured > air_us ? measured : air_us;
    g_duty.record(duty_clock(), charge);
    remember_duty();
    ++g_tx_counter;
    ++g_ack_sent;
    g_radio.listen();
    return true;
}

void on_packet(const uint8_t* buf, size_t len, float rssi, float snr)
{
    const p1lora::Decoded decoded = p1lora::decode(g_key, buf, len);
    if (decoded.status != p1lora::Status::Ok) {
        const char* why = "length";
        if (decoded.status == p1lora::Status::WrongNetwork) {
            ++g_network;
            why = "network";
        } else if (decoded.status == p1lora::Status::KeyCheck) {
            ++g_key_fail;
            why = "key";
        } else if (decoded.status == p1lora::Status::UnknownType) {
            ++g_type;
            why = "type";
        } else if (decoded.status == p1lora::Status::UnknownVersion) {
            ++g_version;
            why = "version";
        } else {
            ++g_length;
        }
        Serial.printf("rx reject %s\n", why);
        return;
    }
    if (decoded.kind != p1lora::Kind::Reading) {
        ++g_type;
        Serial.println("rx reject type");
        return;
    }
    flash_led();
    uint8_t loss = 0;
    bool restart = false;
    if (g_have_last) {
        const p1lora::Gap gap = p1lora::counter_gap(g_last_counter, decoded.counter);
        restart = gap.restart;
        loss = gap.loss;
        if (!restart) {
            g_lost += loss;
        }
    }
    g_have_last = true;
    g_last_counter = decoded.counter;
    ++g_frames;
    const bool acked = send_ack(decoded.counter, snr);
    Serial.printf("rx #%u 0x%02X rssi=%d snr=", decoded.counter, decoded.type, static_cast<int>(rssi));
    print_quarters(snr);
    Serial.print(" key=ok loss=");
    if (restart) {
        Serial.print("restart");
    } else {
        Serial.printf("%u", loss);
    }
    Serial.print(" total=");
    print_loss_pct();
    Serial.printf(" ack=%s t1=%u\n", acked ? "sent" : "skipped", decoded.reading.t1_wh);
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    pinMode(kLed, OUTPUT);
    digitalWrite(kLed, LOW);
    const p1lora::Profile& profile = p1lora::profile(p1lora::ProfileId::A);
    Serial.println("lora-receiver");
    Serial.printf("step %u SF%u power %d dBm sync 0x%02X\n", kLockedStep, profile.steps[kLockedStep].sf, kPowerDbm,
                  profile.sync_word);
    load_duty();
    g_summary = millis();
    g_have_key = p1lora::parse_key(kPairKey, sizeof kPairKey - 1, g_key) == p1lora::KeyStatus::Ok;
    if (!g_have_key) {
        Serial.println("no pair key set");
        return;
    }
    if (kLockedStep >= profile.step_count || !g_radio.begin(profile, kLockedStep, kPowerDbm) || !g_radio.listen()) {
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
    if (g_radio_up) {
        uint8_t buf[255];
        size_t len = 0;
        float rssi = 0;
        float snr = 0;
        const p1lora::Rx got = g_radio.poll(buf, sizeof buf, len, rssi, snr);
        if (got == p1lora::Rx::Crc) {
            ++g_crc;
            Serial.println("rx reject crc");
        } else if (got == p1lora::Rx::Packet) {
            on_packet(buf, len, rssi, snr);
        }
    }
    if (millis() - g_summary >= kSummaryMs) {
        print_summary();
        g_summary = millis();
    }
}
