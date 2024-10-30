// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <string_view>

namespace datadog::core::internal {

using namespace std::string_view_literals;

static const std::string_view kSdkVersion = "0.1.0"sv;

}  // namespace datadog::core::internal
