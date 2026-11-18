#pragma once

// Frame packing, encryption, airtime and the duty-cycle budget for the
// P1 LoRa bridge. Arduino-free. No heap and no globals: the caller owns
// every buffer and the duty-cycle state.

#include <cstddef>
#include <cstdint>

namespace p1lora {

enum class ProfileId : std::uint8_t { A = 0, B = 1 };

struct Step {
    std::uint8_t sf;
    std::uint32_t bw_hz;
};

struct Profile {
    ProfileId id;
    std::uint32_t hz;
    std::int8_t max_dbm;
    std::uint32_t duty_ms_per_hour;
    std::uint8_t step_count;
    Step steps[7];
    std::uint8_t sync_word;
};

const Profile& profile(ProfileId id);

// Semtech airtime. Explicit header, preamble 8, coding rate 4/5, CRC on.
// Low-data-rate optimisation when the symbol time is longer than 16 ms.
// Returns microseconds.
std::uint32_t airtime_us(std::uint8_t sf, std::uint32_t bw_hz, std::uint16_t payload_len);
std::uint32_t airtime_us(ProfileId id, std::uint8_t step, std::uint16_t payload_len);

// Reading interval in tenths of a second, rounded from airtime / duty fraction.
std::uint16_t reading_interval_tenths(std::uint32_t airtime_us, std::uint32_t duty_ms_per_hour);

// Seconds between status frames. At least 60, and at least four times the
// 9-byte status airtime divided by the profile's duty fraction, rounded up.
std::uint32_t status_interval_s(ProfileId id, std::uint8_t step);

// Requested power above the profile maximum is cut down to it. A lower
// request is left alone; the radio driver applies its own floor.
std::int8_t clamp_tx_power(ProfileId id, std::int8_t requested_dbm);

// Plain image of a Duty. The caller keeps it (RTC memory on the device).
// A restored image is not aged: the reboot is treated as taking no time.
// 10 s slots: 360 of them cover the hour, plus one so a slot that still
// overlaps the window is counted in full. Image is 1468 bytes.
constexpr std::uint32_t kDutySlotMs = 10000u;
constexpr int kDutySlots = 361;
constexpr std::uint32_t kDutyMagic = 0x50314476u;

struct DutyImage {
    std::uint32_t magic;
    std::uint32_t budget_us;
    std::uint32_t bucket_us[kDutySlots];
    std::uint32_t last_slot;
    std::uint32_t now_ms;
    std::uint8_t started;
    std::uint8_t pad[3];
    std::uint32_t checksum;
};
static_assert(sizeof(DutyImage) == 1468u, "duty image size");

// Rolling 3600 s window of our own airtime. A 10 s slot that still overlaps
// the window is counted in full, so this may hold a send back a little and
// must not let one through early. next_send_ms spreads what is left of a
// 95 % aim across the hour, and never earlier than wait_ms.
class Duty {
public:
    explicit Duty(std::uint32_t budget_ms_per_hour);

    // The rolling window stays. The next can_send uses this budget, so a
    // profile change cannot spend the previous profile's larger allowance.
    void set_budget_ms(std::uint32_t budget_ms_per_hour);

    bool can_send(std::uint32_t now_ms, std::uint32_t airtime_us);
    void record(std::uint32_t now_ms, std::uint32_t airtime_us);
    // 0 if a send is allowed now. Otherwise how long to wait.
    std::uint32_t wait_ms(std::uint32_t now_ms, std::uint32_t airtime_us) const;
    // Absolute time of the next send of this airtime. The steady interval
    // spreads a 95 % aim across the hour and is never under 1 s. A burst
    // above that aim stretches it, at most twice, and never before the hard cap.
    std::uint32_t next_send_ms(std::uint32_t now_ms, std::uint32_t airtime_us) const;
    // Airtime still charged inside the window, in microseconds.
    std::uint32_t charged_us(std::uint32_t now_ms) const;
    // now_ms is the clock value the buckets were recorded against.
    void save(DutyImage& out, std::uint32_t now_ms) const;
    // False if the magic, checksum or budget does not match. State is unchanged then.
    bool restore(const DutyImage& in);

private:
    static constexpr int kSlots = kDutySlots;

    void roll(std::uint32_t now_ms);
    std::uint32_t used_us(std::uint32_t now_ms);

    std::uint32_t budget_us_;
    std::uint32_t bucket_us_[kSlots]{};
    std::uint32_t last_slot_ = 0;
    bool started_ = false;
};

enum class KeyStatus : std::uint8_t { Ok, Rejected };

struct Key {
    std::uint8_t aes[32]{};
    std::uint8_t net[2]{};
};

// 32 hex characters, either case, into the 16-byte pair key. Anything else
// is rejected. The AES key and the network ID are derived here.
KeyStatus parse_key(const char* hex, std::size_t len, Key& out);

// Empty is kept. 32 hex characters are stored in lower case. Anything else
// is rejected. Used when the key is typed on the page or the serial console.
bool normalise_pair_key(char* value);
// 16 random bytes as 32 lower-case hex characters. cap must be at least 33.
void format_pair_key(const std::uint8_t* rnd16, char* out, std::size_t cap);

constexpr std::uint16_t kAbsent16 = 0xFFFF;
constexpr std::uint32_t kAbsent32 = 0xFFFFFFFFu;
constexpr std::uint8_t kFlagTariff2 = 0x01;
constexpr std::uint8_t kFlagGas = 0x02;
constexpr std::uint8_t kFlagWater = 0x04;
constexpr std::uint8_t kFlagPeak = 0x08;
constexpr std::uint8_t kFlagAccepted = 0x10;

constexpr std::uint8_t kTypeSingle = 0x41;
constexpr std::uint8_t kTypeThree = 0x42;
constexpr std::uint8_t kTypeProbe = 0x04;
constexpr std::uint8_t kTypeStatus = 0x05;
constexpr std::uint8_t kTypeAck = 0x81;
constexpr std::uint8_t kTypeOffer = 0x82;
constexpr std::uint8_t kTypeConfirm = 0x83;

// Status payload: why the transmitter has no reading to send.
// Byte 0 layout: bits 0–6 are the reason below. Bit 7 is "command accepted",
// the same meaning as kFlagAccepted on a reading. A transmitter with no meter
// data only sends status frames, so the accept flag has to live here too.
// The L03 literal 6b0b05010302580200 keeps bit 7 clear.
constexpr std::uint8_t kStatusNoneSinceBoot = 1;
constexpr std::uint8_t kStatusNoneForAWhile = 2;
constexpr std::uint8_t kStatusRejected = 3;
constexpr std::uint8_t kStatusAccepted = 0x80;
constexpr std::uint16_t kSinceNever = 0xFFFF;
constexpr std::uint16_t kSinceMax = 0xFFFE;
// A reading is sent only while the last valid telegram is younger than this.
constexpr std::uint32_t kFreshMs = 10000;

enum class Status : std::uint8_t {
    Ok,
    WrongNetwork,
    UnknownType,
    UnknownVersion,
    WrongLength,
    KeyCheck,
};

struct Reading {
    bool three_phase = false;
    std::uint8_t flags = 0;
    std::uint32_t t1_wh = kAbsent32;
    std::uint32_t t2_wh = kAbsent32;
    std::uint32_t inj1_wh = kAbsent32;
    std::uint32_t inj2_wh = kAbsent32;
    std::uint16_t power_w = kAbsent16;
    std::uint16_t inject_w = kAbsent16;
    std::uint16_t demand_w = kAbsent16;
    std::uint16_t p_plus_w[3]{kAbsent16, kAbsent16, kAbsent16};
    std::uint16_t p_minus_w[3]{kAbsent16, kAbsent16, kAbsent16};
    std::uint16_t v_dv[3]{kAbsent16, kAbsent16, kAbsent16};
    std::uint16_t i_ca[3]{kAbsent16, kAbsent16, kAbsent16};
    std::uint32_t gas_dm3 = 0;
    std::uint32_t peak_time = 0;
    std::uint16_t peak_w = 0;
};

struct Ack {
    std::uint8_t counter = 0;
    std::uint8_t flags = 0;
    std::int8_t snr = 0;
    bool command = false;
    std::uint8_t profile_bit = 0;  // 0 = A, 1 = B
    std::uint8_t mode_bit = 0;     // 0 = auto, 1 = locked
    std::uint8_t step = 0;
    std::uint8_t fallback = 0;     // 0 = off, else minutes / 10
};

enum class Kind : std::uint8_t { None, Reading, Probe, Ack, Offer, Confirm, Status };

struct MeterStatus {
    std::uint8_t reason = 0;  // bits 0–6; bit 7 is carried by `accepted`
    std::uint16_t since_s = kSinceNever;
    std::uint8_t rejected = 0;
    bool accepted = false;
};

struct Decoded {
    Status status = Status::WrongLength;
    Kind kind = Kind::None;
    std::uint8_t type = 0;
    std::uint8_t version = 0;
    std::uint8_t counter = 0;
    Reading reading{};
    std::uint8_t probe_len = 0;
    Ack ack{};
    std::uint8_t step = 0;
    MeterStatus meter{};
};

enum class SendChoice : std::uint8_t { Wait, Reading, Status };

struct SendPlan {
    SendChoice choice = SendChoice::Wait;
    MeterStatus meter{};
};

// Decides a reading versus a status frame from a free-running millisecond clock.
// A status goes out as soon as the telegram is stale, then at status_interval_s.
class MeterGate {
public:
    void note_valid(std::uint32_t now_ms);
    void note_reject(std::uint32_t now_ms);
    SendPlan plan(std::uint32_t now_ms, std::uint32_t status_interval_ms) const;
    void status_sent(std::uint32_t now_ms);

private:
    bool ever_ = false;
    bool status_armed_ = false;
    bool have_reject_ = false;
    std::uint32_t last_valid_ms_ = 0;
    std::uint32_t last_status_ms_ = 0;
    std::uint32_t last_reject_ms_ = 0;
    std::uint8_t rejects_ = 0;
    std::uint8_t rejects_since_valid_ = 0;
};

constexpr std::size_t kMaxFrame = 80;

// Plaintext of a reading, before zero padding. 0 if it does not fit.
std::size_t write_reading_plain(const Reading& reading, std::uint8_t* out, std::size_t cap);

// 16-byte IV for this header under the key.
void frame_iv(const Key& key, const std::uint8_t header[5], std::uint8_t iv[16]);

Status pack_reading(const Key& key, const Reading& reading, std::uint8_t counter,
                    std::uint8_t* out, std::size_t cap, std::size_t& len);

Status pack_probe(const Key& key, std::uint8_t counter, std::uint8_t payload_len,
                  std::uint8_t* out, std::size_t cap, std::size_t& len);

Status pack_ack(const Key& key, std::uint8_t counter, const Ack& ack,
                std::uint8_t* out, std::size_t cap, std::size_t& len);

Status pack_offer(const Key& key, std::uint8_t counter, std::uint8_t step,
                  std::uint8_t* out, std::size_t cap, std::size_t& len);

Status pack_confirm(const Key& key, std::uint8_t counter, std::uint8_t step,
                    std::uint8_t* out, std::size_t cap, std::size_t& len);

Status pack_status(const Key& key, std::uint8_t counter, const MeterStatus& meter,
                   std::uint8_t* out, std::size_t cap, std::size_t& len);

Decoded decode(const Key& key, const std::uint8_t* frame, std::size_t len);

// (counter - last - 1) mod 256. A gap of 128 or more is a transmitter restart.
struct Gap {
    bool restart = false;
    std::uint8_t loss = 0;
};
Gap counter_gap(std::uint8_t last, std::uint8_t current);

// Gas and month peak stay in readings until an ACK names a counter that
// carried the value still current at ACK time.
class Optionals {
public:
    void observe(bool gas_present, std::uint32_t gas_dm3,
                 bool peak_present, std::uint32_t peak_time, std::uint16_t peak_w);
    bool gas_pending() const { return gas_pending_; }
    bool peak_pending() const { return peak_pending_; }
    void sent(std::uint8_t counter, bool carried_gas, bool carried_peak);
    void ack(std::uint8_t counter);

private:
    bool started_ = false;
    bool gas_pending_ = false;
    bool peak_pending_ = false;
    bool have_gas_ = false;
    bool have_peak_ = false;
    std::uint32_t gas_dm3_ = 0;
    std::uint32_t peak_time_ = 0;
    std::uint16_t peak_w_ = 0;
    std::uint32_t gas_gen_ = 0;
    std::uint32_t peak_gen_ = 0;
    std::uint32_t gas_at_[256]{};
    std::uint32_t peak_at_[256]{};
};

// NIST AES-256 and SHA-256 vectors, plus the three-phase frame vector.
// nullptr when they match. Otherwise "aes", "sha" or "frame".
const char* crypto_self_test();

}  // namespace p1lora
