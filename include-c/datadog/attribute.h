// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_ATTRIBUTE_H
#define DATADOG_INCLUDE_ATTRIBUTE_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "datadog/api.h"
#include "datadog/uuid.h"

#ifdef __cplusplus
extern "C" {
#endif

// === Attribute types ===
// - A `dd_attribute_t` value contains a single value of the type indicated by its
//   `dd_value_type_t`.
// - Attributes with string, array, or object values are internally referenced-counted
//   with copy-on-write semantics.

typedef enum {
  DD_VALUE_TYPE_NULL,
  DD_VALUE_TYPE_BOOL,
  DD_VALUE_TYPE_INT,
  DD_VALUE_TYPE_UINT,
  DD_VALUE_TYPE_TIMESTAMP,
  DD_VALUE_TYPE_DOUBLE,
  DD_VALUE_TYPE_UUID,
  DD_VALUE_TYPE_STRING,
  DD_VALUE_TYPE_ARRAY,
  DD_VALUE_TYPE_OBJECT
} dd_value_type_t;

typedef struct dd_attribute {
  dd_value_type_t type;
  union {
    int64_t i64;
    uint64_t u64;
    double f64;
    void* ptr;
  } value;
} dd_attribute_t;

// === Attribute creation functions ===
// - Use `dd_attribute_<type>()` to initialize a new attribute value of the given type.
// - You MUST call `dd_attribute_free()` on the resulting attribute value when finished.

DATADOG_API dd_attribute_t dd_attribute_null(void);
DATADOG_API dd_attribute_t dd_attribute_bool(bool value);
DATADOG_API dd_attribute_t dd_attribute_int(int64_t value);
DATADOG_API dd_attribute_t dd_attribute_uint(uint64_t value);
DATADOG_API dd_attribute_t dd_attribute_timestamp_ns(uint64_t value);
DATADOG_API dd_attribute_t dd_attribute_double(double value);
DATADOG_API dd_attribute_t dd_attribute_uuid(const dd_uuid_t value);
DATADOG_API dd_attribute_t dd_attribute_string(const char* value);
DATADOG_API dd_attribute_t dd_attribute_array(size_t initial_capacity);
DATADOG_API dd_attribute_t dd_attribute_object(size_t initial_capacity);

// === Value setter functions ===
// - Use `dd_attribute_set_<type>` to change the value held by an existing attribute.
// - Changing an existing attribute's type is allowed.
// - These functions may NOT be called on uninitialized attribute values.
// - Prefer `dd_attribute_<type>()` for creating brand new values.
// - If the target attribute is NULL, has no effect.

DATADOG_API void dd_attribute_set_null(dd_attribute_t* attribute);
DATADOG_API void dd_attribute_set_bool(dd_attribute_t* attribute, bool value);
DATADOG_API void dd_attribute_set_int(dd_attribute_t* attribute, int64_t value);
DATADOG_API void dd_attribute_set_uint(dd_attribute_t* attribute, uint64_t value);
DATADOG_API void dd_attribute_set_timestamp_ns(
    dd_attribute_t* attribute, uint64_t value
);
DATADOG_API void dd_attribute_set_double(dd_attribute_t* attribute, double value);
DATADOG_API void dd_attribute_set_uuid(
    dd_attribute_t* attribute, const dd_uuid_t value
);
DATADOG_API void dd_attribute_set_string(dd_attribute_t* attribute, const char* value);
DATADOG_API void dd_attribute_init_array(
    dd_attribute_t* attribute, size_t initial_capacity
);
DATADOG_API void dd_attribute_init_object(
    dd_attribute_t* attribute, size_t initial_capacity
);

// === Attribute lifecycle functions ===
// - Use `dd_attribute_copy()` to create another attribute referencing the same value.
// - Use `dd_attribute_free()` whenever a `dd_attribute_t` value goes out of scope.
// - Assignment/memcpy constitutes a move: the source value must not be used or freed
//   thereafter, and the destination value must be freed normally.
// - If the target attribute is NULL, has no effect, returning `dd_attribute_null()` in
//   the case of copy.

DATADOG_API dd_attribute_t dd_attribute_copy(const dd_attribute_t* other);
DATADOG_API void dd_attribute_free(dd_attribute_t* attribute);

// === Value getter functions ===
// - Use `dd_attribute_get_<type>()` to retrieve the value held by an attribute.
// - If the attribute's type does not exactly match the desired type, the result value
//   will be zero/empty. To prevalidate, check the attribute's `type`.
// - If the target attribute is NULL, the result value will be zero/empty.
// - Value access MUST be done via these functions: reading the `value` member directly
//   is unsupported.

DATADOG_API bool dd_attribute_get_bool(const dd_attribute_t* attribute);
DATADOG_API int64_t dd_attribute_get_int(const dd_attribute_t* attribute);
DATADOG_API uint64_t dd_attribute_get_uint(const dd_attribute_t* attribute);
DATADOG_API uint64_t dd_attribute_get_timestamp_ns(const dd_attribute_t* attribute);
DATADOG_API double dd_attribute_get_double(const dd_attribute_t* attribute);
DATADOG_API dd_uuid_t dd_attribute_get_uuid(const dd_attribute_t* attribute);
DATADOG_API const char* dd_attribute_get_string(const dd_attribute_t* attribute);

// === Array functions ===
// - Use `dd_attribute_array_<op>()` to manipulate the array held by an attribute.
// - `initial_capacity` is a hint for preallocation. `push()` may be called as many
//   times as desired beyond the initial capacity.
// - `get()` returns `dd_attribute_null()` on out-of-bounds index.
// - Values returned from `get()` MUST be freed using `dd_attribute_free()`.
// - If the target attribute is NULL, or is not an array, these calls have no effect.

DATADOG_API size_t dd_attribute_array_len(const dd_attribute_t* attribute);
DATADOG_API dd_attribute_t
dd_attribute_array_get(const dd_attribute_t* attribute, int index);
DATADOG_API void dd_attribute_array_clear(dd_attribute_t* attribute);
DATADOG_API void dd_attribute_array_push(
    dd_attribute_t* attribute, const dd_attribute_t* item
);
DATADOG_API void dd_attribute_array_reserve(dd_attribute_t* attribute, size_t capacity);

// === Object functions ===
// - Use `dd_attribute_object_<op>()` to manipulate the object held by an attribute.
// - `initial_capacity` is a hint for preallocation, not a hard limit on size.
// - `get()` and `value_at()` return `dd_attribute_null()` on failed lookup.
// - Use `find()` to test for membership.
// - Values returned from `get()` and `value_at()` MUST be freed using
//   `dd_attribute_free()`.
// - If the target attribute is NULL, or is not an object, these calls have no effect.

DATADOG_API size_t dd_attribute_object_property_count(const dd_attribute_t* attribute);
DATADOG_API int dd_attribute_object_property_find(
    const dd_attribute_t* attribute, const char* name
);
DATADOG_API const char* dd_attribute_object_name_at(
    const dd_attribute_t* attribute, int index
);
DATADOG_API dd_attribute_t
dd_attribute_object_value_at(const dd_attribute_t* attribute, int index);
DATADOG_API dd_attribute_t
dd_attribute_object_property_get(const dd_attribute_t* attribute, const char* name);
DATADOG_API void dd_attribute_object_property_set(
    dd_attribute_t* attribute, const char* name, const dd_attribute_t* value
);
DATADOG_API void dd_attribute_object_property_delete(
    dd_attribute_t* attribute, const char* name
);
DATADOG_API void dd_attribute_object_reserve(
    dd_attribute_t* attribute, size_t capacity
);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_ATTRIBUTE_H
