#include "P1Syslog.h"

#if defined(ARDUINO)

#include <LittleFS.h>
#include <cstring>
#include <ctime>

namespace p1log {

bool LittleFsStore::begin(bool* formatted, bool format_on_failure)
{
    if (formatted != nullptr) {
        *formatted = false;
    }
    if (LittleFS.begin(false)) {
        mounted_ = true;
        return true;
    }
    if (format_on_failure && LittleFS.begin(true)) {
        mounted_ = true;
        if (formatted != nullptr) {
            *formatted = true;
        }
        return true;
    }
    mounted_ = false;
    return false;
}

bool LittleFsStore::self_test()
{
    if (!mounted_) {
        return false;
    }
    static constexpr char kPath[] = "/.p1_fs_test";
    static constexpr char kPayload[] = "ok";
    File w = LittleFS.open(kPath, "w");
    if (!w) {
        return false;
    }
    const size_t wrote = w.print(kPayload);
    w.close();
    if (wrote != sizeof(kPayload) - 1) {
        LittleFS.remove(kPath);
        return false;
    }
    File r = LittleFS.open(kPath, "r");
    if (!r) {
        LittleFS.remove(kPath);
        return false;
    }
    char buf[8]{};
    const size_t got = r.readBytes(buf, sizeof(buf) - 1);
    r.close();
    LittleFS.remove(kPath);
    return got == sizeof(kPayload) - 1 && std::strcmp(buf, kPayload) == 0;
}

bool LittleFsStore::append(const char* path, std::string_view data)
{
    if (!mounted_ || path == nullptr) {
        return false;
    }
    File f = LittleFS.open(path, "a");
    if (!f) {
        return false;
    }
    const size_t n = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    f.close();
    return n == data.size();
}

std::size_t LittleFsStore::size(const char* path) const
{
    if (!mounted_ || path == nullptr || !LittleFS.exists(path)) {
        return 0;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return 0;
    }
    const size_t n = f.size();
    f.close();
    return n;
}

bool LittleFsStore::remove(const char* path)
{
    if (!mounted_ || path == nullptr) {
        return false;
    }
    if (!LittleFS.exists(path)) {
        return true;
    }
    return LittleFS.remove(path);
}

bool LittleFsStore::rename(const char* from, const char* to)
{
    if (!mounted_ || from == nullptr || to == nullptr) {
        return false;
    }
    if (!LittleFS.exists(from)) {
        return false;
    }
    if (LittleFS.exists(to)) {
        LittleFS.remove(to);
    }
    return LittleFS.rename(from, to);
}

std::size_t LittleFsStore::read(const char* path, std::size_t offset, char* out,
                               std::size_t cap) const
{
    if (!mounted_ || path == nullptr || out == nullptr || cap == 0 || !LittleFS.exists(path)) {
        return 0;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return 0;
    }
    if (!f.seek(offset)) {
        f.close();
        return 0;
    }
    const size_t n = f.readBytes(out, cap);
    f.close();
    return n;
}

}  // namespace p1log

#endif
