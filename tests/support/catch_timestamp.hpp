// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <catch2/catch_tostring.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "datadog/timestamp.hpp"

namespace Catch {

/**
 * Custom Catch2 string conversion for datadog::Timestamp. Include this header before
 * including catch2 to ensure that Timestamps can be used in REQUIRE expressions.
 *
 * Catch2 supports stringification of std::chrono::time_point, but only for timestamps
 * that are convertible to std::chrono::system_clock::duration. Our Timestamp
 * representation uses nanosecond precision, whereas system_clock has microsecond
 * precision on most platforms, so the conversion is not implicit.
 */
template <>
struct StringMaker<datadog::Timestamp> {
  static std::string convert(datadog::Timestamp const& value) {
    using namespace std::chrono;
    auto s = time_point_cast<system_clock::duration>(value);
    std::time_t t = system_clock::to_time_t(s);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
  }
};

}  // namespace Catch