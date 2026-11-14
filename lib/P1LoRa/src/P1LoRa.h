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
};

const Profile& profile(ProfileId id);

// Semtech airtime. Explicit header, preamble 8, coding rate 4/5, CRC on.
// Low-data-rate optimisation when the symbol time is longer than 16 ms.
// Returns microseconds.
std::uint32_t airtime_us(std::uint8_t sf, std::uint32_t bw_hz, std::uint16_t payload_len);
std::uint32_t airtime_us(ProfileId id, std::uint8_t step, std::uint16_t payload_len);

// Reading interval in tenths of a second, rounded from airtime / duty fraction.
std::uint16_t reading_interval_tenths(std::uint32_t airtime_us, std::uint32_t duty_ms_per_hour);

// Rolling 3600 s window of our own airtime. Per-minute sums: a minute that
// still overlaps the window is counted in full, so this may hold a send
// back a little long and must not let one through early.
class Duty {
public:
    explicit Duty(std::uint32_t budget_ms_per_hour);

    bool can_send(std::uint32_t now_ms, std::uint32_t airtime_us);
    void record(std::uint32_t now_ms, std::uint32_t airtime_us);
    // 0 if a send is allowed now. Otherwise how long to wait.
    std::uint32_t wait_ms(std::uint32_t now_ms, std::uint32_t airtime_us) const;

private:
    static constexpr int kSlots = 61;

    void roll(std::uint32_t now_ms);
    std::uint32_t used_us(std::uint32_t now_ms);

    std::uint32_t budget_us_;
    std::uint32_t bucket_us_[kSlots]{};
    std::uint32_t last_minute_ = 0;
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
constexpr std::uint8_t kTypeAck = 0x81;
constexpr std::uint8_t kTypeOffer = 0x82;
constexpr std::uint8_t kTypeConfirm = 0x83;

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

enum class Kind : std::uint8_t { None, Reading, Probe, Ack, Offer, Confirm };

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
