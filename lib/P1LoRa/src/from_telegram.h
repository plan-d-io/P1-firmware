#pragma once

// Telegram → Reading. This header is the only part of P1LoRa that includes
// the DSMR parser. The board test and the rest of the library do not.

#include "P1LoRa.h"

#include <P1DSMR.h>

#include <string_view>

namespace p1lora {

inline int days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

inline bool meter_time(std::string_view ts, std::uint32_t& out)
{
    if (ts.size() != 13) {
        return false;
    }
    int part[6] = {};
    for (int i = 0; i < 12; ++i) {
        const char c = ts[static_cast<std::size_t>(i)];
        if (c < '0' || c > '9') {
            return false;
        }
        part[i / 2] = part[i / 2] * 10 + (c - '0');
    }
    const int year = 2000 + part[0];
    const unsigned month = static_cast<unsigned>(part[1]);
    const unsigned day = static_cast<unsigned>(part[2]);
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    const int days = days_from_civil(year, month, day) - days_from_civil(2000, 1, 1);
    if (days < 0) {
        return false;
    }
    const std::uint32_t sec = static_cast<std::uint32_t>(days) * 86400u
        + static_cast<std::uint32_t>(part[3]) * 3600u
        + static_cast<std::uint32_t>(part[4]) * 60u
        + static_cast<std::uint32_t>(part[5]);
    const char dst = ts[12];
    out = (dst == 'S' || dst == 's') ? (sec | 0x80000000u) : sec;
    return true;
}

inline std::uint16_t sat16_from(std::int32_t v)
{
    if (v <= 0) {
        return 0;
    }
    if (v >= 65535) {
        return 65534;
    }
    return static_cast<std::uint16_t>(v);
}

inline std::uint32_t sat32_from(std::int32_t v)
{
    if (v <= 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(v);
}

inline std::uint16_t field16(bool present, std::int32_t raw, int divisor)
{
    if (!present) {
        return kAbsent16;
    }
    return sat16_from(raw / divisor);
}

inline std::uint32_t field32(bool present, std::int32_t raw)
{
    if (!present) {
        return kAbsent32;
    }
    return sat32_from(raw);
}

inline int tariff_number(std::string_view text)
{
    int n = 0;
    bool any = false;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            any = true;
            n = n * 10 + (c - '0');
        }
    }
    return any ? n : 0;
}

inline Reading from_telegram(const p1dsmr::Data& d)
{
    Reading r;
    const bool l2l3 = d.voltage_l2_present || d.voltage_l3_present
        || d.current_l2_present || d.current_l3_present
        || d.power_delivered_l2_present || d.power_delivered_l3_present
        || d.power_returned_l2_present || d.power_returned_l3_present;
    r.three_phase = l2l3;

    if (d.electricity_tariff_present && tariff_number(d.electricity_tariff) == 2) {
        r.flags = static_cast<std::uint8_t>(r.flags | kFlagTariff2);
    }

    r.t1_wh = field32(d.energy_delivered_tariff1_present, d.energy_delivered_tariff1.int_val());
    r.t2_wh = field32(d.energy_delivered_tariff2_present, d.energy_delivered_tariff2.int_val());
    r.inj1_wh = field32(d.energy_returned_tariff1_present, d.energy_returned_tariff1.int_val());
    r.inj2_wh = field32(d.energy_returned_tariff2_present, d.energy_returned_tariff2.int_val());
    r.power_w = field16(d.power_delivered_present, d.power_delivered.int_val(), 1);
    r.inject_w = field16(d.power_returned_present, d.power_returned.int_val(), 1);
    r.demand_w = field16(d.active_energy_import_current_average_demand_present,
                         d.active_energy_import_current_average_demand.int_val(), 1);

    if (r.three_phase) {
        r.p_plus_w[0] = field16(d.power_delivered_l1_present, d.power_delivered_l1.int_val(), 1);
        r.p_plus_w[1] = field16(d.power_delivered_l2_present, d.power_delivered_l2.int_val(), 1);
        r.p_plus_w[2] = field16(d.power_delivered_l3_present, d.power_delivered_l3.int_val(), 1);
        r.p_minus_w[0] = field16(d.power_returned_l1_present, d.power_returned_l1.int_val(), 1);
        r.p_minus_w[1] = field16(d.power_returned_l2_present, d.power_returned_l2.int_val(), 1);
        r.p_minus_w[2] = field16(d.power_returned_l3_present, d.power_returned_l3.int_val(), 1);
        r.v_dv[0] = field16(d.voltage_l1_present, d.voltage_l1.int_val(), 100);
        r.v_dv[1] = field16(d.voltage_l2_present, d.voltage_l2.int_val(), 100);
        r.v_dv[2] = field16(d.voltage_l3_present, d.voltage_l3.int_val(), 100);
        r.i_ca[0] = field16(d.current_l1_present, d.current_l1.int_val(), 10);
        r.i_ca[1] = field16(d.current_l2_present, d.current_l2.int_val(), 10);
        r.i_ca[2] = field16(d.current_l3_present, d.current_l3.int_val(), 10);
    } else {
        r.v_dv[0] = field16(d.voltage_l1_present, d.voltage_l1.int_val(), 100);
        r.i_ca[0] = field16(d.current_l1_present, d.current_l1.int_val(), 10);
    }

    const bool gas_be = d.gas_delivered_be_present;
    const bool gas_nl = d.gas_delivered_present;
    if (gas_be || gas_nl) {
        r.flags = static_cast<std::uint8_t>(r.flags | kFlagGas);
        const std::int32_t raw = gas_be ? d.gas_delivered_be.int_val() : d.gas_delivered.int_val();
        r.gas_dm3 = sat32_from(raw);
    }

    if (d.active_energy_import_maximum_demand_running_month_present) {
        std::uint32_t when = 0;
        if (meter_time(d.active_energy_import_maximum_demand_running_month.timestamp, when)) {
            r.flags = static_cast<std::uint8_t>(r.flags | kFlagPeak);
            r.peak_time = when;
            r.peak_w = sat16_from(d.active_energy_import_maximum_demand_running_month.int_val());
        }
    }
    return r;
}

}  // namespace p1lora
