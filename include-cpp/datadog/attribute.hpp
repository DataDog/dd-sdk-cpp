#pragma once

#include <cinttypes>
#include <string_view>

namespace datadog {

// Forward declarations
namespace impl {
struct CowValue;
struct AttributeConversion;
struct AttributeSerialization;
struct AttributeDebug;
}

/**
 * Type of value that may be stored in an Attribute.
 */
enum class ValueType : uint8_t
{
    Null,
    Bool,
    Int,
    UInt,
    Double,
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
class Attribute
{
    ValueType type;
    union
    {
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
    static Attribute Double(double value);
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
    void SetDouble(double new_value);
    void SetString(std::string_view new_value);
    void InitArray(size_t initial_capacity = 0);
    void InitObject(size_t initial_capacity = 0);

    /**
     * Returns the type of value held by this attribute.
     */
    ValueType GetType() const;

    /**
     * Returns the value held by this attribute, if and only if GetType() exactly
     * corresponds to the requested value type. If the attribute does not hold a value
     * of the requested type, returns a zero/empty value.
     */
    bool GetBoolValue() const;
    int64_t GetIntValue() const;
    uint64_t GetUIntValue() const;
    double GetDoubleValue() const;
    std::string_view GetStringValue() const;

    /**
     * Returns the number of items stored in this array attribute. If this attribute
     * does not hold a value of type Array, returns 0.
     */
    size_t GetArrayLen() const;
    Attribute GetArrayItem(int index) const;
    void ArrayClear();
    void ArrayPush(const Attribute& item);

    /**
     * Returns the number of properties stored in this object attribute. If this
     * attribute does not hold a value of type Object, returns 0.
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
     * Returns the value of the property with the given name. If this attribute is not
     * an object, or if no such property exists, returns Attribute::Null(). To test for
     * membership definitively, use FindObjectProperty().
     */
    Attribute GetObjectProperty(std::string_view name) const;
    /**
     * Adds the given value to the object, indexed under the given property name. If a
     * property already exists by that name, it will be overwritten with the given
     * value. If this attribute is not an object, this operation does nothing.
     */
    void SetObjectProperty(std::string_view name, const Attribute& attribute);
    /**
     * Removes the property with the given name from the object. If this attribute is
     * not an object, or if no such property exists, this operation does nothing.
     */
    void DeleteObjectProperty(std::string_view name);

public:
    /**
     * Given any number of object attributes, returns a new object attribute that
     * contains the union of all top-level properties found in the input objects.
     *
     * If two or more input objects contain properties with the same name, the value
     * that appears last in the input list will take precedence. For example, merging
     * `{"foo":1,"bar":1}` and `{"bar":"two"}` will result in `{"foo":1,"bar":"two"}`.
     *
     * No recursive merging or reconciliation on nested objects is performed: e.g
     * merging `{"obj":{"foo":1,"bar":2}}` with `{"obj":{"bar":3,"baz":4}}` will result
     * in `{"obj":{"bar":3,"baz":4}}`.
     *
     * If `attributes` contains any non-object values, they will be ignored. If
     * `attributes` contains no object values, the result will be an empty object.
     */
    static Attribute MergeObjects(std::initializer_list<Attribute> attributes);

private:
    /**
     * Used internally whenenver we mutate an underlying non-primitive value. If this
     * Attribute holds the only reference to that value, returns it directly. If this
     * Attribute shares it reference with other Attributes, releases that reference,
     * creates a shallow clone that can be independently modified, stores the clone as
     * our new value, and returns it.
     */
    impl::CowValue* GetCowValueForWrite();

    // Declare implementation-layer helpers as structs: these internal operations
    // require direct access to the Attribute's value, but they are not part of the
    // public interface.
    friend struct impl::AttributeConversion;
    friend struct impl::AttributeSerialization;
    friend struct impl::AttributeDebug;
};

} // namespace datadog
