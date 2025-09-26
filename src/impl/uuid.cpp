#include "uuid.hpp"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#pragma comment(lib, "ole32.lib")
#else
#include <uuid/uuid.h>
#endif

#include "assert.hpp"

namespace datadog::impl {

const uuid uuid::zero = {};

uuid::uuid() : bytes{} {}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
uuid::uuid(const uint8_t value[16]) { std::memcpy(bytes.data(), value, 16); }

void uuid::set(const uint8_t value[16]) { std::memcpy(bytes.data(), value, 16); }

uuid uuid::make_random() {
  uuid value{};
#ifdef _WIN32
  GUID guid;
  HRESULT hr = ::CoCreateGuid(&guid);
  DATADOG_ASSERT(SUCCEEDED(hr), "CoCreateGuid failed");
  value.bytes[0] = static_cast<unsigned char>((guid.Data1 >> 24) & 0xFF);
  value.bytes[1] = static_cast<unsigned char>((guid.Data1 >> 16) & 0xFF);
  value.bytes[2] = static_cast<unsigned char>((guid.Data1 >> 8) & 0xFF);
  value.bytes[3] = static_cast<unsigned char>((guid.Data1) & 0xFF);
  value.bytes[4] = (unsigned char)((guid.Data2 >> 8) & 0xFF);
  value.bytes[5] = (unsigned char)((guid.Data2) & 0xFF);
  value.bytes[6] = (unsigned char)((guid.Data3 >> 8) & 0xFF);
  value.bytes[7] = (unsigned char)((guid.Data3) & 0xFF);
  value.bytes[8] = guid.Data4[0];
  value.bytes[9] = guid.Data4[1];
  value.bytes[10] = guid.Data4[2];
  value.bytes[11] = guid.Data4[3];
  value.bytes[12] = guid.Data4[4];
  value.bytes[13] = guid.Data4[5];
  value.bytes[14] = guid.Data4[6];
  value.bytes[15] = guid.Data4[7];
#else
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  uuid_generate(value.bytes.data());
#endif
  return value;
}

bool uuid::operator==(const uuid& other) const { return bytes == other.bytes; }

bool uuid::operator!=(const uuid& other) const { return bytes != other.bytes; }

}  // namespace datadog::impl
