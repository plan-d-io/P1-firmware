#include "P1Config.h"

#include <cstring>

namespace p1cfg {
namespace {

// ha_class values match the legacy dsmrKeys[] / mbusKeys[] deviceType strings.
// metric/kind match cofyKeys[] — empty metric still has a kind; nullptr metric
// plus None kind means the payload emits neither key.

constexpr Field kFields[] = {
    // Computed — never in a telegram.
    {"A-0:0.0.1", "Total energy consumed", "energy", RegionTag::Common, true,
     "GridElectricityImport", MetricKind::Cumulative},
    {"A-0:0.0.2", "Total energy injected", "energy", RegionTag::Common, true,
     "GridElectricityExport", MetricKind::Cumulative},
    {"A-0:0.0.3", "Total active power", "power", RegionTag::Common, true,
     "GridElectricityPower", MetricKind::Gauge},

    // Default on, parsed.
    {"1-0:1.8.1", "Total consumption T1", "energy", RegionTag::Common, true, "",
     MetricKind::Cumulative},
    {"1-0:1.8.2", "Total consumption T2", "energy", RegionTag::Common, true, "",
     MetricKind::Cumulative},
    {"1-0:2.8.1", "Total injection T1", "energy", RegionTag::Common, true, "",
     MetricKind::Cumulative},
    {"1-0:2.8.2", "Total injection T2", "energy", RegionTag::Common, true, "",
     MetricKind::Cumulative},
    {"0-0:96.14.0", "Active tariff period", "", RegionTag::Common, true, "",
     MetricKind::Gauge},
    {"1-0:1.7.0", "Active power consumption", "power", RegionTag::Common, true, "",
     MetricKind::Gauge},
    {"1-0:2.7.0", "Active power injection", "power", RegionTag::Common, true, "",
     MetricKind::Gauge},
    {"1-0:21.7.0", "Active power consumption L1", "power", RegionTag::Common, true,
     nullptr, MetricKind::None},
    {"1-0:41.7.0", "Active power consumption L2", "power", RegionTag::Common, true,
     nullptr, MetricKind::None},
    {"1-0:61.7.0", "Active power consumption L3", "power", RegionTag::Common, true,
     nullptr, MetricKind::None},
    {"1-0:22.7.0", "Active power injection L1", "power", RegionTag::Common, true,
     nullptr, MetricKind::None},
    {"1-0:42.7.0", "Active power injection L2", "power", RegionTag::Common, true,
     nullptr, MetricKind::None},
    {"1-0:62.7.0", "Active power injection L3", "power", RegionTag::Common, true,
     nullptr, MetricKind::None},
    {"1-0:32.7.0", "Voltage phase 1", "voltage", RegionTag::Common, true,
     "GridElectricityVoltage", MetricKind::Gauge},
    {"1-0:52.7.0", "Voltage phase 2", "voltage", RegionTag::Common, true,
     "GridElectricityVoltage", MetricKind::Gauge},
    {"1-0:72.7.0", "Voltage phase 3", "voltage", RegionTag::Common, true,
     "GridElectricityVoltage", MetricKind::Gauge},
    {"1-0:31.7.0", "Current phase 1", "current", RegionTag::Common, true,
     "GridElectricityCurrent", MetricKind::Gauge},
    {"1-0:51.7.0", "Current phase 2", "current", RegionTag::Common, true,
     "GridElectricityCurrent", MetricKind::Gauge},
    {"1-0:71.7.0", "Current phase 3", "current", RegionTag::Common, true,
     "GridElectricityCurrent", MetricKind::Gauge},
    {"1-0:1.4.0", "Current average demand", "power", RegionTag::Be, true, "",
     MetricKind::Gauge},
    {"1-0:1.6.0", "Maximum demand current month", "power", RegionTag::Be, true, "",
     MetricKind::Gauge},
    {"0-1:24.2.3", "Natural gas consumption", "gas", RegionTag::Be, true,
     "naturalGasImport", MetricKind::Cumulative},
    {"0-1:24.2.1", "Natural gas consumption", "gas", RegionTag::Nl, true, nullptr,
     MetricKind::None},

    // Default off. 0-0:98.1.0 is parsed but not catalogued.
    {"identification", "Identification", "", RegionTag::Common, false, nullptr,
     MetricKind::None},
    {"1-3:0.2.8", "P1 version", "", RegionTag::Nl, false, nullptr, MetricKind::None},
    {"0-0:96.1.4", "P1 version", "", RegionTag::Be, false, nullptr, MetricKind::None},
    {"0-0:1.0.0", "Telegram timestamp", "", RegionTag::Common, false, nullptr,
     MetricKind::None},
    {"0-0:96.1.1", "Equipment id", "", RegionTag::Common, false, nullptr,
     MetricKind::None},
    // Fixture-derived Be tags, not spec-checked: 0-0:17.0.0, 0-0:96.3.10,
    // 1-0:31.4.0, 0-1:24.4.0, 0-2:24.1.0 and 0-2:24.2.1. Inert while
    // REGION is auto. Verify against the specs when auto-detection lands.
    {"0-0:17.0.0", "Electricity threshold", "power", RegionTag::Be, false, nullptr,
     MetricKind::None},
    {"0-0:96.3.10", "Breaker position", "", RegionTag::Be, false, nullptr,
     MetricKind::None},
    {"0-0:96.7.21", "Power failures", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"0-0:96.7.9", "Long power failures", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"1-0:99.97.0", "Power failure log", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"1-0:32.32.0", "Voltage sags L1", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"1-0:52.32.0", "Voltage sags L2", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"1-0:72.32.0", "Voltage sags L3", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"1-0:32.36.0", "Voltage swells L1", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"1-0:52.36.0", "Voltage swells L2", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"1-0:72.36.0", "Voltage swells L3", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"0-0:96.13.0", "Text message", "", RegionTag::Common, false, "",
     MetricKind::Gauge},
    {"1-0:31.4.0", "Fuse rating L1", "current", RegionTag::Be, false, nullptr,
     MetricKind::None},
    {"0-1:24.1.0", "Gas device type", "", RegionTag::Common, false, nullptr,
     MetricKind::None},
    {"0-1:96.1.0", "Gas equipment id", "", RegionTag::Nl, false, nullptr,
     MetricKind::None},
    {"0-1:96.1.1", "Gas equipment id", "", RegionTag::Be, false, nullptr,
     MetricKind::None},
    {"0-1:24.4.0", "Gas valve position", "", RegionTag::Be, false, nullptr,
     MetricKind::None},
    {"0-2:24.1.0", "Water device type", "", RegionTag::Be, false, nullptr,
     MetricKind::None},
    {"0-2:24.2.1", "Water consumption", "water", RegionTag::Be, false, nullptr,
     MetricKind::None},
};

static_assert(sizeof(kFields) / sizeof(kFields[0]) <= kMaxFields);

constexpr std::size_t cstrlen(const char* s)
{
    std::size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

constexpr std::size_t kOverrideNeeded = []() {
    std::size_t n = 0;
    std::size_t max_id = 0;
    for (const Field& f : kFields) {
        const std::size_t len = cstrlen(f.id);
        if (len > max_id) {
            max_id = len;
        }
        ++n;
    }
    return n * (max_id + 3) + 1;
}();

static_assert(kOverrideNeeded <= kOverrideBufCap);
static_assert([]() {
    std::size_t max_id = 0;
    for (const Field& f : kFields) {
        const std::size_t len = cstrlen(f.id);
        if (len > max_id) {
            max_id = len;
        }
    }
    return max_id <= kMaxFieldId;
}());

}  // namespace

const Field* catalogue()
{
    return kFields;
}

std::size_t catalogue_size()
{
    return sizeof(kFields) / sizeof(kFields[0]);
}

const Field* find_field(std::string_view id)
{
    for (const Field& f : kFields) {
        if (id == f.id) {
            return &f;
        }
    }
    return nullptr;
}

bool field_in_region(const Field& f, RegionTag filter)
{
    if (filter == RegionTag::Auto) {
        return true;
    }
    if (f.region == RegionTag::Common) {
        return true;
    }
    return f.region == filter;
}

}  // namespace p1cfg
