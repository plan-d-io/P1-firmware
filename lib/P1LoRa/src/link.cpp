#include "link.h"

namespace p1lora {
namespace {

std::uint16_t on_air_len(AirKind kind, bool three, bool command)
{
    switch (kind) {
    case AirKind::Reading:
    case AirKind::Probe:
        return three ? 53 : 37;
    case AirKind::Status:
        return 9;
    case AirKind::Ack:
        return command ? 10 : 8;
    case AirKind::Offer:
    case AirKind::Confirm:
        return 6;
    case AirKind::None:
        break;
    }
    return 0;
}

std::uint32_t at_least(std::uint32_t value, std::uint32_t floor)
{
    return value < floor ? floor : value;
}

std::int16_t median3(std::int16_t a, std::int16_t b, std::int16_t c)
{
    if (a > b) {
        const std::int16_t t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        const std::int16_t t = b;
        b = c;
        c = t;
    }
    if (a > b) {
        const std::int16_t t = a;
        a = b;
        b = t;
    }
    return b;
}

}  // namespace

std::int16_t floor_qdb(std::uint8_t sf)
{
    if (sf < 7 || sf > 12) {
        return -80;
    }
    // SF7 is −7.5 dB. Each slower spreading factor is another −2.5 dB.
    return static_cast<std::int16_t>(-30 - 10 * (sf - 7));
}

std::int16_t adjust_snr_qdb(std::int16_t measured_qdb, std::uint32_t from_bw, std::uint32_t to_bw)
{
    if (from_bw == 0 || to_bw == 0 || from_bw == to_bw) {
        return measured_qdb;
    }
    if (to_bw > from_bw) {
        return static_cast<std::int16_t>(measured_qdb - kBwOctaveQdb);
    }
    return static_cast<std::int16_t>(measured_qdb + kBwOctaveQdb);
}

std::uint8_t fastest_offer_step(ProfileId id, std::uint8_t measured_step, std::int16_t snr_qdb, std::uint8_t cap)
{
    const Profile& p = profile(id);
    if (p.step_count == 0) {
        return 0;
    }
    if (measured_step >= p.step_count) {
        measured_step = 0;
    }
    const std::uint32_t from_bw = p.steps[measured_step].bw_hz;
    int best = 0;
    for (std::uint8_t s = 0; s < p.step_count; ++s) {
        const std::int16_t eff = adjust_snr_qdb(snr_qdb, from_bw, p.steps[s].bw_hz);
        const std::int16_t need = static_cast<std::int16_t>(floor_qdb(p.steps[s].sf) + kOfferMarginQdb);
        if (eff >= need) {
            best = s;
        }
    }
    if (cap != kNoOfferCap && best > cap) {
        best = cap;
    }
    if (best >= p.step_count) {
        best = p.step_count - 1;
    }
    return static_cast<std::uint8_t>(best);
}

std::uint32_t reading_pace_ms(ProfileId id, std::uint8_t step, bool three_phase)
{
    const Profile& p = profile(id);
    if (step >= p.step_count || p.duty_ms_per_hour == 0) {
        return 1000;
    }
    const std::uint16_t len = three_phase ? 53 : 37;
    const std::uint32_t us = airtime_us(id, step, len);
    const std::uint32_t ms = static_cast<std::uint32_t>(reading_interval_tenths(us, p.duty_ms_per_hour)) * 100u;
    return ms == 0 ? 1u : ms;
}

std::uint32_t revert_window_ms(ProfileId id, std::uint8_t step, bool three_phase)
{
    return at_least(reading_pace_ms(id, step, three_phase) * 3u, kRevertFloorMs);
}

std::uint32_t silence_period_ms(ProfileId id, std::uint8_t step, bool three_phase)
{
    return at_least(reading_pace_ms(id, step, three_phase) * 3u, kSilenceFloorMs);
}

// True when the duty window will not fit one more reading. A hold here is
// this end staying quiet on purpose. It is not a dead channel, so the
// silence timer must not step the link down across it.
bool reading_blocked(Duty& duty, ProfileId id, std::uint8_t step, bool three, std::uint32_t now_ms)
{
    const std::uint32_t air = airtime_us(id, step, on_air_len(AirKind::Reading, three, false));
    if (air == 0) {
        return false;
    }
    return !duty.can_send(now_ms, air);
}

ProbeQuota probe_quota(std::uint32_t interval_ms)
{
    if (interval_ms > kSlowProbeMs) {
        return ProbeQuota{3, 4};
    }
    return ProbeQuota{7, 10};
}

bool probe_quota_met(std::uint8_t heard, std::uint8_t missed, ProbeQuota quota)
{
    const std::uint8_t allow = static_cast<std::uint8_t>(quota.span - quota.need);
    return heard >= quota.need && missed <= allow;
}

bool probe_quota_failed(std::uint8_t heard, std::uint8_t missed, ProbeQuota quota)
{
    const std::uint8_t allow = static_cast<std::uint8_t>(quota.span - quota.need);
    if (missed > allow) {
        return true;
    }
    const unsigned seen = static_cast<unsigned>(heard) + missed;
    return seen >= quota.span && heard < quota.need;
}

bool settings_valid(const LinkSettings& settings)
{
    if (settings.mode != LinkMode::Auto && settings.mode != LinkMode::Locked) {
        return false;
    }
    if (settings.profile != ProfileId::A && settings.profile != ProfileId::B) {
        return false;
    }
    const Profile& p = profile(settings.profile);
    if (settings.locked_step >= p.step_count || settings.last_good_step >= p.step_count) {
        return false;
    }
    if (settings.fallback_min == 0) {
        return true;
    }
    if (settings.fallback_min % 10u != 0) {
        return false;
    }
    return settings.fallback_min / 10u <= 255u;
}

std::uint32_t link_checksum(const LinkImage& image)
{
    std::uint32_t sum = 0x811C9DC5u;
    auto mix = [&](std::uint32_t v) {
        sum ^= v;
        sum *= 16777619u;
    };
    mix(image.magic);
    mix(image.mode);
    mix(image.profile);
    mix(image.locked_step);
    mix(image.last_good_step);
    mix(image.fallback_min);
    return sum;
}

bool link_image_store(const LinkSettings& settings, LinkImage& out)
{
    if (!settings.saved || !settings_valid(settings)) {
        return false;
    }
    out = {};
    out.magic = kLinkMagic;
    out.mode = static_cast<std::uint8_t>(settings.mode);
    out.profile = static_cast<std::uint8_t>(settings.profile);
    out.locked_step = settings.locked_step;
    out.last_good_step = settings.last_good_step;
    out.fallback_min = settings.fallback_min;
    out.checksum = link_checksum(out);
    return true;
}

bool link_image_load(const LinkImage& in, LinkSettings& settings)
{
    if (in.magic != kLinkMagic || in.checksum != link_checksum(in)) {
        return false;
    }
    LinkSettings trial;
    trial.mode = static_cast<LinkMode>(in.mode);
    trial.profile = static_cast<ProfileId>(in.profile);
    trial.locked_step = in.locked_step;
    trial.last_good_step = in.last_good_step;
    trial.fallback_min = in.fallback_min;
    trial.saved = true;
    if (!settings_valid(trial)) {
        return false;
    }
    settings = trial;
    return true;
}

TxLink::TxLink(const LinkSettings& stored, Duty& duty, bool three_phase)
    : duty_(duty), three_(three_phase), committed_(stored)
{
    if (stored.saved && settings_valid(stored)) {
        mode_ = stored.mode;
        fallback_min_ = stored.fallback_min;
        profile_ = stored.profile;
        step_ = stored.mode == LinkMode::Locked ? stored.locked_step : stored.last_good_step;
        phase_ = Phase::Run;
        if (mode_ == LinkMode::Auto) {
            quiet_left_ = kRebootMisses;
        }
    } else {
        committed_.saved = false;
        mode_ = LinkMode::Auto;
        fallback_min_ = 0;
        profile_ = ProfileId::A;
        step_ = 0;
        phase_ = Phase::Search;
    }
    apply_budget();
}

void TxLink::apply_budget()
{
    duty_.set_budget_ms(::p1lora::profile(profile_).duty_ms_per_hour);
}

void TxLink::enter(ProfileId id, std::uint8_t step, Phase phase, std::uint32_t now_ms)
{
    profile_ = id;
    step_ = step;
    phase_ = phase;
    apply_budget();
    last_heard_ms_ = now_ms;
}

void TxLink::arm_revert(std::uint32_t now_ms)
{
    revert_on_ = true;
    revert_at_ = now_ms + revert_window_ms(profile_, step_, three_);
}

void TxLink::revert(std::uint32_t now_ms)
{
    revert_on_ = false;
    accept_next_ = false;
    switch_after_send_ = false;
    offer_attempt_ = false;
    const Mark back = back_;
    const bool have = have_back_;
    have_back_ = false;
    if (!have) {
        mode_ = LinkMode::Auto;
        fallback_min_ = 0;
        enter(ProfileId::A, 0, Phase::Search, now_ms);
    } else {
        mode_ = back.mode;
        fallback_min_ = back.fallback_min;
        enter(back.profile, back.step, back.phase, now_ms);
    }
    next_slot_ms_ = now_ms;
}

void TxLink::store_committed()
{
    committed_.mode = mode_;
    committed_.profile = profile_;
    committed_.fallback_min = fallback_min_;
    committed_.saved = true;
    if (mode_ == LinkMode::Locked) {
        committed_.locked_step = step_;
    } else {
        committed_.last_good_step = step_;
    }
    store_ = true;
}

void TxLink::commit_run(std::uint32_t now_ms)
{
    revert_on_ = false;
    have_back_ = false;
    offer_attempt_ = false;
    phase_ = Phase::Run;
    last_heard_ms_ = now_ms;
    store_committed();
    next_slot_ms_ = now_ms;
}

AirAction TxLink::begin_send(std::uint32_t now_ms, AirKind kind, bool command)
{
    AirAction action;
    const std::uint16_t len = on_air_len(kind, three_, command);
    const std::uint32_t air = airtime_us(profile_, step_, len);
    if (air == 0 || !duty_.can_send(now_ms, air)) {
        action.wait_ms = air == 0 ? 1000u : duty_.wait_ms(now_ms, air);
        if (action.wait_ms == 0) {
            action.wait_ms = 1;
        }
        return action;
    }
    duty_.record(now_ms, air);
    action.kind = kind;
    action.command = command;
    action.sent_profile = profile_;
    action.sent_step = step_;
    action.airtime_us = air;
    return action;
}

std::uint32_t TxLink::pace_ms(AirKind kind) const
{
    if (kind == AirKind::Status) {
        const std::uint32_t sec = status_interval_s(profile_, step_);
        return sec == 0 ? 60000u : sec * 1000u;
    }
    return reading_pace_ms(profile_, step_, three_);
}

bool TxLink::silence_due(std::uint32_t now_ms) const
{
    if (phase_ == Phase::Confirming || phase_ == Phase::Switching) {
        return false;
    }
    if (now_ms < last_heard_ms_) {
        return false;
    }
    return now_ms - last_heard_ms_ >= silence_period_ms(profile_, step_, three_);
}

void TxLink::poll(std::uint32_t now_ms)
{
    // A restored duty clock is already minutes ahead of zero. That is not
    // silence; the quiet stretch starts when this process starts polling.
    if (!clock_armed_) {
        clock_armed_ = true;
        if (last_heard_ms_ == 0) {
            last_heard_ms_ = now_ms;
        }
    }
    if (revert_on_ && now_ms >= revert_at_) {
        revert(now_ms);
        return;
    }
    if (mode_ == LinkMode::Locked) {
        const std::uint32_t wait = static_cast<std::uint32_t>(fallback_min_) * 60000u;
        if (fallback_min_ > 0 && now_ms >= last_heard_ms_ && now_ms - last_heard_ms_ >= wait) {
            mode_ = LinkMode::Auto;
            last_heard_ms_ = now_ms;
            store_committed();
        }
        return;
    }
    if (phase_ == Phase::Run && step_ > 0) {
        if (reading_blocked(duty_, profile_, step_, three_, now_ms)) {
            duty_gagged_ = true;
        } else if (duty_gagged_) {
            duty_gagged_ = false;
            last_heard_ms_ = now_ms;
        }
    }
    if (phase_ == Phase::Run && step_ > 0 && !duty_gagged_ && silence_due(now_ms)) {
        if (silence_strikes_ == 0) {
            silence_strikes_ = 1;
            const std::uint8_t slower = static_cast<std::uint8_t>(step_ - 1u);
            const Phase next = slower == 0 ? Phase::Search : Phase::Run;
            enter(profile_, slower, next, now_ms);
        } else {
            silence_strikes_ = 0;
            enter(profile_, 0, Phase::Search, now_ms);
        }
        next_slot_ms_ = now_ms;
    }
}

AirAction TxLink::on_slot(std::uint32_t now_ms, bool meter_fresh, const MeterStatus& meter)
{
    AirAction early;
    // A switch arms this slot on the old channel, which can be minutes.
    // While the new channel is still unconfirmed, the next frame goes now
    // so it lands inside the revert window. The duty cap still applies.
    if (now_ms < next_slot_ms_ && phase_ != Phase::Switching) {
        early.wait_ms = next_slot_ms_ - now_ms;
        return early;
    }
    const bool data = phase_ == Phase::Run || phase_ == Phase::Switching;
    const AirKind kind = !data ? AirKind::Probe : (meter_fresh ? AirKind::Reading : AirKind::Status);
    const bool accept = accept_next_ &&
                        (kind == AirKind::Reading || kind == AirKind::Status || kind == AirKind::Probe);
    AirAction action = begin_send(now_ms, kind, false);
    if (action.kind == AirKind::None) {
        return action;
    }
    if (kind == AirKind::Status || accept) {
        action.meter = meter;
    }
    if (accept) {
        action.accepted = true;
        action.meter.accepted = true;
        accept_next_ = false;
    }
    next_slot_ms_ = now_ms + pace_ms(kind);
    if (switch_after_send_) {
        back_ = Mark{profile_, step_, phase_, mode_, fallback_min_};
        have_back_ = true;
        offer_attempt_ = false;
        switch_after_send_ = false;
        mode_ = switch_mode_;
        fallback_min_ = switch_fallback_;
        enter(switch_profile_, switch_step_, Phase::Switching, now_ms);
        arm_revert(now_ms);
    }
    return action;
}

void TxLink::on_downlink(std::uint32_t now_ms, const Heard& frame)
{
    quiet_left_ = 0;
    last_heard_ms_ = now_ms;
    silence_strikes_ = 0;
    if (phase_ == Phase::Switching) {
        commit_run(now_ms);
    }
    if (frame.kind == AirKind::Offer) {
        const Profile& here = ::p1lora::profile(profile_);
        if (frame.step >= here.step_count) {
            return;
        }
        if (frame.step == step_ && phase_ != Phase::Search) {
            return;
        }
        back_ = Mark{profile_, step_, phase_, mode_, fallback_min_};
        have_back_ = true;
        offer_attempt_ = true;
        enter(profile_, frame.step, Phase::Confirming, now_ms);
        arm_revert(now_ms);
        next_slot_ms_ = now_ms;
        return;
    }
    if (frame.kind == AirKind::Confirm) {
        if (frame.step == step_ && (phase_ == Phase::Confirming || phase_ == Phase::Search)) {
            commit_run(now_ms);
        }
        return;
    }
    if (frame.kind != AirKind::Ack || !frame.ack.command) {
        return;
    }
    if (phase_ != Phase::Run && phase_ != Phase::Search) {
        return;
    }
    const ProfileId id = frame.ack.profile_bit ? ProfileId::B : ProfileId::A;
    const LinkMode mode = frame.ack.mode_bit ? LinkMode::Locked : LinkMode::Auto;
    if (frame.ack.step >= ::p1lora::profile(id).step_count) {
        return;
    }
    const std::uint16_t fallback = static_cast<std::uint16_t>(frame.ack.fallback) * 10u;
    const bool retune = id != profile_ || frame.ack.step != step_;
    accept_next_ = true;
    next_slot_ms_ = now_ms;
    if (!retune) {
        mode_ = mode;
        fallback_min_ = fallback;
        store_committed();
        return;
    }
    switch_after_send_ = true;
    switch_profile_ = id;
    switch_step_ = frame.ack.step;
    switch_mode_ = mode;
    switch_fallback_ = fallback;
}

void TxLink::on_ack_missed(std::uint32_t now_ms)
{
    // Silence is "no downlink at all". A miss leaves last_heard_ms_ alone.
    // A reboot still on its stored step gives that step three sends, then searches.
    if (quiet_left_ == 0 || mode_ != LinkMode::Auto || phase_ != Phase::Run) {
        return;
    }
    --quiet_left_;
    if (quiet_left_ != 0) {
        return;
    }
    silence_strikes_ = 0;
    enter(profile_, 0, Phase::Search, now_ms);
    next_slot_ms_ = now_ms;
}

void TxLink::force_auto(std::uint32_t now_ms)
{
    if (mode_ == LinkMode::Auto) {
        return;
    }
    mode_ = LinkMode::Auto;
    last_heard_ms_ = now_ms;
    if (phase_ == Phase::Confirming || phase_ == Phase::Switching) {
        revert_on_ = false;
        have_back_ = false;
        offer_attempt_ = false;
        phase_ = step_ == 0 ? Phase::Search : Phase::Run;
    }
    store_committed();
}

LinkView TxLink::view() const
{
    if (phase_ == Phase::Search) {
        return LinkView::Search;
    }
    if (phase_ == Phase::Confirming) {
        return LinkView::Confirming;
    }
    if (phase_ == Phase::Switching) {
        return LinkView::Switching;
    }
    if (mode_ == LinkMode::Locked) {
        return LinkView::Locked;
    }
    return LinkView::Run;
}

bool TxLink::take_store()
{
    const bool due = store_;
    store_ = false;
    return due;
}

std::uint32_t TxLink::next_wake_ms(std::uint32_t now_ms) const
{
    std::uint32_t wake = 0xFFFFFFFFu;
    auto take = [&](std::uint32_t t) {
        if (t > now_ms && t < wake) {
            wake = t;
        }
    };
    if (revert_on_) {
        take(revert_at_);
    }
    if (mode_ == LinkMode::Locked && fallback_min_ > 0) {
        take(last_heard_ms_ + static_cast<std::uint32_t>(fallback_min_) * 60000u);
    } else if (mode_ == LinkMode::Auto && phase_ == Phase::Run && step_ > 0) {
        take(last_heard_ms_ + silence_period_ms(profile_, step_, three_));
    }
    return wake;
}

RxLink::RxLink(const LinkSettings& stored, Duty& duty, bool three_phase)
    : duty_(duty), three_(three_phase), committed_(stored)
{
    if (stored.saved && settings_valid(stored)) {
        mode_ = stored.mode;
        fallback_min_ = stored.fallback_min;
        profile_ = stored.profile;
        step_ = stored.mode == LinkMode::Locked ? stored.locked_step : stored.last_good_step;
        phase_ = Phase::Run;
        if (mode_ == LinkMode::Auto) {
            reboot_arm_ = true;
        }
    } else {
        committed_.saved = false;
        mode_ = LinkMode::Auto;
        fallback_min_ = 0;
        profile_ = ProfileId::A;
        step_ = 0;
        phase_ = Phase::Search;
    }
    apply_budget();
}

void RxLink::apply_budget()
{
    const std::uint32_t budget = ::p1lora::profile(profile_).duty_ms_per_hour;
    duty_.set_budget_ms(budget);
    peer_air_.set_budget_ms(budget);
}

void RxLink::clear_quality()
{
    have_counter_ = false;
    last_counter_ = 0;
    loss_fill_ = 0;
    loss_count_ = 0;
    loss_at_ = 0;
    stay_ok_ = 0;
    stay_lost_ = 0;
    snr_n_ = 0;
    for (std::uint16_t& bin : snr_hist_) {
        bin = 0;
    }
}

void RxLink::clear_candidate()
{
    cand_n_ = 0;
    probes_heard_ = 0;
    probes_missed_ = 0;
    have_probe_ = false;
}

void RxLink::enter(ProfileId id, std::uint8_t step, Phase phase, std::uint32_t now_ms)
{
    const bool moved = id != profile_ || step != step_;
    profile_ = id;
    step_ = step;
    phase_ = phase;
    apply_budget();
    last_heard_ms_ = now_ms;
    if (moved) {
        if (!preserve_step_up_) {
            step_up_wait_ms_ = step_up_base_ms_;
        }
        clear_quality();
        step_entered_ms_ = now_ms;
        probes_heard_ = 0;
        probes_missed_ = 0;
        have_probe_ = false;
        confirm_armed_ = false;
        cand_n_ = 0;
    }
    preserve_step_up_ = false;
    if (phase_ == Phase::Search) {
        clear_candidate();
    }
}

void RxLink::arm_revert(std::uint32_t now_ms)
{
    revert_on_ = true;
    revert_at_ = now_ms + revert_window_ms(profile_, step_, three_);
}

void RxLink::revert(std::uint32_t now_ms)
{
    revert_on_ = false;
    confirm_armed_ = false;
    defer_offer_ = false;
    const bool offer = offer_attempt_;
    const bool step_up = step_up_attempt_;
    const std::uint8_t failed = offered_step_;
    offer_attempt_ = false;
    step_up_attempt_ = false;
    cmd_.on = false;
    cmd_timed_ = false;
    const Mark back = back_;
    const bool have = have_back_;
    have_back_ = false;
    if (offer && step_up) {
        const std::uint32_t doubled = step_up_wait_ms_ > (kStepUpMaxMs / 2u) ? kStepUpMaxMs : step_up_wait_ms_ * 2u;
        step_up_wait_ms_ = doubled > kStepUpMaxMs ? kStepUpMaxMs : doubled;
        preserve_step_up_ = true;
    }
    if (!have) {
        mode_ = LinkMode::Auto;
        fallback_min_ = 0;
        enter(ProfileId::A, 0, Phase::Search, now_ms);
    } else {
        mode_ = back.mode;
        fallback_min_ = back.fallback_min;
        enter(back.profile, back.step, back.phase, now_ms);
    }
    if (offer && !step_up) {
        offer_cap_ = failed == 0 ? 0 : static_cast<std::uint8_t>(failed - 1);
    }
}

void RxLink::store_committed()
{
    committed_.mode = mode_;
    committed_.profile = profile_;
    committed_.fallback_min = fallback_min_;
    committed_.saved = true;
    if (mode_ == LinkMode::Locked) {
        committed_.locked_step = step_;
    } else {
        committed_.last_good_step = step_;
    }
    store_ = true;
}

void RxLink::commit_run(std::uint32_t now_ms)
{
    revert_on_ = false;
    have_back_ = false;
    offer_attempt_ = false;
    confirm_armed_ = false;
    defer_offer_ = false;
    phase_ = Phase::Run;
    last_heard_ms_ = now_ms;
    offer_cap_ = kNoOfferCap;
    step_up_wait_ms_ = step_up_base_ms_;
    step_up_attempt_ = false;
    store_committed();
}

AirAction RxLink::begin_send(std::uint32_t now_ms, AirKind kind, bool with_command)
{
    AirAction action;
    const std::uint16_t len = on_air_len(kind, three_, with_command);
    const std::uint32_t air = airtime_us(profile_, step_, len);
    if (air == 0 || !duty_.can_send(now_ms, air)) {
        action.wait_ms = air == 0 ? 1000u : duty_.wait_ms(now_ms, air);
        if (action.wait_ms == 0) {
            action.wait_ms = 1;
        }
        return action;
    }
    duty_.record(now_ms, air);
    action.kind = kind;
    action.command = with_command;
    action.sent_profile = profile_;
    action.sent_step = step_;
    action.airtime_us = air;
    return action;
}

void RxLink::note_counter(std::uint8_t counter)
{
    auto push = [&](bool lost) {
        if (loss_fill_ == kLossSpan) {
            if (loss_ring_[loss_at_] != 0 && loss_count_ > 0) {
                --loss_count_;
            }
        } else {
            ++loss_fill_;
        }
        loss_ring_[loss_at_] = lost ? 1 : 0;
        if (lost) {
            ++loss_count_;
        }
        loss_at_ = static_cast<std::uint8_t>((loss_at_ + 1u) % kLossSpan);
        if (lost) {
            ++stay_lost_;
        } else {
            ++stay_ok_;
        }
    };
    if (!have_counter_) {
        have_counter_ = true;
        last_counter_ = counter;
        push(false);
        return;
    }
    const Gap gap = counter_gap(last_counter_, counter);
    last_counter_ = counter;
    if (gap.restart) {
        loss_fill_ = 0;
        loss_count_ = 0;
        loss_at_ = 0;
        push(false);
        return;
    }
    for (std::uint8_t i = 0; i < gap.loss; ++i) {
        push(true);
    }
    push(false);
}

void RxLink::note_probe(std::uint8_t counter)
{
    if (have_probe_) {
        const Gap gap = counter_gap(last_probe_, counter);
        if (!gap.restart) {
            const unsigned sum = static_cast<unsigned>(probes_missed_) + gap.loss;
            probes_missed_ = sum > 255u ? 255 : static_cast<std::uint8_t>(sum);
        }
    }
    have_probe_ = true;
    last_probe_ = counter;
    if (probes_heard_ < 0xFF) {
        ++probes_heard_;
    }
}

bool RxLink::loss_over_limit() const
{
    return loss_fill_ == kLossSpan && loss_count_ * 100u > static_cast<unsigned>(kLossSpan) * kLossDownPct;
}

std::int16_t RxLink::median_qdb() const
{
    if (snr_n_ == 0) {
        return -128;
    }
    std::uint32_t need = snr_n_ / 2u + 1u;
    std::uint32_t seen = 0;
    for (int i = 0; i < 256; ++i) {
        seen += snr_hist_[i];
        if (seen >= need) {
            return static_cast<std::int16_t>(i - 128);
        }
    }
    return 127;
}

bool RxLink::step_up_ready(std::uint32_t now_ms) const
{
    if (mode_ != LinkMode::Auto || phase_ != Phase::Run || cmd_.on || revert_on_ || defer_offer_) {
        return false;
    }
    const Profile& here = ::p1lora::profile(profile_);
    if (static_cast<std::uint8_t>(step_ + 1u) >= here.step_count) {
        return false;
    }
    const std::uint8_t next = static_cast<std::uint8_t>(step_ + 1u);
    if (offer_cap_ != kNoOfferCap && next > offer_cap_) {
        return false;
    }
    if (now_ms < step_entered_ms_ || now_ms - step_entered_ms_ < step_up_wait_ms_) {
        return false;
    }
    const std::uint32_t span = stay_ok_ + stay_lost_;
    if (span == 0 || snr_n_ == 0 || stay_lost_ * 100u >= static_cast<std::uint32_t>(kStepUpLossPct) * span) {
        return false;
    }
    const std::int16_t adj = adjust_snr_qdb(median_qdb(), here.steps[step_].bw_hz, here.steps[next].bw_hz);
    const std::int16_t need = static_cast<std::int16_t>(floor_qdb(here.steps[next].sf) + kOfferMarginQdb + kStepUpExtraQdb);
    return adj >= need;
}

void RxLink::queue_down()
{
    cmd_.on = true;
    cmd_.user = false;
    cmd_.mode = LinkMode::Auto;
    cmd_.profile = profile_;
    cmd_.step = static_cast<std::uint8_t>(step_ - 1u);
    cmd_.fallback_min = fallback_min_;
    cmd_timed_ = false;
}

AirAction RxLink::start_offer(std::uint32_t now_ms, std::uint8_t step, bool step_up)
{
    AirAction action = begin_send(now_ms, AirKind::Offer, false);
    if (action.kind == AirKind::None) {
        defer_offer_ = true;
        defer_step_ = step;
        defer_step_up_ = step_up;
        defer_at_ = now_ms + action.wait_ms;
        return action;
    }
    action.step = step;
    back_ = Mark{profile_, step_, phase_, mode_, fallback_min_};
    have_back_ = true;
    offer_attempt_ = true;
    step_up_attempt_ = step_up;
    offered_step_ = step;
    defer_offer_ = false;
    preserve_step_up_ = true;
    enter(profile_, step, Phase::Confirming, now_ms);
    probes_heard_ = 0;
    probes_missed_ = 0;
    have_probe_ = false;
    confirm_armed_ = false;
    cand_n_ = 0;
    arm_revert(now_ms);
    return action;
}

bool RxLink::silence_due(std::uint32_t now_ms) const
{
    if (phase_ == Phase::Confirming || phase_ == Phase::Switching) {
        return false;
    }
    if (now_ms < last_heard_ms_) {
        return false;
    }
    return now_ms - last_heard_ms_ >= silence_period_ms(profile_, step_, three_);
}

bool RxLink::in_flight() const
{
    return cmd_.on || revert_on_ || phase_ == Phase::Confirming || phase_ == Phase::Switching;
}

void RxLink::set_step_up_ms(std::uint32_t ms)
{
    if (ms < 1000u) {
        ms = 1000u;
    }
    step_up_base_ms_ = ms;
    step_up_wait_ms_ = ms;
}

bool RxLink::request(LinkMode mode, ProfileId id, std::uint8_t step, std::uint16_t fallback_min)
{
    LinkSettings trial;
    trial.mode = mode;
    trial.profile = id;
    trial.locked_step = step;
    trial.last_good_step = step;
    trial.fallback_min = fallback_min;
    trial.saved = true;
    if (!settings_valid(trial)) {
        return false;
    }
    if (phase_ == Phase::Confirming || phase_ == Phase::Switching || revert_on_) {
        return false;
    }
    cmd_.on = true;
    cmd_.user = true;
    cmd_.mode = mode;
    cmd_.profile = id;
    cmd_.step = step;
    cmd_.fallback_min = fallback_min;
    cmd_timed_ = false;
    return true;
}

AirAction RxLink::poll(std::uint32_t now_ms)
{
    if (!clock_armed_) {
        clock_armed_ = true;
        if (last_heard_ms_ == 0) {
            last_heard_ms_ = now_ms;
        }
    }
    if (reboot_arm_ && !reboot_deadline_set_) {
        const std::uint32_t pace = reading_pace_ms(profile_, step_, three_);
        std::uint32_t wait = pace > 0xFFFFFFFFu / 3u ? 0xFFFFFFFFu : pace * 3u;
        if (wait < kRebootListenFloorMs) {
            wait = kRebootListenFloorMs;
        }
        const std::uint64_t at = static_cast<std::uint64_t>(now_ms) + wait;
        reboot_deadline_ = at > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(at);
        reboot_deadline_set_ = true;
    }
    if (reboot_arm_ && reboot_deadline_set_ && now_ms >= reboot_deadline_ && phase_ == Phase::Run) {
        reboot_arm_ = false;
        silence_strikes_ = 0;
        enter(profile_, 0, Phase::Search, now_ms);
        return {};
    }
    if (revert_on_ && now_ms >= revert_at_) {
        revert(now_ms);
        return {};
    }
    if (defer_offer_) {
        if (now_ms < defer_at_) {
            return {};
        }
        return start_offer(now_ms, defer_step_, defer_step_up_);
    }
    // One command is attempted for the revert window of its target. After that
    // the receiver stops repeating it, so a dead target cannot be retried forever.
    if (cmd_.on && cmd_timed_ && now_ms >= cmd_until_ && phase_ != Phase::Switching) {
        cmd_.on = false;
        cmd_timed_ = false;
    }
    if (mode_ == LinkMode::Locked) {
        const std::uint32_t wait = static_cast<std::uint32_t>(fallback_min_) * 60000u;
        if (fallback_min_ > 0 && now_ms >= last_heard_ms_ && now_ms - last_heard_ms_ >= wait) {
            mode_ = LinkMode::Auto;
            lock_lost_ = true;
            last_heard_ms_ = now_ms;
            store_committed();
        }
        return {};
    }
    if (phase_ == Phase::Run && step_ > 0) {
        if (reading_blocked(peer_air_, profile_, step_, three_, now_ms)) {
            duty_gagged_ = true;
        } else if (duty_gagged_) {
            duty_gagged_ = false;
            last_heard_ms_ = now_ms;
        }
    }
    if (phase_ == Phase::Run && step_ > 0 && !duty_gagged_ && silence_due(now_ms)) {
        if (silence_strikes_ == 0) {
            silence_strikes_ = 1;
            const std::uint8_t slower = static_cast<std::uint8_t>(step_ - 1u);
            const Phase next = slower == 0 ? Phase::Search : Phase::Run;
            enter(profile_, slower, next, now_ms);
        } else {
            silence_strikes_ = 0;
            enter(profile_, 0, Phase::Search, now_ms);
        }
        return {};
    }
    if (step_up_ready(now_ms)) {
        return start_offer(now_ms, static_cast<std::uint8_t>(step_ + 1u), true);
    }
    return {};
}

AirAction RxLink::on_uplink(std::uint32_t now_ms, const Heard& frame)
{
    reboot_arm_ = false;
    last_heard_ms_ = now_ms;
    silence_strikes_ = 0;
    const std::uint16_t heard_len = on_air_len(frame.kind, three_, false);
    const std::uint32_t heard_air = airtime_us(profile_, step_, heard_len);
    if (heard_air != 0) {
        peer_air_.record(now_ms, heard_air);
    }
    if (phase_ == Phase::Switching) {
        commit_run(now_ms);
    }

    const bool probe = frame.kind == AirKind::Probe;
    const bool probing = phase_ == Phase::Confirming || phase_ == Phase::Search ||
                         (phase_ == Phase::Run && step_ == 0);
    if (probe && probing) {
        note_probe(frame.counter);
    }
    note_counter(frame.counter);
    if (snr_n_ < 0xFFFFFFFFu) {
        int q = frame.snr_qdb;
        if (q < -128) {
            q = -128;
        }
        if (q > 127) {
            q = 127;
        }
        std::uint16_t& bin = snr_hist_[q + 128];
        if (bin < 0xFFFF) {
            ++bin;
            ++snr_n_;
        }
    }

    if (frame.accepted && cmd_.on) {
        const bool retune = cmd_.profile != profile_ || cmd_.step != step_;
        if (retune) {
            back_ = Mark{profile_, step_, phase_, mode_, fallback_min_};
            have_back_ = true;
            offer_attempt_ = false;
            const Command cmd = cmd_;
            cmd_.on = false;
            mode_ = cmd.mode;
            fallback_min_ = cmd.fallback_min;
            enter(cmd.profile, cmd.step, Phase::Switching, now_ms);
            arm_revert(now_ms);
        } else {
            mode_ = cmd_.mode;
            fallback_min_ = cmd_.fallback_min;
            cmd_.on = false;
            if (mode_ == LinkMode::Locked) {
                lock_lost_ = false;
            }
            phase_ = Phase::Run;
            store_committed();
        }
    }

    if (phase_ == Phase::Confirming && confirm_armed_ &&
        (frame.kind == AirKind::Reading || frame.kind == AirKind::Status)) {
        commit_run(now_ms);
    }

    if (frame.kind == AirKind::Probe && phase_ == Phase::Confirming) {
        const ProbeQuota quota = probe_quota(reading_pace_ms(profile_, step_, three_));
        // SNR does not confirm. Only the counter does, and only once the quota is met.
        if (probe_quota_met(probes_heard_, probes_missed_, quota)) {
            if (!confirm_armed_) {
                confirm_armed_ = true;
                arm_revert(now_ms);
            }
            AirAction confirm = begin_send(now_ms, AirKind::Confirm, false);
            confirm.step = step_;
            return confirm;
        }
        return {};
    }

    if (frame.kind == AirKind::Probe && mode_ == LinkMode::Auto && !revert_on_ && !cmd_.on &&
        (phase_ == Phase::Search || (phase_ == Phase::Run && step_ == 0))) {
        if (cand_n_ < 3) {
            int q = frame.snr_qdb;
            if (q < -128) {
                q = -128;
            }
            if (q > 127) {
                q = 127;
            }
            cand_snr_[cand_n_++] = static_cast<std::int16_t>(q);
        }
        const ProbeQuota quota = probe_quota(reading_pace_ms(profile_, step_, three_));
        const bool failed = probe_quota_failed(probes_heard_, probes_missed_, quota);
        const bool met = probe_quota_met(probes_heard_, probes_missed_, quota);
        if (cand_n_ < 3) {
            if (failed && phase_ == Phase::Search) {
                offer_cap_ = step_ == 0 ? 0 : static_cast<std::uint8_t>(step_ - 1u);
                clear_candidate();
            }
            return {};
        }
        const std::int16_t med = median3(cand_snr_[0], cand_snr_[1], cand_snr_[2]);
        const std::uint8_t pick = fastest_offer_step(profile_, step_, med, offer_cap_);
        if (pick != step_ && phase_ == Phase::Search) {
            return start_offer(now_ms, pick, false);
        }
        if (pick == step_ && met) {
            AirAction confirm = begin_send(now_ms, AirKind::Confirm, false);
            if (confirm.kind == AirKind::None) {
                return confirm;
            }
            confirm.step = step_;
            if (phase_ != Phase::Run) {
                commit_run(now_ms);
            }
            return confirm;
        }
        if (failed && phase_ == Phase::Search) {
            offer_cap_ = step_ == 0 ? 0 : static_cast<std::uint8_t>(step_ - 1u);
            clear_candidate();
        }
        return {};
    }

    if (frame.kind != AirKind::Reading && frame.kind != AirKind::Status) {
        return {};
    }
    if (phase_ == Phase::Run && mode_ == LinkMode::Auto && step_ > 0 && !cmd_.on && loss_over_limit()) {
        queue_down();
    }
    const bool with = cmd_.on;
    if (with && !cmd_timed_) {
        const bool retune = cmd_.profile != profile_ || cmd_.step != step_;
        if (retune) {
            cmd_timed_ = true;
            cmd_until_ = now_ms + revert_window_ms(cmd_.profile, cmd_.step, three_);
        }
    }
    AirAction ack = begin_send(now_ms, AirKind::Ack, with);
    if (ack.kind == AirKind::None) {
        return ack;
    }
    ack.ack.counter = frame.counter;
    int q = frame.snr_qdb;
    if (q > 127) {
        q = 127;
    }
    if (q < -128) {
        q = -128;
    }
    ack.ack.snr = static_cast<std::int8_t>(q);
    if (with) {
        ack.ack.command = true;
        ack.ack.flags = 0x02;
        ack.ack.profile_bit = cmd_.profile == ProfileId::B ? 1 : 0;
        ack.ack.mode_bit = cmd_.mode == LinkMode::Locked ? 1 : 0;
        ack.ack.step = cmd_.step;
        ack.ack.fallback = static_cast<std::uint8_t>(cmd_.fallback_min / 10u);
    }
    return ack;
}

LinkView RxLink::view() const
{
    if (phase_ == Phase::Search) {
        return LinkView::Search;
    }
    if (phase_ == Phase::Confirming) {
        return LinkView::Confirming;
    }
    if (phase_ == Phase::Switching) {
        return LinkView::Switching;
    }
    if (lock_lost_) {
        return LinkView::LockLost;
    }
    if (mode_ == LinkMode::Locked) {
        return LinkView::Locked;
    }
    return LinkView::Run;
}

bool RxLink::take_store()
{
    const bool due = store_;
    store_ = false;
    return due;
}

std::uint32_t RxLink::next_wake_ms(std::uint32_t now_ms) const
{
    std::uint32_t wake = 0xFFFFFFFFu;
    auto take = [&](std::uint32_t t) {
        if (t > now_ms && t < wake) {
            wake = t;
        }
    };
    if (revert_on_) {
        take(revert_at_);
    }
    if (defer_offer_) {
        take(defer_at_);
    }
    if (mode_ == LinkMode::Locked && fallback_min_ > 0) {
        take(last_heard_ms_ + static_cast<std::uint32_t>(fallback_min_) * 60000u);
    } else if (mode_ == LinkMode::Auto && phase_ == Phase::Run && step_ > 0) {
        take(last_heard_ms_ + silence_period_ms(profile_, step_, three_));
    }
    if (reboot_arm_ && reboot_deadline_set_) {
        take(reboot_deadline_);
    }
    return wake;
}

}  // namespace p1lora
