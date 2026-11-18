#include <P1LoRa.h>
#include <link.h>

#include <unity.h>

#include <cstdint>
#include <vector>

namespace {

struct SendRec {
    std::uint32_t ms = 0;
    std::uint32_t air_us = 0;
    std::uint32_t budget_us = 0;
};

struct OfferRec {
    std::uint32_t ms = 0;
    std::uint8_t step = 0;
};

std::uint32_t ceil_ms(std::uint32_t us)
{
    return (us + 999u) / 1000u;
}

std::uint32_t xs(std::uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// Both ends, a fake clock, and one loss/SNR pair per profile step.
// Offers, confirms and "command accepted" frames take an extra loss draw.
// A lost command leaves the ACK itself, so the other end still hears the link.
// Seeds are the xorshift state.
struct World {
    p1lora::Duty txd;
    p1lora::Duty rxd;
    p1lora::TxLink tx;
    p1lora::RxLink rx;
    std::uint32_t now = 0;
    std::uint32_t tx_due = 0;
    std::uint32_t rng;
    bool meter = true;
    bool spun = false;
    bool diverge_broke = false;
    std::uint32_t diverge_max = 0;
    std::uint32_t diverge_broke_at = 0;
    std::uint8_t loss_a[6]{};
    std::uint8_t loss_b[7]{};
    std::int16_t snr_a[6]{};
    std::int16_t snr_b[7]{};
    std::uint8_t ctrl_loss = 0;
    std::uint8_t tx_ctr = 0;
    std::uint8_t rx_ctr = 0;
    std::vector<SendRec> tx_log;
    std::vector<SendRec> rx_log;
    std::vector<OfferRec> offers;
    bool saw_profile_b = false;
    bool saw_accepted_status = false;
    p1lora::MeterStatus accepted_status{};
    int watch_step = -1;
    p1lora::ProfileId watch_profile = p1lora::ProfileId::A;
    std::uint32_t watch_ms = 0xFFFFFFFFu;
    bool watch_search = false;
    bool want_home = false;
    bool home_left = false;
    std::uint32_t home_ms = 0xFFFFFFFFu;

    explicit World(p1lora::LinkSettings stored = {}, std::uint32_t seed = 1)
        : txd(360000u),
          rxd(360000u),
          tx(stored, txd, true),
          rx(stored, rxd, true),
          rng(seed == 0 ? 1u : seed)
    {
        set_snr(48);
    }

    void set_snr(std::int16_t qdb)
    {
        for (std::int16_t& s : snr_a) {
            s = qdb;
        }
        for (std::int16_t& s : snr_b) {
            s = qdb;
        }
    }

    void set_loss(std::uint8_t pct)
    {
        for (std::uint8_t& s : loss_a) {
            s = pct;
        }
        for (std::uint8_t& s : loss_b) {
            s = pct;
        }
    }

    bool steady(p1lora::LinkView v) const
    {
        return v == p1lora::LinkView::Run || v == p1lora::LinkView::Locked || v == p1lora::LinkView::LockLost;
    }

    std::uint8_t loss_of(p1lora::ProfileId id, std::uint8_t step) const
    {
        if (id == p1lora::ProfileId::B) {
            return step < 7 ? loss_b[step] : 100;
        }
        return step < 6 ? loss_a[step] : 100;
    }

    std::int16_t snr_of(p1lora::ProfileId id, std::uint8_t step) const
    {
        if (id == p1lora::ProfileId::B) {
            return step < 7 ? snr_b[step] : 0;
        }
        return step < 6 ? snr_a[step] : 0;
    }

    bool dropped(p1lora::ProfileId id, std::uint8_t step, bool control)
    {
        const std::uint8_t pct = loss_of(id, step);
        if (pct >= 100) {
            return true;
        }
        if (pct > 0 && (xs(rng) % 100u) < pct) {
            return true;
        }
        if (control && ctrl_loss > 0 && (xs(rng) % 100u) < ctrl_loss) {
            return true;
        }
        return false;
    }

    static bool control_frame(const p1lora::AirAction& act)
    {
        if (act.kind == p1lora::AirKind::Offer || act.kind == p1lora::AirKind::Confirm) {
            return true;
        }
        if (act.kind == p1lora::AirKind::Ack && act.command) {
            return true;
        }
        return act.accepted;
    }

    void log_send(std::vector<SendRec>& log, std::uint32_t ms, const p1lora::AirAction& act)
    {
        SendRec rec;
        rec.ms = ms;
        rec.air_us = act.airtime_us;
        rec.budget_us = p1lora::profile(act.sent_profile).duty_ms_per_hour * 1000u;
        log.push_back(rec);
        if (act.sent_profile == p1lora::ProfileId::B) {
            saw_profile_b = true;
        }
        if (act.kind == p1lora::AirKind::Offer) {
            offers.push_back(OfferRec{ms, act.step});
        }
        if (act.kind == p1lora::AirKind::Status && act.accepted) {
            saw_accepted_status = true;
            accepted_status = act.meter;
        }
    }

    std::uint32_t allowance(p1lora::ProfileId id, std::uint8_t step) const
    {
        return p1lora::revert_window_ms(id, step, true) + p1lora::silence_period_ms(id, step, true);
    }

    bool diverged = false;
    std::uint32_t div_from = 0;
    std::uint32_t div_allow = 0;

    void note(std::uint32_t t)
    {
        const bool diff = tx.profile() != rx.profile() || tx.step() != rx.step();
        const std::uint32_t allow = allowance(tx.profile(), tx.step()) > allowance(rx.profile(), rx.step())
                                        ? allowance(tx.profile(), tx.step())
                                        : allowance(rx.profile(), rx.step());
        if (diff) {
            if (!diverged) {
                diverged = true;
                div_from = t;
                div_allow = allow;
            } else if (allow > div_allow) {
                div_allow = allow;
            }
            if (t >= div_from && t - div_from > div_allow) {
                diverge_broke = true;
                diverge_broke_at = t;
            }
            if (t >= div_from && t - div_from > diverge_max) {
                diverge_max = t - div_from;
            }
        } else if (diverged) {
            if (t >= div_from && t - div_from > diverge_max) {
                diverge_max = t - div_from;
            }
            diverged = false;
        }
        if (watch_step >= 0 && watch_ms == 0xFFFFFFFFu && tx.step() == static_cast<std::uint8_t>(watch_step) &&
            rx.step() == tx.step() && tx.profile() == watch_profile && rx.profile() == watch_profile) {
            const bool tx_ok = watch_search ? tx.view() == p1lora::LinkView::Search : steady(tx.view());
            const bool rx_ok = watch_search ? rx.view() == p1lora::LinkView::Search : steady(rx.view());
            if (tx_ok && rx_ok) {
                watch_ms = t;
            }
        }
        if (want_home && (tx.profile() != rx.profile() || tx.step() != rx.step())) {
            home_left = true;
        }
        if (want_home && home_left && home_ms == 0xFFFFFFFFu && tx.profile() == rx.profile() && tx.step() == rx.step() &&
            tx.profile() == p1lora::ProfileId::A && steady(tx.view()) && steady(rx.view())) {
            home_ms = t;
        }
    }

    void fly_down(std::uint32_t start, p1lora::AirAction act)
    {
        if (act.kind == p1lora::AirKind::None) {
            return;
        }
        log_send(rx_log, start, act);
        const std::uint32_t arrive = start + ceil_ms(act.airtime_us);
        p1lora::Heard heard;
        heard.kind = act.kind;
        heard.counter = rx_ctr++;
        heard.accepted = act.accepted;
        heard.ack = act.ack;
        heard.step = act.step;
        heard.meter = act.meter;
        heard.snr_qdb = snr_of(act.sent_profile, act.sent_step);
        if (act.kind == p1lora::AirKind::Ack && act.command && ctrl_loss > 0 && (xs(rng) % 100u) < ctrl_loss) {
            act.command = false;
            act.ack.command = false;
            act.ack.flags = static_cast<std::uint8_t>(act.ack.flags & ~0x02u);
            heard.ack.command = false;
            heard.ack.flags = act.ack.flags;
        }
        const bool same = tx.profile() == act.sent_profile && tx.step() == act.sent_step;
        const bool drop = !same || dropped(act.sent_profile, act.sent_step, control_frame(act));
        if (drop) {
            if (act.kind == p1lora::AirKind::Ack) {
                tx.on_ack_missed(arrive);
            }
            note(arrive);
            tx_due = tx.next_slot_ms() > now ? tx.next_slot_ms() : now;
            return;
        }
        tx.on_downlink(arrive, heard);
        note(arrive);
        tx_due = tx.next_slot_ms() > now ? tx.next_slot_ms() : now;
    }

    void fly_up()
    {
        p1lora::MeterStatus body;
        body.reason = p1lora::kStatusNoneForAWhile;
        body.since_s = 10;
        p1lora::AirAction act = tx.on_slot(now, meter, body);
        if (act.kind == p1lora::AirKind::None) {
            tx_due = now + (act.wait_ms == 0 ? 1u : act.wait_ms);
            return;
        }
        log_send(tx_log, now, act);
        const std::uint32_t arrive = now + ceil_ms(act.airtime_us);
        p1lora::Heard heard;
        heard.kind = act.kind;
        heard.counter = tx_ctr++;
        heard.accepted = act.accepted;
        heard.meter = act.meter;
        heard.snr_qdb = snr_of(act.sent_profile, act.sent_step);
        const bool same = rx.profile() == act.sent_profile && rx.step() == act.sent_step;
        if (!same || dropped(act.sent_profile, act.sent_step, control_frame(act))) {
            tx.on_ack_missed(arrive);
            note(arrive);
            tx_due = tx.next_slot_ms() > now ? tx.next_slot_ms() : now;
            return;
        }
        p1lora::AirAction resp = rx.on_uplink(arrive, heard);
        note(arrive);
        if (resp.kind == p1lora::AirKind::None) {
            tx.on_ack_missed(arrive);
            tx_due = tx.next_slot_ms() > now ? tx.next_slot_ms() : now;
            return;
        }
        fly_down(arrive, resp);
    }

    void run_until(std::uint32_t end)
    {
        int guard = 0;
        while (now < end && guard++ < 2000000) {
        tx.poll(now);
        if (tx.next_slot_ms() <= now) {
            tx_due = now;
        } else if (tx.next_slot_ms() < tx_due) {
            tx_due = tx.next_slot_ms();
        }
            p1lora::AirAction extra = rx.poll(now);
            if (extra.kind != p1lora::AirKind::None) {
                fly_down(now, extra);
            }
            if (now >= tx_due) {
                fly_up();
            }
            note(now);
            if (now >= end) {
                break;
            }
            std::uint32_t next = end;
            auto take = [&](std::uint32_t t) {
                if (t > now && t < next) {
                    next = t;
                }
            };
            take(tx_due);
            take(tx.next_wake_ms(now));
            take(rx.next_wake_ms(now));
            if (next <= now) {
                if (tx_due > now) {
                    now += 1;
                }
                continue;
            }
            now = next;
        }
        if (guard >= 2000000) {
            spun = true;
        }
        if (diverged && now >= div_from && now - div_from > diverge_max) {
            diverge_max = now - div_from;
        }
    }

    void check_duty() const
    {
        auto one = [](const std::vector<SendRec>& log) {
            for (std::size_t i = 0; i < log.size(); ++i) {
                std::uint64_t sum = 0;
                const std::uint32_t t = log[i].ms;
                for (std::size_t j = 0; j <= i; ++j) {
                    if (t - log[j].ms <= 3600000u) {
                        sum += log[j].air_us;
                    }
                }
                if (sum > log[i].budget_us) {
                    return false;
                }
            }
            return true;
        };
        TEST_ASSERT_TRUE(one(tx_log));
        TEST_ASSERT_TRUE(one(rx_log));
    }
};

bool both_at(const World& w, std::uint8_t step, p1lora::ProfileId id, p1lora::LinkView view)
{
    return w.tx.step() == step && w.rx.step() == step && w.tx.profile() == id && w.rx.profile() == id &&
           w.tx.view() == view && w.rx.view() == view;
}

void finish(World& w, const char* name)
{
    TEST_ASSERT_FALSE_MESSAGE(w.spun, name);
    TEST_ASSERT_FALSE_MESSAGE(w.diverge_broke, name);
    w.check_duty();
}

}  // namespace

void test_link_offer_snr()
{
    using p1lora::ProfileId;
    TEST_ASSERT_EQUAL_UINT8(5, p1lora::fastest_offer_step(ProfileId::A, 0, 48));
    TEST_ASSERT_EQUAL_UINT8(4, p1lora::fastest_offer_step(ProfileId::A, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, p1lora::fastest_offer_step(ProfileId::A, 0, -32));
    // 12 dB measured at BW 125 is 9 dB at BW 250, still enough for SF7.
    TEST_ASSERT_EQUAL_UINT8(6, p1lora::fastest_offer_step(ProfileId::B, 0, 48));
    // 4 dB at BW 125 is 1 dB at BW 250: SF8 fits, SF7 does not.
    TEST_ASSERT_EQUAL_UINT8(5, p1lora::fastest_offer_step(ProfileId::B, 0, 16));
    // −8 dB at BW 125 loses the extra 3 dB and no longer clears even SF12 at BW 250.
    TEST_ASSERT_EQUAL_UINT8(0, p1lora::fastest_offer_step(ProfileId::B, 0, -32));
    TEST_ASSERT_EQUAL_UINT8(4, p1lora::fastest_offer_step(ProfileId::A, 0, 48, 4));

    p1lora::LinkSettings bad;
    bad.saved = true;
    bad.fallback_min = 15;
    TEST_ASSERT_FALSE(p1lora::settings_valid(bad));
    bad.fallback_min = 10;
    TEST_ASSERT_TRUE(p1lora::settings_valid(bad));
    bad.last_good_step = 6;
    TEST_ASSERT_FALSE(p1lora::settings_valid(bad));
}

void test_link_first_boot()
{
    World w;
    w.watch_step = 5;
    w.run_until(120000u);
    TEST_ASSERT_EQUAL_UINT32(5701u, w.watch_ms);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));
    TEST_ASSERT_TRUE(w.tx.settings().saved);
    TEST_ASSERT_TRUE(w.rx.settings().saved);
    TEST_ASSERT_EQUAL_UINT8(5, w.tx.settings().last_good_step);
    TEST_ASSERT_EQUAL_UINT8(5, w.rx.settings().last_good_step);
    TEST_ASSERT_NOT_EQUAL(0xFFFFFFFFu, w.watch_ms);
    finish(w, "first");
}

void test_link_snr_steps()
{
    World zero;
    zero.set_snr(0);
    zero.watch_step = 4;
    zero.run_until(180000u);
    TEST_ASSERT_EQUAL_UINT32(7490u, zero.watch_ms);
    TEST_ASSERT_TRUE(both_at(zero, 4, p1lora::ProfileId::A, p1lora::LinkView::Run));
    finish(zero, "snr0");

    World weak;
    weak.set_snr(-32);
    weak.watch_step = 0;
    weak.run_until(180000u);
    TEST_ASSERT_EQUAL_UINT32(3458u, weak.watch_ms);
    TEST_ASSERT_TRUE(both_at(weak, 0, p1lora::ProfileId::A, p1lora::LinkView::Run));
    finish(weak, "snr-8");
}

void test_link_loss_step_down()
{
    World w({}, 7);
    w.watch_step = 5;
    w.run_until(120000u);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));
    const std::uint32_t up = w.now;
    w.loss_a[5] = 50;
    w.watch_step = 4;
    w.watch_ms = 0xFFFFFFFFu;
    w.run_until(up + 180000u);
    TEST_ASSERT_EQUAL_UINT32(73249u, w.watch_ms - up);
    TEST_ASSERT_TRUE(both_at(w, 4, p1lora::ProfileId::A, p1lora::LinkView::Run));
    const std::uint32_t at = w.now;
    w.loss_a[5] = 0;
    w.run_until(at + 120000u);
    TEST_ASSERT_TRUE(both_at(w, 4, p1lora::ProfileId::A, p1lora::LinkView::Run));
    finish(w, "loss");
}

void test_link_silence_search()
{
    World w;
    w.watch_step = 5;
    w.run_until(120000u);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));
    const std::uint32_t dead = w.now;
    w.set_loss(100);
    w.watch_step = 0;
    w.watch_search = true;
    w.watch_ms = 0xFFFFFFFFu;
    w.run_until(dead + 900000u);
    TEST_ASSERT_EQUAL_UINT32(602204u, w.watch_ms - dead);
    TEST_ASSERT_EQUAL_UINT8(0, w.tx.step());
    TEST_ASSERT_EQUAL_UINT8(0, w.rx.step());
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::Search), static_cast<int>(w.tx.view()));
    w.set_loss(0);
    w.set_snr(48);
    w.watch_step = 5;
    w.watch_search = false;
    w.watch_ms = 0xFFFFFFFFu;
    const std::uint32_t back = w.now;
    w.run_until(back + 180000u);
    TEST_ASSERT_EQUAL_UINT32(26539u, w.watch_ms - back);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));
    finish(w, "silence");
}

void test_link_step_up()
{
    World w;
    w.set_snr(0);
    w.watch_step = 4;
    w.run_until(180000u);
    TEST_ASSERT_TRUE(both_at(w, 4, p1lora::ProfileId::A, p1lora::LinkView::Run));
    const std::uint32_t at = w.watch_ms;
    w.set_snr(48);
    w.run_until(at + 29u * 60u * 1000u);
    TEST_ASSERT_EQUAL_UINT8(4, w.tx.step());
    TEST_ASSERT_EQUAL_UINT8(4, w.rx.step());
    w.watch_step = 5;
    w.watch_ms = 0xFFFFFFFFu;
    w.run_until(at + 33u * 60u * 1000u);
    TEST_ASSERT_EQUAL_UINT32(2u, static_cast<std::uint32_t>(w.offers.size()));
    TEST_ASSERT_EQUAL_UINT32(at + 1796215u, w.offers.back().ms);
    TEST_ASSERT_EQUAL_UINT32(at + 1798520u, w.watch_ms);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));
    finish(w, "step-up");
}

void test_link_offer_reverts()
{
    World w;
    w.loss_a[5] = 100;
    w.watch_step = 4;
    w.run_until(180000u);
    TEST_ASSERT_EQUAL_UINT32(2u, static_cast<std::uint32_t>(w.offers.size()));
    TEST_ASSERT_EQUAL_UINT8(5, w.offers[0].step);
    TEST_ASSERT_EQUAL_UINT32(2466u, w.offers[0].ms);
    TEST_ASSERT_EQUAL_UINT8(4, w.offers[1].step);
    TEST_ASSERT_EQUAL_UINT32(65924u, w.offers[1].ms);
    TEST_ASSERT_TRUE(both_at(w, 4, p1lora::ProfileId::A, p1lora::LinkView::Run));
    finish(w, "revert");
}

void test_link_no_split()
{
    // Seeds 1000..1199. Control loss percent is (seed * 17) % 61.
    // Offers, confirms and accepted frames are dropped. A lost command is
    // taken off the ACK; the ACK itself still arrives.
    std::uint32_t worst = 0;
    std::uint32_t worst_seed = 0;
    for (std::uint32_t seed = 1000; seed < 1200; ++seed) {
        World w({}, seed);
        w.ctrl_loss = static_cast<std::uint8_t>((seed * 17u) % 61u);
        w.run_until(10u * 60u * 1000u);
        const bool met = w.tx.settings().saved && w.rx.settings().saved && w.tx.step() == w.rx.step() &&
                         w.tx.profile() == w.rx.profile() && w.tx.view() == p1lora::LinkView::Run &&
                         w.rx.view() == p1lora::LinkView::Run;
        TEST_ASSERT_TRUE_MESSAGE(met, "bring-up did not meet");
        TEST_ASSERT_TRUE(w.rx.request(p1lora::LinkMode::Locked, w.rx.profile(), w.rx.step(), 0));
        const std::uint32_t lock_cap = w.now + 300000u;
        while (w.now < lock_cap &&
               (w.tx.view() != p1lora::LinkView::Locked || w.rx.view() != p1lora::LinkView::Locked)) {
            w.run_until(w.now + 1000u);
        }
        TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::Locked), static_cast<int>(w.tx.view()));
        TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::Locked), static_cast<int>(w.rx.view()));
        for (int i = 0; i < 7; ++i) {
            w.loss_b[i] = 100;
        }
        TEST_ASSERT_TRUE(w.rx.request(p1lora::LinkMode::Auto, p1lora::ProfileId::B, 0, 0));
        const std::uint32_t window = p1lora::revert_window_ms(p1lora::ProfileId::B, 0, true);
        w.run_until(w.now + 2u * window + 180000u);
        if (w.diverge_max > worst) {
            worst = w.diverge_max;
            worst_seed = seed;
        }
        TEST_ASSERT_FALSE_MESSAGE(w.spun, "split run spun");
        TEST_ASSERT_FALSE_MESSAGE(w.diverge_broke, "split run diverged too long");
        TEST_ASSERT_EQUAL(static_cast<int>(w.tx.profile()), static_cast<int>(w.rx.profile()));
        TEST_ASSERT_EQUAL_UINT8(w.tx.step(), w.rx.step());
        w.check_duty();
    }
    TEST_ASSERT_EQUAL_UINT32(739800u, worst);
    TEST_ASSERT_EQUAL_UINT32(1000u, worst_seed);
}

void test_link_locked()
{
    p1lora::LinkSettings stored;
    stored.saved = true;
    stored.mode = p1lora::LinkMode::Locked;
    stored.profile = p1lora::ProfileId::A;
    stored.locked_step = 3;
    stored.last_good_step = 3;

    World held(stored, 3);
    held.set_loss(100);
    held.run_until(3600000u);
    TEST_ASSERT_EQUAL_UINT8(3, held.tx.step());
    TEST_ASSERT_EQUAL_UINT8(3, held.rx.step());
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkMode::Locked), static_cast<int>(held.tx.mode()));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkMode::Locked), static_cast<int>(held.rx.mode()));
    TEST_ASSERT_FALSE(held.rx.lock_lost());
    finish(held, "locked");

    stored.fallback_min = 10;
    World fell(stored, 4);
    fell.set_loss(100);
    fell.run_until(10u * 60u * 1000u - 1000u);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkMode::Locked), static_cast<int>(fell.tx.mode()));
    fell.run_until(10u * 60u * 1000u + 1000u);
    TEST_ASSERT_EQUAL_UINT32(601000u, fell.now);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkMode::Auto), static_cast<int>(fell.tx.mode()));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkMode::Auto), static_cast<int>(fell.rx.mode()));
    TEST_ASSERT_TRUE(fell.rx.lock_lost());
    TEST_ASSERT_EQUAL_UINT8(3, fell.tx.step());
    TEST_ASSERT_EQUAL_UINT8(3, fell.rx.step());
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::LockLost), static_cast<int>(fell.rx.view()));
    finish(fell, "fallback");
}

void test_link_commands()
{
    World w;
    w.watch_step = 5;
    w.run_until(120000u);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));
    TEST_ASSERT_TRUE(w.rx.request(p1lora::LinkMode::Locked, p1lora::ProfileId::A, 5, 0));
    w.run_until(w.now + 60000u);
    TEST_ASSERT_EQUAL_UINT8(5, w.tx.step());
    TEST_ASSERT_EQUAL_UINT8(5, w.rx.step());
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::Locked), static_cast<int>(w.tx.view()));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::Locked), static_cast<int>(w.rx.view()));

    TEST_ASSERT_TRUE(w.rx.request(p1lora::LinkMode::Auto, p1lora::ProfileId::B, 0, 0));
    w.watch_step = 0;
    w.watch_profile = p1lora::ProfileId::B;
    w.watch_ms = 0xFFFFFFFFu;
    w.run_until(w.now + 180000u);
    TEST_ASSERT_EQUAL_UINT32(184449u, w.watch_ms);
    TEST_ASSERT_TRUE(both_at(w, 0, p1lora::ProfileId::B, p1lora::LinkView::Run));

    TEST_ASSERT_TRUE(w.rx.request(p1lora::LinkMode::Auto, p1lora::ProfileId::A, 5, 0));
    w.watch_step = 5;
    w.watch_profile = p1lora::ProfileId::A;
    w.watch_ms = 0xFFFFFFFFu;
    w.run_until(w.now + 180000u);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));

    for (int i = 0; i < 7; ++i) {
        w.loss_b[i] = 100;
    }
    const std::uint32_t before = w.now;
    w.want_home = true;
    TEST_ASSERT_TRUE(w.rx.request(p1lora::LinkMode::Auto, p1lora::ProfileId::B, 0, 0));
    const std::uint32_t window = p1lora::revert_window_ms(p1lora::ProfileId::B, 0, true);
    w.run_until(before + window + 120000u);
    TEST_ASSERT_EQUAL_UINT32(before + 740592u, w.home_ms);
    TEST_ASSERT_TRUE(w.saw_profile_b);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::ProfileId::A), static_cast<int>(w.tx.profile()));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::ProfileId::A), static_cast<int>(w.rx.profile()));
    TEST_ASSERT_EQUAL_UINT8(w.tx.step(), w.rx.step());
    finish(w, "commands");
}

void test_link_resume()
{
    p1lora::LinkSettings stored;
    stored.saved = true;
    stored.mode = p1lora::LinkMode::Auto;
    stored.profile = p1lora::ProfileId::A;
    stored.last_good_step = 5;
    stored.locked_step = 3;
    p1lora::Duty duty(360000u);
    p1lora::TxLink tx(stored, duty, true);
    p1lora::MeterStatus meter;
    const p1lora::AirAction first = tx.on_slot(0, true, meter);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::AirKind::Reading), static_cast<int>(first.kind));
    TEST_ASSERT_EQUAL_UINT8(5, tx.step());
    TEST_ASSERT_EQUAL_UINT8(5, first.sent_step);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::Run), static_cast<int>(tx.view()));
}

void test_link_status_accept()
{
    World w;
    w.watch_step = 5;
    w.run_until(120000u);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));
    w.meter = false;
    w.run_until(w.now + 5000u);
    TEST_ASSERT_TRUE(w.rx.request(p1lora::LinkMode::Locked, p1lora::ProfileId::A, w.rx.step(), 0));
    w.run_until(w.now + 120000u);
    TEST_ASSERT_TRUE(w.saw_accepted_status);
    TEST_ASSERT_TRUE(w.accepted_status.accepted);
    p1lora::Key key;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::KeyStatus::Ok),
                      static_cast<int>(p1lora::parse_key("000102030405060708090a0b0c0d0e0f", 32, key)));
    std::uint8_t frame[16];
    std::size_t len = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::Status::Ok),
                      static_cast<int>(p1lora::pack_status(key, 9, w.accepted_status, frame, sizeof frame, len)));
    TEST_ASSERT_EQUAL_UINT8(p1lora::kStatusAccepted, static_cast<std::uint8_t>(frame[5] & p1lora::kStatusAccepted));
    const p1lora::Decoded back = p1lora::decode(key, frame, len);
    TEST_ASSERT_TRUE(back.meter.accepted);
    TEST_ASSERT_EQUAL_UINT8(p1lora::kStatusNoneForAWhile, back.meter.reason);
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::Locked), static_cast<int>(w.rx.view()));
    TEST_ASSERT_EQUAL(static_cast<int>(p1lora::LinkView::Locked), static_cast<int>(w.tx.view()));
    finish(w, "status");
}

void test_link_duty_windows()
{
    World w;
    w.watch_step = 5;
    w.run_until(3600000u);
    TEST_ASSERT_TRUE(both_at(w, 5, p1lora::ProfileId::A, p1lora::LinkView::Run));
    TEST_ASSERT_FALSE(w.tx_log.empty());
    finish(w, "duty");
}
