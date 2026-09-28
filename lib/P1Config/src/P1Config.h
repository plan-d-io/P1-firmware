#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#if defined(ARDUINO)
#include <Preferences.h>
#endif

namespace p1cfg {

// ---------------------------------------------------------------------------
// Settings catalogue — one table, type as a field. Four storage types.
// ---------------------------------------------------------------------------

enum class CfgType : std::uint8_t { Bool, I32, U32, Str };

using CfgFlags = std::uint8_t;
constexpr CfgFlags CFG_IN_API = 1;
constexpr CfgFlags CFG_SECRET = 2;
constexpr CfgFlags CFG_REBOOT = 4;
constexpr CfgFlags CFG_HIDDEN = 8;

constexpr std::uint32_t kSchemaVersion = 6;

constexpr std::size_t kHostMax = 64;
constexpr std::size_t kIdMax = 32;
constexpr std::size_t kUserMax = 32;
constexpr std::size_t kPassMax = 64;
constexpr std::size_t kPrefixMax = 48;
constexpr std::size_t kHaMax = 32;
constexpr std::size_t kRegionMax = 8;
constexpr std::size_t kSsidMax = 33;  // IEEE 802.11 SSID is 32 octets + NUL
constexpr std::size_t kLastResetMax = 64;
constexpr std::size_t kIpMax = 16;  // "255.255.255.255" + NUL
constexpr std::size_t kUuidMax = 20;  // "P1" + 16 hex + NUL
constexpr std::size_t kRelChanMax = 16;
constexpr std::size_t kEmailMax = 64;
constexpr std::size_t kEidClaimMax = 32;

// Largest string setting. JSON parse uses each entry's str_cap; this is the
// bound a scratch buffer would need, and a tripwire if a longer setting is added.
static_assert(kIdMax <= kHostMax);
static_assert(kUserMax <= kHostMax);
static_assert(kPassMax <= kHostMax);
static_assert(kPrefixMax <= kHostMax);
static_assert(kHaMax <= kHostMax);
static_assert(kRegionMax <= kHostMax);
static_assert(kSsidMax <= kHostMax);
static_assert(kLastResetMax <= kHostMax);
static_assert(kIpMax <= kHostMax);
static_assert(kUuidMax <= kHostMax);
static_assert(kRelChanMax <= kHostMax);
static_assert(kEmailMax <= kHostMax);
static_assert(kEidClaimMax <= kHostMax);

// Worst-case FLD_OVR: every catalogue slot as "id=0,". Catalogue static_assert
// checks the real table fits. identification is 14 chars; 16 is headroom.
constexpr std::size_t kMaxFields = 64;
constexpr std::size_t kMaxFieldId = 16;
constexpr std::size_t kOverrideBufCap = kMaxFields * (kMaxFieldId + 3) + 1;

struct Settings {
    bool mqtt_en = false;
    bool mqtt_tls = false;
    bool mqtt_auth = false;
    std::uint32_t mqtt_port = 1883;
    char mqtt_host[kHostMax]{};
    char mqtt_id[kIdMax]{};
    char mqtt_user[kUserMax]{};
    char mqtt_pass[kPassMax]{};
    char mqtt_pfix[kPrefixMax]{};
    bool ha_en = false;
    char ha_device[kHaMax]{};
    std::int32_t frmt_pyld = 3;
    std::uint32_t upl_throttle = 10;
    char region[kRegionMax]{};
    char wifi_ssid[kSsidMax]{};
    char wifi_pass[kPassMax]{};
    std::uint32_t reboots = 0;
    char last_reset[kLastResetMax]{};
    bool fip_en = false;
    char fipaddr[kIpMax]{};
    char fsubn[kIpMax]{};
    char fdefgtw[kIpMax]{};
    char fdns1[kIpMax]{};
    char fdns2[kIpMax]{};
    bool eid_en = false;
    char eidclaim[kEidClaimMax]{};
    char eid_provkey[kPassMax]{};
    char eid_provsec[kPassMax]{};
    char uuid[kUuidMax]{};
    char rel_chan[kRelChanMax]{};
    bool upd_auto = true;
    bool upd_autochk = true;
    char email[kEmailMax]{};
    bool push_full = false;
    bool rint_spiffs = false;
    // Byte offset in /syslog.txt up to which lines have been MQTT-published.
    std::uint32_t log_mark = 0;
    std::uint32_t cfg_ver = 0;
};

struct CfgEntry {
    const char* key;
    const char* label;
    CfgType type;
    CfgFlags flags;
    union {
        bool* b;
        std::int32_t* i;
        std::uint32_t* u;
        char* s;
    } var{};
    union {
        bool b;
        std::int32_t i;
        std::uint32_t u;
        const char* s;
    } def{};
    std::size_t str_cap = 0;
};

// ---------------------------------------------------------------------------
// Field catalogue — one entry per publishable meter value. COFY metadata
// lives here so payload formats 0–3 do not need a second table.
// metric == nullptr and kind == None → emit neither key.
// metric empty string and kind set → omit `metric`, still emit `metricKind`.
// ---------------------------------------------------------------------------

enum class RegionTag : std::uint8_t { Common, Be, Nl, Auto };

enum class MetricKind : std::uint8_t { None, Cumulative, Gauge };

struct Field {
    const char* id;
    const char* label;
    const char* ha_class;  // "" if none
    RegionTag region;
    bool default_on;
    bool selectable;  // appears in the user's field list
    const char* metric;  // nullptr = no mapping; "" = omit the key
    MetricKind kind;
};

const Field* catalogue();
std::size_t catalogue_size();
const Field* find_field(std::string_view id);
bool field_in_region(const Field& f, RegionTag filter);

// ---------------------------------------------------------------------------
// Storage — one interface, two implementations, nothing further.
// ---------------------------------------------------------------------------

class StoreBackend {
public:
    virtual ~StoreBackend() = default;
    virtual bool begin(bool read_only) = 0;
    virtual void end() = 0;
    virtual bool has(const char* key) const = 0;
    virtual bool get_bool(const char* key, bool def) const = 0;
    virtual std::int32_t get_i32(const char* key, std::int32_t def) const = 0;
    virtual std::uint32_t get_u32(const char* key, std::uint32_t def) const = 0;
    virtual void get_str(const char* key, char* out, std::size_t cap,
                         const char* def) const = 0;
    virtual void put_bool(const char* key, bool v) = 0;
    virtual void put_i32(const char* key, std::int32_t v) = 0;
    virtual void put_u32(const char* key, std::uint32_t v) = 0;
    virtual void put_str(const char* key, const char* v) = 0;
    virtual void remove(const char* key) = 0;
};

#if !defined(ARDUINO)
class MemoryBackend final : public StoreBackend {
public:
    bool begin(bool read_only) override;
    void end() override;
    bool has(const char* key) const override;
    bool get_bool(const char* key, bool def) const override;
    std::int32_t get_i32(const char* key, std::int32_t def) const override;
    std::uint32_t get_u32(const char* key, std::uint32_t def) const override;
    void get_str(const char* key, char* out, std::size_t cap,
                 const char* def) const override;
    void put_bool(const char* key, bool v) override;
    void put_i32(const char* key, std::int32_t v) override;
    void put_u32(const char* key, std::uint32_t v) override;
    void put_str(const char* key, const char* v) override;
    void remove(const char* key) override;

    std::size_t write_count() const { return writes_; }

private:
    struct Slot {
        enum class Tag : std::uint8_t { Empty, Bool, I32, U32, Str };
        Tag tag = Tag::Empty;
        bool b = false;
        std::int32_t i = 0;
        std::uint32_t u = 0;
        char s[kOverrideBufCap]{};  // largest stored string is FLD_OVR
    };

    static constexpr std::size_t kCap = 48;
    struct Pair {
        char key[16]{};
        Slot slot;
    };
    Pair slots_[kCap]{};
    std::size_t n_ = 0;
    std::size_t writes_ = 0;
    bool open_ = false;

    Slot* find(const char* key);
    const Slot* find(const char* key) const;
    Slot* insert(const char* key);
};
#endif

#if defined(ARDUINO)
class NvsBackend final : public StoreBackend {
public:
    bool begin(bool read_only) override;
    void end() override;
    bool has(const char* key) const override;
    bool get_bool(const char* key, bool def) const override;
    std::int32_t get_i32(const char* key, std::int32_t def) const override;
    std::uint32_t get_u32(const char* key, std::uint32_t def) const override;
    void get_str(const char* key, char* out, std::size_t cap,
                 const char* def) const override;
    void put_bool(const char* key, bool v) override;
    void put_i32(const char* key, std::int32_t v) override;
    void put_u32(const char* key, std::uint32_t v) override;
    void put_str(const char* key, const char* v) override;
    void remove(const char* key) override;

private:
    mutable Preferences prefs_;
    bool open_ = false;
};
#endif

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

enum class JsonStatus : std::uint8_t { Ok, Parse, UnknownKey, WrongType, Invalid };

const char* json_status_name(JsonStatus s);

// True if s is a dotted-quad (0–255 each). Empty string is not valid.
bool ip_ok(const char* s);
bool rel_chan_ok(const char* s);

// Parse dotted-quad into host-order uint32. Returns false if not ip_ok.
bool parse_ipv4(const char* s, std::uint32_t& host_order);

// Contiguous mask (ones then zeros), e.g. /24 /16 /25. Zero mask is false.
bool mask_contiguous(std::uint32_t mask_host);

// When FIP_EN is on: same subnet, contiguous mask, IP/GW not network or broadcast.
bool static_ip_consistent(const char* ip, const char* mask, const char* gateway);

// Write "P1" + 16 hex from eight random bytes. Returns false if cap too small.
bool format_uuid(char* out, std::size_t cap, const std::uint8_t* rnd8);
// If uuid is empty, fill it. Returns true when a new value was written.
bool ensure_uuid(char* uuid, std::size_t cap, const std::uint8_t* rnd8);

// Comfortable bound for Store::to_json with every API string at capacity.
constexpr std::size_t kToJsonBufMin = 4096;

class Store {
public:
    static constexpr std::size_t kSettingCount = 37;

    explicit Store(StoreBackend& backend);

    void apply_defaults();
    void load();
    void save();

    // Clear WiFi credentials and disable fixed IP; leave static IP fields and
    // every other setting alone.
    void reset_wifi();
    // Every catalogue setting back to its table default, including clearing
    // HA_MIG / HA_SLUG / HA_PFIX which are not settings.
    void factory_reset();

    bool migrated() const { return migrated_; }

    Settings& values() { return v_; }
    const Settings& values() const { return v_; }

    const CfgEntry* settings() const { return table_; }
    std::size_t setting_count() const { return kSettingCount; }
    const CfgEntry* find_setting(std::string_view key) const;

    bool field_on(std::string_view id) const;
    void set_field_on(std::string_view id, bool on);

    // One-shot HA per-entity → device discovery migration. Not a user setting.
    bool ha_migrated() const { return ha_mig_; }
    void set_ha_migrated();

    // Last identity discovery was published under. Not a user setting.
    bool has_published_ha_identity() const { return ha_pub_have_; }
    const char* published_ha_slug() const { return ha_pub_slug_; }
    const char* published_mqtt_pfix() const { return ha_pub_pfix_; }
    void set_published_ha_identity(const char* slug, const char* pfix);
    void clear_published_ha_identity();

    // On failure, if bad_key is non-null and bad_key_cap > 0, writes the
    // offending setting key (NUL-terminated) when one is known. Cleared to
    // empty on success. Existing one-argument call sites keep working.
    JsonStatus apply_json(std::string_view json, char* bad_key = nullptr,
                          std::size_t bad_key_cap = 0);
    // Object of field-id → bool. Rejects unknown / non-selectable ids as
    // UnknownKey and non-bools as WrongType. Restores overrides on failure.
    JsonStatus apply_fields_json(std::string_view json, char* bad_key = nullptr,
                                 std::size_t bad_key_cap = 0);
    // 0 means the buffer was too small (or cap == 0). Success is never 0:
    // the object is at least "{}".
    std::size_t to_json(char* out, std::size_t cap) const;

    // True when a successful apply_json changed REL_CHAN. Cleared on read.
    bool consume_rel_chan_changed();

private:
    void bind_table();
    void load_one(const CfgEntry& e);
    void save_one(const CfgEntry& e);
    void migrate();
    void load_overrides();
    void save_overrides();

    StoreBackend& backend_;
    Settings v_{};
    CfgEntry table_[kSettingCount]{};
    std::uint8_t field_over_[kMaxFields]{};  // 0 = default, 1 = on, 2 = off
    bool migrated_ = false;
    bool ha_mig_ = false;
    bool ha_pub_have_ = false;
    char ha_pub_slug_[kHaMax]{};
    char ha_pub_pfix_[kPrefixMax]{};
    bool rel_chan_changed_ = false;
};

}  // namespace p1cfg
