#include "P1Config.h"

#if !defined(ARDUINO)

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

bool key_eq(const char* a, const char* b)
{
    return std::strcmp(a, b) == 0;
}

}  // namespace

MemoryBackend::Slot* MemoryBackend::find(const char* key)
{
    for (std::size_t i = 0; i < n_; ++i) {
        if (key_eq(slots_[i].key, key)) {
            return &slots_[i].slot;
        }
    }
    return nullptr;
}

const MemoryBackend::Slot* MemoryBackend::find(const char* key) const
{
    for (std::size_t i = 0; i < n_; ++i) {
        if (key_eq(slots_[i].key, key)) {
            return &slots_[i].slot;
        }
    }
    return nullptr;
}

MemoryBackend::Slot* MemoryBackend::insert(const char* key)
{
    if (Slot* s = find(key)) {
        return s;
    }
    if (n_ >= kCap) {
        return nullptr;
    }
    copy_str(slots_[n_].key, sizeof(slots_[n_].key), key);
    slots_[n_].slot = Slot{};
    return &slots_[n_++].slot;
}

bool MemoryBackend::begin(bool read_only)
{
    open_ = true;
    readonly_ = read_only;
    return true;
}

void MemoryBackend::end()
{
    open_ = false;
    readonly_ = false;
}

bool MemoryBackend::has(const char* key) const
{
    const Slot* s = find(key);
    return s != nullptr && s->tag != Slot::Tag::Empty;
}

StoredType MemoryBackend::stored_type(const char* key) const
{
    const Slot* s = find(key);
    if (s == nullptr || s->tag == Slot::Tag::Empty) {
        return StoredType::Absent;
    }
    switch (s->tag) {
    case Slot::Tag::Bool:
        return StoredType::Bool;
    case Slot::Tag::I32:
        return StoredType::I32;
    case Slot::Tag::U32:
        return StoredType::U32;
    case Slot::Tag::Str:
        return StoredType::Str;
    default:
        return StoredType::Other;
    }
}

std::size_t MemoryBackend::str_len(const char* key) const
{
    const Slot* s = find(key);
    if (s == nullptr || s->tag != Slot::Tag::Str) {
        return 0;
    }
    return std::strlen(s->s);
}

bool MemoryBackend::get_bool(const char* key, bool def) const
{
    const Slot* s = find(key);
    if (s == nullptr || s->tag != Slot::Tag::Bool) {
        return def;
    }
    return s->b;
}

std::int32_t MemoryBackend::get_i32(const char* key, std::int32_t def) const
{
    const Slot* s = find(key);
    if (s == nullptr || s->tag != Slot::Tag::I32) {
        return def;
    }
    return s->i;
}

std::uint32_t MemoryBackend::get_u32(const char* key, std::uint32_t def) const
{
    const Slot* s = find(key);
    if (s == nullptr || s->tag != Slot::Tag::U32) {
        return def;
    }
    return s->u;
}

void MemoryBackend::get_str(const char* key, char* out, std::size_t cap,
                            const char* def) const
{
    const Slot* s = find(key);
    if (s == nullptr || s->tag != Slot::Tag::Str) {
        copy_str(out, cap, def);
        return;
    }
    copy_str(out, cap, s->s);
}

void MemoryBackend::put_bool(const char* key, bool v)
{
    if (open_ && readonly_) {
        return;
    }
    if (Slot* s = insert(key)) {
        s->tag = Slot::Tag::Bool;
        s->b = v;
        ++writes_;
    }
}

void MemoryBackend::put_i32(const char* key, std::int32_t v)
{
    if (open_ && readonly_) {
        return;
    }
    if (Slot* s = insert(key)) {
        s->tag = Slot::Tag::I32;
        s->i = v;
        ++writes_;
    }
}

void MemoryBackend::put_u32(const char* key, std::uint32_t v)
{
    if (open_ && readonly_) {
        return;
    }
    if (Slot* s = insert(key)) {
        s->tag = Slot::Tag::U32;
        s->u = v;
        ++writes_;
    }
}

void MemoryBackend::put_str(const char* key, const char* v)
{
    if (open_ && readonly_) {
        return;
    }
    if (v == nullptr) {
        v = "";
    }
    const std::size_t n = std::strlen(v);
    if (n + 1 > sizeof(Slot::s)) {
        return;
    }
    if (Slot* s = insert(key)) {
        s->tag = Slot::Tag::Str;
        copy_str(s->s, sizeof(s->s), v);
        ++writes_;
    }
}

void MemoryBackend::remove(const char* key)
{
    if (open_ && readonly_) {
        return;
    }
    for (std::size_t i = 0; i < n_; ++i) {
        if (key_eq(slots_[i].key, key)) {
            slots_[i] = slots_[n_ - 1];
            slots_[n_ - 1] = Pair{};
            --n_;
            ++writes_;
            return;
        }
    }
}

void MemoryBackend::clear()
{
    if (open_ && readonly_) {
        return;
    }
    for (std::size_t i = 0; i < n_; ++i) {
        slots_[i] = Pair{};
    }
    n_ = 0;
    ++writes_;
}

}  // namespace p1cfg

#endif  // !defined(ARDUINO)
