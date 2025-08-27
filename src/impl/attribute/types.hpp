#include "datadog/attribute.h"
#include "datadog/attribute.hpp"

#include "attribute/cow.hpp"

namespace datadog::impl {

inline ValueType ValueType_FromC(dd_value_type_t value)
{
    static_assert(static_cast<int>(ValueType::Null) == DD_VALUE_TYPE_NULL);
    static_assert(static_cast<int>(ValueType::Int) == DD_VALUE_TYPE_INT);
    static_assert(static_cast<int>(ValueType::UInt) == DD_VALUE_TYPE_UINT);
    static_assert(static_cast<int>(ValueType::Double) == DD_VALUE_TYPE_DOUBLE);
    static_assert(static_cast<int>(ValueType::String) == DD_VALUE_TYPE_STRING);
    static_assert(static_cast<int>(ValueType::Array) == DD_VALUE_TYPE_ARRAY);
    static_assert(static_cast<int>(ValueType::Object) == DD_VALUE_TYPE_OBJECT);
    return static_cast<ValueType>(value);
}

inline dd_value_type_t ValueType_ToC(ValueType value)
{
    static_assert(DD_VALUE_TYPE_NULL == static_cast<int>(ValueType::Null));
    static_assert(DD_VALUE_TYPE_INT == static_cast<int>(ValueType::Int));
    static_assert(DD_VALUE_TYPE_UINT == static_cast<int>(ValueType::UInt));
    static_assert(DD_VALUE_TYPE_DOUBLE == static_cast<int>(ValueType::Double));
    static_assert(DD_VALUE_TYPE_STRING == static_cast<int>(ValueType::String));
    static_assert(DD_VALUE_TYPE_ARRAY == static_cast<int>(ValueType::Array));
    static_assert(DD_VALUE_TYPE_OBJECT == static_cast<int>(ValueType::Object));
    return static_cast<dd_value_type_t>(value);
}

/**
 * Used within the implementation of the API layer to convert between dd_attribute_t and
 * datadog::Attribute, treated as a shallow copy that increments the underlying refcount
 * on any non-primitive value.
 *
 * Forward-declared as a friend of Attribute to allow access to private members.
 */
struct AttributeConversion
{
    static Attribute CopyFromC(const dd_attribute_t& attribute)
    {
        switch (attribute.type)
        {
            case DD_VALUE_TYPE_NULL:
                return Attribute(ValueType::Null, attribute.value.i64);
            case DD_VALUE_TYPE_BOOL:
                return Attribute(ValueType::Bool, attribute.value.i64);
            case DD_VALUE_TYPE_INT:
                return Attribute(ValueType::Int, attribute.value.i64);
            case DD_VALUE_TYPE_UINT:
                return Attribute(ValueType::UInt, attribute.value.u64);
            case DD_VALUE_TYPE_DOUBLE:
                return Attribute(ValueType::Double, attribute.value.f64);

            case DD_VALUE_TYPE_STRING:
            case DD_VALUE_TYPE_ARRAY:
            case DD_VALUE_TYPE_OBJECT:
            {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                auto* value = reinterpret_cast<impl::CowValue*>(attribute.value.ptr);
                value->AddRef();
                return Attribute(ValueType_FromC(attribute.type), value);
            }
        }
    }

    static dd_attribute_t CopyToC(const Attribute& cpp_attribute)
    {
        dd_attribute_t attribute;
        attribute.type = ValueType_ToC(cpp_attribute.type);
        switch (attribute.type)
        {
            case DD_VALUE_TYPE_NULL:
            case DD_VALUE_TYPE_BOOL:
            case DD_VALUE_TYPE_INT:
                attribute.value.i64 = cpp_attribute.value.i64;
                break;
            case DD_VALUE_TYPE_UINT:
                attribute.value.u64 = cpp_attribute.value.u64;
                break;
            case DD_VALUE_TYPE_DOUBLE:
                attribute.value.f64 = cpp_attribute.value.f64;
                break;

            case DD_VALUE_TYPE_STRING:
            case DD_VALUE_TYPE_ARRAY:
            case DD_VALUE_TYPE_OBJECT:
            {
                cpp_attribute.value.ptr->AddRef();
                attribute.value.ptr = cpp_attribute.value.ptr;
            }
            break;
        }
        return attribute;
    }
};

} // namespace datadog::impl
