// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "attribute/cow.hpp"

#include <algorithm>
#include <cstring>

#include "assert.hpp"

namespace datadog::impl {

CowValue::CowValue(CowValueType in_type, size_t initial_capacity) : type(in_type) {
  // Initialize the appropriate STL union member
  switch (type) {
    case CowValueType::UUID:
      break;
    case CowValueType::String:
      // NOTE: This branch is technically unreachable, as all production code uses the
      // std::string_view constructor to initialize string values
      new (&value.string) std::string();
      value.string.reserve(initial_capacity);
      break;
    case CowValueType::Array:
      new (&value.array) std::vector<datadog::Attribute>();
      value.array.reserve(initial_capacity);
      break;
    case CowValueType::Object:
      new (&value.object) std::vector<std::pair<std::string, datadog::Attribute>>();
      value.object.reserve(initial_capacity);
      break;
  }
}

CowValue::CowValue(const uint8_t uuid_value[16]) : type(CowValueType::UUID) {
  value.uid.set(uuid_value);
}

CowValue::CowValue(std::string_view string_value) : type(CowValueType::String) {
  // Copy the passed-in string directly
  new (&value.string) std::string(string_value);
}

CowValue::~CowValue() {
  // Call the appropriate destructor on our union of STL containers
  switch (type) {
    case CowValueType::UUID:
      // UUID values are stored in the CowValue itself
      break;
    case CowValueType::String:
      value.string.~basic_string();
      break;
    case CowValueType::Array:
      value.array.~vector();
      break;
    case CowValueType::Object:
      value.object.~vector();
      break;
  }
}

CowValue::CowValue(const CowValue& other) : ref_count(1), type(other.type) {
  // Call the appropriate STL copy constructor. For array and object values, the STL
  // implementation will call the `Attribute` copy constructor on all values, ensuring
  // that reference counts are automatically incremented for nested CowValues.
  // Therefore, any array elements or subobjects in our clone will point to the same
  // underlying CowValues until the corresponding attributes on the clone are modified.
  switch (type) {
    case CowValueType::UUID:
      value.uid = other.value.uid;
      break;
    case CowValueType::String:
      new (&value.string) std::string(other.value.string);
      break;
    case CowValueType::Array:
      new (&value.array) std::vector<datadog::Attribute>(other.value.array);
      break;
    case CowValueType::Object:
      new (&value.object)
          std::vector<std::pair<std::string, datadog::Attribute>>(other.value.object);
      break;
  }
}

CowValue* CowValue::UUID(const uint8_t value_bytes[16]) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return new CowValue(value_bytes);
}

CowValue* CowValue::String(std::string_view string_value) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return new CowValue(string_value);
}

CowValue* CowValue::Array(size_t initial_capacity) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return new CowValue(CowValueType::Array, initial_capacity);
}

CowValue* CowValue::Object(size_t initial_capacity) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return new CowValue(CowValueType::Object, initial_capacity);
}

void CowValue::AddRef() {
  // No need for synchronization guarantees: we only care about incrementing the value
  // atomically; we're not doing anything based on its value
  ref_count.fetch_add(1, std::memory_order_relaxed);
}

void CowValue::Release() {
  // Decrement the reference count, and delete ourselves if that was the final
  // reference: since we need to synchronize with all writes to ref_count made
  // elsewhere, we need strictly sequential ordering
  if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete this;
  }
}

CowValue* CowValue::Clone() const {
  // Use copy constructor to initialize our clone, which will have a refcount of 1.
  // Leave our own refcount as-is: that's up to the Attribute implementation to handle
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return new CowValue(*this);
}

bool CowValue::IsShared() const {
  return ref_count.load(std::memory_order_acquire) > 1;
}

uuid CowValue::GetUUID() const {
  if (type == CowValueType::UUID) {
    return value.uid;
  }
  return uuid::zero;
}

void CowValue::SetUUID(const uint8_t new_value[16]) {
  if (type == CowValueType::UUID) {
    value.uid.set(new_value);
  }
}

const char* CowValue::CStr() const {
  // NOTE: Returning const char* ensures that we can expose string values directly via
  // the C API - std::string_view does not store a null terminator. The C++ Attribute
  // API converts from const char* to std::string_view.
  if (type == CowValueType::String) {
    return value.string.c_str();
  }
  return "";
}

void CowValue::SetString(std::string_view new_value) {
  if (type == CowValueType::String) {
    value.string = new_value;
  }
}

size_t CowValue::Size() const {
  // Return the size of the underlying container that we're using
  switch (type) {
    case CowValueType::UUID:
      return 0;
    case CowValueType::String:
      return value.string.size();
    case CowValueType::Array:
      return value.array.size();
    case CowValueType::Object:
      return value.object.size();
  }
  DATADOG_ASSERT(false, "unhandled CowValueType enum value");
  return 0;
}

size_t CowValue::Capacity() const {
  // Return the capacity of the underlying container that we're using
  switch (type) {
    case CowValueType::UUID:
      return 0;
    case CowValueType::String:
      return value.string.capacity();
    case CowValueType::Array:
      return value.array.capacity();
    case CowValueType::Object:
      return value.object.capacity();
  }
  DATADOG_ASSERT(false, "unhandled CowValueType enum value");
  return 0;
}

void CowValue::Reserve(size_t new_capacity) {
  switch (type) {
    case CowValueType::UUID:
      break;
    case CowValueType::String:
      value.string.reserve(new_capacity);
      break;
    case CowValueType::Array:
      value.array.reserve(new_capacity);
      break;
    case CowValueType::Object:
      value.object.reserve(new_capacity);
      break;
  }
}

void CowValue::Clear(size_t new_capacity) {
  // Clear the underlying container: any CowValues held in arrays/objects will be
  // released by the Attribute destructor
  switch (type) {
    case CowValueType::UUID:
      break;
    case CowValueType::String:
      value.string.clear();
      if (new_capacity > 0) {
        value.string.reserve(new_capacity);
      }
      break;
    case CowValueType::Array:
      value.array.clear();
      if (new_capacity > 0) {
        value.array.reserve(new_capacity);
      }
      break;
    case CowValueType::Object:
      value.object.clear();
      if (new_capacity > 0) {
        value.object.reserve(new_capacity);
      }
      break;
  }
}

void CowValue::Push(const Attribute& item) {
  // Array only: copy the new value into the vector, appending it to the back
  if (type == CowValueType::Array) {
    // Copy item into array, invoking Attribute copy constructor and incrementing item
    // refcount if non-primitive
    value.array.push_back(item);
  }
}

Attribute CowValue::GetAt(int index) const {
  // Array: access stored Attribute if in range
  if (type == CowValueType::Array && index >= 0 &&
      static_cast<size_t>(index) < value.array.size()) {
    // Create a copy on return, incrementing refcount if non-primitive
    return value.array[index];
  }

  // Object: access Attribute stored as i'th property value, if in range
  if (type == CowValueType::Object && index >= 0 &&
      static_cast<size_t>(index) < value.object.size()) {
    // Create a copy on return, incrementing refcount if non-primitive
    return value.object[index].second;
  }

  // Not an Array or an Object, or index out of bounds: return a null Attribute value
  return Attribute();
}

const char* CowValue::GetPropertyNameCStr(int index) const {
  // Object: quick name lookup, if in bounds, compatible with C API
  if (type == CowValueType::Object && index >= 0 &&
      static_cast<size_t>(index) < value.object.size()) {
    return value.object[index].first.c_str();
  }

  // Not an object, or index out of bounds: return empty string
  return "";
}

int CowValue::FindPropertyIndex(std::string_view name) const {
  if (type == CowValueType::Object) {
    // Find the first pair in our object-values vector that matches the name
    auto found =
        std::find_if(value.object.begin(), value.object.end(), [name](const auto& kvp) {
          return kvp.first == name;
        });

    // If we found a match, return its index
    if (found != value.object.end()) {
      // Invariant: we should never allow an object to have more than one value
      // registered for any given key
      DATADOG_ASSERT(
          std::find_if(
              std::next(found),
              value.object.end(),
              [name](const auto& kvp) { return kvp.first == name; }
          ) == value.object.end(),
          "Multiple object properties with the same name on find"
      );

      // If we have an object with more than INT_MAX values, something's wrong, but fall
      // out to return -1 in that case rather than truncating
      const size_t index = std::distance(value.object.begin(), found);
      if (static_cast<int>(index) <= std::numeric_limits<int>::max()) {
        return static_cast<int>(index);
      }
    }
  }

  // Not an object, or no matching property found: return -1
  return -1;
}

void CowValue::SetProperty(std::string_view name, const Attribute& attribute) {
  if (type == CowValueType::Object) {
    // Check for an existing value with the given name
    auto found =
        std::find_if(value.object.begin(), value.object.end(), [name](const auto& kvp) {
          return kvp.first == name;
        });

    // Either modify the existing entry in place, or add a new one to the back
    if (found != value.object.end()) {
      // Copy-assign attribute into vector, handling AddRef()/Release() calls seamlessly
      // (via Attribute copy-assignment operator) for non-primitive values
      found->second = attribute;

      // Invariant: we should never allow an object to have more than one value
      // registered for any given key
      DATADOG_ASSERT(
          std::find_if(
              std::next(found),
              value.object.end(),
              [name](const auto& kvp) { return kvp.first == name; }
          ) == value.object.end(),
          "Multiple object properties with the same name on set"
      );
    } else {
      // No existing property has this name: append it, creating a copy of the name as
      // well as the (possibly-refcounted) attribute
      value.object.emplace_back(name, attribute);
    }
  }
}

void CowValue::DeleteProperty(std::string_view name) {
  if (type == CowValueType::Object) {
    // Move all elements matching the given name to the end of the vector
    auto new_end = std::remove_if(
        value.object.begin(), value.object.end(), [name](const auto& kvp) {
          return kvp.first == name;
        }
    );

    // Invariant: we should never allow an object to have more than one value registered
    // for any given key
    DATADOG_ASSERT(
        std::distance(new_end, value.object.end()) <= 1,
        "Multiple object properties with the same name on delete"
    );

    // If we found any matching values, remove them, seamlessly handling calls to
    // Release() (for any non-primitive items) via Attribute destructor
    value.object.erase(new_end, value.object.end());
  }
}

}  // namespace datadog::impl
