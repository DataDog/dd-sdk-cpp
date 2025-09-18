// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/attribute.h"

#include <memory>

#include "assert.hpp"
#include "attribute/cow.hpp"
#include "attribute/types.hpp"

// Defensively clamp the maximum array/object capacity to a reasonable upper limit at
// the API boundary, so that e.g. a call to `dd_attribute_array(-1)` (which would
// interpret -1 as SIZE_MAX on implicit conversion to size_t) wouldn't cause a crash
static const size_t MAX_INITIAL_CAPACITY = 1024;

static size_t clamp_initial_capacity(size_t capacity) {
  return std::min(MAX_INITIAL_CAPACITY, capacity);
}

static bool is_primitive_type(dd_value_type_t type) {
  switch (type) {
    case DD_VALUE_TYPE_NULL:
    case DD_VALUE_TYPE_BOOL:
    case DD_VALUE_TYPE_INT:
    case DD_VALUE_TYPE_UINT:
    case DD_VALUE_TYPE_TIMESTAMP:
    case DD_VALUE_TYPE_DOUBLE:
      return true;

    case DD_VALUE_TYPE_STRING:
    case DD_VALUE_TYPE_ARRAY:
    case DD_VALUE_TYPE_OBJECT:
      return false;
  }
  DATADOG_ASSERT(false, "unhandled dd_value_type_t enum value");
  return false;
}

static const datadog::impl::CowValue* get_cow_value(const dd_attribute_t* attribute) {
  DATADOG_ASSERT(
      !is_primitive_type(attribute->type),
      "get_cow_value called on dd_attribute_t with primitive type"
  );
  DATADOG_ASSERT(
      attribute->value.ptr,
      "dd_attribute_t with non-primitive value type has null storage ptr"
  );

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return reinterpret_cast<const datadog::impl::CowValue*>(attribute->value.ptr);
}

static datadog::impl::CowValue* get_cow_value(dd_attribute_t* attribute) {
  DATADOG_ASSERT(
      !is_primitive_type(attribute->type),
      "get_cow_value called on dd_attribute_t with primitive type"
  );
  DATADOG_ASSERT(
      attribute->value.ptr,
      "dd_attribute_t with non-primitive value type has null storage ptr"
  );

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return reinterpret_cast<datadog::impl::CowValue*>(attribute->value.ptr);
}

static datadog::impl::CowValue* get_cow_value_for_write(dd_attribute_t* attribute) {
  DATADOG_ASSERT(
      !is_primitive_type(attribute->type),
      "get_cow_value_for_write called on dd_attribute_t with primitive type"
  );
  DATADOG_ASSERT(
      attribute->value.ptr,
      "dd_attribute_t with non-primitive value type has null storage ptr"
  );

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* cow_value = reinterpret_cast<datadog::impl::CowValue*>(attribute->value.ptr);
  if (!cow_value->IsShared()) {
    return cow_value;
  }

  datadog::impl::CowValue* new_value = cow_value->Clone();
  cow_value->Release();
  attribute->value.ptr = new_value;
  return new_value;
}

extern "C" {

dd_attribute_t dd_attribute_null(void) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_NULL;
  attribute.value.i64 = 0;
  return attribute;
}

dd_attribute_t dd_attribute_bool(bool value) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_BOOL;
  attribute.value.i64 = value ? 1 : 0;
  return attribute;
}

dd_attribute_t dd_attribute_int(int64_t value) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_INT;
  attribute.value.i64 = value;
  return attribute;
}

dd_attribute_t dd_attribute_uint(uint64_t value) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_UINT;
  attribute.value.u64 = value;
  return attribute;
}

dd_attribute_t dd_attribute_timestamp_ns(uint64_t value) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_TIMESTAMP;
  attribute.value.u64 = value;
  return attribute;
}

dd_attribute_t dd_attribute_double(double value) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_DOUBLE;
  attribute.value.f64 = value;
  return attribute;
}

dd_attribute_t dd_attribute_string(const char* value) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_STRING;
  attribute.value.ptr = datadog::impl::CowValue::String(value);
  return attribute;
}

dd_attribute_t dd_attribute_array(size_t initial_capacity) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_ARRAY;
  attribute.value.ptr =
      datadog::impl::CowValue::Array(clamp_initial_capacity(initial_capacity));
  return attribute;
}

dd_attribute_t dd_attribute_object(size_t initial_capacity) {
  dd_attribute_t attribute;
  attribute.type = DD_VALUE_TYPE_OBJECT;
  attribute.value.ptr =
      datadog::impl::CowValue::Object(clamp_initial_capacity(initial_capacity));
  return attribute;
}

void dd_attribute_set_null(dd_attribute_t* attribute) {
  if (!attribute) {
    return;
  }

  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_NULL;
  attribute->value.i64 = 0;
}

void dd_attribute_set_bool(dd_attribute_t* attribute, bool value) {
  if (!attribute) {
    return;
  }

  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_BOOL;
  attribute->value.i64 = value ? 1 : 0;
}

void dd_attribute_set_int(dd_attribute_t* attribute, int64_t value) {
  if (!attribute) {
    return;
  }

  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_INT;
  attribute->value.i64 = value;
}

void dd_attribute_set_uint(dd_attribute_t* attribute, uint64_t value) {
  if (!attribute) {
    return;
  }

  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_UINT;
  attribute->value.u64 = value;
}

void dd_attribute_set_timestamp_ns(dd_attribute_t* attribute, uint64_t value) {
  if (!attribute) {
    return;
  }

  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_TIMESTAMP;
  attribute->value.u64 = value;
}

void dd_attribute_set_double(dd_attribute_t* attribute, double value) {
  if (!attribute) {
    return;
  }

  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_DOUBLE;
  attribute->value.f64 = value;
}

void dd_attribute_set_string(dd_attribute_t* attribute, const char* value) {
  if (!attribute || !value) {
    return;
  }

  // Early-out if already a string: write new string value directly to string storage,
  // cloning a copy if our reference is shared with other attributes
  if (attribute->type == DD_VALUE_TYPE_STRING) {
    get_cow_value_for_write(attribute)->SetString(value);
    return;
  }

  // Type is not string: release if needed, then allocate new string CowValue
  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_STRING;
  attribute->value.ptr = datadog::impl::CowValue::String(value);
}

void dd_attribute_init_array(dd_attribute_t* attribute, size_t initial_capacity) {
  if (!attribute) {
    return;
  }

  // Early-out if already an array: clear the array, reserving the desired storage
  // capacity if applicable
  if (attribute->type == DD_VALUE_TYPE_ARRAY) {
    get_cow_value_for_write(attribute)->Clear(clamp_initial_capacity(initial_capacity));
    return;
  }

  // Type is not array: release if needed, then allocate new array CowValue
  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_ARRAY;
  attribute->value.ptr =
      datadog::impl::CowValue::Array(clamp_initial_capacity(initial_capacity));
}

void dd_attribute_init_object(dd_attribute_t* attribute, size_t initial_capacity) {
  if (!attribute) {
    return;
  }

  // Early-out if already an object: clear the object of all properties, reserving the
  // desired storage capacity if applicable
  if (attribute->type == DD_VALUE_TYPE_OBJECT) {
    get_cow_value_for_write(attribute)->Clear(clamp_initial_capacity(initial_capacity));
    return;
  }

  // Type is not object: release if needed, then allocate new object CowValue
  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  attribute->type = DD_VALUE_TYPE_OBJECT;
  attribute->value.ptr =
      datadog::impl::CowValue::Object(clamp_initial_capacity(initial_capacity));
}

dd_attribute_t dd_attribute_copy(const dd_attribute_t* other) {
  if (!other) {
    return dd_attribute_null();
  }

  dd_attribute_t attribute;
  attribute.type = other->type;
  attribute.value = other->value;  // Bitwise copy of union data

  // If we've copied a pointer to a non-primitive value, increment its reference count
  if (!is_primitive_type(attribute.type)) {
    get_cow_value(&attribute)->AddRef();
  }

  return attribute;
}

void dd_attribute_free(dd_attribute_t* attribute) {
  if (!attribute) {
    return;
  }

  // If we're holding a reference to a non-primitive value, release it
  if (!is_primitive_type(attribute->type)) {
    get_cow_value(attribute)->Release();
  }

  // Defensive: clear the struct to make dd_attribute_free() idempotent
  attribute->type = DD_VALUE_TYPE_NULL;
  attribute->value.i64 = 0;
}

bool dd_attribute_get_bool(const dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_BOOL) {
    return false;
  }
  return attribute->value.i64 != 0;
}

int64_t dd_attribute_get_int(const dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_INT) {
    return 0;
  }
  return attribute->value.i64;
}

uint64_t dd_attribute_get_uint(const dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_UINT) {
    return 0;
  }
  return attribute->value.u64;
}

uint64_t dd_attribute_get_timestamp_ns(const dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_TIMESTAMP) {
    return 0;
  }
  return attribute->value.u64;
}

double dd_attribute_get_double(const dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_DOUBLE) {
    return 0.0;
  }
  return attribute->value.f64;
}

const char* dd_attribute_get_string(const dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_STRING) {
    return "";
  }
  return get_cow_value(attribute)->CStr();
}

size_t dd_attribute_array_len(const dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_ARRAY) {
    return 0;
  }
  return get_cow_value(attribute)->Size();
}

dd_attribute_t dd_attribute_array_get(const dd_attribute_t* attribute, int index) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_ARRAY) {
    return dd_attribute_null();
  }
  datadog::Attribute cpp_attribute = get_cow_value(attribute)->GetAt(index);
  return datadog::impl::AttributeConversion::CopyToC(cpp_attribute);
}

void dd_attribute_array_clear(dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_ARRAY) {
    return;
  }
  get_cow_value_for_write(attribute)->Clear();
}

void dd_attribute_array_push(dd_attribute_t* attribute, const dd_attribute_t* item) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_ARRAY || !item) {
    return;
  }
  datadog::Attribute cpp_item = datadog::impl::AttributeConversion::CopyFromC(*item);
  get_cow_value_for_write(attribute)->Push(cpp_item);
}

void dd_attribute_array_reserve(dd_attribute_t* attribute, size_t capacity) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_ARRAY) {
    return;
  }

  if (get_cow_value(attribute)->Capacity() >= capacity) {
    return;
  }

  get_cow_value_for_write(attribute)->Reserve(capacity);
}

size_t dd_attribute_object_property_count(const dd_attribute_t* attribute) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_OBJECT) {
    return 0;
  }
  return get_cow_value(attribute)->Size();
}

int dd_attribute_object_property_find(
    const dd_attribute_t* attribute, const char* name
) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_OBJECT || !name) {
    return -1;
  }
  return get_cow_value(attribute)->FindPropertyIndex(name);
}

const char* dd_attribute_object_name_at(const dd_attribute_t* attribute, int index) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_OBJECT) {
    return "";
  }
  return get_cow_value(attribute)->GetPropertyNameCStr(index);
}

dd_attribute_t dd_attribute_object_value_at(
    const dd_attribute_t* attribute, int index
) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_OBJECT) {
    return dd_attribute_null();
  }
  datadog::Attribute cpp_attribute = get_cow_value(attribute)->GetAt(index);
  return datadog::impl::AttributeConversion::CopyToC(cpp_attribute);
}

dd_attribute_t dd_attribute_object_property_get(
    const dd_attribute_t* attribute, const char* name
) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_OBJECT || !name) {
    return dd_attribute_null();
  }
  const int index = get_cow_value(attribute)->FindPropertyIndex(name);
  if (index < 0) {
    return dd_attribute_null();
  }
  datadog::Attribute cpp_attribute = get_cow_value(attribute)->GetAt(index);
  return datadog::impl::AttributeConversion::CopyToC(cpp_attribute);
}

void dd_attribute_object_property_set(
    dd_attribute_t* attribute, const char* name, const dd_attribute_t* value
) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_OBJECT || !name || !value) {
    return;
  }
  datadog::Attribute cpp_value = datadog::impl::AttributeConversion::CopyFromC(*value);
  get_cow_value_for_write(attribute)->SetProperty(name, cpp_value);
}

void dd_attribute_object_property_delete(dd_attribute_t* attribute, const char* name) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_OBJECT || !name) {
    return;
  }
  get_cow_value_for_write(attribute)->DeleteProperty(name);
}

void dd_attribute_object_reserve(dd_attribute_t* attribute, size_t capacity) {
  if (!attribute || attribute->type != DD_VALUE_TYPE_OBJECT) {
    return;
  }

  if (get_cow_value(attribute)->Capacity() >= capacity) {
    return;
  }

  get_cow_value_for_write(attribute)->Reserve(capacity);
}
}
