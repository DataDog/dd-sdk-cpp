#include <catch2/catch_test_macros.hpp>

#include <cinttypes>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "attribute/json.hpp"
#include "attribute/types.hpp"
#include "datadog/attribute.h"

TEST_CASE("dd_attribute", "[unit][attribute][c-api]")
{
    auto require_array_ops_are_noop = [](dd_attribute_t& attribute) -> void
    {
        dd_attribute_array_clear(&attribute);

        dd_attribute_t array_item = dd_attribute_int(100);
        dd_attribute_array_push(&attribute, &array_item);
        dd_attribute_free(&array_item);

        REQUIRE(dd_attribute_array_len(&attribute) == 0);

        dd_attribute_t item_0 = dd_attribute_array_get(&attribute, 0);
        REQUIRE(item_0.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&item_0);
    };

    auto require_object_ops_are_noop = [](dd_attribute_t& attribute) -> void
    {
        dd_attribute_object_property_delete(&attribute, "foo");

        dd_attribute property_value = dd_attribute_int(100);
        dd_attribute_object_property_set(&attribute, "foo", &property_value);
        dd_attribute_free(&property_value);

        REQUIRE(dd_attribute_object_property_count(&attribute) == 0);
        REQUIRE(dd_attribute_object_property_find(&attribute, "foo") == -1);
        REQUIRE(std::string(dd_attribute_object_name_at(&attribute, 0)) == "");

        dd_attribute_t value_0 = dd_attribute_object_value_at(&attribute, 0);
        REQUIRE(value_0.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&value_0);

        dd_attribute_t value_foo = dd_attribute_object_property_get(&attribute, "foo");
        REQUIRE(value_foo.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&value_foo);
    };

    SECTION("M allow all operations W type is null")
    {
        // Given a null attribute value
        dd_attribute_t attribute = dd_attribute_null();
        REQUIRE(attribute.type == DD_VALUE_TYPE_NULL);

        // get_<type>() for all types returns zero; array/object funcs are no-op
        REQUIRE(dd_attribute_get_bool(&attribute) == false);
        REQUIRE(dd_attribute_get_int(&attribute) == 0ll);
        REQUIRE(dd_attribute_get_uint(&attribute) == 0ull);
        REQUIRE(dd_attribute_get_double(&attribute) == 0.0);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "");
        require_array_ops_are_noop(attribute);
        require_object_ops_are_noop(attribute);

        // dd_attribute_copy() creates another null value
        dd_attribute_t copy = dd_attribute_copy(&attribute);
        REQUIRE(copy.type == DD_VALUE_TYPE_NULL);

        // Cleanup
        dd_attribute_free(&copy);
        dd_attribute_free(&attribute);
    }

    SECTION("M allow all operations W type is bool")
    {
        // Given a boolean attribute value
        dd_attribute_t attribute = dd_attribute_bool(true);
        REQUIRE(attribute.type == DD_VALUE_TYPE_BOOL);

        // get_bool() returns value
        REQUIRE(dd_attribute_get_bool(&attribute) == true);

        // get_<type>() for other types returns zero; array/object funcs are no-op
        REQUIRE(dd_attribute_get_int(&attribute) == 0ll);
        REQUIRE(dd_attribute_get_uint(&attribute) == 0ull);
        REQUIRE(dd_attribute_get_double(&attribute) == 0.0);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "");
        require_array_ops_are_noop(attribute);
        require_object_ops_are_noop(attribute);

        // dd_attribute_copy() creates another bool attribute with the same value
        dd_attribute_t copy = dd_attribute_copy(&attribute);
        REQUIRE(copy.type == DD_VALUE_TYPE_BOOL);
        REQUIRE(dd_attribute_get_bool(&copy) == true);

        // set_bool() updates value; changing copy doesn't change original
        dd_attribute_set_bool(&copy, false);
        REQUIRE(dd_attribute_get_bool(&copy) == false);
        REQUIRE(dd_attribute_get_bool(&attribute) == true);

        // dd_attribute_free() must be called once for all dd_attribute_t values
        dd_attribute_free(&copy);
        dd_attribute_free(&attribute);
    }

    SECTION("M allow all operations W type is int")
    {
        // Given an int attribute value
        dd_attribute_t attribute = dd_attribute_int(-100);
        REQUIRE(attribute.type == DD_VALUE_TYPE_INT);

        // get_int() returns value
        REQUIRE(dd_attribute_get_int(&attribute) == -100);

        // get_<type>() for other types returns zero; array/object funcs are no-op
        REQUIRE(dd_attribute_get_bool(&attribute) == false);
        REQUIRE(dd_attribute_get_uint(&attribute) == 0ull);
        REQUIRE(dd_attribute_get_double(&attribute) == 0.0);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "");
        require_array_ops_are_noop(attribute);
        require_object_ops_are_noop(attribute);

        // dd_attribute_copy() creates another int attribute with the same value
        dd_attribute_t copy = dd_attribute_copy(&attribute);
        REQUIRE(copy.type == DD_VALUE_TYPE_INT);
        REQUIRE(dd_attribute_get_int(&copy) == -100);

        // set_int() updates value; changing copy doesn't change original
        dd_attribute_set_int(&copy, -99);
        REQUIRE(dd_attribute_get_int(&copy) == -99);
        REQUIRE(dd_attribute_get_int(&attribute) == -100);

        // dd_attribute_free() must be called once for all dd_attribute_t values
        dd_attribute_free(&copy);
        dd_attribute_free(&attribute);
    }

    SECTION("M allow all operations W type is uint")
    {
        // Given a uint attribute value
        dd_attribute_t attribute = dd_attribute_uint(100);
        REQUIRE(attribute.type == DD_VALUE_TYPE_UINT);

        // get_uint() returns value
        REQUIRE(dd_attribute_get_uint(&attribute) == 100);

        // get_<type>() for other types returns zero; array/object funcs are no-op
        REQUIRE(dd_attribute_get_bool(&attribute) == false);
        REQUIRE(dd_attribute_get_int(&attribute) == 0ll);
        REQUIRE(dd_attribute_get_double(&attribute) == 0.0);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "");
        require_array_ops_are_noop(attribute);
        require_object_ops_are_noop(attribute);

        // dd_attribute_copy() creates another uint attribute with the same value
        dd_attribute_t copy = dd_attribute_copy(&attribute);
        REQUIRE(copy.type == DD_VALUE_TYPE_UINT);
        REQUIRE(dd_attribute_get_uint(&copy) == 100);

        // set_uint() updates value; changing copy doesn't change original
        dd_attribute_set_uint(&copy, 99);
        REQUIRE(dd_attribute_get_uint(&copy) == 99);
        REQUIRE(dd_attribute_get_uint(&attribute) == 100);

        // dd_attribute_free() must be called once for all dd_attribute_t values
        dd_attribute_free(&copy);
        dd_attribute_free(&attribute);
    }

    SECTION("M allow all operations W type is double")
    {
        // Given a double attribute value
        dd_attribute_t attribute = dd_attribute_double(1.2345);
        REQUIRE(attribute.type == DD_VALUE_TYPE_DOUBLE);

        // get_double() returns value
        REQUIRE(dd_attribute_get_double(&attribute) == 1.2345);

        // get_<type>() for other types returns zero; array/object funcs are no-op
        REQUIRE(dd_attribute_get_bool(&attribute) == false);
        REQUIRE(dd_attribute_get_int(&attribute) == 0ll);
        REQUIRE(dd_attribute_get_uint(&attribute) == 0ull);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "");
        require_array_ops_are_noop(attribute);
        require_object_ops_are_noop(attribute);

        // dd_attribute_copy() creates another double attribute with the same value
        dd_attribute_t copy = dd_attribute_copy(&attribute);
        REQUIRE(copy.type == DD_VALUE_TYPE_DOUBLE);
        REQUIRE(dd_attribute_get_double(&copy) == 1.2345);

        // set_double() updates value; changing copy doesn't change original
        dd_attribute_set_double(&copy, -99.989);
        REQUIRE(dd_attribute_get_double(&copy) == -99.989);
        REQUIRE(dd_attribute_get_double(&attribute) == 1.2345);

        // dd_attribute_free() must be called once for all dd_attribute_t values
        dd_attribute_free(&copy);
        dd_attribute_free(&attribute);
    }

    SECTION("M allow all operations W type is string")
    {
        // Given a string attribute value
        dd_attribute_t attribute = dd_attribute_string("hello");
        REQUIRE(attribute.type == DD_VALUE_TYPE_STRING);

        // get_string() returns value
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "hello");

        // get_<type>() for other types returns zero; array/object funcs are no-op
        REQUIRE(dd_attribute_get_bool(&attribute) == false);
        REQUIRE(dd_attribute_get_int(&attribute) == 0ll);
        REQUIRE(dd_attribute_get_uint(&attribute) == 0ull);
        REQUIRE(dd_attribute_get_double(&attribute) == 0.0);
        require_array_ops_are_noop(attribute);
        require_object_ops_are_noop(attribute);

        // dd_attribute_copy() creates another string attribute with the same value
        dd_attribute_t copy = dd_attribute_copy(&attribute);
        REQUIRE(copy.type == DD_VALUE_TYPE_STRING);
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "hello");

        // set_string() updates value; changing copy doesn't change original
        dd_attribute_set_string(&copy, "world");
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "world");
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "hello");

        // dd_attribute_free() must be called once for all dd_attribute_t values
        dd_attribute_free(&copy);
        dd_attribute_free(&attribute);
    }

    SECTION("M allow all operations W type is array")
    {
        // Given an array attribute value
        dd_attribute_t attribute = dd_attribute_array(8);
        REQUIRE(attribute.type == DD_VALUE_TYPE_ARRAY);

        // push() adds new items into the array
        dd_attribute_t int_100 = dd_attribute_int(100);
        dd_attribute_array_push(&attribute, &int_100);
        dd_attribute_free(&int_100);

        // len() reports number of items
        REQUIRE(dd_attribute_array_len(&attribute) == 1);

        // get() returns a copy of the requested item
        dd_attribute_t item_0 = dd_attribute_array_get(&attribute, 0);
        REQUIRE(item_0.type == DD_VALUE_TYPE_INT);
        REQUIRE(dd_attribute_get_int(&item_0) == 100);
        dd_attribute_free(&item_0);

        // get() returns a null attribute if out of bounds
        dd_attribute_t item_1 = dd_attribute_array_get(&attribute, 1);
        REQUIRE(item_1.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&item_1);

        dd_attribute_t item_neg1 = dd_attribute_array_get(&attribute, -1);
        REQUIRE(item_neg1.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&item_neg1);

        // get_<type>() for all types returns zero; object funcs are no-op
        REQUIRE(dd_attribute_get_bool(&attribute) == false);
        REQUIRE(dd_attribute_get_int(&attribute) == 0ll);
        REQUIRE(dd_attribute_get_uint(&attribute) == 0ull);
        REQUIRE(dd_attribute_get_double(&attribute) == 0.0);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "");
        require_object_ops_are_noop(attribute);

        // dd_attribute_copy() creates another array attribute with the same values
        dd_attribute_t copy = dd_attribute_copy(&attribute);
        REQUIRE(dd_attribute_array_len(&copy) == 1);

        // push() on the copy doesn't affect the original
        dd_attribute_t int_200 = dd_attribute_int(200);
        dd_attribute_array_push(&copy, &int_200);
        dd_attribute_free(&int_200);
        REQUIRE(dd_attribute_array_len(&attribute) == 1);
        REQUIRE(dd_attribute_array_len(&copy) == 2);

        // clear() removes all items; clearing original doesn't affect copy
        dd_attribute_array_clear(&attribute);
        REQUIRE(dd_attribute_array_len(&attribute) == 0);
        REQUIRE(dd_attribute_array_len(&copy) == 2);

        // dd_attribute_init_array() also clears if called on an existing array
        dd_attribute_init_array(&copy, 16);
        REQUIRE(dd_attribute_array_len(&copy) == 0);

        // dd_attribute_free() must be called once for all dd_attribute_t values
        dd_attribute_free(&copy);
        dd_attribute_free(&attribute);
    }

    SECTION("M allow all operations W type is object")
    {
        // Given an object attribute value
        dd_attribute_t attribute = dd_attribute_object(8);
        REQUIRE(attribute.type == DD_VALUE_TYPE_OBJECT);

        // property_set() adds new properties to the object
        dd_attribute_t int_100 = dd_attribute_int(100);
        dd_attribute_t int_200 = dd_attribute_int(200);
        dd_attribute_object_property_set(&attribute, "foo", &int_100);
        dd_attribute_object_property_set(&attribute, "bar", &int_200);
        dd_attribute_free(&int_100);
        dd_attribute_free(&int_200);

        // property_set() overwrites existing value on name conflict
        dd_attribute_t int_300 = dd_attribute_int(300);
        dd_attribute_object_property_set(&attribute, "foo", &int_300);
        dd_attribute_free(&int_300);

        // property_delete() removes existing properties
        dd_attribute_object_property_delete(&attribute, "bar");

        // property_delete() is a no-op if property does not exist
        dd_attribute_object_property_delete(&attribute, "nothing");

        // get_<type>() for all types returns zero; array funcs are no-op
        REQUIRE(dd_attribute_get_bool(&attribute) == false);
        REQUIRE(dd_attribute_get_int(&attribute) == 0ll);
        REQUIRE(dd_attribute_get_uint(&attribute) == 0ull);
        REQUIRE(dd_attribute_get_double(&attribute) == 0.0);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "");
        require_array_ops_are_noop(attribute);

        // property_count() reports number of properties
        REQUIRE(dd_attribute_object_property_count(&attribute) == 1);

        // property_find() returns index of property matching name, or -1 if not found
        REQUIRE(dd_attribute_object_property_find(&attribute, "foo") == 0);
        REQUIRE(dd_attribute_object_property_find(&attribute, "bar") == -1);

        // name_at() returns name of property stored at index, or "" if out of bounds
        REQUIRE(std::string(dd_attribute_object_name_at(&attribute, 0)) == "foo");
        REQUIRE(std::string(dd_attribute_object_name_at(&attribute, 1)) == "");
        REQUIRE(std::string(dd_attribute_object_name_at(&attribute, -1)) == "");

        // value_at() returns attribute value stored at index
        dd_attribute_t value_0 = dd_attribute_object_value_at(&attribute, 0);
        REQUIRE(value_0.type == DD_VALUE_TYPE_INT);
        REQUIRE(dd_attribute_get_int(&value_0) == 300);
        dd_attribute_free(&value_0);

        // value_at() returns dd_attribute_null() if index is out of bounds
        dd_attribute_t value_1 = dd_attribute_object_value_at(&attribute, 1);
        REQUIRE(value_1.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&value_1);

        dd_attribute_t value_neg1 = dd_attribute_object_value_at(&attribute, -1);
        REQUIRE(value_neg1.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&value_neg1);

        // property_get() returns attribute value associated with name
        dd_attribute_t value_foo = dd_attribute_object_property_get(&attribute, "foo");
        REQUIRE(value_foo.type == DD_VALUE_TYPE_INT);
        REQUIRE(dd_attribute_get_int(&value_foo) == 300);
        dd_attribute_free(&value_foo);

        // property_get() returns dd_attribute_null() if no such property exists
        dd_attribute_t value_bar = dd_attribute_object_property_get(&attribute, "bar");
        REQUIRE(value_bar.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&value_bar);

        // dd_attribute_copy() creates another object with the same values
        dd_attribute_t copy = dd_attribute_copy(&attribute);
        REQUIRE(dd_attribute_object_property_count(&copy) == 1);

        // property_set() on copy doesn't affect original
        dd_attribute_t int_400 = dd_attribute_int(400);
        dd_attribute_object_property_set(&copy, "baz", &int_400);
        dd_attribute_free(&int_400);
        REQUIRE(dd_attribute_object_property_count(&attribute) == 1);
        REQUIRE(dd_attribute_object_property_count(&copy) == 2);

        // property_delete() on original doesn't affect copy
        dd_attribute_object_property_delete(&attribute, "foo");
        REQUIRE(dd_attribute_object_property_count(&attribute) == 0);
        REQUIRE(dd_attribute_object_property_count(&copy) == 2);

        // dd_attribute_init_object() deletes all properties if called on an object
        dd_attribute_init_object(&copy, 16);
        REQUIRE(dd_attribute_object_property_count(&copy) == 0);

        // dd_attribute_free() must be called once for all dd_attribute_t values
        dd_attribute_free(&copy);
        dd_attribute_free(&attribute);
    }

    SECTION("M safely ignore all operations W target attribute is NULL")
    {
        // Given a valid attribute value to pass as an array item / object property
        dd_attribute_t valid = dd_attribute_string("I am a valid argument");

        // dd_attribute_set_<type> are all safe no-ops when called without an attribute
        dd_attribute_set_null(nullptr);
        dd_attribute_set_bool(nullptr, true);
        dd_attribute_set_int(nullptr, -1);
        dd_attribute_set_uint(nullptr, 1);
        dd_attribute_set_double(nullptr, 1.0);
        dd_attribute_set_string(nullptr, "hello");
        dd_attribute_init_array(nullptr, 16);
        dd_attribute_init_object(nullptr, 16);

        // dd_attribute_copy() returns dd_attribute_null if called without a source
        dd_attribute_t copy_of_nothing = dd_attribute_copy(nullptr);
        REQUIRE(copy_of_nothing.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&copy_of_nothing);

        // dd_attribute_free() is a no-op
        dd_attribute_free(nullptr);

        // dd_attribute_get_<type> all return zero/default values
        REQUIRE(dd_attribute_get_bool(nullptr) == false);
        REQUIRE(dd_attribute_get_int(nullptr) == 0ll);
        REQUIRE(dd_attribute_get_uint(nullptr) == 0ull);
        REQUIRE(dd_attribute_get_double(nullptr) == 0.0);
        REQUIRE(std::string(dd_attribute_get_string(nullptr)) == "");

        // Const dd_attribute_array operations return 0/null
        REQUIRE(dd_attribute_array_len(nullptr) == 0);

        dd_attribute_t item_from_nowhere = dd_attribute_array_get(nullptr, 0);
        REQUIRE(item_from_nowhere.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&item_from_nowhere);

        // Mutable dd_attribute_array operations are ignored
        dd_attribute_array_clear(nullptr);
        dd_attribute_array_push(nullptr, &valid);

        // Const dd_attribute_object operations return 0/-1/null
        REQUIRE(dd_attribute_object_property_count(nullptr) == 0);
        REQUIRE(dd_attribute_object_property_find(nullptr, "valid-name") == -1);
        REQUIRE(std::string(dd_attribute_object_name_at(nullptr, 0)) == "");

        dd_attribute_t head_of_nothing = dd_attribute_object_value_at(nullptr, 0);
        REQUIRE(head_of_nothing.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&head_of_nothing);

        dd_attribute_t nobody = dd_attribute_object_property_get(nullptr, "valid-name");
        REQUIRE(nobody.type == DD_VALUE_TYPE_NULL);
        dd_attribute_free(&nobody);

        // Mutable dd_attribute_object operations are ignored
        dd_attribute_object_property_set(nullptr, "valid-name", &valid);
        dd_attribute_object_property_delete(nullptr, "valid-name");

        // Cleanup
        dd_attribute_free(&valid);
    }

    SECTION("M reject set_string() W value is NULL")
    {
        // Given a string attribute
        dd_attribute_t attribute = dd_attribute_string("hello");

        // When we call set_string() but pass a NULL value
        dd_attribute_set_string(&attribute, nullptr);

        // Then the operation is ignored
        REQUIRE(attribute.type == DD_VALUE_TYPE_STRING);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "hello");

        // But passing "" is valid and clears the string
        dd_attribute_set_string(&attribute, "");
        REQUIRE(attribute.type == DD_VALUE_TYPE_STRING);
        REQUIRE(std::string(dd_attribute_get_string(&attribute)) == "");

        // And: Given an attribute of a non-string type
        dd_attribute_t other = dd_attribute_int(100);

        // Then calling set_string() with a NULL value does not modify the existing
        // attribute at all; it rejects the operation outright
        dd_attribute_set_string(&other, nullptr);
        REQUIRE(other.type == DD_VALUE_TYPE_INT);
        REQUIRE(dd_attribute_get_int(&other) == 100);

        // But passing "" is valid and changes the type and value of the attribute
        dd_attribute_set_string(&other, "");
        REQUIRE(other.type == DD_VALUE_TYPE_STRING);
        REQUIRE(std::string(dd_attribute_get_string(&other)) == "");

        // Cleanup
        dd_attribute_free(&other);
        dd_attribute_free(&attribute);
    }

    SECTION("M reject array_push() W item is NULL")
    {
        // Given an array attribute
        dd_attribute_t attribute = dd_attribute_array(4);

        // When we call push() but pass a NULL value
        dd_attribute_array_push(&attribute, nullptr);

        // Then the operation is ignored
        REQUIRE(dd_attribute_array_len(&attribute) == 0);

        // Cleanup
        dd_attribute_free(&attribute);
    }

    SECTION("M reject object_property_set() W name is NULL")
    {
        // Given an object attribute
        dd_attribute_t attribute = dd_attribute_object(4);

        // When we attempt to set a property without providing a valid name
        dd_attribute_t int_100 = dd_attribute_int(100);
        dd_attribute_object_property_set(&attribute, nullptr, &int_100);
        dd_attribute_free(&int_100);

        // Then the operation is ignored
        REQUIRE(dd_attribute_object_property_count(&attribute) == 0);

        // Cleanup
        dd_attribute_free(&attribute);
    }

    SECTION("M accept object_property_set() W name is empty string")
    {
        // Given an object attribute
        dd_attribute_t attribute = dd_attribute_object(4);

        // When we attempt to set a property with an empty name, which is acceptable
        // since we accept any name that would be a valid JSON property name
        dd_attribute_t int_100 = dd_attribute_int(100);
        dd_attribute_object_property_set(&attribute, "", &int_100);
        dd_attribute_free(&int_100);

        // Then the operation is successful
        REQUIRE(dd_attribute_object_property_count(&attribute) == 1);

        // Cleanup
        dd_attribute_free(&attribute);
    }

    SECTION("M reject object_property_delete() W name is NULL")
    {
        // Given an object attribute with an empty-string property name
        dd_attribute_t attribute = dd_attribute_object(4);
        dd_attribute_t int_100 = dd_attribute_int(100);
        dd_attribute_object_property_set(&attribute, "", &int_100);
        dd_attribute_free(&int_100);

        // When we attempt to delete a property, passing NULL as the target name
        dd_attribute_object_property_delete(&attribute, NULL);

        // Then the operation is ignored
        REQUIRE(dd_attribute_object_property_count(&attribute) == 1);

        // Cleanup
        dd_attribute_free(&attribute);
    }

    SECTION("M ignore object_property_find() W called with NULL name")
    {
        // Given an object attribute with an empty-string property name
        dd_attribute_t attribute = dd_attribute_object(4);
        dd_attribute_t int_100 = dd_attribute_int(100);
        dd_attribute_object_property_set(&attribute, "", &int_100);
        dd_attribute_free(&int_100);

        // When we call find(), passing a NULL property name
        int index = dd_attribute_object_property_find(&attribute, nullptr);

        // Then the operation returns -1
        REQUIRE(index == -1);

        // And finding our empty-string property works fine
        REQUIRE(dd_attribute_object_property_find(&attribute, "") == 0);

        // Cleanup
        dd_attribute_free(&attribute);
    }

    SECTION("M prevent massive allocation W initial array capacity is huge")
    {
        // Given a size_t value initialized from -1
        const size_t initial_capacity = static_cast<size_t>(-1);
        REQUIRE(initial_capacity == std::numeric_limits<size_t>::max());

        // When we attempt to create an array with that initial capacity
        dd_attribute_t attribute = dd_attribute_array(initial_capacity);

        // Then we refrain from allocating SIZE_MAX * sizeof(Attribute) bytes, which
        // would surely fail
        REQUIRE(dd_attribute_array_len(&attribute) == 0); // Still works

        // And the same clamping behavior is employed when we reinitialize our array
        dd_attribute_init_array(&attribute, initial_capacity);
        REQUIRE(dd_attribute_array_len(&attribute) == 0);

        // And also when we change an existing value's type to array
        dd_attribute other = dd_attribute_string("so long, it's been good to know you");
        dd_attribute_init_array(&other, initial_capacity);
        REQUIRE(dd_attribute_array_len(&other) == 0);

        // Cleanup
        dd_attribute_free(&other);
        dd_attribute_free(&attribute);
    }

    SECTION("M prevent massive allocation W initial object capacity is huge")
    {
        // Given a size_t value initialized from -1
        const size_t initial_capacity = static_cast<size_t>(-1);
        REQUIRE(initial_capacity == std::numeric_limits<size_t>::max());

        // When we attempt to create an object with that initial capacity
        dd_attribute_t attribute = dd_attribute_object(initial_capacity);

        // Then we refrain from allocating SIZE_MAX * sizeof(pair<string, Attribute>)
        // bytes, which would surely fail
        REQUIRE(dd_attribute_object_property_count(&attribute) == 0); // Still works

        // And the same clamping behavior is employed when we reinitialize our object
        dd_attribute_init_object(&attribute, initial_capacity);
        REQUIRE(dd_attribute_object_property_count(&attribute) == 0);

        // And also when we change an existing value's type to object
        dd_attribute other = dd_attribute_string("so long, it's been good to know you");
        dd_attribute_init_object(&other, initial_capacity);
        REQUIRE(dd_attribute_object_property_count(&other) == 0);

        // Cleanup
        dd_attribute_free(&other);
        dd_attribute_free(&attribute);
    }

    SECTION("M increment reference count W attribute is copied")
    {
        // Given two attributes referencing the same string value
        dd_attribute_t attribute = dd_attribute_string("hello");
        dd_attribute_t copy = dd_attribute_copy(&attribute);

        // When we free the original attribute
        dd_attribute_free(&attribute);

        // Then we can still access the copy
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "hello");
        dd_attribute_set_string(&copy, "world");
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "world");

        // Cleanup
        dd_attribute_free(&copy);
    }

    SECTION("M increment reference count W array item is copied on access")
    {
        // Given an array attribute containing a string item
        dd_attribute_t array = dd_attribute_array(4);
        dd_attribute_t hello = dd_attribute_string("hello");
        dd_attribute_array_push(&array, &hello);
        dd_attribute_free(&hello);

        // When we access the array and get a copy of our item
        dd_attribute_t copy = dd_attribute_array_get(&array, 0);

        // And we clear the array, releasing its reference to the string value
        dd_attribute_array_clear(&array);
        REQUIRE(dd_attribute_array_len(&array) == 0);

        // Then we can still access the copy
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "hello");
        dd_attribute_set_string(&copy, "world");
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "world");

        // Cleanup
        dd_attribute_free(&copy);
        dd_attribute_free(&array);
    }

    SECTION("M increment reference count W object property value is copied on get")
    {
        // Given an object attribute containing a string property
        dd_attribute_t obj = dd_attribute_object(4);
        dd_attribute_t hello = dd_attribute_string("hello");
        dd_attribute_object_property_set(&obj, "foo", &hello);
        dd_attribute_free(&hello);

        // When we access the object and get a copy of our property value
        dd_attribute_t copy = dd_attribute_object_property_get(&obj, "foo");

        // And we delete the property from the object, releasing its reference to the
        // string value
        dd_attribute_object_property_delete(&obj, "foo");
        REQUIRE(dd_attribute_object_property_count(&obj) == 0);

        // Then we can still access the copy
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "hello");
        dd_attribute_set_string(&copy, "world");
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "world");

        // Cleanup
        dd_attribute_free(&copy);
        dd_attribute_free(&obj);
    }

    SECTION("M increment reference count W object property value is copied on value_at")
    {
        // Given an object attribute containing a string property
        dd_attribute_t obj = dd_attribute_object(4);
        dd_attribute_t hello = dd_attribute_string("hello");
        dd_attribute_object_property_set(&obj, "foo", &hello);
        dd_attribute_free(&hello);

        // When we access the object and get a copy of our property value
        dd_attribute_t copy = dd_attribute_object_value_at(&obj, 0);

        // And we delete the property from the object, releasing its reference to the
        // string value
        dd_attribute_object_property_delete(&obj, "foo");
        REQUIRE(dd_attribute_object_property_count(&obj) == 0);

        // Then we can still access the copy
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "hello");
        dd_attribute_set_string(&copy, "world");
        REQUIRE(std::string(dd_attribute_get_string(&copy)) == "world");

        // Cleanup
        dd_attribute_free(&copy);
        dd_attribute_free(&obj);
    }

    SECTION("M release values W non-empty array is freed")
    {
        // Given an array attribute containing a string item
        dd_attribute_t array = dd_attribute_array(4);
        dd_attribute_t hello = dd_attribute_string("hello");
        dd_attribute_array_push(&array, &hello);
        dd_attribute_free(&hello);

        // When we free the array without calling clear
        dd_attribute_free(&array);

        // Then Attribute values contained in the array are implicitly released, and no
        // memory leaks occur
    }

    SECTION("M release values W non-empty object is freed")
    {
        // Given an object attribute containing a string property
        dd_attribute_t obj = dd_attribute_object(4);
        dd_attribute_t hello = dd_attribute_string("hello");
        dd_attribute_object_property_set(&obj, "foo", &hello);
        dd_attribute_free(&hello);

        // When we free the object while it contains properties
        dd_attribute_free(&obj);

        // Then Attribute values contained in the object as properties are implicitly
        // released, and no memory leaks occur
    }

    SECTION("M convert between data types W set functions are called")
    {
        // Given a description, for every type, of how we:
        // - create a new value of that type, initialized with a known value
        // - reinitialize an existing value to that type, with the same value
        // - verify that a value of that type has the expected value
        struct TypeTest
        {
            std::string_view name;
            std::function<dd_attribute_t()> init_func;
            std::function<void(dd_attribute_t*)> set_func;
            std::function<void(const dd_attribute_t*)> check_func;
        };
        std::vector<TypeTest> types = {
            {"null",
             dd_attribute_null,
             dd_attribute_set_null,
             [](const dd_attribute_t* attr)
             {
                 REQUIRE(attr->type == DD_VALUE_TYPE_NULL);
                 REQUIRE(dd_attribute_get_int(attr) == 0);
             }},

            {"bool",
             []() { return dd_attribute_bool(true); },
             [](dd_attribute_t* attr) { dd_attribute_set_bool(attr, true); },
             [](const dd_attribute_t* attr)
             {
                 REQUIRE(attr->type == DD_VALUE_TYPE_BOOL);
                 REQUIRE(dd_attribute_get_bool(attr) == true);
             }},

            {"int",
             []() { return dd_attribute_int(-100); },
             [](dd_attribute_t* attr) { dd_attribute_set_int(attr, -100); },
             [](const dd_attribute_t* attr)
             {
                 REQUIRE(attr->type == DD_VALUE_TYPE_INT);
                 REQUIRE(dd_attribute_get_int(attr) == -100);
             }},

            {"uint",
             []() { return dd_attribute_uint(999); },
             [](dd_attribute_t* attr) { dd_attribute_set_uint(attr, 999); },
             [](const dd_attribute_t* attr)
             {
                 REQUIRE(attr->type == DD_VALUE_TYPE_UINT);
                 REQUIRE(dd_attribute_get_uint(attr) == 999);
             }},

            {"double",
             []() { return dd_attribute_double(5.6789); },
             [](dd_attribute_t* attr) { dd_attribute_set_double(attr, 5.6789); },
             [](const dd_attribute_t* attr)
             {
                 REQUIRE(attr->type == DD_VALUE_TYPE_DOUBLE);
                 REQUIRE(dd_attribute_get_double(attr) == 5.6789);
             }},

            {"string",
             []() { return dd_attribute_string("hello"); },
             [](dd_attribute_t* attr) { dd_attribute_set_string(attr, "hello"); },
             [](const dd_attribute_t* attr)
             {
                 REQUIRE(attr->type == DD_VALUE_TYPE_STRING);
                 REQUIRE(std::string(dd_attribute_get_string(attr)) == "hello");
             }},

            {"array",
             []()
             {
                 dd_attribute_t array = dd_attribute_array(4);
                 dd_attribute_t int_100 = dd_attribute_int(100);
                 dd_attribute_t string_hi = dd_attribute_string("hi");
                 dd_attribute_array_push(&array, &int_100);
                 dd_attribute_array_push(&array, &string_hi);
                 dd_attribute_free(&int_100);
                 dd_attribute_free(&string_hi);
                 return array; // No free; transferring ownership to caller
             },
             [](dd_attribute_t* attr)
             {
                 dd_attribute_init_array(attr, 4);
                 dd_attribute_t int_100 = dd_attribute_int(100);
                 dd_attribute_t string_hi = dd_attribute_string("hi");
                 dd_attribute_array_push(attr, &int_100);
                 dd_attribute_array_push(attr, &string_hi);
                 dd_attribute_free(&int_100);
                 dd_attribute_free(&string_hi);
             },
             [](const dd_attribute_t* attr)
             {
                 REQUIRE(attr->type == DD_VALUE_TYPE_ARRAY);
                 REQUIRE(dd_attribute_array_len(attr) == 2);
                 dd_attribute_t item_0 = dd_attribute_array_get(attr, 0);
                 dd_attribute_t item_1 = dd_attribute_array_get(attr, 1);
                 REQUIRE(item_0.type == DD_VALUE_TYPE_INT);
                 REQUIRE(dd_attribute_get_int(&item_0) == 100);
                 REQUIRE(item_1.type == DD_VALUE_TYPE_STRING);
                 REQUIRE(std::string(dd_attribute_get_string(&item_1)) == "hi");
                 dd_attribute_free(&item_0);
                 dd_attribute_free(&item_1);
             }},

            {"object",
             []()
             {
                 dd_attribute_t obj = dd_attribute_object(4);
                 dd_attribute_t int_100 = dd_attribute_int(100);
                 dd_attribute_t string_hi = dd_attribute_string("hi");
                 dd_attribute_object_property_set(&obj, "foo", &int_100);
                 dd_attribute_object_property_set(&obj, "bar", &string_hi);
                 dd_attribute_free(&int_100);
                 dd_attribute_free(&string_hi);
                 return obj; // No free; transferring ownership to caller
             },
             [](dd_attribute_t* attr)
             {
                 dd_attribute_init_object(attr, 4);
                 dd_attribute_t int_100 = dd_attribute_int(100);
                 dd_attribute_t string_hi = dd_attribute_string("hi");
                 dd_attribute_object_property_set(attr, "foo", &int_100);
                 dd_attribute_object_property_set(attr, "bar", &string_hi);
                 dd_attribute_free(&int_100);
                 dd_attribute_free(&string_hi);
             },
             [](const dd_attribute_t* attr)
             {
                 REQUIRE(attr->type == DD_VALUE_TYPE_OBJECT);
                 REQUIRE(dd_attribute_object_property_count(attr) == 2);
                 dd_attribute_t value_0 = dd_attribute_object_value_at(attr, 0);
                 dd_attribute_t value_foo =
                     dd_attribute_object_property_get(attr, "foo");
                 dd_attribute_t value_1 = dd_attribute_object_value_at(attr, 1);
                 dd_attribute_t value_bar =
                     dd_attribute_object_property_get(attr, "bar");
                 REQUIRE(value_0.type == DD_VALUE_TYPE_INT);
                 REQUIRE(value_foo.type == DD_VALUE_TYPE_INT);
                 REQUIRE(dd_attribute_get_int(&value_0) == 100);
                 REQUIRE(dd_attribute_get_int(&value_foo) == 100);
                 REQUIRE(value_1.type == DD_VALUE_TYPE_STRING);
                 REQUIRE(value_bar.type == DD_VALUE_TYPE_STRING);
                 REQUIRE(std::string(dd_attribute_get_string(&value_1)) == "hi");
                 REQUIRE(std::string(dd_attribute_get_string(&value_bar)) == "hi");
                 dd_attribute_free(&value_0);
                 dd_attribute_free(&value_1);
                 dd_attribute_free(&value_foo);
                 dd_attribute_free(&value_bar);
             }},
        };

        // We should be able to create a value of any type, then convert it to a value
        // of any type (including itself), and get the expected value, across all N^2
        // permutations
        for (const auto& src_type : types)
        {
            for (const auto& dst_type : types)
            {
                DYNAMIC_SECTION(" {" << src_type.name << " -> " << dst_type.name << "}")
                {
                    // Given a attribute created as the source type
                    dd_attribute attribute = src_type.init_func();
                    src_type.check_func(&attribute);

                    // When we change it to a value of the destination type
                    dst_type.set_func(&attribute);

                    // Then it adopts the expected type and value
                    dst_type.check_func(&attribute);

                    // Cleanup
                    dd_attribute_free(&attribute);
                }
            }
        }
    }

    SECTION("M create a clone W array is pushed onto itself")
    {
        // Given an array `[1]`
        dd_attribute_t array = dd_attribute_array(2);
        dd_attribute_t one = dd_attribute_int(1);
        dd_attribute_array_push(&array, &one);
        dd_attribute_free(&one);

        // When we attempt to create `[1,[1]]`, passing the same `array` pointer
        dd_attribute_array_push(&array, &array);

        // Then we end up with our desired value, rather than crashing, because the
        // dd_attribute API implicitly clones the operand in order to convert it to a
        // C++ Attribute value
        REQUIRE(array.type == DD_VALUE_TYPE_ARRAY);
        REQUIRE(dd_attribute_array_len(&array) == 2);

        dd_attribute item_0 = dd_attribute_array_get(&array, 0);
        REQUIRE(dd_attribute_get_int(&item_0) == 1);
        dd_attribute_free(&item_0);

        dd_attribute item_1 = dd_attribute_array_get(&array, 1);
        REQUIRE(dd_attribute_array_len(&item_1) == 1);
        dd_attribute item_1_item_0 = dd_attribute_array_get(&item_1, 0);
        REQUIRE(dd_attribute_get_int(&item_1_item_0) == 1);
        dd_attribute_free(&item_1_item_0);
        dd_attribute_free(&item_1);

        // Cleanup
        dd_attribute_free(&array);
    }

    SECTION("M create a clone W object is set as property on itself")
    {
        // Given an object `{"foo":1}`
        dd_attribute_t obj = dd_attribute_object(2);
        dd_attribute_t one = dd_attribute_int(1);
        dd_attribute_object_property_set(&obj, "foo", &one);
        dd_attribute_free(&one);

        // When we attempt to create `{"foo":1,"bar":{"foo":1}}`, passing the same `obj`
        // pointer
        dd_attribute_object_property_set(&obj, "bar", &obj);

        // Then we end up with our desired value, rather than crashing, because the
        // dd_attribute API implicitly clones the operand in order to convert it to a
        // C++ Attribute value
        REQUIRE(obj.type == DD_VALUE_TYPE_OBJECT);
        REQUIRE(dd_attribute_object_property_count(&obj) == 2);

        dd_attribute foo = dd_attribute_object_property_get(&obj, "foo");
        REQUIRE(dd_attribute_get_int(&foo) == 1);
        dd_attribute_free(&foo);

        dd_attribute bar = dd_attribute_object_property_get(&obj, "bar");
        REQUIRE(dd_attribute_object_property_count(&bar) == 1);
        dd_attribute bar_foo = dd_attribute_object_property_get(&bar, "foo");
        REQUIRE(dd_attribute_get_int(&bar_foo) == 1);
        dd_attribute_free(&bar_foo);
        dd_attribute_free(&bar);

        // Cleanup
        dd_attribute_free(&obj);
    }

    SECTION("M not leak memory W primitive values are used without being freed")
    {
        // NOTE: This test documents an implementation detail: it essentially verifies
        // that misuse of the API is technically safe for a certain subset of values.
        // You should always call dd_attribute_free() as required by the API, regardless
        // of the type of the underlying value.

        // Given an array: which we must always free
        dd_attribute_t array = dd_attribute_array(8);

        // And a series of primitive (i.e. non-string, non-compound) values: which
        // should be freed when we're done with them, even though they don't technically
        // hold heap memory, because that's an implementation detail, and because just
        // freeing everything is much more consistent and foolproof as an API design
        // choice
        dd_attribute_t temp_null = dd_attribute_null();
        dd_attribute_t temp_bool = dd_attribute_bool(true);
        dd_attribute_t temp_int = dd_attribute_int(-1);
        dd_attribute_t temp_uint = dd_attribute_uint(1);
        dd_attribute_t temp_double = dd_attribute_double(1.1);

        // When we push our values into the array
        dd_attribute_array_push(&array, &temp_null);
        dd_attribute_array_push(&array, &temp_bool);
        dd_attribute_array_push(&array, &temp_int);
        dd_attribute_array_push(&array, &temp_uint);
        dd_attribute_array_push(&array, &temp_double);

        // And we neglect to free the temporary values

        // And we access the items held in our array, which creates more copies
        REQUIRE(dd_attribute_array_get(&array, 0).type == DD_VALUE_TYPE_NULL);
        REQUIRE(dd_attribute_array_get(&array, 1).type == DD_VALUE_TYPE_BOOL);
        REQUIRE(dd_attribute_array_get(&array, 2).type == DD_VALUE_TYPE_INT);
        REQUIRE(dd_attribute_array_get(&array, 3).type == DD_VALUE_TYPE_UINT);
        REQUIRE(dd_attribute_array_get(&array, 4).type == DD_VALUE_TYPE_DOUBLE);

        // And we neglect to store those result values and free them when done

        // Then we don't get any memory leaks, even though this is unsupported behavior,
        // because these single-word primitive values are stored on the stack

        // Whereas for strings, arrays, and objects, this usage pattern would invariably
        // cause memory leaks, so calling dd_attribute_free() is an absolute requirement
        dd_attribute_t temp_string = dd_attribute_string("hello");
        dd_attribute_array_push(&array, &temp_string);
        dd_attribute_free(&temp_string);

        dd_attribute_t item_5 = dd_attribute_array_get(&array, 5);
        REQUIRE(item_5.type == DD_VALUE_TYPE_STRING);
        REQUIRE(std::string(dd_attribute_get_string(&item_5)) == "hello");
        dd_attribute_free(&item_5);

        // And we must, of course, free the array
        dd_attribute_free(&array);
    }

    SECTION("M clear attribute to null W freed")
    {
        // NOTE: This test documents an implementation detail: it verifies that
        // dd_attribute_free() clears the state of the dd_attribute_t passed to it,
        // effectively resetting it to a dd_attribute_null() value. This is primarily a
        // defensive choice to make double-frees harmless. While it's technically
        // possible for the resulting `dd_attribute_t` value to be reused after being
        // freed, the API does not make any guarantees to that effect.

        // Given an attribute
        dd_attribute_t attribute = dd_attribute_string("hello");

        // When we call dd_attribute_free
        dd_attribute_free(&attribute);

        // Then the dd_attribute_t struct is cleared
        REQUIRE(attribute.type == DD_VALUE_TYPE_NULL);

        // Such that freeing it again is a no-op
        dd_attribute_free(&attribute);
    }
}

TEST_CASE("dd_attribute JSON example", "[unit][attribute][c-api]")
{
    // This test validates the integration of multiple units of functionality: more
    // exhaustive JSON serialization tests can be found in the implementation layer's
    // `json_test.cpp`.

    // Essentially, this test validates that we can build a complex object using
    // dd_attribute_t, and that it will be faithfully converted to a C++ Attribute and
    // properly serialized to JSON.

    // We want to build this JSON object:
    /*
        {
            "process": {
                "pid": 9238451,
                "name": "my-cool-program",
                "args": ["--mode", "good"]
            },
            "state": {
                "rect": [[0.03333, -12.3], [94, 98.7001]],
                "state": [{}],
                "offset": -1,
                "active": true
            },
            "tags": ["blue", "meh", null]
        }
    */

    // Copying our desired object and running `pbpaste | jq -c | pbcopy` gives us:
    const std::string want_json =
        R"({"process":{"pid":9238451,"name":"my-cool-program","args":["--mode","good"]},"state":{"rect":[[0.03333,-12.3],[94,98.7001]],"state":[{}],"offset":-1,"active":true},"tags":["blue","meh",null]})";

    // Given the dd_attribute calls required to construct that object
    dd_attribute_t obj = dd_attribute_object(3);
    {
        dd_attribute_t process = dd_attribute_object(3);
        {
            dd_attribute_t pid = dd_attribute_uint(9238451);
            dd_attribute_t name = dd_attribute_string("my-cool-program");
            dd_attribute_t args = dd_attribute_array(2);
            {
                dd_attribute_t arg_0 = dd_attribute_string("--mode");
                dd_attribute_t arg_1 = dd_attribute_string("good");
                dd_attribute_array_push(&args, &arg_0);
                dd_attribute_array_push(&args, &arg_1);
                dd_attribute_free(&arg_0);
                dd_attribute_free(&arg_1);
            }
            dd_attribute_object_property_set(&process, "pid", &pid);
            dd_attribute_object_property_set(&process, "name", &name);
            dd_attribute_object_property_set(&process, "args", &args);
            dd_attribute_free(&pid);
            dd_attribute_free(&name);
            dd_attribute_free(&args);
        }
        dd_attribute_object_property_set(&obj, "process", &process);
        dd_attribute_free(&process);

        dd_attribute_t state = dd_attribute_object(4);
        {
            dd_attribute_t rect = dd_attribute_array(2);
            {
                dd_attribute_t coord_0 = dd_attribute_array(2);
                dd_attribute_t coord_0_x = dd_attribute_double(0.03333);
                dd_attribute_t coord_0_y = dd_attribute_double(-12.3);
                dd_attribute_array_push(&coord_0, &coord_0_x);
                dd_attribute_array_push(&coord_0, &coord_0_y);
                dd_attribute_free(&coord_0_x);
                dd_attribute_free(&coord_0_y);

                dd_attribute_t coord_1 = dd_attribute_array(2);
                dd_attribute_t coord_1_x = dd_attribute_double(94.0);
                dd_attribute_t coord_1_y = dd_attribute_double(98.7001);
                dd_attribute_array_push(&coord_1, &coord_1_x);
                dd_attribute_array_push(&coord_1, &coord_1_y);
                dd_attribute_free(&coord_1_x);
                dd_attribute_free(&coord_1_y);

                dd_attribute_array_push(&rect, &coord_0);
                dd_attribute_array_push(&rect, &coord_1);
                dd_attribute_free(&coord_0);
                dd_attribute_free(&coord_1);
            }
            dd_attribute_object_property_set(&state, "rect", &rect);
            dd_attribute_free(&rect);

            dd_attribute_t inner_state = dd_attribute_array(1);
            {
                dd_attribute_t empty_object = dd_attribute_object(0);
                dd_attribute_array_push(&inner_state, &empty_object);
                dd_attribute_free(&empty_object);
            }
            dd_attribute_object_property_set(&state, "state", &inner_state);
            dd_attribute_free(&inner_state);

            dd_attribute_t offset = dd_attribute_int(-1);
            dd_attribute_t active = dd_attribute_bool(true);
            dd_attribute_object_property_set(&state, "offset", &offset);
            dd_attribute_object_property_set(&state, "active", &active);
            dd_attribute_free(&offset);
            dd_attribute_free(&active);
        }
        dd_attribute_object_property_set(&obj, "state", &state);
        dd_attribute_free(&state);

        dd_attribute_t tags = dd_attribute_array(3);
        {
            dd_attribute_t tag_0 = dd_attribute_string("blue");
            dd_attribute_t tag_1 = dd_attribute_string("meh");
            dd_attribute_t tag_2 = dd_attribute_null();
            dd_attribute_array_push(&tags, &tag_0);
            dd_attribute_array_push(&tags, &tag_1);
            dd_attribute_array_push(&tags, &tag_2);
            dd_attribute_free(&tag_0);
            dd_attribute_free(&tag_1);
            dd_attribute_free(&tag_2);
        }
        dd_attribute_object_property_set(&obj, "tags", &tags);
        dd_attribute_free(&tags);
    }
    datadog::Attribute cpp_obj = datadog::impl::AttributeConversion::CopyFromC(obj);
    dd_attribute_free(&obj);

    // When we serialize that attribute to JSON
    std::vector<uint8_t> buffer;
    datadog::impl::AttributeSerialization::ToJSON(cpp_obj, buffer);

    // Then it's formatted exactly the same as our jq output
    std::string_view got_json{reinterpret_cast<char*>(buffer.data()), buffer.size()};
    REQUIRE(got_json == want_json);
}
