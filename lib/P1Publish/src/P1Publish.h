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

// MQTT_PFIX + slugged catalogue label. 0 if the buffer is too small.
std::size_t reading_topic(const p1cfg::Store& cfg, const p1cfg::Field& field, char* out,
                          std::size_t cap);

// MQTT_PFIX with the trailing slash removed. 0 if the buffer is too small.
std::size_t availability_topic(const p1cfg::Store& cfg, char* out, std::size_t cap);

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
