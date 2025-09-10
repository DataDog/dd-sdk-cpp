#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "datadog/attribute.hpp"

namespace datadog::impl {

/**
 * Wrapper for an attribute value of a known type.
 *
 * Used within the implementation when we want to keep track of a long-lived value
 * that's stored as an attribute, while clearly documenting its expected type (and
 * guarding against accidental use of other value types).
 */
template<ValueType T>
struct TypedAttribute
{
    Attribute attribute;

    /**
     * Default-initializes a TypedAttribute to hold a null value.
     */
    TypedAttribute()
    {
    }

    /**
     * Initializes a new TypedAttribute wrapper to hold the given Attribute.
     */
    explicit TypedAttribute(const Attribute& in_attribute)
        : attribute(in_attribute)
    {
    }

    /**
     * Returns true if the attribute holds a value of the required type.
     */
    explicit operator bool() const
    {
        return attribute.GetType() == T;
    }

    /**
     * Returns the underlying Attribute value if its type matches the required type;
     * otherwise returns Attribute::Null().
     */
    Attribute operator*() const
    {
        if (attribute.GetType() == T)
        {
            return attribute;
        }
        return Attribute();
    }
};

struct StringAttribute : public TypedAttribute<ValueType::String>
{
    /**
     * Initializes a new StringAttribute from a std::string_view.
     */
    explicit StringAttribute(std::string_view value)
        : TypedAttribute(Attribute::String(value))
    {
    }

    /**
     * Initializes a new StringAttribute from an optional std::string value.
     */
    explicit StringAttribute(const std::optional<std::string>& value)
        : TypedAttribute(value ? Attribute::String(*value) : Attribute())
    {
    }
};

struct ArrayAttribute : public TypedAttribute<ValueType::Array>
{
    explicit ArrayAttribute(size_t initial_capacity)
        : TypedAttribute(Attribute::Array(initial_capacity)) {};
};

struct ObjectAttribute : public TypedAttribute<ValueType::Object>
{
    explicit ObjectAttribute(size_t initial_capacity)
        : TypedAttribute(Attribute::Object(initial_capacity)) {};
};

} // namespace datadog::impl
