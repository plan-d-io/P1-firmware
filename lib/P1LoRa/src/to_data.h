#pragma once

// Reading → parsed telegram. Header-only, like from_telegram.h. The frame
// does not carry the meter's own timestamp, the equipment id, or which gas
// channel it was. Gas is written on the Belgian M-Bus field (0-1:24.2.3),
// which is the one from_telegram prefers. The caller supplies the reading
// timestamp and the gas timestamp; both stay absent when the pointer is null.

#include "from_telegram.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace p1lora {

inline void civil_from_days(int z, int& y, unsigned& m, unsigned& d)
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
}

// Inverse of from_telegram.h's meter_time. Writes 13 characters and a NUL.
inline bool format_meter_time(std::uint32_t packed, char* out, std::size_t cap)
{
    if (out == nullptr || cap < 14) {
        return false;
    }
    const bool summer = (packed & 0x80000000u) != 0;
    const std::uint32_t sec = packed & 0x7fffffffu;
    const int days = static_cast<int>(sec / 86400u);
    const unsigned rem = sec % 86400u;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days_from_civil(2000, 1, 1) + days, year, month, day);
    const unsigned hh = rem / 3600u;
    const unsigned mm = (rem / 60u) % 60u;
    const unsigned ss = rem % 60u;
    if (year < 2000 || year > 2099) {
        return false;
    }
    const int yy = year - 2000;
    out[0] = static_cast<char>('0' + yy / 10);
    out[1] = static_cast<char>('0' + yy % 10);
    out[2] = static_cast<char>('0' + month / 10);
    out[3] = static_cast<char>('0' + month % 10);
    out[4] = static_cast<char>('0' + day / 10);
    out[5] = static_cast<char>('0' + day % 10);
    out[6] = static_cast<char>('0' + hh / 10);
    out[7] = static_cast<char>('0' + hh % 10);
    out[8] = static_cast<char>('0' + mm / 10);
    out[9] = static_cast<char>('0' + mm % 10);
    out[10] = static_cast<char>('0' + ss / 10);
    out[11] = static_cast<char>('0' + ss % 10);
    out[12] = summer ? 'S' : 'W';
    out[13] = '\0';
    return true;
}

inline void put_u32(bool on, std::uint32_t raw, dsmr_parser::FixedValue& field, bool& present)
{
    if (!on || raw == kAbsent32) {
        present = false;
        return;
    }
    present = true;
    field._value = static_cast<std::int32_t>(raw);
}

inline void put_u16(bool on, std::uint16_t raw, int scale, dsmr_parser::FixedValue& field, bool& present)
{
    if (!on || raw == kAbsent16) {
        present = false;
        return;
    }
    present = true;
    field._value = static_cast<std::int32_t>(raw) * scale;
}

// timestamp and gas_timestamp are 13-character meter strings. Null leaves
// that timestamp absent. The object is replaced on every call, so a reused
// Data cannot keep a field this frame does not carry. Gas and the month
// peak come back only when the reading still has them.
inline void to_data(const Reading& r, p1dsmr::Data& d, const char* timestamp, const char* gas_timestamp)
{
    // ParsedData cannot be assigned. Reconstruct it in place.
    std::destroy_at(&d);
    std::construct_at(&d);
    if (timestamp != nullptr && timestamp[0] != '\0') {
        d.timestamp_present = true;
        d.timestamp = timestamp;
    } else {
        d.timestamp_present = false;
    }
    const bool tariff2 = (r.flags & kFlagTariff2) != 0;
    d.electricity_tariff_present = true;
    d.electricity_tariff = tariff2 ? "0002" : "0001";

    put_u32(true, r.t1_wh, d.energy_delivered_tariff1, d.energy_delivered_tariff1_present);
    put_u32(true, r.t2_wh, d.energy_delivered_tariff2, d.energy_delivered_tariff2_present);
    put_u32(true, r.inj1_wh, d.energy_returned_tariff1, d.energy_returned_tariff1_present);
    put_u32(true, r.inj2_wh, d.energy_returned_tariff2, d.energy_returned_tariff2_present);
    put_u16(true, r.power_w, 1, d.power_delivered, d.power_delivered_present);
    put_u16(true, r.inject_w, 1, d.power_returned, d.power_returned_present);
    put_u16(true, r.demand_w, 1, d.active_energy_import_current_average_demand,
            d.active_energy_import_current_average_demand_present);

    put_u16(true, r.v_dv[0], 100, d.voltage_l1, d.voltage_l1_present);
    put_u16(true, r.i_ca[0], 10, d.current_l1, d.current_l1_present);
    if (r.three_phase) {
        put_u16(true, r.p_plus_w[0], 1, d.power_delivered_l1, d.power_delivered_l1_present);
        put_u16(true, r.p_plus_w[1], 1, d.power_delivered_l2, d.power_delivered_l2_present);
        put_u16(true, r.p_plus_w[2], 1, d.power_delivered_l3, d.power_delivered_l3_present);
        put_u16(true, r.p_minus_w[0], 1, d.power_returned_l1, d.power_returned_l1_present);
        put_u16(true, r.p_minus_w[1], 1, d.power_returned_l2, d.power_returned_l2_present);
        put_u16(true, r.p_minus_w[2], 1, d.power_returned_l3, d.power_returned_l3_present);
        put_u16(true, r.v_dv[1], 100, d.voltage_l2, d.voltage_l2_present);
        put_u16(true, r.v_dv[2], 100, d.voltage_l3, d.voltage_l3_present);
        put_u16(true, r.i_ca[1], 10, d.current_l2, d.current_l2_present);
        put_u16(true, r.i_ca[2], 10, d.current_l3, d.current_l3_present);
    } else {
        // A single-phase frame has no per-phase power. L1 follows the total
        // so the receiver can publish the L1 sensors. L2 and L3 are cleared
        // because the receiver reuses one Data object.
        put_u16(true, r.power_w, 1, d.power_delivered_l1, d.power_delivered_l1_present);
        put_u16(true, r.inject_w, 1, d.power_returned_l1, d.power_returned_l1_present);
        put_u16(false, 0, 1, d.power_delivered_l2, d.power_delivered_l2_present);
        put_u16(false, 0, 1, d.power_delivered_l3, d.power_delivered_l3_present);
        put_u16(false, 0, 1, d.power_returned_l2, d.power_returned_l2_present);
        put_u16(false, 0, 1, d.power_returned_l3, d.power_returned_l3_present);
        put_u16(false, 0, 100, d.voltage_l2, d.voltage_l2_present);
        put_u16(false, 0, 100, d.voltage_l3, d.voltage_l3_present);
        put_u16(false, 0, 10, d.current_l2, d.current_l2_present);
        put_u16(false, 0, 10, d.current_l3, d.current_l3_present);
    }

    if ((r.flags & kFlagGas) != 0) {
        d.gas_delivered_be_present = true;
        d.gas_delivered_be._value = static_cast<std::int32_t>(r.gas_dm3);
        if (gas_timestamp != nullptr && gas_timestamp[0] != '\0') {
            d.gas_delivered_be.timestamp = gas_timestamp;
        }
    } else {
        d.gas_delivered_be_present = false;
    }
    if ((r.flags & kFlagPeak) != 0) {
        d.active_energy_import_maximum_demand_running_month_present = true;
        d.active_energy_import_maximum_demand_running_month._value = r.peak_w;
        static char peak_ts[14];
        if (format_meter_time(r.peak_time, peak_ts, sizeof(peak_ts))) {
            d.active_energy_import_maximum_demand_running_month.timestamp = peak_ts;
        }
    } else {
        d.active_energy_import_maximum_demand_running_month_present = false;
    }
}

}  // namespace p1lora
