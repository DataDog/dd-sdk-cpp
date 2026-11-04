// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/platform/uuid.hpp"

#ifdef _WIN32
// clang-format off
#include "datadog/impl/types/windows_headers.hpp"
#include <objbase.h> // Must follow windows.h
// clang-format on
#pragma comment(lib, "ole32.lib")
#else
#include <uuid/uuid.h>
#endif

#include "datadog/impl/types/assert.hpp"
#include "datadog/impl/types/endian.hpp"

namespace datadog::platform {

void UuidGenerate(uint8_t out[16]) {
#ifdef _WIN32
  GUID guid;
  HRESULT hr = ::CoCreateGuid(&guid);
  DATADOG_ASSERT(SUCCEEDED(hr), "CoCreateGuid failed");
  impl::WriteBigEndian32(out, guid.Data1);
  impl::WriteBigEndian16(out + 4, guid.Data2);
  impl::WriteBigEndian16(out + 6, guid.Data3);
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
