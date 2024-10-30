// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <cstdint>

namespace datadog::core {

// A function for providing the current date time in nanoseconds since unix
// epoch. The default implementation uses std::chrono::system_clock
using DateTimeProvider = uint64_t (*)();
uint64_t DefaultDateTimeProvider();

}  // namespace datadog::core
