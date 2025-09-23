#include "uuid.hpp"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#pragma comment(lib, "ole32.lib")
#else
#include <uuid/uuid.h>
#endif

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
  bytes[0] = static_cast<unsigned char>((guid.Data1 >> 24) & 0xFF);
  bytes[1] = static_cast<unsigned char>((guid.Data1 >> 16) & 0xFF);
  bytes[2] = static_cast<unsigned char>((guid.Data1 >> 8) & 0xFF);
  bytes[3] = static_cast<unsigned char>((guid.Data1) & 0xFF);
  bytes[4] = (unsigned char)((guid.Data2 >> 8) & 0xFF);
  bytes[5] = (unsigned char)((guid.Data2) & 0xFF);
  bytes[6] = (unsigned char)((guid.Data3 >> 8) & 0xFF);
  bytes[7] = (unsigned char)((guid.Data3) & 0xFF);
  bytes[8] = guid.Data4[0];
  bytes[9] = guid.Data4[1];
  bytes[10] = guid.Data4[2];
  bytes[11] = guid.Data4[3];
  bytes[12] = guid.Data4[4];
  bytes[13] = guid.Data4[5];
  bytes[14] = guid.Data4[6];
  bytes[15] = guid.Data4[7];
#else
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  uuid_generate(value.bytes.data());
#endif
  return value;
}

bool uuid::operator==(const uuid& other) const { return bytes == other.bytes; }

bool uuid::operator!=(const uuid& other) const { return bytes != other.bytes; }

}  // namespace datadog::impl
