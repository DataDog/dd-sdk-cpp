// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// Only define this implementation on Apple platforms
#if defined(__APPLE__)

#include <uuid/uuid.h>

#include "datadog/impl/types/platform/uuid.hpp"

namespace datadog::platform {

void UuidGenerate(uint8_t out[16]) {
  // On macOS, uuid_generate_random() is part of libc
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  uuid_generate_random(out);
}

}  // namespace datadog::platform

#endif  // defined(__APPLE__)
