#pragma once

#include <dsmr_parser/fields.h>
#include <dsmr_parser/packet_accumulator.h>
#include <dsmr_parser/parser.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string>
#include <string_view>

namespace p1dsmr {

namespace f = dsmr_parser::fields;

// Stock dsmr_parser fields used by the dongle and the host tests. No local
// DEFINE_FIELD: Belgian codes the library already has are included; codes
// fields left out of this parser (grid config, EANs, water-on-24.2.3) are
// not added here.
using Data = dsmr_parser::ParsedData<
    f::identification,
    f::p1_version,
    f::p1_version_be,
    f::timestamp,
    f::equipment_id,
    f::energy_delivered_tariff1,
    f::energy_delivered_tariff2,
    f::energy_returned_tariff1,
    f::energy_returned_tariff2,
    f::electricity_tariff,
    f::power_delivered,
    f::power_returned,
    f::electricity_threshold,
    f::electricity_switch_position,
    f::electricity_failures,
    f::electricity_long_failures,
    f::electricity_failure_log,
    f::electricity_sags_l1,
    f::electricity_sags_l2,
    f::electricity_sags_l3,
    f::electricity_swells_l1,
    f::electricity_swells_l2,
    f::electricity_swells_l3,
    f::message_long,
    f::voltage_l1,
    f::voltage_l2,
    f::voltage_l3,
    f::current_l1,
    f::current_l2,
    f::current_l3,
    f::current_fuse_l1,
    f::power_delivered_l1,
    f::power_delivered_l2,
    f::power_delivered_l3,
    f::power_returned_l1,
    f::power_returned_l2,
    f::power_returned_l3,
    f::gas_device_type,
    f::gas_equipment_id,
    f::gas_equipment_id_be,
    f::gas_valve_position,
    f::gas_delivered,
    f::gas_delivered_be,
    f::water_device_type,
    f::water_equipment_id,
    f::water_delivered,
    f::active_energy_import_current_average_demand,
    f::active_energy_import_maximum_demand_running_month,
    f::active_energy_import_maximum_demand_last_13_months>;

struct FeedStats {
    std::size_t ok = 0;
    std::size_t rejected = 0;
};

// Byte-in, parsed telegram out — or a rejection. Arduino-free.
//
// data() and the string_views inside it are valid until the next feed()
// that starts a new packet (a '/'). Copy values you need to keep.
class Reader {
public:
    enum class Status : std::uint8_t { NeedMore, Ok, Rejected };

    static constexpr std::size_t kBufferSize = 4096;

    Reader();
    ~Reader();

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    Status feed(std::uint8_t byte);

    template <typename OnOk>
    FeedStats feed(std::span<const std::uint8_t> bytes, OnOk&& on_ok);

    FeedStats feed(std::span<const std::uint8_t> bytes);

    const Data& data() const;
    std::string_view raw() const { return raw_; }
    bool has_telegram() const { return has_; }

private:
    void reset_data();
    Data& data_mut();
    bool parse_telegram(const dsmr_parser::DsmrUnencryptedTelegram& telegram);

    std::uint8_t buf_[kBufferSize]{};
    dsmr_parser::PacketAccumulator acc_;
    alignas(Data) unsigned char data_store_[sizeof(Data)];
    std::string raw_;
    int crc_nibbles_left_ = 0;
    bool has_ = false;
};

template <typename OnOk>
FeedStats Reader::feed(std::span<const std::uint8_t> bytes, OnOk&& on_ok)
{
    FeedStats stats;
    for (const std::uint8_t b : bytes) {
        switch (feed(b)) {
        case Status::Ok:
            ++stats.ok;
            on_ok(data());
            break;
        case Status::Rejected:
            ++stats.rejected;
            break;
        case Status::NeedMore:
            break;
        }
    }
    return stats;
}

}  // namespace p1dsmr
