// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <string>
#include <unordered_map>

namespace datadog::core {

// This is a major open design question: How do we store the "JSON-Like" objects
// that we use as attributes across the Datadog SDK? And how do we make this
// type easilly available / accessible in C? This is a much longer discussion so
// for now I'm going to put in a placeholder std::map type so I can at least
// show where the type will be used.

using DatadogAttributes = std::unordered_map<std::string, std::string>;

}  // namespace datadog::core
