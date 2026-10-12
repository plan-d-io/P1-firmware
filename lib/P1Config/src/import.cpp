#include "P1Config.h"

#include <cstdio>
#include <cstring>

namespace p1cfg {

namespace {

void copy_str(char* dst, std::size_t cap, const char* src)
{
    if (cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::size_t n = 0;
    while (n + 1 < cap && src[n] != '\0') {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

}  // namespace

void Store::import_from(StoreBackend& legacy)
{
    auto take_bool = [&](const char* lkey, bool* dest) {
        const StoredType t = legacy.stored_type(lkey);
        if (t == StoredType::Absent) {
            return;
        }
        if (t != StoredType::Bool) {
            skip_legacy(lkey);
            return;
        }
        *dest = legacy.get_bool(lkey, *dest);
        ++legacy_imported_;
    };
    auto take_u32 = [&](const char* lkey, std::uint32_t* dest, bool port) {
        const StoredType t = legacy.stored_type(lkey);
        if (t == StoredType::Absent) {
            return;
        }
        if (t != StoredType::U32) {
            skip_legacy(lkey);
            return;
        }
        const std::uint32_t v = legacy.get_u32(lkey, *dest);
        if (port && (v < 1u || v > 65535u)) {
            skip_legacy(lkey);
            return;
        }
        *dest = v;
        ++legacy_imported_;
    };
    auto take_i32 = [&](const char* lkey, std::int32_t* dest) {
        const StoredType t = legacy.stored_type(lkey);
        if (t == StoredType::Absent) {
            return;
        }
        if (t != StoredType::I32) {
            skip_legacy(lkey);
            return;
        }
        const std::int32_t v = legacy.get_i32(lkey, *dest);
        if (std::strcmp(lkey, "FRMT_PYLD") == 0 && (v < 0 || v > 3)) {
            skip_legacy(lkey);
            return;
        }
        *dest = v;
        ++legacy_imported_;
    };
    auto take_str = [&](const char* lkey, char* dest, std::size_t cap) {
        const StoredType t = legacy.stored_type(lkey);
        if (t == StoredType::Absent) {
            return;
        }
        if (t != StoredType::Str) {
            skip_legacy(lkey);
            return;
        }
        if (legacy.str_len(lkey) + 1 > cap) {
            skip_legacy(lkey);
            return;
        }
        legacy.get_str(lkey, dest, cap, dest);
        ++legacy_imported_;
    };
    auto take_ip = [&](const char* lkey, char* dest, std::size_t cap) {
        const StoredType t = legacy.stored_type(lkey);
        if (t == StoredType::Absent) {
            return;
        }
        if (t != StoredType::U32) {
            skip_legacy(lkey);
            return;
        }
        if (!format_arduino_ipv4(legacy.get_u32(lkey, 0), dest, cap)) {
            skip_legacy(lkey);
            dest[0] = '\0';
            return;
        }
        ++legacy_imported_;
    };

    take_str("WIFI_SSID", v_.wifi_ssid, sizeof(v_.wifi_ssid));
    take_str("WIFI_PASSWD", v_.wifi_pass, sizeof(v_.wifi_pass));
    take_bool("FIP_EN", &v_.fip_en);
    take_ip("FIPADDR", v_.fipaddr, sizeof(v_.fipaddr));
    take_ip("FSUBN", v_.fsubn, sizeof(v_.fsubn));
    take_ip("FDEFGTW", v_.fdefgtw, sizeof(v_.fdefgtw));
    take_ip("FDNS1", v_.fdns1, sizeof(v_.fdns1));
    take_ip("FDNS2", v_.fdns2, sizeof(v_.fdns2));
    take_bool("MQTT_EN", &v_.mqtt_en);
    take_bool("MQTT_TLS", &v_.mqtt_tls);
    take_bool("MQTT_AUTH", &v_.mqtt_auth);
    take_u32("MQTT_PORT", &v_.mqtt_port, true);
    take_str("MQTT_HOST", v_.mqtt_host, sizeof(v_.mqtt_host));
    take_str("MQTT_ID", v_.mqtt_id, sizeof(v_.mqtt_id));
    take_str("MQTT_USER", v_.mqtt_user, sizeof(v_.mqtt_user));
    take_str("MQTT_PFIX", v_.mqtt_pfix, sizeof(v_.mqtt_pfix));
    take_str("MQTT_PASS", v_.mqtt_pass, sizeof(v_.mqtt_pass));
    take_bool("HA_EN", &v_.ha_en);
    take_str("HA_DEVICE", v_.ha_device, sizeof(v_.ha_device));
    take_i32("FRMT_PYLD", &v_.frmt_pyld);
    take_u32("UPL_THROTTLE", &v_.upl_throttle, false);
    take_bool("PUSH_FULL", &v_.push_full);
    take_bool("UPD_AUTO", &v_.upd_auto);
    take_bool("UPD_AUTOCHK", &v_.upd_autochk);
    take_bool("EID_EN", &v_.eid_en);
    take_str("EIDCLAIM", v_.eidclaim, sizeof(v_.eidclaim));
    take_str("UUID", v_.uuid, sizeof(v_.uuid));
    take_str("EMAIL", v_.email, sizeof(v_.email));
    take_u32("reboots", &v_.reboots, false);

    bool beta = false;
    bool alpha = false;
    bool v2 = false;
    auto flag = [&](const char* key, bool* dest) {
        const StoredType t = legacy.stored_type(key);
        if (t == StoredType::Absent) {
            return;
        }
        if (t != StoredType::Bool) {
            skip_legacy(key);
            return;
        }
        *dest = legacy.get_bool(key, false);
    };
    flag("BETA_FLT", &beta);
    flag("ALPHA_FLT", &alpha);
    flag("V2_FLT", &v2);
    copy_str(v_.rel_chan, sizeof(v_.rel_chan), (beta || alpha || v2) ? "develop" : "main");
    ++legacy_imported_;

    if (v_.fip_en && (v_.fipaddr[0] == '\0' || v_.fsubn[0] == '\0' || v_.fdefgtw[0] == '\0')) {
        v_.fip_en = false;
        skip_legacy("FIP_EN");
    } else if (v_.fip_en) {
        if (!ip_ok(v_.fipaddr) || !ip_ok(v_.fsubn) || !ip_ok(v_.fdefgtw) || !ip_ok(v_.fdns1) ||
            (v_.fdns2[0] != '\0' && !ip_ok(v_.fdns2)) ||
            !static_ip_consistent(v_.fipaddr, v_.fsubn, v_.fdefgtw)) {
            v_.fip_en = false;
            skip_legacy("FIP_EN");
        }
    }
}

}  // namespace p1cfg
