// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "datadog/attribute.hpp"
#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Wrapper for an attribute value of a known type.
 *
 * Used within the implementation when we want to keep track of a long-lived value
 * that's stored as an attribute, while clearly documenting its expected type (and
 * guarding against accidental use of other value types).
 */
template <ValueType T>
struct TypedAttribute {
  Attribute attribute;

  /**
   * Default-initializes a TypedAttribute to hold a null value.
   */
  TypedAttribute() {}

  /**
   * Initializes a new TypedAttribute wrapper to hold the given Attribute.
   */
  explicit TypedAttribute(const Attribute& in_attribute) : attribute(in_attribute) {}

  /**
   * Returns true if the attribute holds a value of the required type.
   */
  explicit operator bool() const { return attribute.GetType() == T; }

  /**
   * Returns the underlying Attribute value if its type matches the required type;
   * otherwise returns Attribute::Null().
   */
  Attribute operator*() const {
    if (attribute.GetType() == T) {
      return attribute;
    }
    return Attribute();
  }
};

struct UUIDAttribute : public TypedAttribute<ValueType::UUID> {
  /**
   * Default-initializes a UUID attribute to null.
   */
  UUIDAttribute() {}

  /**
   * Initializes a UUID attribute from the provided value.
   */
  explicit UUIDAttribute(const UUID& value)
      : TypedAttribute(Attribute::UUID(value.bytes.data())) {}

  /**
   * For convenience: returns the current UUID value stored in this attribute, or
   * UUID::Zero as a fallback.
   */
  UUID Get() const { return attribute.GetUUIDValue(); }

  /**
   * Updates the UUID value stored in this attribute.
   */
  void Set(const UUID& value) { attribute.SetUUID(value.bytes.data()); }
};

struct StringAttribute : public TypedAttribute<ValueType::String> {
  /**
   * Default-initializes a string attribute to null.
   */
  StringAttribute() {}

  /**
   * Initializes a new StringAttribute from a std::string_view.
   */
  explicit StringAttribute(std::string_view value)
      : TypedAttribute(Attribute::String(value)) {}

  /**
   * Initializes a new StringAttribute from an optional std::string value.
   */
  explicit StringAttribute(const std::optional<std::string>& value)
      : TypedAttribute(value ? Attribute::String(*value) : Attribute()) {}

  /**
   * For convenience: returns a view of the string value currently stored in this
   * attribute, or empty string as a fallback.
   */
  std::string_view Get() const { return attribute.GetStringValue(); }
};

struct ArrayAttribute : public TypedAttribute<ValueType::Array> {
  explicit ArrayAttribute(size_t initial_capacity)
      : TypedAttribute(Attribute::Array(initial_capacity)) {};
};

struct ObjectAttribute : public TypedAttribute<ValueType::Object> {
  explicit ObjectAttribute(size_t initial_capacity)
      : TypedAttribute(Attribute::Object(initial_capacity)) {};
};

}  // namespace datadog::impl
