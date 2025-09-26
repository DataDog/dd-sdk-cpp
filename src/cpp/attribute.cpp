// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/attribute.hpp"

#include <cstring>
#include <iostream>

#include "assert.hpp"
#include "attribute/cow.hpp"

namespace datadog {

// Defensively clamp the maximum array/object capacity to a reasonable upper limit at
// the API boundary, so that e.g. a call to `datadog::Attribute::Array(-1)` (which would
// interpret -1 as SIZE_MAX on implicit conversion to size_t) wouldn't cause a crash
static const size_t MAX_INITIAL_CAPACITY = 1024;

static size_t _clamp_initial_capacity(size_t capacity) {
  return std::min(MAX_INITIAL_CAPACITY, capacity);
}

static bool _is_primitive_type(ValueType type) {
  switch (type) {
    case ValueType::Null:
    case ValueType::Bool:
    case ValueType::Int:
    case ValueType::UInt:
    case ValueType::Timestamp:
    case ValueType::Double:
      return true;

    case ValueType::UUID:
    case ValueType::String:
    case ValueType::Array:
    case ValueType::Object:
      return false;
  }
  DATADOG_ASSERT(false, "unhandled ValueType enum value");
  return false;
}

Attribute::Attribute(ValueType in_type, int64_t in_i64) : type(in_type), value() {
  value.i64 = in_i64;
}

Attribute::Attribute(ValueType in_type, uint64_t in_u64) : type(in_type), value() {
  value.u64 = in_u64;
}

Attribute::Attribute(ValueType in_type, double in_f64) : type(in_type), value() {
  value.f64 = in_f64;
}

Attribute::Attribute(ValueType in_type, impl::CowValue* in_ptr)
    : type(in_type), value() {
  value.ptr = in_ptr;
}

Attribute::Attribute() : type(ValueType::Null), value() { value.ptr = nullptr; }

Attribute::Attribute(const Attribute& other)
    : type(other.type),
      value(other.value)  // Bit-level copy of union data
{
  // If we've copied a pointer to a non-primitive value, increment its reference count
  if (!_is_primitive_type(type)) {
    value.ptr->AddRef();
  }
}

Attribute::~Attribute() {
  // If we hold a reference to a CowValue, release it
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }
}

Attribute& Attribute::operator=(const Attribute& other) {
  // Guard against self-assignment
  if (this == &other) {
    return *this;
  }

  // If our current value is non-primitive, release our reference to it
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  // Adopt the type of the other attribute, then copy its value, incrementing its
  // reference count if it's non-primitive
  type = other.type;
  value = other.value;  // Bit-level copy of union data
  if (!_is_primitive_type(type)) {
    value.ptr->AddRef();
  }
  return *this;
}

Attribute::Attribute(Attribute&& other) noexcept
    : type(other.type),
      value(other.value)  // Bit-level copy of union data
{
  // If the value is non-primitive, ownership has been transferred simply by virtue of
  // the pointer copy: reference count remains as-is. Just leave the original object in
  // a valid but empty state to ensure that it retains no references.
  other.type = ValueType::Null;
  other.value.i64 = 0;
}

Attribute& Attribute::operator=(Attribute&& other) noexcept {
  // Guard against self-assignment
  if (this == &other) {
    return *this;
  }

  // Release our current value if non-primitive
  if (!_is_primitive_type(type) && value.ptr) {
    value.ptr->Release();
  }

  // Transfer ownership from other
  type = other.type;
  value = other.value;  // Bit-level copy of union data

  // Leave other in valid but empty state
  other.type = ValueType::Null;
  other.value.i64 = 0;

  return *this;
}

Attribute Attribute::Null() { return Attribute(); }

Attribute Attribute::Bool(bool value) {
  const int64_t int_value = value ? 1 : 0;
  return Attribute(ValueType::Bool, int_value);
}

Attribute Attribute::Int(int64_t value) { return Attribute(ValueType::Int, value); }

Attribute Attribute::UInt(uint64_t value) { return Attribute(ValueType::UInt, value); }

Attribute Attribute::TimestampFromNanoseconds(uint64_t value) {
  return Attribute(ValueType::Timestamp, value);
}

Attribute Attribute::Double(double value) {
  return Attribute(ValueType::Double, value);
}

Attribute Attribute::UUID(const uint8_t value[16]) {
  return Attribute(ValueType::UUID, impl::CowValue::UUID(value));
}

Attribute Attribute::String(std::string_view value) {
  return Attribute(ValueType::String, impl::CowValue::String(value));
}

Attribute Attribute::Array(size_t initial_capacity) {
  return Attribute(
      ValueType::Array, impl::CowValue::Array(_clamp_initial_capacity(initial_capacity))
  );
}

Attribute Attribute::Object(size_t initial_capacity) {
  return Attribute(
      ValueType::Object,
      impl::CowValue::Object(_clamp_initial_capacity(initial_capacity))
  );
}

void Attribute::SetNull() {
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::Null;
  value.i64 = 0;
}

void Attribute::SetBool(bool new_value) {
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::Bool;
  value.i64 = new_value ? 1 : 0;
}

void Attribute::SetInt(int64_t new_value) {
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::Int;
  value.i64 = new_value;
}

void Attribute::SetUInt(uint64_t new_value) {
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::UInt;
  value.u64 = new_value;
}

void Attribute::SetTimestampAsNanoseconds(uint64_t new_value) {
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::Timestamp;
  value.u64 = new_value;
}

void Attribute::SetDouble(double new_value) {
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::Double;
  value.f64 = new_value;
}

void Attribute::SetUUID(const uint8_t new_value[16]) {
  // If already a UUID, avoid allocation: simply overwrite UUID value
  if (type == ValueType::UUID) {
    GetCowValueForWrite()->SetUUID(new_value);
    return;
  }

  // Type is not UUID: release if needed, then allocate new UUID CowValue
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::UUID;
  value.ptr = impl::CowValue::UUID(new_value);
}

void Attribute::SetString(std::string_view new_value) {
  // Early-out if already a string: write new string value directly to string storage,
  // cloning a copy if our reference is shared with other attributes
  if (type == ValueType::String) {
    GetCowValueForWrite()->SetString(new_value);
    return;
  }

  // Type is not string: release if needed, then allocate new string CowValue
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::String;
  value.ptr = impl::CowValue::String(new_value);
}

void Attribute::InitArray(size_t initial_capacity) {
  // Early-out if already an array: clear the array, reserving the desired storage
  // capacity if applicable
  if (type == ValueType::Array) {
    GetCowValueForWrite()->Clear(_clamp_initial_capacity(initial_capacity));
    return;
  }

  // Type is not array: release if needed, then allocate new array CowValue
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::Array;
  value.ptr = impl::CowValue::Array(_clamp_initial_capacity(initial_capacity));
}

void Attribute::InitObject(size_t initial_capacity) {
  // Early-out if already an object: clear the object of all properties, reserving the
  // desired storage capacity if applicable
  if (type == ValueType::Object) {
    GetCowValueForWrite()->Clear(_clamp_initial_capacity(initial_capacity));
    return;
  }

  // Type is not object: release if needed, then allocate new object CowValue
  if (!_is_primitive_type(type)) {
    value.ptr->Release();
  }

  type = ValueType::Object;
  value.ptr = impl::CowValue::Object(_clamp_initial_capacity(initial_capacity));
}

ValueType Attribute::GetType() const { return type; }

bool Attribute::GetBoolValue() const {
  if (type != ValueType::Bool) {
    return false;
  }
  return value.i64 != 0;
}

int64_t Attribute::GetIntValue() const {
  if (type != ValueType::Int) {
    return 0;
  }
  return value.i64;
}

uint64_t Attribute::GetUIntValue() const {
  if (type != ValueType::UInt) {
    return 0;
  }
  return value.u64;
}

uint64_t Attribute::GetTimestampValueAsNanoseconds() const {
  if (type != ValueType::Timestamp) {
    return 0;
  }
  return value.u64;
}

double Attribute::GetDoubleValue() const {
  if (type != ValueType::Double) {
    return 0.0;
  }
  return value.f64;
}

void Attribute::GetUUIDValue(uint8_t out_value[16]) const {
  const auto v = (type == ValueType::UUID) ? value.ptr->GetUUID() : impl::uuid::zero;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  std::memcpy(out_value, v.bytes.data(), 16);
}

std::string_view Attribute::GetStringValue() const {
  if (type != ValueType::String) {
    return std::string_view{};
  }
  return value.ptr->CStr();
}

size_t Attribute::GetArrayLen() const {
  if (type != ValueType::Array) {
    return 0;
  }
  return value.ptr->Size();
}

Attribute Attribute::GetArrayItem(int index) const {
  if (type != ValueType::Array) {
    return Attribute();
  }
  return value.ptr->GetAt(index);
}

void Attribute::ArrayClear() {
  if (type != ValueType::Array) {
    return;
  }
  GetCowValueForWrite()->Clear();
}

void Attribute::ArrayPush(const Attribute& item) {
  if (type != ValueType::Array) {
    return;
  }

  // If the caller wants to add an array as an item of itself, create a clone so as not
  // to create an infinitely-recursive data structure
  if (&item == this) {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const Attribute copy = item;
    ArrayPush(copy);
    return;
  }

  // Otherwise, push normally
  GetCowValueForWrite()->Push(item);
}

void Attribute::ArrayReserve(size_t capacity) {
  if (type != ValueType::Array) {
    return;
  }

  // Even though Reserve() is idempotent, GetCowValueForWrite() is not: inspect current
  // capacity so we don't clone needlessly
  if (value.ptr->Capacity() >= capacity) {
    return;
  }

  GetCowValueForWrite()->Reserve(capacity);
}

size_t Attribute::GetObjectPropertyCount() const {
  if (type != ValueType::Object) {
    return 0;
  }
  return value.ptr->Size();
}

int Attribute::FindObjectProperty(std::string_view name) const {
  if (type != ValueType::Object) {
    return -1;
  }
  return value.ptr->FindPropertyIndex(name);
}

std::string_view Attribute::GetObjectPropertyNameAt(int index) const {
  if (type != ValueType::Object) {
    return std::string_view{};
  }
  return value.ptr->GetPropertyNameCStr(index);
}

Attribute Attribute::GetObjectPropertyValueAt(int index) const {
  if (type != ValueType::Object) {
    return Attribute();
  }
  return value.ptr->GetAt(index);
}

Attribute Attribute::GetObjectProperty(std::string_view name) const {
  if (type != ValueType::Object) {
    return Attribute();
  }
  const int index = value.ptr->FindPropertyIndex(name);
  if (index < 0) {
    return Attribute();
  }
  return value.ptr->GetAt(index);
}

void Attribute::SetObjectProperty(std::string_view name, const Attribute& attribute) {
  if (type != ValueType::Object) {
    return;
  }

  // If the caller wants to add an object as a property of itself, create a clone so as
  // not to create an infinitely-recursive data structure
  if (&attribute == this) {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const Attribute copy = attribute;
    SetObjectProperty(name, copy);
    return;
  }

  // Otherwise, set the property normally
  GetCowValueForWrite()->SetProperty(name, attribute);
}

void Attribute::DeleteObjectProperty(std::string_view name) {
  if (type != ValueType::Object) {
    return;
  }
  GetCowValueForWrite()->DeleteProperty(name);
}

void Attribute::ReserveObjectPropertyCapacity(size_t capacity) {
  if (type != ValueType::Object) {
    return;
  }

  // Inspect current capacity so we don't clone needlessly
  if (value.ptr->Capacity() >= capacity) {
    return;
  }

  GetCowValueForWrite()->Reserve(capacity);
}

impl::CowValue* Attribute::GetCowValueForWrite() {
  // This is a private helper function; it should only be called if we've already
  // checked that our value is a COW type
  DATADOG_ASSERT(
      !_is_primitive_type(type),
      "GetCowValueForWrite called on attribute of primitive type"
  );

  // Note that IsShared and Clone/Release are not atomic; we assume that the same
  // Attribute will not be used concurrently by multiple threads
  if (value.ptr->IsShared()) {
    impl::CowValue* new_value = value.ptr->Clone();
    value.ptr->Release();
    value.ptr = new_value;
  }
  return value.ptr;
}

}  // namespace datadog
