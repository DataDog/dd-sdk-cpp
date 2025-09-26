// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <cinttypes>
#include <string_view>

#include "datadog/api.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class CowValue;
struct AttributeConversion;
struct AttributeSerialization;
struct AttributeDebug;
}  // namespace impl

/**
 * Type of value that may be stored in an Attribute.
 */
enum class ValueType : uint8_t {
  Null,
  Bool,
  Int,
  UInt,
  Timestamp,
  Double,
  UUID,
  String,
  Array,
  Object
};

/**
 * A JSON-like value with copy-on-write semantics.
 *
 * An Attribute stores a value of any supported type. Primitive types are stored inline;
 * non-primitive types (string, array, and object) use heap memory.
 *
 * Attribute is optimized for storing, serializing, and "freezing" information. For
 * example, an Attribute may hold a large, deeply-nested object value. When that value
 * is first initialized, some overhead is incurred. Thereafter, though, creating a copy
 * of the Attribute simply copies the pointer to its underlying value, incrementing its
 * shared reference count.
 *
 * If a value with multiple shared references is mutated in any way, it ceases to be
 * shared: the Attribute making the modification clones the shared value, creating a
 * shallow copy that can be independently modified.
 *
 * Attribute is not optimized for retrieval or mutation. Values are generally assumed to
 * be immutable once created.
 *
 * Attributes are not guaranteed to be thread-safe. An Attribute must not be accessed or
 * modified from multiple threads concurrently.
 */
class DATADOG_API Attribute {
  ValueType type;
  union {
    int64_t i64;
    uint64_t u64;
    double f64;
    impl::CowValue* ptr;
  } value;

  // Private constructors: use static functions to construct new Attributes
  explicit Attribute(ValueType in_type, int64_t in_i64);
  explicit Attribute(ValueType in_type, uint64_t in_u64);
  explicit Attribute(ValueType in_type, double in_f64);
  explicit Attribute(ValueType in_type, impl::CowValue* in_ptr);

 public:
  /**
   * A default-constructed Attribute is equivalent to Attribute::Null().
   */
  Attribute();

  /**
   * Releases any references to non-primitive values (string, array, or object). If
   * the attribute holds the last remaining reference to a non-primitive value, that
   * value will be destroyed.
   */
  ~Attribute();
  /**
   * Copies an attribute. If the attribute holds a primitive value, the copy is
   * trivial. If the value held is non-primitive, this operation is a trivial pointer
   * copy and an increment of the underlying value's reference count.
   */
  Attribute(const Attribute& other);
  Attribute& operator=(const Attribute& other);
  Attribute(Attribute&& other) noexcept;
  Attribute& operator=(Attribute&& other) noexcept;

  /**
   * Creates a new Attribute with the given value.
   */
  static Attribute Null();
  static Attribute Bool(bool value);
  static Attribute Int(int64_t value);
  static Attribute UInt(uint64_t value);
  static Attribute TimestampFromNanoseconds(uint64_t value);
  static Attribute Double(double value);
  static Attribute UUID(const uint8_t value[16]);
  static Attribute String(std::string_view value);
  /**
   * Creates a new array Attribute that can hold an arbitrary number of Attribute
   * values as items. `initial_capacity` is a hint for preallocation.
   */
  static Attribute Array(size_t initial_capacity = 0);
  /**
   * Creates a new object Attribute that can hold an arbitrary number of Attribute
   * values as named properties. `initial_capacity` is a hint for preallocation.
   */
  static Attribute Object(size_t initial_capacity = 0);

  /**
   * Updates the value held by this Attribute, potentially changing its type as well.
   */
  void SetNull();
  void SetBool(bool new_value);
  void SetInt(int64_t new_value);
  void SetUInt(uint64_t new_value);
  void SetTimestampAsNanoseconds(uint64_t new_value);
  void SetDouble(double new_value);
  void SetUUID(const uint8_t new_value[16]);
  void SetString(std::string_view new_value);
  void InitArray(size_t initial_capacity = 0);
  void InitObject(size_t initial_capacity = 0);

  /**
   * Returns the type of value held by this attribute.
   */
  ValueType GetType() const;

  /**
   * Returns the value held by this attribute, if and only if GetType() exactly
   * corresponds to the requested value type. If the attribute does not hold a value of
   * the requested type, returns a zero/empty value.
   */
  bool GetBoolValue() const;
  int64_t GetIntValue() const;
  uint64_t GetUIntValue() const;
  uint64_t GetTimestampValueAsNanoseconds() const;
  double GetDoubleValue() const;
  void GetUUIDValue(uint8_t out_value[16]) const;
  std::string_view GetStringValue() const;

  /**
   * Returns the number of items stored in this array attribute. If this attribute does
   * not hold a value of type Array, returns 0.
   */
  size_t GetArrayLen() const;
  Attribute GetArrayItem(int index) const;
  void ArrayClear();
  void ArrayPush(const Attribute& item);
  /**
   * Reserves space for up to `capacity` items, reallocating if necessary. If the array
   * already has the enough space reserved to fit the given number of items, has no
   * effect. Will not shrink previously-allocated space. If this attribute is not an
   * array, has no effect.
   */
  void ArrayReserve(size_t capacity);

  /**
   * Returns the number of properties stored in this object attribute. If this attribute
   * does not hold a value of type Object, returns 0.
   */
  size_t GetObjectPropertyCount() const;
  /**
   * Returns the index at which the property with the given name is stored. If this
   * attribute is not an object, or if no such property exists, returns -1.
   */
  int FindObjectProperty(std::string_view name) const;
  /**
   * Returns the name of the property stored at the given index. If this attribute is
   * not an object, or if no such property exists, returns "".
   */
  std::string_view GetObjectPropertyNameAt(int index) const;
  /**
   * Returns the value of the property stored at the given index. If this attribute is
   * not an object, or if no such property exists, returns Attribute::Null().
   */
  Attribute GetObjectPropertyValueAt(int index) const;
  /**
   * Returns the value of the property with the given name. If this attribute is not an
   * object, or if no such property exists, returns Attribute::Null(). To test for
   * membership definitively, use FindObjectProperty().
   */
  Attribute GetObjectProperty(std::string_view name) const;
  /**
   * Adds the given value to the object, indexed under the given property name. If a
   * property already exists by that name, it will be overwritten with the given value.
   * If this attribute is not an object, this operation does nothing.
   */
  void SetObjectProperty(std::string_view name, const Attribute& attribute);
  /**
   * Removes the property with the given name from the object. If this attribute is not
   * an object, or if no such property exists, this operation does nothing.
   */
  void DeleteObjectProperty(std::string_view name);
  /**
   * Reserves space for up to `capacity` properties, reallocating if necessary. If the
   * object already has the enough space reserved to fit the given number of properties,
   * has no effect. Will not shrink previously-allocated space. If this attribute is not
   * an object, has no effect.
   */
  void ReserveObjectPropertyCapacity(size_t capacity);

 public:
  template <
      typename Clock = std::chrono::system_clock,
      typename Duration = std::chrono::nanoseconds>
  static Attribute Timestamp(std::chrono::time_point<Clock, Duration> value) {
    auto count = value.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(count);
    return TimestampFromNanoseconds(static_cast<uint64_t>(nanoseconds.count()));
  }

  template <
      typename Clock = std::chrono::system_clock,
      typename Duration = std::chrono::nanoseconds>
  void SetTimestamp(std::chrono::time_point<Clock, Duration> value) {
    auto count = value.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(count);
    SetTimestampAsNanoseconds(static_cast<uint64_t>(nanoseconds.count()));
  }

  template <
      typename Clock = std::chrono::system_clock,
      typename Duration = std::chrono::nanoseconds>
  std::chrono::time_point<Clock, Duration> GetTimestampValue() {
    const uint64_t nanoseconds = GetTimestampValueAsNanoseconds();
    auto duration_ns = std::chrono::nanoseconds(nanoseconds);
    auto duration_target = std::chrono::duration_cast<Duration>(duration_ns);
    return std::chrono::time_point<Clock, Duration>(duration_target);
  }

 private:
  /**
   * Used internally whenenver we mutate an underlying non-primitive value. If this
   * Attribute holds the only reference to that value, returns it directly. If this
   * Attribute shares it reference with other Attributes, releases that reference,
   * creates a shallow clone that can be independently modified, stores the clone as our
   * new value, and returns it.
   */
  impl::CowValue* GetCowValueForWrite();

  // Declare implementation-layer helpers as structs: these internal operations require
  // direct access to the Attribute's value, but they are not part of the public
  // interface.
  friend struct impl::AttributeConversion;
  friend struct impl::AttributeSerialization;
  friend struct impl::AttributeDebug;
};

}  // namespace datadog
