// P1 LoRa receiver on the shared application. The radio task owns the
// SX1276: it waits on DIO0, decodes, and sends the ACK before the
// application loop hears about the frame. WiFi, MQTT and HTTP run in
// the loop and cannot hold an ACK.

#ifndef P1_TX_DBM
#define P1_TX_DBM 17
#endif

#include "../app/product.h"

#include <P1LoRa.h>
#include <P1LoRaRadio.h>
#include <link.h>
#include <to_data.h>

#include <Arduino.h>
#include <Preferences.h>

#include <bootloader_random.h>
#include <esp_attr.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

namespace {

constexpr int8_t kPowerDbm = P1_TX_DBM;

char g_key_text[33]{};
char g_mode[8]{};
char g_profile[4]{};
char g_step_text[4]{};
char g_fallback[8]{};
constexpr char kKeyConfirm[] = "The transmitter must be reflashed with the new key.";

bool accept_mode(char* value)
{
    if (std::strcmp(value, "auto") == 0 || std::strcmp(value, "locked") == 0) {
        return true;
    }
    return false;
}

bool accept_profile(char* value)
{
    if (value[0] == 'a' || value[0] == 'A') {
        value[0] = 'A';
        value[1] = '\0';
        return true;
    }
    if (value[0] == 'b' || value[0] == 'B') {
        value[0] = 'B';
        value[1] = '\0';
        return true;
    }
    return false;
}

bool accept_step(char* value)
{
    return value[0] >= '0' && value[0] <= '6' && value[1] == '\0';
}

bool accept_fallback(char* value)
{
    if (value[0] == '\0') {
        return false;
    }
    for (const char* p = value; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    const int n = std::atoi(value);
    if (n < 0 || n > 2550 || (n != 0 && n % 10 != 0)) {
        return false;
    }
    std::snprintf(value, 8, "%d", n);
    return true;
}

p1cfg::ExtraSetting g_settings[] = {
    {"LORA_KEY", "Pair key", p1cfg::CfgType::Str,
     p1cfg::CFG_IN_API | p1cfg::CFG_SECRET | p1cfg::CFG_REBOOT, g_key_text, sizeof(g_key_text), "",
     "LoRa", kKeyConfirm, p1lora::normalise_pair_key},
    {"LORA_MODE", "Mode", p1cfg::CfgType::Str, p1cfg::CFG_IN_API, g_mode, sizeof(g_mode), "auto",
     "LoRa", nullptr, accept_mode},
    {"LORA_PROFILE", "Profile", p1cfg::CfgType::Str, p1cfg::CFG_IN_API, g_profile, sizeof(g_profile),
     "A", "LoRa", nullptr, accept_profile},
    {"LORA_STEP", "Locked step", p1cfg::CfgType::Str, p1cfg::CFG_IN_API, g_step_text, sizeof(g_step_text),
     "0", "LoRa", nullptr, accept_step},
    {"LORA_FALLBACK", "Fallback minutes", p1cfg::CfgType::Str, p1cfg::CFG_IN_API, g_fallback,
     sizeof(g_fallback), "0", "LoRa", nullptr, accept_fallback},
};
constexpr int kLed = 25;
constexpr int kButton = 4;
constexpr int kRadioIrq = 26;
constexpr unsigned kQueueSlots = 4;
constexpr unsigned kLossWindow = 100;

const p1lora::RadioPins kPins{5, 19, 27, 18, kRadioIrq, 23, 33};

enum class HeardKind : std::uint8_t { Reading = 1, Status = 2 };

struct Heard {
    HeardKind kind;
    p1lora::Reading reading;
    p1lora::MeterStatus meter;
};

enum class LinkState : std::uint8_t { Waiting, Ok, NoData, Silent, NoKey };

p1lora::Radio g_radio(kPins);
p1lora::Duty g_duty(p1lora::profile(p1lora::ProfileId::A).duty_ms_per_hour);
p1lora::Key g_key;
RTC_NOINIT_ATTR p1lora::DutyImage g_duty_image;
alignas(p1lora::RxLink) unsigned char g_link_mem[sizeof(p1lora::RxLink)];
p1lora::RxLink* g_link = nullptr;

p1app::Product::OnTelegram g_on_telegram = nullptr;
p1app::Product::LogFn g_log = nullptr;

std::uint32_t g_duty_origin = 0;
std::atomic<std::uint8_t> g_step{0};
std::atomic<std::uint8_t> g_profile_id{0};
std::uint8_t g_tx_counter = 0;
p1lora::ProfileId g_on_profile = p1lora::ProfileId::A;
std::uint8_t g_on_step = 0;
bool g_have_last = false;
std::uint8_t g_last_counter = 0;
bool g_have_key = false;
bool g_radio_up = false;

std::atomic<std::uint32_t> g_frames{0};
std::atomic<std::uint32_t> g_readings{0};
std::atomic<std::uint32_t> g_network{0};
std::atomic<std::uint32_t> g_key_fail{0};
std::atomic<std::uint32_t> g_length{0};
std::atomic<std::uint32_t> g_last_ms{0};
std::atomic<std::uint8_t> g_last_kind{0};
std::atomic<std::int16_t> g_rssi{0};
std::atomic<std::int16_t> g_snr_centi{0};
std::atomic<std::uint8_t> g_loss_pct{0};
std::atomic<bool> g_have_signal{false};
std::atomic<std::uint8_t> g_reason{0};
std::atomic<std::uint16_t> g_since{p1lora::kSinceNever};
std::atomic<std::uint8_t> g_req{0};
std::atomic<std::uint8_t> g_view_id{0};
std::atomic<std::uint8_t> g_mode_id{0};
std::atomic<std::uint8_t> g_in_flight{0};
std::atomic<std::uint8_t> g_last_good{0};
std::atomic<int> g_snr_offset{0};
std::atomic<std::uint8_t> g_drop_pct{0};
std::atomic<std::uint16_t> g_dwell_min{0};
std::uint32_t g_drop_n = 0;

struct Ask {
    std::uint8_t mode = 0;
    std::uint8_t profile = 0;
    std::uint8_t step = 0;
    std::uint16_t fallback = 0;
};
Ask g_ask{};
std::atomic<std::uint8_t> g_ask_seq{0};
std::atomic<std::uint8_t> g_ask_result{0};
std::atomic<std::uint8_t> g_ask_seen{0};
char g_seen_mode[8]{};
char g_seen_profile[4]{};
char g_seen_step[4]{};
char g_seen_fallback[8]{};
bool g_form_ready = false;

std::uint8_t g_loss_ring[kLossWindow]{};
unsigned g_loss_pos = 0;
unsigned g_loss_fill = 0;

SemaphoreHandle_t g_rx_sem = nullptr;
StaticSemaphore_t g_rx_sem_buf;
QueueHandle_t g_heard = nullptr;
StaticQueue_t g_heard_buf;
std::uint8_t g_heard_storage[kQueueSlots * sizeof(Heard)];
StackType_t g_radio_stack[8192];
StaticTask_t g_radio_tcb;

bool g_have_gas = false;
std::uint32_t g_gas_dm3 = 0;
char g_gas_ts[14]{};
bool g_have_peak = false;
std::uint16_t g_peak_w = 0;
std::uint32_t g_peak_time = 0;
char g_reading_ts[14]{};
LinkState g_logged = LinkState::Waiting;
unsigned g_ok_count = 0;

std::uint32_t duty_clock()
{
    const std::uint32_t ms = millis();
    if (g_duty_origin > 0xffffffffu - ms) {
        return 0xffffffffu;
    }
    return ms + g_duty_origin;
}

void remember_duty()
{
    g_duty.save(g_duty_image, duty_clock());
}

void load_duty()
{
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool power = reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
    if (!power && g_duty.restore(g_duty_image)) {
        g_duty_origin = g_duty_image.now_ms;
        if (g_log != nullptr) {
            g_log(p1log::Level::Info, "duty restored");
        }
        return;
    }
    if (g_log != nullptr) {
        g_log(p1log::Level::Info, "duty empty");
    }
}

void note_loss_slot(bool got)
{
    g_loss_ring[g_loss_pos] = got ? 1 : 0;
    g_loss_pos = (g_loss_pos + 1) % kLossWindow;
    if (g_loss_fill < kLossWindow) {
        ++g_loss_fill;
    }
    unsigned ones = 0;
    for (unsigned i = 0; i < g_loss_fill; ++i) {
        ones += g_loss_ring[i];
    }
    const unsigned missed = g_loss_fill - ones;
    g_loss_pct.store(static_cast<std::uint8_t>((missed * 100u) / g_loss_fill));
}

int snr_quarters(float snr_db)
{
    int q = static_cast<int>(snr_db * 4.0f + (snr_db >= 0 ? 0.5f : -0.5f));
    if (q > 32767) {
        q = 32767;
    }
    if (q < -32768) {
        q = -32768;
    }
    return q;
}

const char* view_name(p1lora::LinkView view)
{
    switch (view) {
    case p1lora::LinkView::Search:
        return "search";
    case p1lora::LinkView::Confirming:
        return "confirming";
    case p1lora::LinkView::Switching:
        return "switching";
    case p1lora::LinkView::Locked:
        return "locked";
    case p1lora::LinkView::LockLost:
        return "lock lost";
    case p1lora::LinkView::Run:
        break;
    }
    return "run";
}

void format_step(char* out, std::size_t cap, p1lora::ProfileId id, std::uint8_t step)
{
    const p1lora::Profile& profile = p1lora::profile(id);
    if (step >= profile.step_count) {
        std::snprintf(out, cap, "-");
        return;
    }
    std::snprintf(out, cap, "SF%u/%lu", profile.steps[step].sf,
                  static_cast<unsigned long>(profile.steps[step].bw_hz / 1000u));
}

bool load_link(p1lora::LinkSettings& out)
{
    Preferences prefs;
    if (!prefs.begin("p1link", true)) {
        return false;
    }
    p1lora::LinkImage image{};
    const std::size_t n = prefs.getBytes("set", &image, sizeof image);
    prefs.end();
    if (n != sizeof image) {
        return false;
    }
    return p1lora::link_image_load(image, out);
}

void store_link(const p1lora::LinkSettings& settings)
{
    p1lora::LinkImage image{};
    if (!p1lora::link_image_store(settings, image)) {
        return;
    }
    Preferences prefs;
    if (!prefs.begin("p1link", false)) {
        return;
    }
    prefs.putBytes("set", &image, sizeof image);
    prefs.end();
}

void clear_link_store()
{
    Preferences prefs;
    if (!prefs.begin("p1link", false)) {
        return;
    }
    prefs.remove("set");
    prefs.end();
}

void persist_link()
{
    if (g_link != nullptr && g_link->take_store()) {
        store_link(g_link->settings());
    }
}

void publish_link_snap()
{
    if (g_link == nullptr) {
        return;
    }
    g_view_id.store(static_cast<std::uint8_t>(g_link->view()));
    g_mode_id.store(static_cast<std::uint8_t>(g_link->mode()));
    g_profile_id.store(static_cast<std::uint8_t>(g_link->profile()));
    g_step.store(g_link->step());
    g_last_good.store(g_link->settings().last_good_step);
    g_in_flight.store(g_link->in_flight() ? 1 : 0);
}

void note_link_line()
{
    if (g_link == nullptr) {
        return;
    }
    static p1lora::LinkView last_view = p1lora::LinkView::Search;
    static p1lora::ProfileId last_profile = p1lora::ProfileId::A;
    static std::uint8_t last_step = 0xFF;
    static bool have = false;
    const p1lora::LinkView view = g_link->view();
    const p1lora::ProfileId id = g_link->profile();
    const std::uint8_t step = g_link->step();
    if (have && view == last_view && id == last_profile && step == last_step) {
        publish_link_snap();
        return;
    }
    have = true;
    last_view = view;
    last_profile = id;
    last_step = step;
    char step_txt[16];
    format_step(step_txt, sizeof step_txt, id, step);
    Serial.printf("link %s %c step %u %s%s\n", view_name(view), id == p1lora::ProfileId::B ? 'B' : 'A', step,
                  step_txt, g_link->in_flight() ? " pending" : "");
    publish_link_snap();
}

void follow_radio()
{
    if (g_link == nullptr || !g_radio_up) {
        return;
    }
    const p1lora::ProfileId id = g_link->profile();
    const std::uint8_t step = g_link->step();
    if (id == g_on_profile && step == g_on_step) {
        return;
    }
    const p1lora::Profile& profile = p1lora::profile(id);
    if (step >= profile.step_count || !g_radio.configure(profile, step, kPowerDbm)) {
        Serial.println("retune failed");
        return;
    }
    g_on_profile = id;
    g_on_step = step;
    g_radio.listen();
    note_link_line();
}

void charge_sent(std::uint32_t linked_us, std::uint32_t radio_us, std::uint32_t measured)
{
    const std::uint32_t need = radio_us > measured ? radio_us : measured;
    if (need > linked_us) {
        g_duty.record(duty_clock(), need - linked_us);
    }
    remember_duty();
}

bool deliver(const p1lora::AirAction& action)
{
    if (action.kind != p1lora::AirKind::Ack && action.kind != p1lora::AirKind::Offer &&
        action.kind != p1lora::AirKind::Confirm) {
        return false;
    }
    std::uint8_t frame[16];
    std::size_t len = 0;
    p1lora::Status packed = p1lora::Status::WrongLength;
    if (action.kind == p1lora::AirKind::Ack) {
        packed = p1lora::pack_ack(g_key, g_tx_counter, action.ack, frame, sizeof frame, len);
    } else if (action.kind == p1lora::AirKind::Offer) {
        packed = p1lora::pack_offer(g_key, g_tx_counter, action.step, frame, sizeof frame, len);
    } else {
        packed = p1lora::pack_confirm(g_key, g_tx_counter, action.step, frame, sizeof frame, len);
    }
    if (packed != p1lora::Status::Ok) {
        g_radio.listen();
        return false;
    }
    std::uint32_t measured = 0;
    const std::uint32_t radio_us =
        p1lora::airtime_us(g_on_profile, g_on_step, static_cast<std::uint16_t>(len));
    if (!g_radio.send(frame, len, measured)) {
        charge_sent(action.airtime_us, radio_us, 0);
        g_radio.listen();
        return false;
    }
    charge_sent(action.airtime_us, radio_us, measured);
    ++g_tx_counter;
    follow_radio();
    g_radio.listen();
    return true;
}

void apply_dwell()
{
    if (g_link == nullptr) {
        return;
    }
    static std::uint16_t applied = 0xFFFF;
    const std::uint16_t minutes = g_dwell_min.load();
    if (minutes == applied) {
        return;
    }
    applied = minutes;
    const std::uint32_t ms = minutes == 0 ? p1lora::kStepUpMs : static_cast<std::uint32_t>(minutes) * 60000u;
    g_link->set_step_up_ms(ms);
}

void take_ask()
{
    if (g_link == nullptr) {
        return;
    }
    const std::uint8_t seq = g_ask_seq.load();
    if (seq == 0 || seq == g_ask_seen) {
        return;
    }
    const Ask ask = g_ask;
    if (g_ask_seq.load() != seq) {
        return;
    }
    g_ask_seen.store(seq);
    const p1lora::LinkMode mode = ask.mode == 1 ? p1lora::LinkMode::Locked : p1lora::LinkMode::Auto;
    const p1lora::ProfileId id = ask.profile == 1 ? p1lora::ProfileId::B : p1lora::ProfileId::A;
    g_ask_result.store(g_link->request(mode, id, ask.step, ask.fallback) ? 1 : 2);
}

void push_heard(const Heard& item)
{
    if (g_heard == nullptr) {
        return;
    }
    if (xQueueSend(g_heard, &item, 0) == pdTRUE) {
        return;
    }
    Heard drop;
    xQueueReceive(g_heard, &drop, 0);
    xQueueSend(g_heard, &item, 0);
}

void on_packet(const std::uint8_t* buf, std::size_t len, float rssi, float snr)
{
    const p1lora::Decoded decoded = p1lora::decode(g_key, buf, len);
    if (decoded.status != p1lora::Status::Ok) {
        if (decoded.status == p1lora::Status::WrongNetwork) {
            g_network.fetch_add(1);
        } else if (decoded.status == p1lora::Status::KeyCheck) {
            g_key_fail.fetch_add(1);
        } else if (decoded.status == p1lora::Status::WrongLength) {
            g_length.fetch_add(1);
        }
        return;
    }
    if (decoded.kind != p1lora::Kind::Reading && decoded.kind != p1lora::Kind::Status &&
        decoded.kind != p1lora::Kind::Probe) {
        return;
    }
    if (g_have_last) {
        const p1lora::Gap gap = p1lora::counter_gap(g_last_counter, decoded.counter);
        if (!gap.restart) {
            const unsigned missed = gap.loss > 100 ? 100 : gap.loss;
            for (unsigned i = 0; i < missed; ++i) {
                note_loss_slot(false);
            }
        }
    }
    g_have_last = true;
    g_last_counter = decoded.counter;
    note_loss_slot(true);
    g_frames.fetch_add(1);
    g_last_ms.store(millis());
    g_rssi.store(static_cast<std::int16_t>(rssi));
    int centi = static_cast<int>(snr * 100.0f + (snr >= 0 ? 0.5f : -0.5f));
    if (centi > 32767) {
        centi = 32767;
    }
    if (centi < -32768) {
        centi = -32768;
    }
    g_snr_centi.store(static_cast<std::int16_t>(centi));
    g_have_signal.store(true);

    int link_q = snr_quarters(snr) - g_snr_offset.load() * 4;
    p1lora::Heard up;
    up.counter = decoded.counter;
    up.snr_qdb = static_cast<std::int16_t>(link_q);
    if (decoded.kind == p1lora::Kind::Probe) {
        up.kind = p1lora::AirKind::Probe;
    } else if (decoded.kind == p1lora::Kind::Status) {
        up.kind = p1lora::AirKind::Status;
        up.meter = decoded.meter;
        up.accepted = decoded.meter.accepted;
    } else {
        up.kind = p1lora::AirKind::Reading;
        up.accepted = (decoded.reading.flags & p1lora::kFlagAccepted) != 0;
    }
    p1lora::AirAction action{};
    if (g_link != nullptr) {
        action = g_link->on_uplink(duty_clock(), up);
        persist_link();
    }
    if (action.kind != p1lora::AirKind::None) {
        deliver(action);
    } else {
        follow_radio();
        g_radio.listen();
    }
    note_link_line();

    if (decoded.kind == p1lora::Kind::Probe) {
        return;
    }
    Heard item{};
    if (decoded.kind == p1lora::Kind::Status) {
        item.kind = HeardKind::Status;
        item.meter = decoded.meter;
        g_last_kind.store(2);
        g_reason.store(decoded.meter.reason);
        g_since.store(decoded.meter.since_s);
    } else {
        item.kind = HeardKind::Reading;
        item.reading = decoded.reading;
        g_last_kind.store(1);
        g_readings.fetch_add(1);
    }
    push_heard(item);
}

void IRAM_ATTR dio0_isr()
{
    BaseType_t woken = pdFALSE;
    if (g_rx_sem != nullptr) {
        xSemaphoreGiveFromISR(g_rx_sem, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void service_radio()
{
    std::uint8_t buf[255];
    std::size_t len = 0;
    float rssi = 0;
    float snr = 0;
    const p1lora::Rx got = g_radio.poll(buf, sizeof buf, len, rssi, snr);
    if (got != p1lora::Rx::Packet) {
        return;
    }
    ++g_drop_n;
    const std::uint8_t drop = g_drop_pct.load();
    // Spread the share across the hundred. A leading block of drops is a
    // silence, so the 50-frame loss rule would never see it.
    if (drop > 0 && ((g_drop_n * drop) % 100u) < drop) {
        g_radio.listen();
        return;
    }
    on_packet(buf, len, rssi, snr);
}

void radio_task(void*)
{
    for (;;) {
        if (g_rx_sem != nullptr) {
            xSemaphoreTake(g_rx_sem, pdMS_TO_TICKS(20));
        }
        const std::uint8_t req = g_req.exchange(0);
        if (req == 2 || req == 3) {
            remember_duty();
            if (req == 3) {
                clear_link_store();
                Serial.println("link cleared");
            } else {
                Serial.println("restart");
            }
            Serial.flush();
            ESP.restart();
        }
        if (g_link != nullptr) {
            apply_dwell();
            take_ask();
            const p1lora::AirAction offer = g_link->poll(duty_clock());
            persist_link();
            if (offer.kind != p1lora::AirKind::None) {
                deliver(offer);
            } else {
                follow_radio();
            }
            note_link_line();
        }
        if (g_radio_up && digitalRead(kRadioIrq) == HIGH) {
            service_radio();
        }
    }
}

std::uint32_t silence_ms()
{
    const std::uint32_t sec = p1lora::status_interval_s(
        g_profile_id.load() == 1 ? p1lora::ProfileId::B : p1lora::ProfileId::A, g_step.load());
    return sec * 3u * 1000u;
}

LinkState link_state()
{
    if (!g_have_key) {
        return LinkState::NoKey;
    }
    const std::uint32_t last = g_last_ms.load();
    const std::uint32_t age = last == 0 ? millis() : millis() - last;
    if (age >= silence_ms()) {
        return LinkState::Silent;
    }
    if (last != 0 && g_last_kind.load() == 2) {
        return LinkState::NoData;
    }
    if (last != 0 && g_last_kind.load() == 1) {
        return LinkState::Ok;
    }
    return LinkState::Waiting;
}

const char* state_text(LinkState state)
{
    switch (state) {
    case LinkState::Ok:
        return "ok";
    case LinkState::NoData:
        return "no meter data";
    case LinkState::Silent:
        return "silent";
    case LinkState::NoKey:
        return "no pair key";
    case LinkState::Waiting:
        return "waiting";
    }
    return "waiting";
}

void note_state()
{
    const LinkState now = link_state();
    if (now == g_logged || g_log == nullptr) {
        g_logged = now;
        return;
    }
    g_logged = now;
    if (now != LinkState::NoData && now != LinkState::Silent && now != LinkState::NoKey) {
        return;
    }
    char msg[64];
    if (now == LinkState::NoData) {
        const std::uint16_t since = g_since.load();
        if (since == p1lora::kSinceNever) {
            std::snprintf(msg, sizeof(msg), "transmitter no meter data reason=%u since=never",
                          g_reason.load());
        } else {
            std::snprintf(msg, sizeof(msg), "transmitter no meter data reason=%u since=%us",
                          g_reason.load(), since);
        }
    } else {
        std::snprintf(msg, sizeof(msg), "transmitter %s", state_text(now));
    }
    g_log(p1log::Level::Info, msg);
}

bool local_meter_time(char* out, std::size_t cap)
{
    if (out == nullptr || cap < 14) {
        return false;
    }
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    const time_t now = time(nullptr);
    if (now < 1700000000) {
        return false;
    }
    struct tm wall {};
    if (localtime_r(&now, &wall) == nullptr || wall.tm_year < 100) {
        return false;
    }
    const int yy = wall.tm_year % 100;
    const unsigned month = static_cast<unsigned>(wall.tm_mon + 1);
    const unsigned day = static_cast<unsigned>(wall.tm_mday);
    out[0] = static_cast<char>('0' + yy / 10);
    out[1] = static_cast<char>('0' + yy % 10);
    out[2] = static_cast<char>('0' + static_cast<int>(month) / 10);
    out[3] = static_cast<char>('0' + static_cast<int>(month) % 10);
    out[4] = static_cast<char>('0' + static_cast<int>(day) / 10);
    out[5] = static_cast<char>('0' + static_cast<int>(day) % 10);
    out[6] = static_cast<char>('0' + wall.tm_hour / 10);
    out[7] = static_cast<char>('0' + wall.tm_hour % 10);
    out[8] = static_cast<char>('0' + wall.tm_min / 10);
    out[9] = static_cast<char>('0' + wall.tm_min % 10);
    out[10] = static_cast<char>('0' + wall.tm_sec / 10);
    out[11] = static_cast<char>('0' + wall.tm_sec % 10);
    out[12] = wall.tm_isdst > 0 ? 'S' : 'W';
    out[13] = '\0';
    return true;
}

void remember_slow(const p1lora::Reading& reading)
{
    if ((reading.flags & p1lora::kFlagGas) != 0 && (!g_have_gas || reading.gas_dm3 != g_gas_dm3)) {
        g_have_gas = true;
        g_gas_dm3 = reading.gas_dm3;
        g_gas_ts[0] = '\0';
    }
    if (g_have_gas && g_gas_ts[0] == '\0') {
        local_meter_time(g_gas_ts, sizeof(g_gas_ts));
    }
    if ((reading.flags & p1lora::kFlagPeak) != 0) {
        g_have_peak = true;
        g_peak_w = reading.peak_w;
        g_peak_time = reading.peak_time;
    }
}

void deliver(const Heard& item)
{
    if (g_on_telegram == nullptr || item.kind != HeardKind::Reading) {
        return;
    }
    remember_slow(item.reading);
    p1lora::Reading reading = item.reading;
    if (g_have_gas) {
        reading.flags = static_cast<std::uint8_t>(reading.flags | p1lora::kFlagGas);
        reading.gas_dm3 = g_gas_dm3;
    }
    if (g_have_peak) {
        reading.flags = static_cast<std::uint8_t>(reading.flags | p1lora::kFlagPeak);
        reading.peak_w = g_peak_w;
        reading.peak_time = g_peak_time;
    }
    const char* stamp = nullptr;
    if (local_meter_time(g_reading_ts, sizeof(g_reading_ts))) {
        stamp = g_reading_ts;
    }
    // Reused across frames. to_data clears it first. Gas and the month peak
    // are the only values copied onto `reading` above.
    static p1dsmr::Data data;
    p1lora::to_data(reading, data, stamp, g_gas_ts[0] != '\0' ? g_gas_ts : nullptr);
    g_on_telegram(data, {});
    ++g_ok_count;
}

void drain_heard()
{
    if (g_heard == nullptr) {
        note_state();
        return;
    }
    Heard item;
    while (xQueueReceive(g_heard, &item, 0) == pdTRUE) {
        deliver(item);
    }
    note_state();
}

void board_begin()
{
    pinMode(kLed, OUTPUT);
    digitalWrite(kLed, LOW);
    pinMode(kButton, INPUT_PULLDOWN);
}

void led_write(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    // The app drives the LED before board_begin, so the pin mode lives here.
    pinMode(kLed, OUTPUT);
    digitalWrite(kLed, (r | g | b) != 0 ? HIGH : LOW);
}

bool button_down()
{
    return digitalRead(kButton) == HIGH;
}

void source_begin(p1app::Product::OnTelegram on_telegram, p1app::Product::LogFn log,
                  const std::uint32_t*, p1app::Product::UartBringup& uart)
{
    g_on_telegram = on_telegram;
    g_log = log;
    uart.heap_before = static_cast<unsigned>(ESP.getFreeHeap());
    uart.heap_after = uart.heap_before;
    uart.rx_buf = 0;
}

void remember_form()
{
    std::snprintf(g_seen_mode, sizeof(g_seen_mode), "%s", g_mode);
    std::snprintf(g_seen_profile, sizeof(g_seen_profile), "%s", g_profile);
    std::snprintf(g_seen_step, sizeof(g_seen_step), "%s", g_step_text);
    std::snprintf(g_seen_fallback, sizeof(g_seen_fallback), "%s", g_fallback);
    g_form_ready = true;
}

bool form_changed()
{
    return std::strcmp(g_seen_mode, g_mode) != 0 || std::strcmp(g_seen_profile, g_profile) != 0 ||
           std::strcmp(g_seen_step, g_step_text) != 0 || std::strcmp(g_seen_fallback, g_fallback) != 0;
}

void post_form()
{
    Ask ask;
    ask.mode = std::strcmp(g_mode, "locked") == 0 ? 1 : 0;
    ask.profile = g_profile[0] == 'B' ? 1 : 0;
    ask.step = static_cast<std::uint8_t>(g_step_text[0] - '0');
    ask.fallback = static_cast<std::uint16_t>(std::atoi(g_fallback));
    g_ask = ask;
    std::uint8_t seq = static_cast<std::uint8_t>(g_ask_seq.load() + 1u);
    if (seq == 0) {
        seq = 1;
    }
    g_ask_seq.store(seq);
}

void watch_form()
{
    if (!g_form_ready) {
        remember_form();
        return;
    }
    const std::uint8_t result = g_ask_result.exchange(0);
    if (result == 1) {
        remember_form();
        return;
    }
    if (!form_changed()) {
        return;
    }
    if (g_ask_seq.load() != g_ask_seen.load() && g_ask_seq.load() != 0) {
        return;
    }
    post_form();
}

void start_radio()
{
    g_have_key = p1lora::parse_key(g_key_text, std::strlen(g_key_text), g_key) == p1lora::KeyStatus::Ok;
    if (!g_have_key) {
        if (g_log != nullptr) {
            g_log(p1log::Level::Info, "no pair key set");
        }
        return;
    }
    load_duty();
    p1lora::LinkSettings stored{};
    if (!load_link(stored)) {
        stored = {};
    }
    g_link = new (g_link_mem) p1lora::RxLink(stored, g_duty, true);
    const p1lora::Profile& profile = p1lora::profile(g_link->profile());
    if (g_link->step() >= profile.step_count || !g_radio.begin(profile, g_link->step(), kPowerDbm) ||
        !g_radio.listen()) {
        if (g_log != nullptr) {
            g_log(p1log::Level::Error, "radio begin failed");
        }
        return;
    }
    g_on_profile = g_link->profile();
    g_on_step = g_link->step();
    publish_link_snap();
    g_radio_up = true;
    g_rx_sem = xSemaphoreCreateBinaryStatic(&g_rx_sem_buf);
    g_heard = xQueueCreateStatic(kQueueSlots, sizeof(Heard), g_heard_storage, &g_heard_buf);
    attachInterrupt(digitalPinToInterrupt(kRadioIrq), dio0_isr, RISING);
    xTaskCreateStatic(radio_task, "lora", sizeof(g_radio_stack), nullptr, 2, g_radio_stack, &g_radio_tcb);
    note_link_line();
}

bool after_settings()
{
    remember_form();
    bool generated = false;
    if (g_key_text[0] == '\0') {
        // The hardware generator is only a true random source while WiFi or
        // Bluetooth is running, or while the bootloader entropy source is on.
        // WiFi has not started yet, so enable that source for this read only.
        bootloader_random_enable();
        std::uint8_t rnd[16];
        esp_fill_random(rnd, sizeof(rnd));
        bootloader_random_disable();
        p1lora::format_pair_key(rnd, g_key_text, sizeof(g_key_text));
        generated = g_key_text[0] != '\0';
        if (generated && g_log != nullptr) {
            g_log(p1log::Level::Info, "pair key generated");
        }
    }
    start_radio();
    return generated;
}

void source_request(std::uint32_t, std::uint32_t, bool) {}

void source_drain()
{
    watch_form();
    drain_heard();
}

void note_fresh(std::uint32_t) {}

void meter(p1app::Product::MeterSnap& out)
{
    out = {};
    out.ok = g_ok_count;
    out.last_ms = g_last_ms.load();
}

p1web::TelegramDump dump_none()
{
    return {};
}

bool serial_cmd(char* line)
{
#ifdef P1_DESK_TEST
    if (std::strcmp(line, "restart") == 0) {
        g_req.store(2);
        return true;
    }
    if (std::strcmp(line, "linkclear") == 0) {
        if (!g_radio_up) {
            clear_link_store();
            Serial.println("link cleared");
            Serial.flush();
            ESP.restart();
        }
        g_req.store(3);
        return true;
    }
    if (std::strncmp(line, "snroffset ", 10) == 0) {
        g_snr_offset.store(std::atoi(line + 10));
        Serial.printf("snroffset %d\n", g_snr_offset.load());
        return true;
    }
    if (std::strncmp(line, "drop ", 5) == 0) {
        int pct = std::atoi(line + 5);
        if (pct < 0) {
            pct = 0;
        }
        if (pct > 100) {
            pct = 100;
        }
        g_drop_pct.store(static_cast<std::uint8_t>(pct));
        Serial.printf("drop %u\n", g_drop_pct.load());
        return true;
    }
    if (std::strncmp(line, "dwell ", 6) == 0) {
        int minutes = std::atoi(line + 6);
        if (minutes < 0) {
            minutes = 0;
        }
        if (minutes > 2550) {
            minutes = 2550;
        }
        g_dwell_min.store(static_cast<std::uint16_t>(minutes));
        Serial.printf("dwell %u\n", g_dwell_min.load());
        return true;
    }
    if (std::strcmp(line, "link") == 0) {
        char step_txt[16];
        const p1lora::ProfileId id =
            g_profile_id.load() == 1 ? p1lora::ProfileId::B : p1lora::ProfileId::A;
        format_step(step_txt, sizeof step_txt, id, g_step.load());
        const p1lora::LinkView view = static_cast<p1lora::LinkView>(g_view_id.load());
        const unsigned dwell = g_dwell_min.load() == 0 ? 120u : g_dwell_min.load();
        Serial.printf("link %s mode %s profile %c step %u %s last %u%s snroffset %d drop %u dwell %u\n",
                      view_name(view), g_mode_id.load() == 1 ? "locked" : "auto",
                      id == p1lora::ProfileId::B ? 'B' : 'A', g_step.load(), step_txt, g_last_good.load(),
                      g_in_flight.load() != 0 ? " pending" : "", g_snr_offset.load(), g_drop_pct.load(),
                      dwell);
        return true;
    }
#else
    (void)line;
#endif
    return false;
}

const char* json_num(char* buf, std::size_t cap, int value)
{
    std::snprintf(buf, cap, "%d", value);
    return buf;
}

std::size_t diagnostics(p1app::Product::DiagPoint* out, std::size_t cap)
{
    if (out == nullptr || cap < 11) {
        return 0;
    }
    static char rssi[16];
    static char snr[16];
    static char loss[8];
    static char state[24];
    static char age[16];
    static char frames[16];
    static char rejects[16];
    static char phase[24];
    static char mode[16];
    static char profile[8];
    static char step[24];
    const LinkState now = link_state();
    if (g_have_signal.load()) {
        json_num(rssi, sizeof(rssi), g_rssi.load());
        const int centi = g_snr_centi.load();
        const char* sign = centi < 0 ? "-" : "";
        const int abs_c = centi < 0 ? -centi : centi;
        std::snprintf(snr, sizeof(snr), "%s%d.%02d", sign, abs_c / 100, abs_c % 100);
    } else {
        std::snprintf(rssi, sizeof(rssi), "null");
        std::snprintf(snr, sizeof(snr), "null");
    }
    json_num(loss, sizeof(loss), g_loss_pct.load());
    std::snprintf(state, sizeof(state), "\"%s\"", state_text(now));
    const std::uint32_t last = g_last_ms.load();
    if (last == 0) {
        std::snprintf(age, sizeof(age), "null");
    } else {
        json_num(age, sizeof(age), static_cast<int>((millis() - last) / 1000u));
    }
    json_num(frames, sizeof(frames), static_cast<int>(g_frames.load()));
    const unsigned rej = g_network.load() + g_key_fail.load() + g_length.load();
    json_num(rejects, sizeof(rejects), static_cast<int>(rej));

    out[0] = {"link_rssi", "Link RSSI", "dBm", "signal_strength", rssi};
    out[1] = {"link_snr", "Link SNR", "dB", "signal_strength", snr};
    out[2] = {"link_loss", "Link loss", "%", "", loss};
    out[3] = {"link_state", "Transmitter", "", "", state};
    out[4] = {"link_age", "Seconds since last frame", "s", "duration", age};
    out[5] = {"link_frames", "Frames received", "", "", frames};
    out[6] = {"link_rejects", "Rejects", "", "", rejects};
    const p1lora::LinkView view = static_cast<p1lora::LinkView>(g_view_id.load());
    const p1lora::ProfileId id = g_profile_id.load() == 1 ? p1lora::ProfileId::B : p1lora::ProfileId::A;
    char step_txt[16];
    format_step(step_txt, sizeof step_txt, id, g_step.load());
    std::snprintf(phase, sizeof(phase), "\"%s\"", view_name(view));
    std::snprintf(mode, sizeof(mode), "\"%s\"", g_mode_id.load() == 1 ? "locked" : "auto");
    std::snprintf(profile, sizeof(profile), "\"%c\"", id == p1lora::ProfileId::B ? 'B' : 'A');
    std::snprintf(step, sizeof(step), "\"%s\"", step_txt);
    out[7] = {"link_phase", "Link", "", "", phase};
    out[8] = {"link_mode", "Link mode", "", "", mode};
    out[9] = {"link_profile", "Link profile", "", "", profile};
    out[10] = {"link_step", "Link step", "", "", step};
    return 11;
}

void put_line(p1app::Product::StatusLine* out, std::size_t& n, std::size_t cap, const char* label,
              const char* value)
{
    if (n >= cap) {
        return;
    }
    std::snprintf(out[n].label, sizeof(out[n].label), "%s", label);
    std::snprintf(out[n].value, sizeof(out[n].value), "%s", value);
    ++n;
}

std::size_t status_lines(p1app::Product::StatusLine* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    // Three lines, so the shared status page (1024 bytes) still fits.
    char radio[48];
    char state[64];
    char counts[48];
    const LinkState now = link_state();
    char step_txt[16];
    const p1lora::ProfileId id = g_profile_id.load() == 1 ? p1lora::ProfileId::B : p1lora::ProfileId::A;
    format_step(step_txt, sizeof step_txt, id, g_step.load());
    const char* phase = view_name(static_cast<p1lora::LinkView>(g_view_id.load()));
    if (g_have_signal.load()) {
        const int centi = g_snr_centi.load();
        const char* sign = centi < 0 ? "-" : "";
        const int abs_c = centi < 0 ? -centi : centi;
        std::snprintf(radio, sizeof(radio), "%d dBm %s%d.%02d dB %u%%", g_rssi.load(), sign, abs_c / 100,
                      abs_c % 100, g_loss_pct.load());
    } else {
        std::snprintf(radio, sizeof(radio), "-");
    }
    if (now == LinkState::NoData) {
        const std::uint16_t since = g_since.load();
        if (since == p1lora::kSinceNever) {
            std::snprintf(state, sizeof(state), "no meter data r%u", g_reason.load());
        } else {
            std::snprintf(state, sizeof(state), "no meter data r%u %us", g_reason.load(), since);
        }
    } else {
        std::snprintf(state, sizeof(state), "%s %c %s%s / %s", phase, id == p1lora::ProfileId::B ? 'B' : 'A',
                      step_txt, g_in_flight.load() != 0 ? " pending" : "", state_text(now));
    }
    const std::uint32_t last = g_last_ms.load();
    const unsigned rej = g_network.load() + g_key_fail.load() + g_length.load();
    if (last == 0) {
        std::snprintf(counts, sizeof(counts), "%u never rej %u", static_cast<unsigned>(g_frames.load()),
                      rej);
    } else {
        std::snprintf(counts, sizeof(counts), "%u %us rej %u", static_cast<unsigned>(g_frames.load()),
                      static_cast<unsigned>((millis() - last) / 1000u), rej);
    }
    std::size_t n = 0;
    put_line(out, n, cap, "Link", radio);
    put_line(out, n, cap, "State", state);
    put_line(out, n, cap, "Frames", counts);
    return n;
}

const p1app::Product kProduct = {
    "P1 LoRa receiver",
    false,
    false,
    false,
    false,
    "p1lora",
    "3.00",
    300,
    "P1 LoRa receiver",
    "P1 LoRa receiver for DSMR compatible utility meters",
    "P1 LoRa receiver debug monitoring",
    diagnostics,
    status_lines,
    board_begin,
    led_write,
    button_down,
    source_begin,
    source_request,
    source_drain,
    note_fresh,
    meter,
    dump_none,
    dump_none,
    serial_cmd,
    "LoRa receiver",
    "data/devices/lora_receiver/",
    "https://github.com/plan-d-io/P1-firmware",
    "LORA_KEY",
    g_settings,
    sizeof(g_settings) / sizeof(g_settings[0]),
    after_settings,
};

}  // namespace

const p1app::Product& receiver_product()
{
    return kProduct;
}
