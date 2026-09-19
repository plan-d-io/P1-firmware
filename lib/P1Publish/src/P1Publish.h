#pragma once

#include <P1Config.h>
#include <P1DSMR.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace p1pub {

constexpr std::size_t kTopicMax = 96;
// Sized for message_long / the failure log (up to 2048 chars) plus JSON wrapping.
constexpr std::size_t kPayloadMax = 3072;
// Heap buffer for a device discovery payload. Allocated at publish, then freed.
constexpr std::size_t kDiscoveryCap = 16384;
constexpr std::size_t kUniqueIdMax = 96;

// Product origin for device discovery (required by Home Assistant).
constexpr const char kOriginName[] = "P1 dongle";
constexpr const char kSupportUrl[] = "https://github.com/plan-d-io/P1-dongle";
constexpr const char kManufacturer[] = "plan-d.io";
constexpr const char kMeterModel[] = "P1 dongle for DSMR compatible utility meters";
constexpr const char kDongleModel[] = "P1 dongle debug monitoring";

constexpr const char kMigrateDiscovery[] = "{\"migrate_discovery\":true}";

// MQTT_PFIX + slugged catalogue label. 0 if the buffer is too small.
std::size_t reading_topic(const p1cfg::Store& cfg, const p1cfg::Field& field, char* out,
                          std::size_t cap);

// MQTT_PFIX with the trailing slash removed. 0 if the buffer is too small.
std::size_t availability_topic(const p1cfg::Store& cfg, char* out, std::size_t cap);

// Lowercase, spaces to underscores. 0 if the buffer is too small.
std::size_t slug(std::string_view s, char* out, std::size_t cap);

// slug(HA_DEVICE) + '_' + slug(label). The unique_id the fielded firmware used.
std::size_t meter_unique_id(std::string_view ha_device, std::string_view label, char* out,
                            std::size_t cap);

std::size_t device_discovery_topic(std::string_view object_id, char* out, std::size_t cap);

// MQTT_PFIX with a trailing slash stripped. Same rule as availability_topic().
std::size_t availability_from_prefix(std::string_view pfix, char* out, std::size_t cap);

// Topics to empty-retain when the published identity changes, in order:
// old device discovery, then old availability if the prefix changed.
// last empty → 0 (first publish). last == current → 0.
std::size_t identity_clear_topics(std::string_view last_slug, std::string_view last_pfix,
                                  std::string_view cur_slug, std::string_view cur_pfix,
                                  void (*fn)(std::string_view, void*), void* ctx);

const char* ha_state_class(const p1cfg::Field& field);
const char* ha_unit(const p1cfg::Field& field);

bool reading_present(const p1dsmr::Data& data, std::string_view id);
std::uint64_t discovery_field_mask(const p1dsmr::Data& data, const p1cfg::Store& cfg);
std::uint64_t enabled_field_mask(const p1cfg::Store& cfg);

// include_mask: full components. stub_mask: {"p":"sensor"} only (entity removal).
// config_url is the device configuration_url (http://<ip>).
std::size_t meter_discovery(const p1cfg::Store& cfg, const char* fw_ver, const char* config_url,
                            std::uint64_t include_mask, std::uint64_t stub_mask, char* out,
                            std::size_t cap);

std::size_t dongle_discovery(const p1cfg::Store& cfg, const char* device_id, const char* fw_ver,
                             const char* config_url, char* out, std::size_t cap);

// Old per-entity config topics: 50 catalogue labels + 11 sensors + reboot switch
// under both device ids. Returns how many topics were visited.
std::size_t for_each_legacy_discovery_topic(const p1cfg::Store& cfg, const char* device_id,
                                            const char* legacy_id,
                                            void (*fn)(std::string_view, void*), void* ctx);

// Turns one telegram into topic/payload pairs. No network, no heap per telegram.
// The views passed to `on_msg` are valid only for the duration of that call.
//
// Must live at file scope, never on a task stack. The object is the two
// buffers (~3.2 KB); the Arduino loop task is 8 KB.
class Builder {
public:
    template <typename Fn>
    std::size_t emit(const p1dsmr::Data& data, const p1cfg::Store& cfg, Fn&& on_msg)
    {
        struct Wrap {
            Fn* fn;
            static void call(void* ctx, std::string_view topic, std::string_view payload)
            {
                (*static_cast<Wrap*>(ctx)->fn)(topic, payload);
            }
        };
        Wrap w{&on_msg};
        return emit_impl(data, cfg, &w, &Wrap::call);
    }

private:
    std::size_t emit_impl(const p1dsmr::Data& data, const p1cfg::Store& cfg, void* ctx,
                          void (*on_msg)(void*, std::string_view, std::string_view));

    char topic_[kTopicMax]{};
    char payload_[kPayloadMax]{};
};

}  // namespace p1pub
