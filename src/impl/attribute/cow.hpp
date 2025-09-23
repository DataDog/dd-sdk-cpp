// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <cinttypes>
#include <limits>
#include <string>
#include <vector>

#include "datadog/attribute.hpp"
#include "uuid.hpp"

namespace datadog::impl {

/**
 * Encapsulates a single non-primitive value that may be referenced by multiple
 * attributes.
 *
 * Keeps track of reference count: each time we copy an attribute that points to a
 * CowValue, we increment the existing value's refcount.
 *
 * Implements copy-on-write behavior: each time we _modify_ an attribute that points to
 * a CowValue for which we don't have unique ownership, we detach that value, creating a
 * new clone that can be independently modified without affecting other references.
 *
 * A CowValue is approximately 32 bytes in and of itself. Depending on the type of value
 * being stored, CowValue will use the appropriate STL container, which may allocate
 * more heap memory.
 */
class CowValue {
 private:
  /**
   * Type of non-primitive value that can be stored using copy-on-write semantics.
   */
  enum class CowValueType : uint8_t { UUID, String, Array, Object };

  /** Number of independent references to this value. */
  std::atomic<int> ref_count{1};

  /** Type of value being stored; used to discriminate `data`. */
  CowValueType type;

  /** Underlying container for the value we're storing. */
  union Data {
    uuid uuid;
    std::string string;
    std::vector<Attribute> array;
    std::vector<std::pair<std::string, Attribute>> object;

    // Define do-nothing constructor, destructor, and copy constructor: `Data` can never
    // be used outside of CowValue, which takes full responsibility for invoking special
    // member functions on the appropriate union member
    Data() {}
    ~Data() {}
    Data(const Data&) {}
    // A CowValue can't be copy-assigned or moved; hence neither can its union
    Data& operator=(const Data&) = delete;
    Data(Data&&) = delete;
    Data& operator=(Data&&) = delete;
  } value;

 private:
  /**
   * Initializes a new CowValue of the given type, with refcount 1, preallocating space
   * for the desired number of elements.
   *
   * Callers should not directly construct CowValue objects; use `CowValue::String()`,
   * `CowValue::Array()`, and `CowValue::Object()` instead.
   */
  explicit CowValue(CowValueType in_type, size_t initial_capacity);

  /**
   * Initializes a new CowValue for a UUID, with refcount 1.
   */
  explicit CowValue(const uint8_t uuid_value[16]);

  /**
   * Initializes a new CowValue for a string, with refcount 1.
   */
  explicit CowValue(std::string_view string_value);

  /**
   * Frees all underlying memory used to store this value.
   *
   * Invoked by Release() when the reference count is decremented to zero. Callers may
   * not explicitly delete a CowValue: call Release() instead.
   */
  ~CowValue();

  /**
   * Creates a new copy of the CowValue, with a refcount of 1, initialized to store a
   * shallow copy of the value held by the original object.
   *
   * For arrays and objects, this creates a new container but preserves shared
   * references to nested CowValues.
   *
   * Invoked by Clone().
   */
  CowValue(const CowValue& other);

  // Disallow copy-assignment: callers should use Clone().
  CowValue& operator=(const CowValue& other) = delete;

  // Disallow moving of reference-counted values
  CowValue(CowValue&& other) = delete;
  CowValue& operator=(CowValue&& other) = delete;

 public:
  /**
   * Allocates a new CowValue to hold the given value.
   *
   * The caller (i.e. the Datadog Attribute implementation) MUST call Release() whenever
   * a CowValue pointer goes out of scope.
   */
  static CowValue* UUID(const uint8_t value_bytes[16]);
  static CowValue* String(std::string_view string_value);
  static CowValue* Array(size_t initial_capacity);
  static CowValue* Object(size_t initial_capacity);

  /**
   * Increments the reference count, recording the fact that one more attribute now
   * points to this value.
   *
   * MUST be called when a CowValue pointer is copied.
   */
  void AddRef();

  /**
   * Decrements the reference count. If no references remain thereafter, deletes the
   * CowValue object.
   *
   * MUST be called when a CowValue pointer goes out of scope.
   */
  void Release();

  /**
   * Creates a shallow copy of this CowValue, resulting in a new value that can be
   * independently modified without affecting references to the original value that are
   * still held elsewhere.
   *
   * MUST be called before a CowValue is modified, unless the caller has exclusive
   * access to the CowValue (i.e. IsShared() is false).
   */
  CowValue* Clone() const;

  /**
   * Returns true if this CowValue is referenced from multiple places, indicating that
   * it must be cloned before it can be safely modified.
   *
   * Note that IsShared() and Clone() do not occur atomically: it's assumed that no two
   * threads will attempt to modify the same CowValue concurrently.
   */
  bool IsShared() const;

 public:
  /**
   * If storing a UUID, returns a copy of its value. Returns uuid::zero otherwise.
   */
  uuid GetUUID() const;

  /**
   * If storing a UUID, directly modifies its value. If not storing a UUID, has no
   * effect.
   */
  void SetUUID(const uint8_t new_value[16]);

  /**
   * If storing a string, returns its value. Returns an empty string otherwise.
   */
  const char* CStr() const;

  /**
   * If storing a string, directly modifies its value. If not storing a string, has no
   * effect.
   */
  void SetString(std::string_view new_value);

  /**
   * If storing a string, returns its length. If storing an array, returns the number of
   * items in that array. If storing an object, returns the number of properties in that
   * object.
   */
  size_t Size() const;

  /**
   * Returns the currently-reserved capacity of the underlying container, in terms of
   * the number of characters, array items, or object properties.
   *
   * @note Reserve() is idempotent, but we still expose capacity here so that an
   *  Attribute can avoid cloning its CowValue if no reallocation is actually needed.
   */
  size_t Capacity() const;

  /**
   * If storing an array or object, reserves space for up to new_capacity elements. If
   * sufficient space is already available, has no effect.
   */
  void Reserve(size_t new_capacity);

  /**
   * For all types, clears the underlying storage. If new_capacity is greater than
   * existing capacity, reallocates to ensure the given number of elements can be
   * stored. If new_capacity is 0, leaves capacity unchanged.
   */
  void Clear(size_t new_capacity = 0);

  /**
   * If storing an array, copies a new item into that array. For all other types, does
   * nothing.
   */
  void Push(const Attribute& item);

  /**
   * If storing an array, returns a copy of the Attribute value stored at the given
   * array index, if that index is valid. If storing an object, returns the a copy of
   * the Attribute value associated with the property stored at that index, if valid.
   *
   * If the provided index is out of range, or if this CowValue is storing neither an
   * array nor an object, returns a new Attribute with a null value.
   */
  Attribute GetAt(int index) const;

  /**
   * If storing an object, and index is within [0..Size), returns the name of the
   * property stored at that position. Otherwise, returns an empty string.
   */
  const char* GetPropertyNameCStr(int index) const;

  /**
   * If storing an object, searches for a property with the given name and returns its
   * index if found. If not storing an object, or if no such property is found, returns
   * -1.
   */
  int FindPropertyIndex(std::string_view name) const;

  /**
   * If storing an object, copies the given attribute value into the object as a
   * property with the given name. If the object already has a property with the given
   * name, it will be replaced.
   *
   * If not storing an object, has no effect.
   */
  void SetProperty(std::string_view name, const Attribute& attribute);

  /**
   * If storing an object, and if the object has a property with the given name, removes
   * that property from the object. Otherwise, has no effect.
   */
  void DeleteProperty(std::string_view name);

 private:
  friend struct AttributeDebug;
};

}  // namespace datadog::impl
