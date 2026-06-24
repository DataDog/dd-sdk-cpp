// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/platform/uuid.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#pragma comment(lib, "ole32.lib")
#else
#include <uuid/uuid.h>
#endif

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::platform {

void UuidGenerate(uint8_t out[16]) {
#ifdef _WIN32
  GUID guid;
  HRESULT hr = ::CoCreateGuid(&guid);
  DATADOG_ASSERT(SUCCEEDED(hr), "CoCreateGuid failed");
  out[0] = static_cast<uint8_t>((guid.Data1 >> 24) & 0xFF);
  out[1] = static_cast<uint8_t>((guid.Data1 >> 16) & 0xFF);
  out[2] = static_cast<uint8_t>((guid.Data1 >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>((guid.Data1) & 0xFF);
  out[4] = static_cast<uint8_t>((guid.Data2 >> 8) & 0xFF);
  out[5] = static_cast<uint8_t>((guid.Data2) & 0xFF);
  out[6] = static_cast<uint8_t>((guid.Data3 >> 8) & 0xFF);
  out[7] = static_cast<uint8_t>((guid.Data3) & 0xFF);
  out[8] = guid.Data4[0];
  out[9] = guid.Data4[1];
  out[10] = guid.Data4[2];
  out[11] = guid.Data4[3];
  out[12] = guid.Data4[4];
  out[13] = guid.Data4[5];
  out[14] = guid.Data4[6];
  out[15] = guid.Data4[7];
#else
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  uuid_generate_random(out);
#endif
}

}  // namespace datadog::platform
