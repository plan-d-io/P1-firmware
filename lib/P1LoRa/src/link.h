#pragma once

// Link behaviour for both ends of a pair. Event-driven: the caller passes
// time in and owns the radio, the NVS write and the duty image. No heap and
// no Arduino types. L05b is what connects this to the boards.
//
// The receiver decides step changes. The transmitter follows. A command or an
// offer that changes the radio is provisional until the other end is heard on
// the new setting; the revert window below sends both back to the previous
// setting if that does not happen. Locking the step already in use stores
// the mode and does not retune.

#include "P1LoRa.h"

#include <cstdint>

namespace p1lora {

// Quarter-dB units, the same scale as Ack::snr (1 dB = 4).
constexpr std::int16_t kOfferMarginQdb = 40;   // 10.0 dB
constexpr std::int16_t kStepUpExtraQdb = 10;   // 2.5 dB
constexpr std::int16_t kBwOctaveQdb = 12;      // 3.0 dB, 10·log10(2)

constexpr std::uint8_t kConfirmHeard = 3;
constexpr std::uint8_t kConfirmOf = 5;
constexpr std::uint8_t kLossSpan = 20;
constexpr std::uint8_t kLossDownPct = 20;      // step down above this, not at it
constexpr std::uint8_t kStepUpLossPct = 2;     // step up below this
constexpr std::uint32_t kStepUpMs = 30u * 60u * 1000u;
constexpr std::uint32_t kSilenceFloorMs = 120000;
constexpr std::uint32_t kRevertFloorMs = 60000;
constexpr std::uint8_t kNoOfferCap = 0xFF;

// SX1276 demodulation floor for SF7..SF12, in quarter-dB.
std::int16_t floor_qdb(std::uint8_t sf);

// SNR measured at from_bw, expressed as it would have been measured at to_bw.
// Doubling the bandwidth costs 3.0 dB; halving it gives 3.0 dB back.
std::int16_t adjust_snr_qdb(std::int16_t measured_qdb, std::uint32_t from_bw, std::uint32_t to_bw);

// Fastest step whose floor + 10 dB fits. cap limits a retry after a failed offer
// (kNoOfferCap = no limit). The slowest step is used when nothing fits.
std::uint8_t fastest_offer_step(ProfileId id, std::uint8_t measured_step, std::int16_t snr_qdb,
                                std::uint8_t cap = kNoOfferCap);

// Steady send spacing, and the two windows that are multiples of it.
std::uint32_t reading_pace_ms(ProfileId id, std::uint8_t step, bool three_phase);
std::uint32_t revert_window_ms(ProfileId id, std::uint8_t step, bool three_phase);
std::uint32_t silence_period_ms(ProfileId id, std::uint8_t step, bool three_phase);

enum class LinkMode : std::uint8_t { Auto = 0, Locked = 1 };

enum class LinkView : std::uint8_t {
    Search,
    Run,
    Confirming,
    Switching,
    Locked,
    LockLost,
};

// What L05b writes to NVS. fallback_min is minutes (0 = off). On the air the
// command byte is minutes/10, matching Ack::fallback.
struct LinkSettings {
    LinkMode mode = LinkMode::Auto;
    ProfileId profile = ProfileId::A;
    std::uint8_t locked_step = 0;
    std::uint8_t last_good_step = 0;
    std::uint16_t fallback_min = 0;
    bool saved = false;
};

bool settings_valid(const LinkSettings& settings);

enum class AirKind : std::uint8_t { None, Reading, Status, Probe, Ack, Offer, Confirm };

struct AirAction {
    AirKind kind = AirKind::None;
    bool accepted = false;
    bool command = false;
    Ack ack{};
    std::uint8_t step = 0;
    MeterStatus meter{};
    ProfileId sent_profile = ProfileId::A;
    std::uint8_t sent_step = 0;
    std::uint32_t airtime_us = 0;
    std::uint32_t wait_ms = 0;
};

// A frame the other end actually heard. snr_qdb was measured on sent_step.
struct Heard {
    AirKind kind = AirKind::None;
    std::uint8_t counter = 0;
    std::int16_t snr_qdb = 0;
    bool accepted = false;
    Ack ack{};
    std::uint8_t step = 0;
    MeterStatus meter{};
};

class TxLink {
public:
    TxLink(const LinkSettings& stored, Duty& duty, bool three_phase);

    // Silence, fallback and revert. Nothing is transmitted from here.
    void poll(std::uint32_t now_ms);
    // The send slot is due. meter_fresh chooses a reading over a status frame.
    AirAction on_slot(std::uint32_t now_ms, bool meter_fresh, const MeterStatus& meter);
    void on_downlink(std::uint32_t now_ms, const Heard& frame);
    void on_ack_missed(std::uint32_t now_ms);

    LinkView view() const;
    LinkMode mode() const { return mode_; }
    ProfileId profile() const { return profile_; }
    std::uint8_t step() const { return step_; }
    const LinkSettings& settings() const { return committed_; }
    bool take_store();
    std::uint32_t next_slot_ms() const { return next_slot_ms_; }
    // Next silence / revert / fallback instant strictly after now, or max uint32.
    std::uint32_t next_wake_ms(std::uint32_t now_ms) const;

private:
    enum class Phase : std::uint8_t { Search, Run, Confirming, Switching };

    struct Mark {
        ProfileId profile = ProfileId::A;
        std::uint8_t step = 0;
        Phase phase = Phase::Search;
        LinkMode mode = LinkMode::Auto;
        std::uint16_t fallback_min = 0;
    };

    void apply_budget();
    void enter(ProfileId id, std::uint8_t step, Phase phase, std::uint32_t now_ms);
    void arm_revert(std::uint32_t now_ms);
    void revert(std::uint32_t now_ms);
    void commit_run(std::uint32_t now_ms);
    void store_committed();
    AirAction begin_send(std::uint32_t now_ms, AirKind kind, bool command);
    std::uint32_t pace_ms(AirKind kind) const;
    bool silence_due(std::uint32_t now_ms) const;

    Duty& duty_;
    bool three_;
    LinkSettings committed_{};
    bool store_ = false;
    LinkMode mode_ = LinkMode::Auto;
    std::uint16_t fallback_min_ = 0;
    ProfileId profile_ = ProfileId::A;
    std::uint8_t step_ = 0;
    Phase phase_ = Phase::Search;
    Mark back_{};
    bool have_back_ = false;
    bool offer_attempt_ = false;
    bool revert_on_ = false;
    std::uint32_t revert_at_ = 0;
    std::uint32_t last_heard_ms_ = 0;
    std::uint32_t next_slot_ms_ = 0;
    bool accept_next_ = false;
    bool switch_after_send_ = false;
    ProfileId switch_profile_ = ProfileId::A;
    std::uint8_t switch_step_ = 0;
    LinkMode switch_mode_ = LinkMode::Auto;
    std::uint16_t switch_fallback_ = 0;
};

class RxLink {
public:
    RxLink(const LinkSettings& stored, Duty& duty, bool three_phase);

    // May return an offer when a step-up or a deferred offer is due.
    AirAction poll(std::uint32_t now_ms);
    AirAction on_uplink(std::uint32_t now_ms, const Heard& frame);
    // Queues a command for the next ACK. False when the values are not valid
    // or a radio change is already in flight.
    bool request(LinkMode mode, ProfileId profile, std::uint8_t step, std::uint16_t fallback_min);

    LinkView view() const;
    LinkMode mode() const { return mode_; }
    ProfileId profile() const { return profile_; }
    std::uint8_t step() const { return step_; }
    const LinkSettings& settings() const { return committed_; }
    bool lock_lost() const { return lock_lost_; }
    bool take_store();
    std::uint32_t next_wake_ms(std::uint32_t now_ms) const;

private:
    enum class Phase : std::uint8_t { Search, Run, Confirming, Switching };

    struct Mark {
        ProfileId profile = ProfileId::A;
        std::uint8_t step = 0;
        Phase phase = Phase::Search;
        LinkMode mode = LinkMode::Auto;
        std::uint16_t fallback_min = 0;
    };

    struct Command {
        bool on = false;
        bool user = false;
        LinkMode mode = LinkMode::Auto;
        ProfileId profile = ProfileId::A;
        std::uint8_t step = 0;
        std::uint16_t fallback_min = 0;
    };

    void apply_budget();
    void enter(ProfileId id, std::uint8_t step, Phase phase, std::uint32_t now_ms);
    void arm_revert(std::uint32_t now_ms);
    void revert(std::uint32_t now_ms);
    void commit_run(std::uint32_t now_ms);
    void store_committed();
    AirAction begin_send(std::uint32_t now_ms, AirKind kind, bool with_command);
    void note_counter(std::uint8_t counter);
    void clear_quality();
    bool loss_over_limit() const;
    bool step_up_ready(std::uint32_t now_ms) const;
    std::int16_t median_qdb() const;
    void queue_down();
    AirAction start_offer(std::uint32_t now_ms, std::uint8_t step);
    bool silence_due(std::uint32_t now_ms) const;

    Duty& duty_;
    bool three_;
    LinkSettings committed_{};
    bool store_ = false;
    LinkMode mode_ = LinkMode::Auto;
    std::uint16_t fallback_min_ = 0;
    ProfileId profile_ = ProfileId::A;
    std::uint8_t step_ = 0;
    Phase phase_ = Phase::Search;
    Mark back_{};
    bool have_back_ = false;
    bool offer_attempt_ = false;
    std::uint8_t offered_step_ = 0;
    bool revert_on_ = false;
    std::uint32_t revert_at_ = 0;
    std::uint32_t last_heard_ms_ = 0;
    std::uint32_t step_entered_ms_ = 0;
    bool lock_lost_ = false;
    Command cmd_{};
    bool confirm_armed_ = false;
    bool have_probe_ = false;
    std::uint8_t last_probe_ = 0;
    std::uint8_t probes_heard_ = 0;
    std::uint8_t probes_missed_ = 0;
    bool have_counter_ = false;
    std::uint8_t last_counter_ = 0;
    std::uint8_t loss_ring_[kLossSpan]{};
    std::uint8_t loss_fill_ = 0;
    std::uint8_t loss_count_ = 0;
    std::uint8_t loss_at_ = 0;
    std::uint32_t stay_ok_ = 0;
    std::uint32_t stay_lost_ = 0;
    std::uint16_t snr_hist_[256]{};
    std::uint32_t snr_n_ = 0;
    std::uint8_t offer_cap_ = kNoOfferCap;
    bool defer_offer_ = false;
    std::uint8_t defer_step_ = 0;
    std::uint32_t defer_at_ = 0;
    bool cmd_timed_ = false;
    std::uint32_t cmd_until_ = 0;
};

}  // namespace p1lora
