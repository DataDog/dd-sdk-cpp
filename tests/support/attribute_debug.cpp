// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "support/attribute_debug.hpp"

#include <sstream>
#include <unordered_set>

#include "datadog/impl/attribute/cow.hpp"

namespace datadog::impl {

static size_t _compute_string_heap_size(const std::string& s) {
  // Take the address of the start and end of the std::string value
  const char* s_start = reinterpret_cast<const char*>(&s);
  const char* s_end = s_start + sizeof(std::string);

  // If the string's data pointer lies within that address range, small string
  // optimization (SSO) is in use: the string has not allocated any heap memory
  const char* data = s.data();
  if (data >= s_start && data < s_end) {
    return 0;
  }

  // Otherwise, capacity() reflects the number of bytes allocated on the heap
  return s.capacity();
}

void AttributeDebug::Dump(
    const Attribute& a, std::ostream& out, const std::string& prefix
) {
  std::string indent = std::string(prefix.size(), ' ');
  std::string next_indent = indent + "  ";

  switch (a.type) {
    case ValueType::Null:
      out << prefix << ". null\n";
      break;
    case ValueType::Bool:
      if (a.value.i64 != 0) {
        out << prefix << ". true\n";
      } else {
        out << prefix << ". false\n";
      }
      break;
    case ValueType::Int:
      out << prefix << ". " << a.value.i64 << "ll\n";
      break;
    case ValueType::UInt:
      out << prefix << ". " << a.value.u64 << "ull\n";
      break;
    case ValueType::Timestamp:
      out << prefix << ". " << a.value.u64 << "t\n";
      break;
    case ValueType::Double:
      out << prefix << ". " << a.value.f64 << "\n";
      break;
    case ValueType::UUID: {
      out << prefix << a.value.ptr->ref_count.load() << " ";
      const auto data = a.value.ptr->value.uid.bytes;
      out << std::hex << static_cast<int>(data[0]) << static_cast<int>(data[1])
          << static_cast<int>(data[2]) << static_cast<int>(data[3]) << "-"
          << static_cast<int>(data[4]) << static_cast<int>(data[5]) << "-"
          << static_cast<int>(data[6]) << static_cast<int>(data[7]) << "-"
          << static_cast<int>(data[8]) << static_cast<int>(data[9]) << "-"
          << static_cast<int>(data[10]) << static_cast<int>(data[11])
          << static_cast<int>(data[12]) << static_cast<int>(data[13])
          << static_cast<int>(data[14]) << static_cast<int>(data[15]) << std::dec
          << "\n";
    } break;
    case ValueType::String:
      out << prefix << a.value.ptr->ref_count.load() << " " << a.value.ptr->value.string
          << "\n";
      break;
    case ValueType::Array: {
      out << prefix << a.value.ptr->ref_count.load() << " [\n";
      for (const auto& item : a.value.ptr->value.array) {
        Dump(item, out, next_indent);
      }
      out << indent << "  ]\n";
    } break;
    case ValueType::Object: {
      out << prefix << a.value.ptr->ref_count.load() << " {\n";
      for (const auto& kvp : a.value.ptr->value.object) {
        std::string next_prefix;
        next_prefix += next_indent;
        next_prefix += kvp.first;
        next_prefix += ": ";
        Dump(kvp.second, out, next_prefix);
      }
      out << indent << "  }\n";
    } break;
  }
}

std::string AttributeDebug::ToString(const Attribute& a) {
  std::ostringstream oss;
  Dump(a, oss, "");
  return oss.str();
}

size_t AttributeDebug::ComputeHeapSizeImpl(
    const Attribute& a, std::unordered_set<const CowValue*>& visited
) {
  switch (a.type) {
    // Primitive types are stored in the Attribute directly; they do not require
    // additional heap memory
    case ValueType::Null:
    case ValueType::Bool:
    case ValueType::Int:
    case ValueType::UInt:
    case ValueType::Timestamp:
    case ValueType::Double:
      return 0;

    // Non-primitive types use CowValue as their underlying storage: each CowValue
    // struct is heap-allocated, and it uses an underlying STL container (string or
    // vector) depending on the value type, so count them both
    case ValueType::UUID:
    case ValueType::String:
    case ValueType::Array:
    case ValueType::Object: {
      // If we've already counted this CowValue, don't count it again
      const CowValue* cow_ptr = a.value.ptr;
      if (visited.find(cow_ptr) != visited.end()) {
        return 0;
      }
      visited.insert(cow_ptr);

      // Accumulate the total size, making sure to include the heap-allocated CowValue
      // struct in our total
      size_t total_size = sizeof(CowValue);
      if (a.type == ValueType::UUID) {
        // UUIDs are stored in the CowValue; they require no additional heap memory
      } else if (a.type == ValueType::String) {
        // For strings, add the number of bytes allocated on the heap by std::string
        // (which may be 0 if using SSO)
        total_size += _compute_string_heap_size(cow_ptr->value.string);
      } else if (a.type == ValueType::Array) {
        // For arrays, add the number of bytes reserved by
        // std::vector<Attribute>, then recursively compute the heap size of all array
        // items
        total_size += cow_ptr->value.array.capacity() * sizeof(Attribute);
        for (const auto& item : cow_ptr->value.array) {
          total_size += AttributeDebug::ComputeHeapSizeImpl(item, visited);
        }
      } else if (a.type == ValueType::Object) {
        // For objects, add the number of bytes reserved by
        // std::vector<std::pair<std::string,Attribute>>, then add the number of bytes
        // reserved by each property name string, then recursively sum the heap sizes of
        // all property values
        total_size += cow_ptr->value.object.capacity() *
                      sizeof(std::pair<std::string, Attribute>);
        for (const auto& kvp : cow_ptr->value.object) {
          total_size += _compute_string_heap_size(kvp.first);
          total_size += AttributeDebug::ComputeHeapSizeImpl(kvp.second, visited);
        }
      }
      return total_size;
    }
  }
  return 0;
}

size_t AttributeDebug::ComputeHeapSize(const Attribute& a) {
  std::unordered_set<const CowValue*> visited;
  return AttributeDebug::ComputeHeapSizeImpl(a, visited);
}

}  // namespace datadog::impl
