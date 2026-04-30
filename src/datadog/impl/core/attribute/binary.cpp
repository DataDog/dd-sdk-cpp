// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/attribute/binary.hpp"

#include <cinttypes>
#include <cstring>

#include "datadog/impl/core/attribute/cow.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"

namespace datadog::impl {

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
AttributeBinarySerialization::Result AttributeBinarySerialization::Write(
    const Attribute& attr, File& file
) {
  // Prepare a small buffer to contain our single-byte ValueType identifier, plus an
  // additional value of up to 16 bytes (for primitive/UUID values or compound-type size
  // prefix)
  char buf[17];

  // Store the attribute type in the first byte of the buffer: all binary-encoded
  // attributes begin with a single-byte type identifier
  buf[0] = static_cast<char>(attr.type);

  // Helper function to flush N bytes from buf to the file, returning a Result struct
  // that propagates any filesystem error and sets an `ok` flag iff FilesystemResult is
  // OK _and_ all data was written
  auto flush_buf = [&file, &buf](size_t n) -> Result {
    auto write_res = file.Write(&buf[0], n);
    if (write_res.value == FilesystemResult::OK && write_res.bytes_written == n) {
      return Result{FilesystemResult::OK, true};
    }
    return Result{write_res.value, false};
  };

  // Branch based on type, populating buf and flushing it to the file for primitive
  // types and UUID, or writing additional payload data for compound types
  switch (attr.type) {
    case ValueType::Null:
      // Null: <1-byte type prefix>; no value
      return flush_buf(1);
    case ValueType::Bool:
    case ValueType::Int:
    case ValueType::Timestamp:
      // Primitive types encoded as int64_t: <1-byte type prefix> + <8-byte value>
      std::memcpy(&buf[1], &attr.value.i64, 8);
      return flush_buf(9);
    case ValueType::UInt:
      // Primitive types encoded as uint64_t: <1-byte type prefix> + <8-byte value>
      std::memcpy(&buf[1], &attr.value.u64, 8);
      return flush_buf(9);
    case ValueType::Double:
      // Primitive types encoded as double: <1-byte type prefix> + <8-byte value>
      std::memcpy(&buf[1], &attr.value.f64, 8);
      return flush_buf(9);
    case ValueType::UUID: {
      // UUID: <1-byte type prefix> + <16-byte raw UUID value>
      const CowValue& cow = *attr.value.ptr;
      std::memcpy(&buf[1], cow.value.uid.bytes.data(), 16);
      return flush_buf(17);
    }
    case ValueType::String: {
      // String: <1-byte type prefix> + <8-byte unsigned length> ...
      const CowValue& cow = *attr.value.ptr;
      const uint64_t str_len = cow.value.string.size();
      std::memcpy(&buf[1], &str_len, 8);
      if (auto res = flush_buf(9); !res.ok) {
        return res;
      }

      // ... + <str_len bytes of raw string data, w/o terminator>
      if (str_len > 0) {
        auto write_res = file.Write(cow.value.string.data(), str_len);
        if (write_res.value != FilesystemResult::OK ||
            write_res.bytes_written != str_len) {
          return Result{write_res.value, false};
        }
      }
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::Array: {
      // Array: <1-byte type prefix> + <8-byte unsigned item count> ...
      const CowValue& cow = *attr.value.ptr;
      const uint64_t num_items = cow.value.array.size();
      std::memcpy(&buf[1], &num_items, 8);
      if (auto res = flush_buf(9); !res.ok) {
        return res;
      }

      // ...then, for each item in the array:
      for (const Attribute& array_item : cow.value.array) {
        // <binary-encoded Attribute value>
        if (auto res = Write(array_item, file); !res.ok) {
          return res;
        }
      }
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::Object: {
      // Object: <1-byte type prefix> + <8-byte unsigned property count> ...
      const CowValue& cow = *attr.value.ptr;
      const uint64_t num_properties = cow.value.object.size();
      std::memcpy(&buf[1], &num_properties, 8);
      if (auto res = flush_buf(9); !res.ok) {
        return res;
      }

      // ...then, for each property in the object:
      for (const auto& [property_name, property_value] : cow.value.object) {
        // <length-prefixed string indicating property name>
        const uint64_t property_name_len = property_name.size();
        std::memcpy(&buf[0], &property_name_len, 8);
        if (auto res = flush_buf(8); !res.ok) {
          return res;
        }
        if (property_name_len > 0) {
          auto write_res = file.Write(property_name.data(), property_name_len);
          if (write_res.value != FilesystemResult::OK ||
              write_res.bytes_written != property_name_len) {
            return Result{write_res.value, false};
          }
        }

        // + <binary-encoded Attribute value>
        if (auto res = Write(property_value, file); !res.ok) {
          return res;
        }
      }
      return Result{FilesystemResult::OK, true};
    }
  }
}

AttributeBinarySerialization::Result AttributeBinarySerialization::Parse(
    File& file, Attribute& out_attr
) {
  return ParseImpl(file, out_attr, 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
AttributeBinarySerialization::Result AttributeBinarySerialization::ParseImpl(
    File& file, Attribute& out_attr, size_t depth
) {
  // Halt parsing and consider the data invalid if we've exceeded a reasonable max
  // recursion depth
  if (depth > 64) {
    return Result{FilesystemResult::OK, false};
  }

  // Prepare a buffer that's large enough to fit any primitive or UUID value, as well as
  // single-byte type identifiers
  char buf[16];

  // Helper function to read N bytes from the file into buf, returning a Result struct
  // that propagates any filesystem error and sets an `ok` flag iff FilesystemResult is
  // OK _and_ we read all N bytes
  auto read_into_buf = [&file, &buf](size_t n) -> Result {
    auto read_res = file.Read(&buf[0], n);
    if (read_res.value == FilesystemResult::OK && read_res.bytes_read == n) {
      return Result{FilesystemResult::OK, true};
    }
    return Result{read_res.value, false};
  };

  // Read a single byte to get our encoded ValueType identifier
  if (auto res = read_into_buf(1); !res.ok) {
    return res;
  }

  // Verify that the type ID falls within the valid range for ValueType: if not, the
  // data is malformed
  const uint8_t type_id = buf[0];
  static_assert(
      static_cast<uint8_t>(ValueType::Object) == 9,
      "a ValueType enumeration has been inserted before ValueType::Object; this will "
      "break AttributeBinarySerialization compatibility"
  );
  if (type_id > static_cast<uint8_t>(ValueType::Object)) {
    return Result{FilesystemResult::OK, false};
  }

  // Branch based on ValueType, initiating more reads and storing the relevant data in
  // out_attr
  switch (static_cast<ValueType>(type_id)) {
    case ValueType::Null:
      // Null: no value
      out_attr.SetNull();
      return Result{FilesystemResult::OK, true};
    case ValueType::Bool: {
      // Bool: int64_t encoding false (0) or true (canonically 1, but in practice
      // anything other than 0)
      if (auto res = read_into_buf(8); !res.ok) {
        return res;
      }
      int64_t i64;  // NOLINT(cppcoreguidelines-init-variables)
      std::memcpy(&i64, &buf[0], 8);
      out_attr.SetBool(i64 != 0);
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::Int: {
      // Int: int64_t value
      if (auto res = read_into_buf(8); !res.ok) {
        return res;
      }
      int64_t i64;  // NOLINT(cppcoreguidelines-init-variables)
      std::memcpy(&i64, &buf[0], 8);
      out_attr.SetInt(i64);
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::UInt: {
      // UInt: uint64_t value
      if (auto res = read_into_buf(8); !res.ok) {
        return res;
      }
      uint64_t u64;  // NOLINT(cppcoreguidelines-init-variables)
      std::memcpy(&u64, &buf[0], 8);
      out_attr.SetUInt(u64);
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::Timestamp: {
      // Timestamp: int64_t value representing nanoseconds elapsed since epoch
      if (auto res = read_into_buf(8); !res.ok) {
        return res;
      }
      int64_t i64;  // NOLINT(cppcoreguidelines-init-variables)
      std::memcpy(&i64, &buf[0], 8);
      out_attr.SetTimestamp(Timestamp{std::chrono::nanoseconds(i64)});
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::Double: {
      // Double: double value
      if (auto res = read_into_buf(8); !res.ok) {
        return res;
      }
      double f64;  // NOLINT(cppcoreguidelines-init-variables)
      std::memcpy(&f64, &buf[0], 8);
      out_attr.SetDouble(f64);
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::UUID: {
      // UUID: raw 16 bytes held by UUID buffer
      UUID uuid{};
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      auto read_res = file.Read(reinterpret_cast<char*>(uuid.bytes.data()), 16);
      if (read_res.value != FilesystemResult::OK || read_res.bytes_read != 16) {
        return Result{read_res.value, false};
      }
      out_attr.SetUUID(uuid);
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::String: {
      // String: uint64_t length prefix...
      if (auto res = read_into_buf(8); !res.ok) {
        return res;
      }
      uint64_t str_len;  // NOLINT(cppcoreguidelines-init-variables)
      std::memcpy(&str_len, &buf[0], 8);

      // (reject string values that exceed 64k, as a hard internal limit to prevent
      // massive allocations due to corrupt or malicious data)
      if (str_len > 65535) {
        return Result{FilesystemResult::OK, false};
      }

      // ...then non-null-terminated character data
      out_attr = Attribute(ValueType::String, CowValue::String(std::string_view{}));
      CowValue& cow = *out_attr.value.ptr;
      if (str_len > 0) {
        cow.value.string.resize(str_len);
        auto read_res = file.Read(cow.value.string.data(), str_len);
        if (read_res.value != FilesystemResult::OK || read_res.bytes_read != str_len) {
          return Result{read_res.value, false};
        }
      }
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::Array: {
      // Array: uint64_t item count...
      if (auto res = read_into_buf(8); !res.ok) {
        return res;
      }
      uint64_t num_items;  // NOLINT(cppcoreguidelines-init-variables)
      std::memcpy(&num_items, &buf[0], 8);

      // (reject array values that exceed 4096 items, as a hard internal limit to
      // prevent massive allocations due to corrupt or malicious data)
      if (num_items > 4096) {
        return Result{FilesystemResult::OK, false};
      }

      // ...then a sequence of binary-encoded Attribute values for each item
      out_attr = Attribute::Array(num_items);
      for (uint64_t i = 0; i < num_items; ++i) {
        Attribute item;
        if (auto res = ParseImpl(file, item, depth + 1); !res.ok) {
          return res;
        }
        out_attr.ArrayPush(item);
      }
      return Result{FilesystemResult::OK, true};
    }
    case ValueType::Object: {
      // Object: uint64_t property count...
      if (auto res = read_into_buf(8); !res.ok) {
        return res;
      }
      uint64_t num_properties;  // NOLINT(cppcoreguidelines-init-variables)
      std::memcpy(&num_properties, &buf[0], 8);

      // (reject object values that exceed 4096 properties, as a hard internal limit to
      // prevent massive allocations due to corrupt or malicious data)
      if (num_properties > 4096) {
        return Result{FilesystemResult::OK, false};
      }

      // ...then a sequence of entries, each consisting of:
      out_attr = Attribute::Object(num_properties);
      for (uint64_t i = 0; i < num_properties; ++i) {
        // Length-prefixed string for property name (rejecting overly-long names)
        if (auto res = read_into_buf(8); !res.ok) {
          return res;
        }
        uint64_t name_len;  // NOLINT(cppcoreguidelines-init-variables)
        std::memcpy(&name_len, &buf[0], 8);
        if (name_len > 65535) {
          return Result{FilesystemResult::OK, false};
        }
        std::string name;
        if (name_len > 0) {
          name.resize(name_len);
          auto read_res = file.Read(name.data(), name_len);
          if (read_res.value != FilesystemResult::OK ||
              read_res.bytes_read != name_len) {
            return Result{read_res.value, false};
          }
        }

        // ...and binary-encoded property value
        Attribute value;
        if (auto res = ParseImpl(file, value, depth + 1); !res.ok) {
          return res;
        }
        out_attr.SetObjectProperty(name, value);
      }
      return Result{FilesystemResult::OK, true};
    }
  }
}

}  // namespace datadog::impl
