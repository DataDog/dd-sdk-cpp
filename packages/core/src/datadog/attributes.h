// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <string>
#include <unordered_map>

namespace datadog::core {

// This is a placeholder for what will eventually be a holder for Datadog
// Attributes, which will need to be a "JSON-Like" object that we use as
// attributes across the Datadog SDK

using DatadogAttributes = std::unordered_map<std::string, std::string>;

}  // namespace datadog::core
