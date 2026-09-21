#pragma once
#include <cstddef>
#include <cstdint>

namespace p1web {
namespace assets {

extern const std::uint8_t kStyleCssGz[];
extern const std::size_t kStyleCssGzLen;
extern const std::size_t kStyleCssRawLen;

extern const std::uint8_t kRestartingHtmlGz[];
extern const std::size_t kRestartingHtmlGzLen;
extern const std::size_t kRestartingHtmlRawLen;

extern const std::uint8_t kConfigHtmlGz[];
extern const std::size_t kConfigHtmlGzLen;
extern const std::size_t kConfigHtmlRawLen;

extern const char kSetupHtmlTpl[];
extern const char kStatusHtmlTpl[];

}  // namespace assets
}  // namespace p1web
