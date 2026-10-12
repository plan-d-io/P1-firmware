#pragma once

#include <P1DSMR.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace p1eid {

constexpr char kHelloUrl[] = "https://hooks.energyid.eu/hello";
constexpr char kDeviceName[] = "P1 dongle by Plan-D";

// Probe lengths (12 Oct 2026 claimed hello) plus headroom. Values not recorded.
// webhookUrl 71 → 128; authorization 188 → 384; x-twin-id 36 → 40;
// recordName 6 → 64; recordNumber 11 → 32; claimUrl ~107 → 192;
// whole claimed JSON 473 → 1536.
constexpr std::size_t kClaimCodeMax = 32;
constexpr std::size_t kClaimUrlMax = 192;
constexpr std::size_t kWebhookUrlMax = 128;
constexpr std::size_t kAuthMax = 384;
constexpr std::size_t kTwinIdMax = 40;
constexpr std::size_t kRecordNameMax = 64;
constexpr std::size_t kRecordNumberMax = 32;
constexpr std::size_t kHelloBodyMax = 320;
constexpr std::size_t kReplyMax = 1536;
constexpr std::size_t kBatchMax = 512;
constexpr std::size_t kErrorMax = 64;

constexpr std::uint32_t kIntervalMinS = 60;
constexpr std::uint32_t kIntervalMaxS = 86400;
constexpr std::uint32_t kIntervalDefaultS = 3600;

enum class ReplyKind : std::uint8_t { Unclaimed, Claimed, Error };

struct HelloReply {
    ReplyKind kind = ReplyKind::Error;
    char claim_code[kClaimCodeMax]{};
    char claim_url[kClaimUrlMax]{};
    std::int64_t exp = 0;
    char webhook_url[kWebhookUrlMax]{};
    char authorization[kAuthMax]{};
    char twin_id[kTwinIdMax]{};
    char record_name[kRecordNameMax]{};
    char record_number[kRecordNumberMax]{};
    char allowed_interval[16]{};
    bool have_upload_interval = false;
    std::uint32_t upload_interval = 0;
    std::uint32_t interval_s = kIntervalDefaultS;
    std::int64_t token_expiry = 0;
};

std::size_t hello_body(char* out, std::size_t cap, const char* claim_code, const char* device_id,
                       const char* firmware_version, const char* ip);

bool parse_hello_reply(std::string_view json, HelloReply& out);

std::uint32_t parse_iso_interval(std::string_view iso);
std::uint32_t choose_interval(bool have_upload, std::uint32_t upload_s,
                              std::string_view allowed_iso);
bool parse_sas_expiry(std::string_view authorization, std::int64_t& out);

bool dsmr_to_unix(std::string_view ts, std::int64_t& out);

std::size_t upload_batch(const p1dsmr::Data& data, char* out, std::size_t cap);

}  // namespace p1eid
