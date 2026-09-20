#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace p1log {

enum class Level : std::uint8_t { Debug = 0, Info = 1, Warn = 2, Error = 3 };

constexpr std::size_t kFileCap = 32768;
constexpr std::size_t kReplayCap = 256;
constexpr std::size_t kMsgMax = 160;
constexpr std::size_t kLineMax = 220;
constexpr std::size_t kTopicMax = 64;
constexpr char kActivePath[] = "/syslog.txt";
constexpr char kPrevPath[] = "/syslog0.txt";
// Separates the human line from the delivered flag. Never appears in messages
// we format ourselves; chosen so a message containing ", 0" cannot corrupt it.
constexpr char kFlagSep = '\x1e';

const char* level_name(Level level);

// Unix seconds. Injected in host tests; wall clock + NTP on the device.
using ClockFn = std::uint64_t (*)();
using UptimeFn = std::uint32_t (*)();
using PublishFn = bool (*)(std::string_view topic, std::string_view payload, void* ctx);
using SerialFn = void (*)(std::string_view line, void* ctx);

void set_clock(ClockFn unix_s, UptimeFn uptime_s);
bool time_valid();
std::uint64_t unix_now();

// Same builder for live publish and store-and-forward replay.
std::size_t syslog_topic(std::string_view device_id, char* out, std::size_t cap);

std::size_t format_iso_utc(std::uint64_t unix_s, char* out, std::size_t cap);
std::size_t format_human_line(Level level, std::string_view msg, std::uint64_t unix_s, char* out,
                              std::size_t cap);

// Filesystem surface used by the logger. Arduino wraps LittleFS; tests use RAM.
class FileStore {
public:
    virtual ~FileStore() = default;
    virtual bool append(const char* path, std::string_view data) = 0;
    virtual std::size_t size(const char* path) const = 0;
    virtual bool remove(const char* path) = 0;
    virtual bool rename(const char* from, const char* to) = 0;
    // Read up to cap bytes from offset. Returns bytes copied; 0 at EOF / missing.
    virtual std::size_t read(const char* path, std::size_t offset, char* out,
                             std::size_t cap) const = 0;
};

#if !defined(ARDUINO)
class MemoryFileStore final : public FileStore {
public:
    bool append(const char* path, std::string_view data) override;
    std::size_t size(const char* path) const override;
    bool remove(const char* path) override;
    bool rename(const char* from, const char* to) override;
    std::size_t read(const char* path, std::size_t offset, char* out,
                     std::size_t cap) const override;

    void clear();
    const char* contents(const char* path) const;

private:
    static constexpr std::size_t kFiles = 4;
    static constexpr std::size_t kFileBytes = kFileCap + 512;
    struct Slot {
        char path[24]{};
        char data[kFileBytes]{};
        std::size_t n = 0;
        bool used = false;
    };
    Slot slots_[kFiles]{};
    Slot* find(const char* path);
    const Slot* find(const char* path) const;
    Slot* insert(const char* path);
};
#endif

#if defined(ARDUINO)
class LittleFsStore final : public FileStore {
public:
    // Mount with format-on-failure. Returns false if still unusable.
    // *formatted is set when the partition was erased.
    bool begin(bool* formatted = nullptr);
    bool self_test();

    bool append(const char* path, std::string_view data) override;
    std::size_t size(const char* path) const override;
    bool remove(const char* path) override;
    bool rename(const char* from, const char* to) override;
    std::size_t read(const char* path, std::size_t offset, char* out,
                     std::size_t cap) const override;

    bool mounted() const { return mounted_; }

private:
    bool mounted_ = false;
};
#endif

class Logger {
public:
    Logger(FileStore& fs, PublishFn publish, void* publish_ctx, SerialFn serial, void* serial_ctx);

    void set_device_id(const char* id);
    void set_mqtt_ready(bool ready);

    void log(Level level, std::string_view msg);
    // Flush a pending repeat count without a new message (timer / shutdown).
    void flush_repeat();

    // Replay undelivered lines after MQTT comes up. Returns how many were sent.
    std::size_t replay_undelivered();

    std::size_t rotations() const { return rotations_; }
    std::size_t active_size() const;

private:
    void emit_line(Level level, std::string_view msg, bool count_as_content);
    bool write_record(std::string_view human, bool delivered);
    void rotate_if_needed(std::size_t incoming);
    bool mqtt_publish_human(std::string_view human);

    FileStore& fs_;
    PublishFn publish_;
    void* publish_ctx_;
    SerialFn serial_;
    void* serial_ctx_;
    char device_id_[16]{};
    bool mqtt_ready_ = false;

    Level last_level_ = Level::Debug;
    char last_msg_[kMsgMax]{};
    std::uint32_t repeat_ = 0;
    bool have_last_ = false;
    std::size_t rotations_ = 0;
};

// Legacy rtc_get_reset_reason strings, keyed from esp_reset_reason().
const char* hw_reset_reason_string(int esp_reset_reason);

}  // namespace p1log
