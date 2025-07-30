#pragma once

#include <cinttypes>

namespace datadog::impl {

using FeatureId = uint32_t;

constexpr FeatureId CreateFeatureId(const char fourcc[5]) {
    const uint32_t a = static_cast<uint32_t>(fourcc[0]) << 0;
    const uint32_t b = static_cast<uint32_t>(fourcc[1]) << 8;
    const uint32_t c = static_cast<uint32_t>(fourcc[2]) << 16;
    const uint32_t d = static_cast<uint32_t>(fourcc[3]) << 24;
    return a | b | c | d;
}

}
