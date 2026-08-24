// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>

#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::impl {

size_t GetJsonSize(const DiagnosticAttributeValue& value);

size_t WriteJson(char* dst, size_t n, const DiagnosticAttributeValue& value);

size_t GetJsonSize(const DiagnosticAttributeList& value);

size_t WriteJson(char* dst, size_t n, const DiagnosticAttributeList& value);

}  // namespace datadog::impl
