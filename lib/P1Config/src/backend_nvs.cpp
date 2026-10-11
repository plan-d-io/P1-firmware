#include "P1Config.h"

#include <cstring>

#if defined(ARDUINO)

namespace p1cfg {

NvsBackend::NvsBackend(const char* ns) : ns_(ns != nullptr ? ns : "p1cfg") {}

bool NvsBackend::begin(bool read_only)
{
    open_ = prefs_.begin(ns_, read_only);
    return open_;
}

void NvsBackend::end()
{
    if (open_) {
        prefs_.end();
        open_ = false;
    }
}

bool NvsBackend::has(const char* key) const
{
    return prefs_.isKey(key);
}

StoredType NvsBackend::stored_type(const char* key) const
{
    if (!prefs_.isKey(key)) {
        return StoredType::Absent;
    }
    switch (prefs_.getType(key)) {
    case PT_U8:
        return StoredType::Bool;
    case PT_I32:
        return StoredType::I32;
    case PT_U32:
        return StoredType::U32;
    case PT_STR:
        return StoredType::Str;
    default:
        return StoredType::Other;
    }
}

std::size_t NvsBackend::str_len(const char* key) const
{
    if (prefs_.getType(key) != PT_STR) {
        return 0;
    }
    char tmp[128];
    const std::size_t n = prefs_.getString(key, tmp, sizeof(tmp));
    if (n == 0) {
        return 0;
    }
    // getString writes a NUL; n is the stored length including NUL on some
    // cores and excluding it on others. Prefer strlen of what we got.
    return std::strlen(tmp);
}

bool NvsBackend::get_bool(const char* key, bool def) const
{
    return prefs_.getBool(key, def);
}

std::int32_t NvsBackend::get_i32(const char* key, std::int32_t def) const
{
    return prefs_.getInt(key, def);
}

std::uint32_t NvsBackend::get_u32(const char* key, std::uint32_t def) const
{
    return prefs_.getUInt(key, def);
}

void NvsBackend::get_str(const char* key, char* out, std::size_t cap,
                         const char* def) const
{
    if (cap == 0) {
        return;
    }
    const std::size_t n = prefs_.getString(key, out, cap);
    if (n == 0 && def != nullptr && !prefs_.isKey(key)) {
        std::size_t i = 0;
        while (i + 1 < cap && def[i] != '\0') {
            out[i] = def[i];
            ++i;
        }
        out[i] = '\0';
    }
}

void NvsBackend::put_bool(const char* key, bool v)
{
    prefs_.putBool(key, v);
}

void NvsBackend::put_i32(const char* key, std::int32_t v)
{
    prefs_.putInt(key, v);
}

void NvsBackend::put_u32(const char* key, std::uint32_t v)
{
    prefs_.putUInt(key, v);
}

void NvsBackend::put_str(const char* key, const char* v)
{
    prefs_.putString(key, v);
}

void NvsBackend::remove(const char* key)
{
    prefs_.remove(key);
}

void NvsBackend::clear()
{
    prefs_.clear();
}

}  // namespace p1cfg

#endif
