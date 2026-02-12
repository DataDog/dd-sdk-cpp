// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

namespace datadog::impl {

bool SetDefaultApplicationStoragePath(std::string_view path);

std::string_view GetDefaultApplicationStoragePath();

}  // namespace datadog::impl
