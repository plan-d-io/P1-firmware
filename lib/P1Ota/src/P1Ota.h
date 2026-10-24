#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace p1ota {

// Max URL length for the version / firmware endpoints.
constexpr std::size_t kUrlMax = 160;

// Channel setting → GitHub path segment.
// Returns nullptr if channel is not main or develop.
const char* channel_path(std::string_view channel);

// Build the version-file or firmware-binary URL for a channel.
// Returns written length, or 0 on failure.
std::size_t version_url(std::string_view channel, char* out, std::size_t cap);
std::size_t firmware_url(std::string_view channel, char* out, std::size_t cap);

// Parse a version file body. Tolerates leading/trailing whitespace.
// Returns true and writes a positive integer on success.
// Empty, non-integer, zero, and negative are all failure (not version zero).
bool parse_version(std::string_view body, unsigned& out);

// Display string "N.NN" from an integer like 301 → "3.01". Truncates if cap is tight.
std::size_t format_version(unsigned num, char* out, std::size_t cap);

// Parse "N.NN" (or "N.N") display string into the integer convention (×100).
// Used by the host test that locks kFwVer to kFwVerNum.
bool parse_display_version(std::string_view s, unsigned& out);

// Automatic skip of a failed version lasts this long. A test-only compile
// flag shortens it for the desk; it is never set on a release build.
#if defined(P1_TEST_OTA_FAIL_SKIP_S)
constexpr std::uint32_t kAutoFailSkipS = static_cast<std::uint32_t>(P1_TEST_OTA_FAIL_SKIP_S);
#else
constexpr std::uint32_t kAutoFailSkipS = 23u * 3600u;
#endif

// Automatic install skips `failed` (0 = none) only while elapsed_s is still
// under kAutoFailSkipS. Manual install does not skip. Requires found > running.
bool should_install(unsigned found, unsigned running, unsigned failed, bool automatic,
                    std::uint32_t elapsed_s = 0);

// Seconds since the failure was noticed. Uses the persisted unix stamp when
// the clock is valid; otherwise millis() since boot_ms_at (this boot only).
std::uint32_t fail_elapsed_s(std::uint32_t unix_noticed, std::uint32_t boot_ms_at,
                             bool clock_valid, std::uint32_t now_unix,
                             std::uint32_t now_boot_ms);

// Unix time to persist once the clock is valid: backdated by uptime since
// boot_ms_at, so a later power cut does not restart the wait.
std::uint32_t fail_unix_stamp(std::uint32_t now_unix, std::uint32_t now_boot_ms,
                              std::uint32_t boot_ms_at);

// "2026-10-25 15:04:00 UTC". 0 if the buffer is too small.
std::size_t format_unix_utc(std::uint32_t unix, char* out, std::size_t cap);
// "boot+23h" / "boot+3m" / "boot+12s". 0 if the buffer is too small.
std::size_t format_boot_age(std::uint32_t elapsed_s, char* out, std::size_t cap);

// Storage guard while a new image is on trial (pending-verify).
// Rollback if on trial and filesystem or settings storage failed.
// Format-on-mount-failure is allowed only off trial.
bool storage_on_trial_should_rollback(bool on_trial, bool filesystem_ok, bool settings_ok);
bool storage_format_allowed(bool on_trial);

}  // namespace p1ota
