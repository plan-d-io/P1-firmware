#include "P1Syslog.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#include <ctime>
#endif

namespace p1log {
namespace {

ClockFn g_unix = nullptr;
UptimeFn g_uptime = nullptr;

bool append_view(char* out, std::size_t cap, std::size_t& n, std::string_view s)
{
    if (n + s.size() + 1 > cap) {
        return false;
    }
    for (char c : s) {
        out[n++] = c;
    }
    return true;
}

bool append_char(char* out, std::size_t cap, std::size_t& n, char c)
{
    if (n + 2 > cap) {
        return false;
    }
    out[n++] = c;
    return true;
}

void write_nul(char* out, std::size_t cap)
{
    if (out != nullptr && cap > 0) {
        out[0] = '\0';
    }
}

void civil_from_days(std::int64_t z, std::uint32_t& y, std::uint32_t& m, std::uint32_t& d)
{
    // Howard Hinnant civil_from_days, z = days since 1970-01-01.
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<std::uint32_t>(static_cast<std::int64_t>(yoe) + era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
}

std::uint64_t default_unix()
{
    if (g_unix != nullptr) {
        return g_unix();
    }
#if defined(ARDUINO)
    return static_cast<std::uint64_t>(time(nullptr));
#else
    return 0;
#endif
}

std::uint32_t default_uptime()
{
    if (g_uptime != nullptr) {
        return g_uptime();
    }
#if defined(ARDUINO)
    return millis() / 1000u;
#else
    return 0;
#endif
}

}  // namespace

const char* level_name(Level level)
{
    switch (level) {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO";
    case Level::Warn:
        return "WARN";
    case Level::Error:
        return "ERROR";
    }
    return "INFO";
}

void set_clock(ClockFn unix_s, UptimeFn uptime_s)
{
    g_unix = unix_s;
    g_uptime = uptime_s;
}

bool time_valid()
{
    // Anything before 2020-01-01 is treated as unsynced (epoch + uptime).
    return default_unix() >= 1577836800ull;
}

std::uint64_t unix_now()
{
    if (time_valid()) {
        return default_unix();
    }
    return static_cast<std::uint64_t>(default_uptime());
}

std::size_t syslog_topic(std::string_view device_id, char* out, std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap == 0 || device_id.empty()) {
        return 0;
    }
    std::size_t n = 0;
    if (!append_view(out, cap, n, "sys/devices/") || !append_view(out, cap, n, device_id) ||
        !append_view(out, cap, n, "/syslog")) {
        write_nul(out, cap);
        return 0;
    }
    out[n] = '\0';
    return n;
}

std::size_t format_iso_utc(std::uint64_t unix_s, char* out, std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap < 21) {
        return 0;
    }
    const std::uint64_t days = unix_s / 86400ull;
    const std::uint32_t sod = static_cast<std::uint32_t>(unix_s % 86400ull);
    std::uint32_t y = 0;
    std::uint32_t m = 0;
    std::uint32_t d = 0;
    civil_from_days(static_cast<std::int64_t>(days), y, m, d);
    const std::uint32_t hh = sod / 3600u;
    const std::uint32_t mm = (sod % 3600u) / 60u;
    const std::uint32_t ss = sod % 60u;
    const int n = std::snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                                static_cast<unsigned>(y), static_cast<unsigned>(m),
                                static_cast<unsigned>(d), static_cast<unsigned>(hh),
                                static_cast<unsigned>(mm), static_cast<unsigned>(ss));
    if (n <= 0 || static_cast<std::size_t>(n) >= cap) {
        write_nul(out, cap);
        return 0;
    }
    return static_cast<std::size_t>(n);
}

std::size_t format_human_line(Level level, std::string_view msg, std::uint64_t unix_s, char* out,
                              std::size_t cap)
{
    write_nul(out, cap);
    if (out == nullptr || cap == 0) {
        return 0;
    }
    char iso[24];
    if (format_iso_utc(unix_s, iso, sizeof(iso)) == 0) {
        return 0;
    }
    std::size_t n = 0;
    if (!append_view(out, cap, n, iso) || !append_char(out, cap, n, ' ') ||
        !append_view(out, cap, n, level_name(level)) || !append_char(out, cap, n, ' ')) {
        write_nul(out, cap);
        return 0;
    }
    for (char c : msg) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
        if (!append_char(out, cap, n, c)) {
            write_nul(out, cap);
            return 0;
        }
    }
    out[n] = '\0';
    return n;
}

Logger::Logger(FileStore& fs, PublishFn publish, void* publish_ctx, SerialFn serial,
               void* serial_ctx)
    : fs_(fs), publish_(publish), publish_ctx_(publish_ctx), serial_(serial),
      serial_ctx_(serial_ctx)
{
}

void Logger::set_device_id(const char* id)
{
    if (id == nullptr) {
        device_id_[0] = '\0';
        return;
    }
    std::size_t n = 0;
    while (n + 1 < sizeof(device_id_) && id[n] != '\0') {
        device_id_[n] = id[n];
        ++n;
    }
    device_id_[n] = '\0';
}

void Logger::set_mqtt_ready(bool ready)
{
    mqtt_ready_ = ready;
    if (ready) {
        session_sent_ = 0;
    }
}

void Logger::set_log_mark(std::uint32_t mark)
{
    mark_ = mark;
}

void Logger::set_mark_persist(PersistMarkFn fn, void* ctx)
{
    persist_mark_ = fn;
    persist_ctx_ = ctx;
}

void Logger::persist_mark()
{
    if (persist_mark_ != nullptr) {
        persist_mark_(mark_, persist_ctx_);
    }
}

std::size_t Logger::active_size() const
{
    return fs_.size(kActivePath);
}

bool Logger::replay_pending() const
{
    if (!mqtt_ready_ || device_id_[0] == '\0') {
        return false;
    }
    const std::size_t total = fs_.size(kActivePath);
    return static_cast<std::size_t>(mark_) < total;
}

void Logger::flush_repeat()
{
    if (!have_last_ || repeat_ == 0) {
        return;
    }
    char msg[48];
    std::snprintf(msg, sizeof(msg), "Previous message repeated %lu times",
                  static_cast<unsigned long>(repeat_));
    repeat_ = 0;
    // Do not fold the summary into another repeat of itself.
    have_last_ = false;
    emit_line(Level::Warn, msg, false);
}

void Logger::log(Level level, std::string_view msg)
{
    if (msg.size() > kMsgMax) {
        msg = msg.substr(0, kMsgMax);
    }
    if (have_last_ && level == last_level_ && msg == std::string_view(last_msg_)) {
        ++repeat_;
        return;
    }
    if (have_last_ && repeat_ > 0) {
        flush_repeat();
    }
    emit_line(level, msg, true);
}

void Logger::emit_line(Level level, std::string_view msg, bool count_as_content)
{
    char human[kLineMax];
    if (format_human_line(level, msg, unix_now(), human, sizeof(human)) == 0) {
        return;
    }
    if (serial_ != nullptr) {
        serial_(human, serial_ctx_);
    }

    if (level != Level::Debug) {
        const std::size_t before = fs_.size(kActivePath);
        write_record(human);
        if (mqtt_ready_) {
            if (mqtt_publish_human(human) && mark_ == before) {
                // Contiguous prefix was caught up; this live line extends it.
                mark_ = static_cast<std::uint32_t>(fs_.size(kActivePath));
                persist_mark();
            }
        }
    }

    if (count_as_content) {
        last_level_ = level;
        std::size_t n = 0;
        for (char c : msg) {
            if (n + 1 >= sizeof(last_msg_)) {
                break;
            }
            last_msg_[n++] = c;
        }
        last_msg_[n] = '\0';
        have_last_ = true;
        repeat_ = 0;
    }
}

bool Logger::mqtt_publish_human(std::string_view human)
{
    if (publish_ == nullptr || device_id_[0] == '\0') {
        return false;
    }
    char topic[kTopicMax];
    if (syslog_topic(device_id_, topic, sizeof(topic)) == 0) {
        return false;
    }
    char esc[kLineMax * 2];
    std::size_t e = 0;
    for (char c : human) {
        if (c == '"' || c == '\\') {
            if (e + 2 >= sizeof(esc)) {
                return false;
            }
            esc[e++] = '\\';
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            continue;
        }
        if (e + 1 >= sizeof(esc)) {
            return false;
        }
        esc[e++] = c;
    }
    esc[e] = '\0';
    char payload[kLineMax + 160];
    // value's ISO stamp is event time (when the line was logged). The numeric
    // timestamp field is transport time (when this MQTT message is sent). On a
    // live line they coincide; on a replayed line the gap is how long delivery
    // was blocked — that mismatch is intentional, not a bug.
    const std::uint64_t ts = unix_now();
    const int m =
        std::snprintf(payload, sizeof(payload),
                      "{\"friendly_name\":\"System log\",\"value\":\"%s\","
                      "\"entity\":\"%s\",\"sensorId\":\"syslog\",\"timestamp\":%llu}",
                      esc, device_id_, static_cast<unsigned long long>(ts));
    if (m <= 0 || static_cast<std::size_t>(m) >= sizeof(payload)) {
        return false;
    }
    return publish_(topic, std::string_view(payload, static_cast<std::size_t>(m)), publish_ctx_);
}

void Logger::rotate_if_needed(std::size_t incoming)
{
    const std::size_t cur = fs_.size(kActivePath);
    if (cur + incoming <= kFileCap) {
        return;
    }
    fs_.remove(kPrevPath);
    fs_.rename(kActivePath, kPrevPath);
    ++rotations_;
    mark_ = 0;
    persist_mark();
}

bool Logger::write_record(std::string_view human)
{
    char rec[kLineMax + 4];
    std::size_t n = 0;
    if (!append_view(rec, sizeof(rec), n, human) || !append_char(rec, sizeof(rec), n, '\n')) {
        return false;
    }
    rotate_if_needed(n);
    return fs_.append(kActivePath, std::string_view(rec, n));
}

bool Logger::read_line_at(std::size_t offset, char* out, std::size_t cap,
                          std::size_t* line_end) const
{
    if (out == nullptr || cap == 0 || line_end == nullptr) {
        return false;
    }
    write_nul(out, cap);
    const std::size_t total = fs_.size(kActivePath);
    if (offset >= total) {
        *line_end = offset;
        return false;
    }
    std::size_t n = 0;
    std::size_t pos = offset;
    char chunk[64];
    while (pos < total) {
        const std::size_t got = fs_.read(kActivePath, pos, chunk, sizeof(chunk));
        if (got == 0) {
            break;
        }
        for (std::size_t i = 0; i < got; ++i) {
            const char c = chunk[i];
            ++pos;
            if (c == '\n') {
                out[n] = '\0';
                *line_end = pos;
                return true;
            }
            if (c == '\r') {
                continue;
            }
            if (n + 1 < cap) {
                out[n++] = c;
            }
        }
    }
    out[n] = '\0';
    *line_end = pos;
    return n > 0;
}

std::size_t Logger::count_lines(std::size_t from, std::size_t to) const
{
    if (from >= to) {
        return 0;
    }
    std::size_t count = 0;
    std::size_t pos = from;
    char chunk[64];
    bool in_line = false;
    while (pos < to) {
        const std::size_t want = (to - pos) < sizeof(chunk) ? (to - pos) : sizeof(chunk);
        const std::size_t got = fs_.read(kActivePath, pos, chunk, want);
        if (got == 0) {
            break;
        }
        for (std::size_t i = 0; i < got; ++i) {
            const char c = chunk[i];
            if (c == '\n') {
                if (in_line) {
                    ++count;
                }
                in_line = false;
            } else if (c != '\r') {
                in_line = true;
            }
        }
        pos += got;
    }
    if (in_line) {
        ++count;
    }
    return count;
}

std::size_t Logger::replay_undelivered()
{
    if (!mqtt_ready_ || publish_ == nullptr || device_id_[0] == '\0') {
        return 0;
    }
    const std::size_t total = fs_.size(kActivePath);
    if (static_cast<std::size_t>(mark_) > total) {
        mark_ = static_cast<std::uint32_t>(total);
        persist_mark();
    }
    if (static_cast<std::size_t>(mark_) >= total) {
        return 0;
    }

    if (session_sent_ >= kReplayCap) {
        const std::size_t remaining = count_lines(mark_, total);
        mark_ = static_cast<std::uint32_t>(total);
        persist_mark();
        char msg[72];
        std::snprintf(msg, sizeof(msg), "%u earlier lines not replayed, see log file",
                      static_cast<unsigned>(remaining));
        // Through the normal logger so it lands in the file and on MQTT.
        log(Level::Warn, msg);
        return 1;
    }

    char human[kLineMax];
    std::size_t line_end = mark_;
    if (!read_line_at(mark_, human, sizeof(human), &line_end)) {
        mark_ = static_cast<std::uint32_t>(total);
        persist_mark();
        return 0;
    }
    if (human[0] == '\0') {
        // Blank line — advance past it without counting against the cap.
        mark_ = static_cast<std::uint32_t>(line_end);
        persist_mark();
        return 0;
    }
    if (!mqtt_publish_human(human)) {
        return 0;
    }
    mark_ = static_cast<std::uint32_t>(line_end);
    persist_mark();
    ++session_sent_;
    return 1;
}

const char* hw_reset_reason_string(int esp_reset_reason)
{
    // Map esp_reset_reason_t to the ROM strings the legacy published.
    switch (esp_reset_reason) {
    case 1:  // ESP_RST_POWERON
        return "POWERON_RESET";
    case 2:  // ESP_RST_EXT
        return "EXT_CPU_RESET";
    case 3:  // ESP_RST_SW
        return "SW_CPU_RESET";
    case 4:  // ESP_RST_PANIC
        return "SW_CPU_RESET";
    case 5:  // ESP_RST_INT_WDT
        return "TG0WDT_SYS_RESET";
    case 6:  // ESP_RST_TASK_WDT
        return "TGWDT_CPU_RESET";
    case 7:  // ESP_RST_WDT
        return "OWDT_RESET";
    case 8:  // ESP_RST_DEEPSLEEP
        return "DEEPSLEEP_RESET";
    case 9:  // ESP_RST_BROWNOUT
        return "RTCWDT_BROWN_OUT_RESET";
    case 10:  // ESP_RST_SDIO
        return "SDIO_RESET";
    default:
        return "NO_MEAN";
    }
}

#if !defined(ARDUINO)

MemoryFileStore::Slot* MemoryFileStore::find(const char* path)
{
    for (Slot& s : slots_) {
        if (s.used && std::strcmp(s.path, path) == 0) {
            return &s;
        }
    }
    return nullptr;
}

const MemoryFileStore::Slot* MemoryFileStore::find(const char* path) const
{
    for (const Slot& s : slots_) {
        if (s.used && std::strcmp(s.path, path) == 0) {
            return &s;
        }
    }
    return nullptr;
}

MemoryFileStore::Slot* MemoryFileStore::insert(const char* path)
{
    if (Slot* s = find(path)) {
        return s;
    }
    for (Slot& s : slots_) {
        if (!s.used) {
            s.used = true;
            s.n = 0;
            s.data[0] = '\0';
            std::size_t i = 0;
            while (i + 1 < sizeof(s.path) && path[i] != '\0') {
                s.path[i] = path[i];
                ++i;
            }
            s.path[i] = '\0';
            return &s;
        }
    }
    return nullptr;
}

void MemoryFileStore::clear()
{
    for (Slot& s : slots_) {
        s = Slot{};
    }
}

const char* MemoryFileStore::contents(const char* path) const
{
    const Slot* s = find(path);
    return s != nullptr ? s->data : "";
}

bool MemoryFileStore::append(const char* path, std::string_view data)
{
    Slot* s = insert(path);
    if (s == nullptr || s->n + data.size() > kFileBytes - 1) {
        return false;
    }
    for (char c : data) {
        s->data[s->n++] = c;
    }
    s->data[s->n] = '\0';
    return true;
}

std::size_t MemoryFileStore::size(const char* path) const
{
    const Slot* s = find(path);
    return s != nullptr ? s->n : 0;
}

bool MemoryFileStore::remove(const char* path)
{
    if (Slot* s = find(path)) {
        *s = Slot{};
        return true;
    }
    return true;
}

bool MemoryFileStore::rename(const char* from, const char* to)
{
    Slot* src = find(from);
    if (src == nullptr) {
        return false;
    }
    char tmp[kFileBytes];
    const std::size_t n = src->n;
    std::memcpy(tmp, src->data, n + 1);
    remove(from);
    remove(to);
    Slot* dst = insert(to);
    if (dst == nullptr) {
        return false;
    }
    std::memcpy(dst->data, tmp, n + 1);
    dst->n = n;
    return true;
}

std::size_t MemoryFileStore::read(const char* path, std::size_t offset, char* out,
                                 std::size_t cap) const
{
    const Slot* s = find(path);
    if (s == nullptr || out == nullptr || cap == 0 || offset >= s->n) {
        return 0;
    }
    const std::size_t n = (s->n - offset) < cap ? (s->n - offset) : cap;
    std::memcpy(out, s->data + offset, n);
    return n;
}

#endif

}  // namespace p1log
