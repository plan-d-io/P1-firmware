#include "P1LoRa.h"

#include <cstring>

extern "C" {
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
}

namespace p1lora {
namespace {

void sha256(const std::uint8_t* data, std::size_t len, std::uint8_t out[32])
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, 0) == 0 && mbedtls_sha256_update(&ctx, data, len) == 0) {
        mbedtls_sha256_finish(&ctx, out);
    }
    mbedtls_sha256_free(&ctx);
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

void put_u16(std::uint8_t* p, std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

void put_u32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

std::uint16_t get_u16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t get_u32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool known_type(std::uint8_t type)
{
    switch (type) {
    case kTypeSingle:
    case kTypeThree:
    case kTypeProbe:
    case kTypeStatus:
    case kTypeAck:
    case kTypeOffer:
    case kTypeConfirm:
        return true;
    default:
        return false;
    }
}

std::size_t plain_len(const Reading& reading)
{
    std::size_t n = reading.three_phase ? 47 : 27;
    if ((reading.flags & kFlagGas) != 0) {
        n += 4;
    }
    if ((reading.flags & kFlagPeak) != 0) {
        n += 6;
    }
    return n;
}

void write_header(std::uint8_t out[5], const Key& key, std::uint8_t type, std::uint8_t counter)
{
    out[0] = key.net[0];
    out[1] = key.net[1];
    out[2] = type;
    out[3] = 1;
    out[4] = counter;
}

void cbc_encrypt(const Key& key, const std::uint8_t iv[16], std::uint8_t* buf, std::size_t len)
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    std::uint8_t iv_copy[16];
    std::memcpy(iv_copy, iv, 16);
    if (mbedtls_aes_setkey_enc(&ctx, key.aes, 256) == 0) {
        mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, len, iv_copy, buf, buf);
    }
    mbedtls_aes_free(&ctx);
}

void cbc_decrypt(const Key& key, const std::uint8_t iv[16], std::uint8_t* buf, std::size_t len)
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    std::uint8_t iv_copy[16];
    std::memcpy(iv_copy, iv, 16);
    if (mbedtls_aes_setkey_dec(&ctx, key.aes, 256) == 0) {
        mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, len, iv_copy, buf, buf);
    }
    mbedtls_aes_free(&ctx);
}

bool reserved_reading_flags(std::uint8_t flags)
{
    // Bit 2 is water, not built. Bits 5–7 are reserved.
    return (flags & static_cast<std::uint8_t>(~(kFlagTariff2 | kFlagGas | kFlagPeak | kFlagAccepted))) != 0;
}

Status check_header(const Key& key, const std::uint8_t* frame, std::size_t len, std::uint8_t& type)
{
    if (len < 5) {
        return Status::WrongLength;
    }
    if (frame[0] != key.net[0] || frame[1] != key.net[1]) {
        return Status::WrongNetwork;
    }
    type = frame[2];
    if (!known_type(type)) {
        return Status::UnknownType;
    }
    if (frame[3] != 1) {
        return Status::UnknownVersion;
    }
    return Status::Ok;
}

Reading parse_reading(bool three, const std::uint8_t* p)
{
    Reading r;
    r.three_phase = three;
    r.flags = p[0];
    r.t1_wh = get_u32(p + 1);
    r.t2_wh = get_u32(p + 5);
    r.inj1_wh = get_u32(p + 9);
    r.inj2_wh = get_u32(p + 13);
    r.power_w = get_u16(p + 17);
    r.inject_w = get_u16(p + 19);
    r.demand_w = get_u16(p + 21);
    if (!three) {
        r.v_dv[0] = get_u16(p + 23);
        r.i_ca[0] = get_u16(p + 25);
    } else {
        for (int i = 0; i < 3; ++i) {
            r.p_plus_w[i] = get_u16(p + 23 + 2 * i);
            r.p_minus_w[i] = get_u16(p + 29 + 2 * i);
            r.v_dv[i] = get_u16(p + 35 + 2 * i);
            r.i_ca[i] = get_u16(p + 41 + 2 * i);
        }
    }
    std::size_t o = three ? 47 : 27;
    if ((r.flags & kFlagGas) != 0) {
        r.gas_dm3 = get_u32(p + o);
        o += 4;
    }
    if ((r.flags & kFlagPeak) != 0) {
        r.peak_time = get_u32(p + o);
        r.peak_w = get_u16(p + o + 4);
    }
    return r;
}

Status decode_reading(const Key& key, const std::uint8_t* frame, std::size_t len, bool three, Decoded& out)
{
    const std::size_t pay = len - 5;
    if (pay == 0 || (pay % 16) != 0 || pay > 64) {
        return Status::WrongLength;
    }
    if (three) {
        if (pay != 48 && pay != 64) {
            return Status::WrongLength;
        }
    } else if (pay != 32 && pay != 48) {
        return Status::WrongLength;
    }

    std::uint8_t buf[64];
    std::memcpy(buf, frame + 5, pay);
    std::uint8_t iv[16];
    frame_iv(key, frame, iv);
    cbc_decrypt(key, iv, buf, pay);

    const std::uint8_t flags = buf[0];
    if (reserved_reading_flags(flags)) {
        return Status::KeyCheck;
    }
    std::size_t plain = three ? 47 : 27;
    if ((flags & kFlagGas) != 0) {
        plain += 4;
    }
    if ((flags & kFlagPeak) != 0) {
        plain += 6;
    }
    const std::size_t padded = (plain + 15u) & ~15u;
    if (padded != pay) {
        return Status::KeyCheck;
    }
    for (std::size_t i = plain; i < pay; ++i) {
        if (buf[i] != 0) {
            return Status::KeyCheck;
        }
    }
    out.kind = Kind::Reading;
    out.reading = parse_reading(three, buf);
    return Status::Ok;
}

bool same_bytes(const std::uint8_t* a, const std::uint8_t* b, std::size_t n)
{
    return std::memcmp(a, b, n) == 0;
}

const Profile kA = {
    ProfileId::A,
    869525000u,
    17,
    360000u,
    6,
    {
        {12, 125000u},
        {11, 125000u},
        {10, 125000u},
        {9, 125000u},
        {8, 125000u},
        {7, 125000u},
        {0, 0},
    },
    0x2B,
};

// Profile B's ceiling is written as about 12–13 dBm. 13 is the top of that range.
const Profile kB = {
    ProfileId::B,
    868300000u,
    13,
    36000u,
    7,
    {
        {12, 125000u},
        {12, 250000u},
        {11, 250000u},
        {10, 250000u},
        {9, 250000u},
        {8, 250000u},
        {7, 250000u},
    },
    0x2B,
};

}  // namespace

const Profile& profile(ProfileId id)
{
    return id == ProfileId::B ? kB : kA;
}

std::uint32_t airtime_us(std::uint8_t sf, std::uint32_t bw_hz, std::uint16_t payload_len)
{
    if (sf < 7 || sf > 12 || bw_hz == 0) {
        return 0;
    }
    const std::uint64_t tsym = (1000000ull << sf) / bw_hz;
    const int de = tsym > 16000u ? 1 : 0;
    const int num = 8 * static_cast<int>(payload_len) - 4 * static_cast<int>(sf) + 44;
    const int den = 4 * (static_cast<int>(sf) - 2 * de);
    int groups = 0;
    if (num > 0 && den > 0) {
        groups = (num + den - 1) / den;
    }
    const int payload_sym = 8 + groups * 5;
    const std::uint64_t pre = tsym * 49u / 4u;
    const std::uint64_t total = pre + static_cast<std::uint64_t>(payload_sym) * tsym;
    return static_cast<std::uint32_t>(total);
}

std::uint32_t airtime_us(ProfileId id, std::uint8_t step, std::uint16_t payload_len)
{
    const Profile& p = profile(id);
    if (step >= p.step_count) {
        return 0;
    }
    return airtime_us(p.steps[step].sf, p.steps[step].bw_hz, payload_len);
}

std::int8_t clamp_tx_power(ProfileId id, std::int8_t requested_dbm)
{
    const std::int8_t cap = profile(id).max_dbm;
    return requested_dbm > cap ? cap : requested_dbm;
}

std::uint16_t reading_interval_tenths(std::uint32_t airtime_us, std::uint32_t duty_ms_per_hour)
{
    if (duty_ms_per_hour == 0) {
        return 0;
    }
    const std::uint64_t num = static_cast<std::uint64_t>(airtime_us) * 36ull + duty_ms_per_hour / 2u;
    return static_cast<std::uint16_t>(num / duty_ms_per_hour);
}

std::uint32_t status_interval_s(ProfileId id, std::uint8_t step)
{
    const Profile& p = profile(id);
    if (step >= p.step_count || p.duty_ms_per_hour == 0) {
        return 0;
    }
    // 9 bytes on air: header plus the 4-byte plaintext status payload.
    const std::uint32_t air = airtime_us(id, step, 9);
    if (air == 0) {
        return 0;
    }
    // 4 * airtime / duty fraction, in microseconds, rounded up to a whole second.
    // duty fraction = duty_ms_per_hour / 3_600_000.
    const std::uint64_t duty = p.duty_ms_per_hour;
    const std::uint64_t us = (4ull * static_cast<std::uint64_t>(air) * 3600000ull + duty - 1ull) / duty;
    std::uint32_t sec = static_cast<std::uint32_t>((us + 999999ull) / 1000000ull);
    if (sec < 60u) {
        sec = 60u;
    }
    return sec;
}

Duty::Duty(std::uint32_t budget_ms_per_hour)
    : budget_us_(budget_ms_per_hour * 1000u)
{
}

void Duty::roll(std::uint32_t now_ms)
{
    const std::uint32_t minute = now_ms / 60000u;
    if (!started_) {
        started_ = true;
        last_minute_ = minute;
        return;
    }
    if (minute <= last_minute_) {
        return;
    }
    const std::uint32_t jump = minute - last_minute_;
    if (jump >= kSlots) {
        for (std::uint32_t& b : bucket_us_) {
            b = 0;
        }
    } else {
        for (std::uint32_t i = 1; i <= jump; ++i) {
            bucket_us_[(last_minute_ + i) % kSlots] = 0;
        }
    }
    last_minute_ = minute;
}

std::uint32_t Duty::used_us(std::uint32_t now_ms)
{
    roll(now_ms);
    const std::uint32_t minute = now_ms / 60000u;
    const std::uint32_t window_start = now_ms >= 3600000u ? now_ms - 3600000u : 0;
    const std::uint32_t oldest = window_start / 60000u;
    std::uint32_t sum = 0;
    for (std::uint32_t m = oldest; m <= minute; ++m) {
        const std::uint32_t add = bucket_us_[m % kSlots];
        if (sum > 0xFFFFFFFFu - add) {
            return 0xFFFFFFFFu;
        }
        sum += add;
    }
    return sum;
}

bool Duty::can_send(std::uint32_t now_ms, std::uint32_t airtime_us)
{
    if (airtime_us > budget_us_) {
        return false;
    }
    return used_us(now_ms) <= budget_us_ - airtime_us;
}

void Duty::record(std::uint32_t now_ms, std::uint32_t airtime_us)
{
    roll(now_ms);
    std::uint32_t& slot = bucket_us_[(now_ms / 60000u) % kSlots];
    if (slot > 0xFFFFFFFFu - airtime_us) {
        slot = 0xFFFFFFFFu;
    } else {
        slot += airtime_us;
    }
}

std::uint32_t Duty::charged_us(std::uint32_t now_ms) const
{
    Duty copy(*this);
    return copy.used_us(now_ms);
}

std::uint32_t duty_checksum(const DutyImage& image)
{
    std::uint32_t sum = 0x811C9DC5u;
    auto mix = [&](std::uint32_t v) {
        sum ^= v;
        sum *= 16777619u;
    };
    mix(image.magic);
    mix(image.budget_us);
    for (std::uint32_t bucket : image.bucket_us) {
        mix(bucket);
    }
    mix(image.last_minute);
    mix(image.now_ms);
    mix(image.started);
    return sum;
}

void Duty::save(DutyImage& out, std::uint32_t now_ms) const
{
    out = {};
    out.magic = kDutyMagic;
    out.budget_us = budget_us_;
    for (int i = 0; i < kSlots; ++i) {
        out.bucket_us[i] = bucket_us_[i];
    }
    out.last_minute = last_minute_;
    out.now_ms = now_ms;
    out.started = started_ ? 1 : 0;
    out.checksum = duty_checksum(out);
}

bool Duty::restore(const DutyImage& in)
{
    if (in.magic != kDutyMagic || in.budget_us != budget_us_ || in.checksum != duty_checksum(in)) {
        return false;
    }
    for (int i = 0; i < kSlots; ++i) {
        bucket_us_[i] = in.bucket_us[i];
    }
    last_minute_ = in.last_minute;
    started_ = in.started != 0;
    return true;
}

std::uint32_t Duty::wait_ms(std::uint32_t now_ms, std::uint32_t airtime_us) const
{
    if (airtime_us > budget_us_) {
        return 0xFFFFFFFFu;
    }
    Duty probe(*this);
    if (probe.can_send(now_ms, airtime_us)) {
        return 0;
    }
    std::uint32_t t = now_ms;
    for (int i = 0; i < 70; ++i) {
        const std::uint32_t window_start = t >= 3600000u ? t - 3600000u : 0;
        const std::uint32_t oldest = window_start / 60000u;
        const std::uint64_t expire = static_cast<std::uint64_t>(oldest + 1u) * 60000ull + 3600000ull;
        std::uint64_t next = expire > t ? expire : static_cast<std::uint64_t>(t) + 1u;
        if (next > 0xFFFFFFFFull) {
            return 0xFFFFFFFFu;
        }
        t = static_cast<std::uint32_t>(next);
        if (probe.can_send(t, airtime_us)) {
            return t - now_ms;
        }
    }
    return 3600000u;
}

KeyStatus parse_key(const char* hex, std::size_t len, Key& out)
{
    if (hex == nullptr || len != 32) {
        return KeyStatus::Rejected;
    }
    std::uint8_t raw[16];
    for (std::size_t i = 0; i < 16; ++i) {
        const int hi = hex_nibble(hex[2 * i]);
        const int lo = hex_nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return KeyStatus::Rejected;
        }
        raw[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    std::uint8_t buf[40];
    const char* aes_label = "P1LoRa AES key";
    const char* net_label = "P1LoRa network ID";
    std::memcpy(buf, aes_label, 14);
    std::memcpy(buf + 14, raw, 16);
    sha256(buf, 30, out.aes);
    std::memcpy(buf, net_label, 17);
    std::memcpy(buf + 17, raw, 16);
    std::uint8_t dig[32];
    sha256(buf, 33, dig);
    out.net[0] = dig[0];
    out.net[1] = dig[1];
    return KeyStatus::Ok;
}

bool normalise_pair_key(char* value)
{
    if (value == nullptr) {
        return false;
    }
    if (value[0] == '\0') {
        return true;
    }
    if (std::strlen(value) != 32) {
        return false;
    }
    for (std::size_t i = 0; i < 32; ++i) {
        const char c = value[i];
        if (c >= 'A' && c <= 'F') {
            value[i] = static_cast<char>(c - 'A' + 'a');
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

void format_pair_key(const std::uint8_t* rnd16, char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (rnd16 == nullptr || cap < 33) {
        return;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 16; ++i) {
        out[2 * i] = kHex[rnd16[i] >> 4];
        out[2 * i + 1] = kHex[rnd16[i] & 0x0f];
    }
    out[32] = '\0';
}

std::size_t write_reading_plain(const Reading& reading, std::uint8_t* out, std::size_t cap)
{
    const std::size_t n = plain_len(reading);
    if (out == nullptr || cap < n) {
        return 0;
    }
    std::memset(out, 0, n);
    out[0] = reading.flags;
    put_u32(out + 1, reading.t1_wh);
    put_u32(out + 5, reading.t2_wh);
    put_u32(out + 9, reading.inj1_wh);
    put_u32(out + 13, reading.inj2_wh);
    put_u16(out + 17, reading.power_w);
    put_u16(out + 19, reading.inject_w);
    put_u16(out + 21, reading.demand_w);
    if (!reading.three_phase) {
        put_u16(out + 23, reading.v_dv[0]);
        put_u16(out + 25, reading.i_ca[0]);
    } else {
        for (int i = 0; i < 3; ++i) {
            put_u16(out + 23 + 2 * i, reading.p_plus_w[i]);
            put_u16(out + 29 + 2 * i, reading.p_minus_w[i]);
            put_u16(out + 35 + 2 * i, reading.v_dv[i]);
            put_u16(out + 41 + 2 * i, reading.i_ca[i]);
        }
    }
    std::size_t o = reading.three_phase ? 47 : 27;
    if ((reading.flags & kFlagGas) != 0) {
        put_u32(out + o, reading.gas_dm3);
        o += 4;
    }
    if ((reading.flags & kFlagPeak) != 0) {
        put_u32(out + o, reading.peak_time);
        put_u16(out + o + 4, reading.peak_w);
    }
    return n;
}

void frame_iv(const Key& key, const std::uint8_t header[5], std::uint8_t iv[16])
{
    std::uint8_t block[16] = {};
    std::memcpy(block, header, 5);
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key.aes, 256) == 0) {
        std::uint8_t out[16];
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, block, out);
        std::memcpy(block, out, 16);
    }
    mbedtls_aes_free(&ctx);
    std::memcpy(iv, block, 16);
}

Status pack_reading(const Key& key, const Reading& reading, std::uint8_t counter,
                    std::uint8_t* out, std::size_t cap, std::size_t& len)
{
    std::uint8_t plain[64];
    const std::size_t n = write_reading_plain(reading, plain, sizeof plain);
    if (n == 0) {
        return Status::WrongLength;
    }
    const std::size_t padded = (n + 15u) & ~15u;
    std::memset(plain + n, 0, padded - n);
    if (cap < 5 + padded) {
        return Status::WrongLength;
    }
    write_header(out, key, reading.three_phase ? kTypeThree : kTypeSingle, counter);
    std::uint8_t iv[16];
    frame_iv(key, out, iv);
    cbc_encrypt(key, iv, plain, padded);
    std::memcpy(out + 5, plain, padded);
    len = 5 + padded;
    return Status::Ok;
}

Status pack_probe(const Key& key, std::uint8_t counter, std::uint8_t payload_len,
                  std::uint8_t* out, std::size_t cap, std::size_t& len)
{
    if (payload_len != 32 && payload_len != 48) {
        return Status::WrongLength;
    }
    if (cap < static_cast<std::size_t>(5 + payload_len)) {
        return Status::WrongLength;
    }
    write_header(out, key, kTypeProbe, counter);
    std::memset(out + 5, 0, payload_len);
    len = static_cast<std::size_t>(5 + payload_len);
    return Status::Ok;
}

Status pack_ack(const Key& key, std::uint8_t counter, const Ack& ack,
                std::uint8_t* out, std::size_t cap, std::size_t& len)
{
    const std::size_t n = ack.command ? 10 : 8;
    if (cap < n) {
        return Status::WrongLength;
    }
    if (ack.step > 15 || ack.profile_bit > 1 || ack.mode_bit > 1) {
        return Status::WrongLength;
    }
    write_header(out, key, kTypeAck, counter);
    out[5] = ack.counter;
    std::uint8_t flags = static_cast<std::uint8_t>(ack.flags & 0x01);
    if (ack.command) {
        flags = static_cast<std::uint8_t>(flags | 0x02);
    }
    out[6] = flags;
    out[7] = static_cast<std::uint8_t>(ack.snr);
    if (ack.command) {
        out[8] = static_cast<std::uint8_t>(ack.profile_bit | (ack.mode_bit << 1) | (ack.step << 2));
        out[9] = ack.fallback;
    }
    len = n;
    return Status::Ok;
}

Status pack_offer(const Key& key, std::uint8_t counter, std::uint8_t step,
                  std::uint8_t* out, std::size_t cap, std::size_t& len)
{
    if (cap < 6) {
        return Status::WrongLength;
    }
    write_header(out, key, kTypeOffer, counter);
    out[5] = step;
    len = 6;
    return Status::Ok;
}

Status pack_confirm(const Key& key, std::uint8_t counter, std::uint8_t step,
                    std::uint8_t* out, std::size_t cap, std::size_t& len)
{
    if (cap < 6) {
        return Status::WrongLength;
    }
    write_header(out, key, kTypeConfirm, counter);
    out[5] = step;
    len = 6;
    return Status::Ok;
}

Status pack_status(const Key& key, std::uint8_t counter, const MeterStatus& meter,
                   std::uint8_t* out, std::size_t cap, std::size_t& len)
{
    if (out == nullptr || cap < 9) {
        return Status::WrongLength;
    }
    write_header(out, key, kTypeStatus, counter);
    out[5] = meter.reason;
    put_u16(out + 6, meter.since_s);
    out[8] = meter.rejected;
    len = 9;
    return Status::Ok;
}

Decoded decode(const Key& key, const std::uint8_t* frame, std::size_t len)
{
    Decoded out;
    std::uint8_t type = 0;
    out.status = check_header(key, frame, len, type);
    if (out.status != Status::Ok) {
        return out;
    }
    out.type = type;
    out.version = frame[3];
    out.counter = frame[4];
    const std::size_t pay = len - 5;

    if (type == kTypeSingle || type == kTypeThree) {
        out.status = decode_reading(key, frame, len, type == kTypeThree, out);
        return out;
    }
    if (type == kTypeProbe) {
        if (pay != 32 && pay != 48) {
            out.status = Status::WrongLength;
            return out;
        }
        for (std::size_t i = 0; i < pay; ++i) {
            if (frame[5 + i] != 0) {
                out.status = Status::WrongLength;
                return out;
            }
        }
        out.kind = Kind::Probe;
        out.probe_len = static_cast<std::uint8_t>(pay);
        out.status = Status::Ok;
        return out;
    }
    if (type == kTypeAck) {
        if (pay != 3 && pay != 5) {
            out.status = Status::WrongLength;
            return out;
        }
        const std::uint8_t flags = frame[6];
        if ((flags & ~0x03) != 0) {
            out.status = Status::WrongLength;
            return out;
        }
        const bool cmd = (flags & 0x02) != 0;
        if (cmd != (pay == 5)) {
            out.status = Status::WrongLength;
            return out;
        }
        out.kind = Kind::Ack;
        out.ack.counter = frame[5];
        out.ack.flags = flags;
        out.ack.snr = static_cast<std::int8_t>(frame[7]);
        out.ack.command = cmd;
        if (cmd) {
            out.ack.profile_bit = static_cast<std::uint8_t>(frame[8] & 0x01);
            out.ack.mode_bit = static_cast<std::uint8_t>((frame[8] >> 1) & 0x01);
            out.ack.step = static_cast<std::uint8_t>((frame[8] >> 2) & 0x0F);
            out.ack.fallback = frame[9];
            if ((frame[8] & 0xC0) != 0) {
                out.status = Status::WrongLength;
                out.kind = Kind::None;
                return out;
            }
        }
        out.status = Status::Ok;
        return out;
    }
    if (type == kTypeStatus) {
        if (pay != 4) {
            out.status = Status::WrongLength;
            return out;
        }
        out.kind = Kind::Status;
        out.meter.reason = frame[5];
        out.meter.since_s = get_u16(frame + 6);
        out.meter.rejected = frame[8];
        out.status = Status::Ok;
        return out;
    }
    if (pay != 1) {
        out.status = Status::WrongLength;
        return out;
    }
    out.kind = type == kTypeOffer ? Kind::Offer : Kind::Confirm;
    out.step = frame[5];
    out.status = Status::Ok;
    return out;
}

void MeterGate::note_valid(std::uint32_t now_ms)
{
    ever_ = true;
    last_valid_ms_ = now_ms;
    status_armed_ = false;
    rejects_since_valid_ = 0;
}

void MeterGate::note_reject(std::uint32_t now_ms)
{
    have_reject_ = true;
    last_reject_ms_ = now_ms;
    if (rejects_ < 0xFF) {
        ++rejects_;
    }
    if (rejects_since_valid_ < 0xFF) {
        ++rejects_since_valid_;
    }
}

void MeterGate::status_sent(std::uint32_t now_ms)
{
    status_armed_ = true;
    last_status_ms_ = now_ms;
    rejects_ = 0;
}

SendPlan MeterGate::plan(std::uint32_t now_ms, std::uint32_t status_interval_ms) const
{
    SendPlan out;
    const bool fresh = ever_ && (now_ms - last_valid_ms_) < kFreshMs;
    if (!ever_) {
        out.meter.since_s = kSinceNever;
    } else {
        const std::uint32_t sec = (now_ms - last_valid_ms_) / 1000u;
        out.meter.since_s = sec > kSinceMax ? kSinceMax : static_cast<std::uint16_t>(sec);
    }
    out.meter.rejected = rejects_;
    if (fresh) {
        out.choice = SendChoice::Reading;
        return out;
    }
    // Reason 3 only while rejected telegrams are still arriving. A reject
    // older than the fresh window, or a valid telegram after it, is silence.
    const bool rejecting = have_reject_ && rejects_since_valid_ > 0 && (now_ms - last_reject_ms_) < kFreshMs;
    if (rejecting) {
        out.meter.reason = kStatusRejected;
    } else if (!ever_) {
        out.meter.reason = kStatusNoneSinceBoot;
    } else {
        out.meter.reason = kStatusNoneForAWhile;
    }
    if (!status_armed_ || status_interval_ms == 0 || (now_ms - last_status_ms_) >= status_interval_ms) {
        out.choice = SendChoice::Status;
        return out;
    }
    out.choice = SendChoice::Wait;
    return out;
}

Gap counter_gap(std::uint8_t last, std::uint8_t current)
{
    const std::uint8_t loss = static_cast<std::uint8_t>(current - last - 1);
    Gap g;
    if (loss >= 128) {
        g.restart = true;
        g.loss = 0;
    } else {
        g.loss = loss;
    }
    return g;
}

void Optionals::observe(bool gas_present, std::uint32_t gas_dm3,
                        bool peak_present, std::uint32_t peak_time, std::uint16_t peak_w)
{
    auto bump = [](std::uint32_t& gen) {
        gen = gen == 0xFFFFFFFFu ? 1u : gen + 1u;
    };
    if (!started_) {
        started_ = true;
        if (gas_present) {
            gas_pending_ = true;
            have_gas_ = true;
            gas_dm3_ = gas_dm3;
            gas_gen_ = 1;
        }
        if (peak_present) {
            peak_pending_ = true;
            have_peak_ = true;
            peak_time_ = peak_time;
            peak_w_ = peak_w;
            peak_gen_ = 1;
        }
        return;
    }
    if (gas_present) {
        if (!have_gas_ || gas_dm3_ != gas_dm3) {
            gas_pending_ = true;
            bump(gas_gen_);
        }
        have_gas_ = true;
        gas_dm3_ = gas_dm3;
    }
    if (peak_present) {
        if (!have_peak_ || peak_time_ != peak_time || peak_w_ != peak_w) {
            peak_pending_ = true;
            bump(peak_gen_);
        }
        have_peak_ = true;
        peak_time_ = peak_time;
        peak_w_ = peak_w;
    }
}

void Optionals::sent(std::uint8_t counter, bool carried_gas, bool carried_peak)
{
    gas_at_[counter] = carried_gas ? gas_gen_ : 0;
    peak_at_[counter] = carried_peak ? peak_gen_ : 0;
}

void Optionals::ack(std::uint8_t counter)
{
    if (gas_at_[counter] != 0 && gas_at_[counter] == gas_gen_) {
        gas_pending_ = false;
    }
    if (peak_at_[counter] != 0 && peak_at_[counter] == peak_gen_) {
        peak_pending_ = false;
    }
}

const char* crypto_self_test()
{
    const std::uint8_t aes_key[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    const std::uint8_t plain[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const std::uint8_t expect_ct[16] = {
        0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf, 0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89};
    std::uint8_t block[16];
    std::memcpy(block, plain, 16);
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    std::uint8_t out[16];
    const int aes_ok = mbedtls_aes_setkey_enc(&ctx, aes_key, 256) == 0 &&
                       mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, block, out) == 0;
    mbedtls_aes_free(&ctx);
    if (!aes_ok || !same_bytes(out, expect_ct, 16)) {
        return "aes";
    }

    std::uint8_t dig[32];
    const char* abc = "abc";
    sha256(reinterpret_cast<const std::uint8_t*>(abc), 3, dig);
    const std::uint8_t expect_sha[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    if (!same_bytes(dig, expect_sha, 32)) {
        return "sha";
    }

    Key key;
    if (parse_key("000102030405060708090a0b0c0d0e0f", 32, key) != KeyStatus::Ok) {
        return "frame";
    }
    Reading r;
    r.three_phase = true;
    r.flags = 0x09;
    r.t1_wh = 182077;
    r.t2_wh = 199084;
    r.inj1_wh = 23;
    r.inj2_wh = 134;
    r.power_w = 289;
    r.inject_w = 0;
    r.demand_w = 260;
    r.p_plus_w[0] = 75;
    r.p_plus_w[1] = 15;
    r.p_plus_w[2] = 198;
    r.p_minus_w[0] = r.p_minus_w[1] = r.p_minus_w[2] = 0;
    r.v_dv[0] = 2421;
    r.v_dv[1] = 2417;
    r.v_dv[2] = 2403;
    r.i_ca[0] = 53;
    r.i_ca[1] = 7;
    r.i_ca[2] = 97;
    r.peak_time = 0xAC6DB4C0u;
    r.peak_w = 2396;
    std::uint8_t frame[kMaxFrame];
    std::size_t len = 0;
    if (pack_reading(key, r, 7, frame, sizeof frame, len) != Status::Ok) {
        return "frame";
    }
    const std::uint8_t expect[] = {
        0x6b, 0x0b, 0x42, 0x01, 0x07, 0xb5, 0x35, 0x5d, 0xdf, 0xab, 0xc4, 0x7f, 0x48, 0xd5, 0x08, 0xd3,
        0x98, 0x38, 0x86, 0x37, 0x78, 0x37, 0xab, 0xc2, 0x46, 0x66, 0x59, 0x5f, 0x24, 0xfc, 0x78, 0xa6,
        0x8b, 0xfc, 0x5a, 0x21, 0x51, 0xc3, 0x13, 0x2c, 0x0b, 0x1a, 0x87, 0xd0, 0x9b, 0x97, 0x50, 0x5f,
        0x73, 0x28, 0xd1, 0xf7, 0xe3, 0x35, 0x6d, 0x67, 0xc1, 0x8e, 0x68, 0x15, 0xf7, 0x55, 0xb8, 0x60,
        0xb0, 0x47, 0x04, 0xc3, 0x7c};
    if (len != sizeof expect || !same_bytes(frame, expect, sizeof expect)) {
        return "frame";
    }
    return nullptr;
}

}  // namespace p1lora
