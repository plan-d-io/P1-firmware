#include "P1DSMR.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace p1dsmr {
namespace {

struct LibLog {
    char msg[160]{};
    bool overflow = false;
    bool crc_mismatch = false;
    bool bad_crc_char = false;
    bool short_crc = false;
    unsigned recv = 0;
    unsigned calc = 0;
};

LibLog g_lib{};
bool g_log_installed = false;

void lib_log_reset()
{
    g_lib.msg[0] = '\0';
    g_lib.overflow = false;
    g_lib.crc_mismatch = false;
    g_lib.bad_crc_char = false;
    g_lib.short_crc = false;
    g_lib.recv = 0;
    g_lib.calc = 0;
}

void on_lib_log(dsmr_parser::LogLevel, const char* fmt, va_list args)
{
    std::vsnprintf(g_lib.msg, sizeof(g_lib.msg), fmt, args);
    if (std::strstr(g_lib.msg, "Buffer overflow") != nullptr) {
        g_lib.overflow = true;
        return;
    }
    unsigned recv = 0;
    unsigned calc = 0;
    if (std::sscanf(g_lib.msg, "CRC mismatch: received 0x%X, calculated 0x%X", &recv, &calc) ==
        2) {
        g_lib.crc_mismatch = true;
        g_lib.recv = recv;
        g_lib.calc = calc;
        return;
    }
    if (std::strstr(g_lib.msg, "Incorrect CRC character") != nullptr) {
        g_lib.bad_crc_char = true;
        return;
    }
    if (std::strstr(g_lib.msg, "does not have enough bytes for CRC") != nullptr) {
        g_lib.short_crc = true;
    }
}

template <typename... Ts>
bool field_present(dsmr_parser::ParsedData<Ts...>& data, const dsmr_parser::ObisId& id)
{
    bool present = false;
    auto check = [&](auto& field) -> bool {
        using F = std::remove_reference_t<decltype(field)>;
        if (!(F::id == id)) {
            return false;
        }
        present = field.present();
        return true;
    };
    (void)(check(static_cast<Ts&>(data)) || ...);
    return present;
}

template <typename... Ts>
void clear_present(dsmr_parser::ParsedData<Ts...>& data, const dsmr_parser::ObisId& id)
{
    auto clr = [&](auto& field) -> bool {
        using F = std::remove_reference_t<decltype(field)>;
        if (!(F::id == id)) {
            return false;
        }
        field.present() = false;
        return true;
    };
    (void)(clr(static_cast<Ts&>(data)) || ...);
}

}  // namespace

Reader::Reader() : acc_(std::span<std::uint8_t>(buf_, kBufferSize), true)
{
    new (data_store_) Data();
    if (!g_log_installed) {
        dsmr_parser::Logger::set_log_function(on_lib_log);
        g_log_installed = true;
    }
}

Reader::~Reader()
{
    data_mut().~Data();
}

void Reader::reset_data()
{
    data_mut().~Data();
    new (data_store_) Data();
}

Data& Reader::data_mut()
{
    return *std::launder(reinterpret_cast<Data*>(data_store_));
}

const Data& Reader::data() const
{
    return *std::launder(reinterpret_cast<const Data*>(data_store_));
}

void Reader::note_skip(std::string_view line)
{
    ++skipped_count_;
    if (skipped_line_[0] != '\0') {
        return;
    }
    const std::size_t n = (line.size() < kSkipLineMax) ? line.size() : kSkipLineMax;
    std::memcpy(skipped_line_, line.data(), n);
    skipped_line_[n] = '\0';
}

void Reader::note_reject(const char* reason)
{
    if (reason == nullptr) {
        reject_reason_[0] = '\0';
        return;
    }
    std::snprintf(reject_reason_, sizeof(reject_reason_), "%s", reason);
}

void Reader::save_rejected()
{
    last_rejected_.assign(reinterpret_cast<const char*>(capture_), capture_len_);
    has_ = false;
    raw_.clear();
}

bool Reader::consume_line(std::string_view input)
{
    if (input.empty()) {
        return true;
    }

    dsmr_parser::ObisId id;
    auto idres = dsmr_parser::parse_obis(id, input);
    if (!idres) {
        note_skip(input);
        return true;
    }

    const bool was_present = field_present(data_mut(), id);
    auto datares = data_mut().parse_line(id, *idres);
    if (!datares) {
        if (!was_present) {
            clear_present(data_mut(), id);
        }
        note_skip(input);
        return true;
    }

    if ((*datares).data() != (*idres).data() && !(*datares).empty()) {
        if (!was_present) {
            clear_present(data_mut(), id);
        }
        note_skip(input);
    }
    return true;
}

void Reader::parse_valid_crc(const dsmr_parser::DsmrUnencryptedTelegram& telegram)
{
    reset_data();
    skipped_count_ = 0;
    skipped_line_[0] = '\0';

    const auto full = telegram.content_without_crc();
    if (full.size() < 2) {
        note_skip(full);
        return;
    }
    auto input = full.substr(1, full.size() - 2);

    std::size_t pos = 0;
    std::size_t line_start = 0;

    while (pos < input.size()) {
        if (input[pos] == '\r' || input[pos] == '\n') {
            auto res = data_mut().parse_line(dsmr_parser::ObisId(255, 255, 255, 255, 255, 255),
                                             input.substr(line_start, pos - line_start));
            if (!res) {
                clear_present(data_mut(), dsmr_parser::ObisId(255, 255, 255, 255, 255, 255));
                note_skip(input.substr(line_start, pos - line_start));
            }
            line_start = ++pos;
            break;
        }
        ++pos;
    }

    bool open_bracket = false;
    bool skip_current = false;
    while (pos < input.size()) {
        char c = input[pos];
        char nc = (pos + 1 < input.size()) ? input[pos + 1] : '\0';

        if ((c == '(' && nc == '(') || (c == ')' && nc == ')')) {
            ++pos;
            c = nc;
        }

        if (c == '(') {
            if (open_bracket) {
                skip_current = true;
            }
            open_bracket = true;
        } else if (c == ')') {
            if (!open_bracket) {
                skip_current = true;
            } else {
                open_bracket = false;
            }
        } else if (c == '\r' || c == '\n') {
            bool continuation =
                open_bracket || ((input.size() - pos > 2) && (input[pos + 1] == '(' || input[pos + 2] == '('));
            if (!continuation) {
                const auto line = input.substr(line_start, pos - line_start);
                if (skip_current) {
                    note_skip(line);
                } else {
                    consume_line(line);
                }
                skip_current = false;
                open_bracket = false;
                line_start = pos + 1;
            }
        }

        ++pos;
    }

    if (pos != line_start) {
        note_skip(input.substr(line_start, pos - line_start));
    }
}

Reader::Status Reader::feed(std::uint8_t byte)
{
    lib_log_reset();

    if (byte == '/') {
        auto parsed = acc_.process_byte(byte);
        (void)parsed;
        if (g_lib.overflow) {
            note_reject("buffer overflow");
            save_rejected();
            capture_len_ = 0;
            capture_[capture_len_++] = byte;
            crc_nibbles_left_ = 0;
            return Status::Rejected;
        }
        capture_len_ = 0;
        capture_[capture_len_++] = byte;
        crc_nibbles_left_ = 0;
        return Status::NeedMore;
    }

    if (capture_len_ < kBufferSize) {
        capture_[capture_len_++] = byte;
    }

    auto parsed = acc_.process_byte(byte);

    if (g_lib.overflow) {
        note_reject("buffer overflow");
        save_rejected();
        crc_nibbles_left_ = 0;
        return Status::Rejected;
    }

    if (parsed) {
        crc_nibbles_left_ = 0;
        parse_valid_crc(*parsed);
        raw_.assign(parsed->full_content());
        has_ = true;
        if (skipped_count_ > 0) {
            last_with_skip_ = raw_;
        }
        return Status::Ok;
    }

    if (byte == '!') {
        crc_nibbles_left_ = 4;
        return Status::NeedMore;
    }

    if (crc_nibbles_left_ > 0) {
        --crc_nibbles_left_;
        if (crc_nibbles_left_ == 0) {
            if (g_lib.crc_mismatch) {
                char reason[kReasonMax];
                std::snprintf(reason, sizeof(reason),
                              "CRC mismatch: received 0x%04X, calculated 0x%04X", g_lib.recv,
                              g_lib.calc);
                note_reject(reason);
            } else {
                note_reject("malformed CRC");
            }
            save_rejected();
            return Status::Rejected;
        }
    }

    return Status::NeedMore;
}

FeedStats Reader::feed(std::span<const std::uint8_t> bytes)
{
    return feed(bytes, [](const Data&) {});
}

}  // namespace p1dsmr
