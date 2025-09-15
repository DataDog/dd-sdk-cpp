// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <limits>
#include <vector>

#include "attribute/json.hpp"
#include "datadog/attribute.hpp"

using namespace datadog;

TEST_CASE("Attribute", "[unit][attribute][cpp-api]") {
  auto require_array_ops_are_noop = [](Attribute& attribute) -> void {
    attribute.ArrayClear();
    attribute.ArrayPush(Attribute::Int(100));
    REQUIRE(attribute.GetArrayLen() == 0);
    REQUIRE(attribute.GetArrayItem(0).GetType() == ValueType::Null);
    attribute.ArrayReserve(std::numeric_limits<size_t>::max());
  };

  auto require_object_ops_are_noop = [](Attribute& attribute) -> void {
    attribute.DeleteObjectProperty("foo");
    attribute.SetObjectProperty("foo", Attribute::Int(100));
    REQUIRE(attribute.GetObjectPropertyCount() == 0);
    REQUIRE(attribute.FindObjectProperty("foo") == -1);
    REQUIRE(attribute.GetObjectPropertyNameAt(0) == "");
    REQUIRE(attribute.GetObjectPropertyValueAt(0).GetType() == ValueType::Null);
    REQUIRE(attribute.GetObjectProperty("foo").GetType() == ValueType::Null);
    attribute.ReserveObjectPropertyCapacity(std::numeric_limits<size_t>::max());
  };

  SECTION("M allow all operations W type is null") {
    // Given a null attribute value
    Attribute attribute = Attribute::Null();
    REQUIRE(attribute.GetType() == ValueType::Null);

    // Get<Type>() for all types returns zero; array/object funcs are no-op
    REQUIRE(attribute.GetBoolValue() == false);
    REQUIRE(attribute.GetIntValue() == 0ll);
    REQUIRE(attribute.GetUIntValue() == 0ull);
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 0ull);
    REQUIRE(attribute.GetDoubleValue() == 0.0);
    REQUIRE(attribute.GetStringValue() == "");
    require_array_ops_are_noop(attribute);
    require_object_ops_are_noop(attribute);

    // Copy-assignment creates another null value
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::Null);
  }

  SECTION("M allow all operations W type is bool") {
    // Given a boolean attribute value
    Attribute attribute = Attribute::Bool(true);
    REQUIRE(attribute.GetType() == ValueType::Bool);

    // GetBoolValue() returns value
    REQUIRE(attribute.GetBoolValue() == true);

    // Get<Type>() for other types returns zero; array/object funcs are no-op
    REQUIRE(attribute.GetIntValue() == 0ll);
    REQUIRE(attribute.GetUIntValue() == 0ull);
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 0ull);
    REQUIRE(attribute.GetDoubleValue() == 0.0);
    REQUIRE(attribute.GetStringValue() == "");
    require_array_ops_are_noop(attribute);
    require_object_ops_are_noop(attribute);

    // Copy-assignment creates another bool attribute with the same value
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::Bool);
    REQUIRE(copy.GetBoolValue() == true);

    // SetBool() updates value; changing copy doesn't change original
    copy.SetBool(false);
    REQUIRE(copy.GetBoolValue() == false);
    REQUIRE(attribute.GetBoolValue() == true);
  }

  SECTION("M allow all operations W type is int") {
    // Given an int attribute value
    Attribute attribute = Attribute::Int(-100);
    REQUIRE(attribute.GetType() == ValueType::Int);

    // GetIntValue() returns value
    REQUIRE(attribute.GetIntValue() == -100);

    // Get<Type>() for other types returns zero; array/object funcs are no-op
    REQUIRE(attribute.GetBoolValue() == false);
    REQUIRE(attribute.GetUIntValue() == 0ull);
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 0ull);
    REQUIRE(attribute.GetDoubleValue() == 0.0);
    REQUIRE(attribute.GetStringValue() == "");
    require_array_ops_are_noop(attribute);
    require_object_ops_are_noop(attribute);

    // Copy-assignment creates another int attribute with the same value
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::Int);
    REQUIRE(copy.GetIntValue() == -100);

    // SetInt() updates value; changing copy doesn't change original
    copy.SetInt(-99);
    REQUIRE(copy.GetIntValue() == -99);
    REQUIRE(attribute.GetIntValue() == -100);
  }

  SECTION("M allow all operations W type is uint") {
    // Given a uint attribute value
    Attribute attribute = Attribute::UInt(100);
    REQUIRE(attribute.GetType() == ValueType::UInt);

    // GetUIntValue() returns value
    REQUIRE(attribute.GetUIntValue() == 100);

    // Get<Type>() for other types returns zero; array/object funcs are no-op
    REQUIRE(attribute.GetBoolValue() == false);
    REQUIRE(attribute.GetIntValue() == 0ll);
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 0ull);
    REQUIRE(attribute.GetDoubleValue() == 0.0);
    REQUIRE(attribute.GetStringValue() == "");
    require_array_ops_are_noop(attribute);
    require_object_ops_are_noop(attribute);

    // Copy-assignment creates another uint attribute with the same value
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::UInt);
    REQUIRE(copy.GetUIntValue() == 100);

    // SetUInt() updates value; changing copy doesn't change original
    copy.SetUInt(99);
    REQUIRE(copy.GetUIntValue() == 99);
    REQUIRE(attribute.GetUIntValue() == 100);
  }

  SECTION("M allow all operations W type is timestamp") {
    // Given a timestamp attribute value
    Attribute attribute = Attribute::TimestampFromNanoseconds(946684799999999999);
    REQUIRE(attribute.GetType() == ValueType::Timestamp);

    // GetTimestampValueAsNanoseconds() returns value
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 946684799999999999);

    // Get<Type>() for other types returns zero; array/object funcs are no-op
    REQUIRE(attribute.GetBoolValue() == false);
    REQUIRE(attribute.GetIntValue() == 0ll);
    REQUIRE(attribute.GetUIntValue() == 0ull);
    REQUIRE(attribute.GetDoubleValue() == 0.0);
    REQUIRE(attribute.GetStringValue() == "");
    require_array_ops_are_noop(attribute);
    require_object_ops_are_noop(attribute);

    // Copy-assignment creates another uint attribute with the same value
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::Timestamp);
    REQUIRE(copy.GetTimestampValueAsNanoseconds() == 946684799999999999);

    // SetTimestampAsNanoseconds() updates value; changing copy doesn't change
    // original
    copy.SetTimestampAsNanoseconds(145296000000000000);
    REQUIRE(copy.GetTimestampValueAsNanoseconds() == 145296000000000000);
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 946684799999999999);
  }

  SECTION("M support timestamp W value is std::chrono time_point {nanoseconds}") {
    // Given input std::chrono::time_point values, with nanosecond precision
    using Clock = std::chrono::system_clock;
    using Duration = std::chrono::nanoseconds;
    using Timestamp = std::chrono::time_point<Clock, Duration>;
    Timestamp eve_of_y2k(Duration(946684799999999999));
    Timestamp moment_nixon_resigned(Duration(145296000000000000));

    // When we construct an Attribute from a time_point
    Attribute attribute = Attribute::Timestamp(eve_of_y2k);
    REQUIRE(attribute.GetType() == ValueType::Timestamp);

    // Then our underlying timestamp value is recorded faithfully
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 946684799999999999);

    // And we can retrieve an equivalent time_point via GetTimestamp()
    Timestamp retrieved = attribute.GetTimestampValue();
    bool ok = retrieved == eve_of_y2k;  // Catch2 can't auto-format time_point
    REQUIRE(ok);

    // And we can modify the value by setting a direct nanosecond count
    attribute.SetTimestampAsNanoseconds(145296000000000000);

    // And when we retrieve our value, it's equivalent with the expected time_point
    retrieved = attribute.GetTimestampValue();
    ok = retrieved == moment_nixon_resigned;
    REQUIRE(ok);
  }

  SECTION("M support timestamp W value is std::chrono time_point {milliseconds}") {
    // Given input std::chrono::time_point values, with millisecond precision
    using Clock = std::chrono::system_clock;
    using Duration = std::chrono::milliseconds;
    using Timestamp = std::chrono::time_point<Clock, Duration>;
    Timestamp eve_of_y2k(Duration(946684799999));
    Timestamp moment_nixon_resigned(Duration(145296000000));

    // When we construct an Attribute from a time_point
    Attribute attribute = Attribute::Timestamp(eve_of_y2k);
    REQUIRE(attribute.GetType() == ValueType::Timestamp);

    // Then our underlying timestamp value is recorded faithfully
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 946684799999000000);

    // And we can retrieve an equivalent time_point via GetTimestamp()
    Timestamp retrieved = attribute.GetTimestampValue<Clock, Duration>();
    bool ok = retrieved == eve_of_y2k;  // Catch2 can't auto-format time_point
    REQUIRE(ok);

    // And we can modify the value by setting a direct nanosecond count
    attribute.SetTimestampAsNanoseconds(145296000000000000);

    // And when we retrieve our value, it's equivalent with the expected time_point
    retrieved = attribute.GetTimestampValue<Clock, Duration>();
    ok = retrieved == moment_nixon_resigned;
    REQUIRE(ok);
  }

  SECTION("M allow all operations W type is double") {
    // Given a double attribute value
    Attribute attribute = Attribute::Double(1.2345);
    REQUIRE(attribute.GetType() == ValueType::Double);

    // GetDoubleValue() returns value
    REQUIRE(attribute.GetDoubleValue() == 1.2345);

    // Get<Type>() for other types returns zero; array/object funcs are no-op
    REQUIRE(attribute.GetBoolValue() == false);
    REQUIRE(attribute.GetIntValue() == 0ll);
    REQUIRE(attribute.GetUIntValue() == 0ull);
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 0ull);
    REQUIRE(attribute.GetStringValue() == "");
    require_array_ops_are_noop(attribute);
    require_object_ops_are_noop(attribute);

    // Copy-assignment creates another double attribute with the same value
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::Double);
    REQUIRE(copy.GetDoubleValue() == 1.2345);

    // SetDouble() updates value; changing copy doesn't change original
    copy.SetDouble(-99.989);
    REQUIRE(copy.GetDoubleValue() == -99.989);
    REQUIRE(attribute.GetDoubleValue() == 1.2345);
  }

  SECTION("M allow all operations W type is string") {
    // Given a string attribute value
    Attribute attribute = Attribute::String("hello");
    REQUIRE(attribute.GetType() == ValueType::String);

    // GetStringValue() returns value
    REQUIRE(attribute.GetStringValue() == "hello");

    // Get<Type>() for other types returns zero; array/object funcs are no-op
    REQUIRE(attribute.GetBoolValue() == false);
    REQUIRE(attribute.GetIntValue() == 0ll);
    REQUIRE(attribute.GetUIntValue() == 0ull);
    REQUIRE(attribute.GetTimestampValueAsNanoseconds() == 0ull);
    REQUIRE(attribute.GetDoubleValue() == 0.0);
    require_array_ops_are_noop(attribute);
    require_object_ops_are_noop(attribute);

    // Copy-assignment creates another string attribute with the same value
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::String);
    REQUIRE(copy.GetStringValue() == "hello");

    // SetString() updates value; changing copy doesn't change original
    copy.SetString("world");
    REQUIRE(copy.GetStringValue() == "world");
    REQUIRE(attribute.GetStringValue() == "hello");
  }

  SECTION("M allow all operations W type is array") {
    // Given an array attribute value
    Attribute attribute = Attribute::Array(8);
    REQUIRE(attribute.GetType() == ValueType::Array);

    // ArrayPush() adds new items into the array
    attribute.ArrayPush(Attribute::Int(100));

    // GetArrayLen() reports number of items
    REQUIRE(attribute.GetArrayLen() == 1);

    // GetArrayItem() returns a copy of the requested item
    Attribute item_0 = attribute.GetArrayItem(0);
    REQUIRE(item_0.GetType() == ValueType::Int);
    REQUIRE(item_0.GetIntValue() == 100);

    // GetArrayItem() returns a null attribute if out of bounds
    REQUIRE(attribute.GetArrayItem(1).GetType() == ValueType::Null);
    REQUIRE(attribute.GetArrayItem(-1).GetType() == ValueType::Null);

    // Get<Type>() for all types returns zero; object funcs are no-op
    REQUIRE(attribute.GetBoolValue() == false);
    REQUIRE(attribute.GetIntValue() == 0ll);
    REQUIRE(attribute.GetUIntValue() == 0ull);
    REQUIRE(attribute.GetDoubleValue() == 0.0);
    REQUIRE(attribute.GetStringValue() == "");
    require_object_ops_are_noop(attribute);

    // Copy-assignment creates another array attribute with the same values
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::Array);
    REQUIRE(copy.GetArrayLen() == 1);
    REQUIRE(copy.GetArrayItem(0).GetIntValue() == 100);

    // ArrayPush() on the copy doesn't affect the original
    copy.ArrayPush(Attribute::Int(200));
    REQUIRE(copy.GetArrayLen() == 2);
    REQUIRE(copy.GetArrayItem(0).GetIntValue() == 100);
    REQUIRE(copy.GetArrayItem(1).GetIntValue() == 200);
    REQUIRE(attribute.GetArrayLen() == 1);
    REQUIRE(attribute.GetArrayItem(0).GetIntValue() == 100);
    REQUIRE(attribute.GetArrayItem(1).GetType() == ValueType::Null);

    // ArrayClear() removes all items; clearing original doesn't affect copy
    attribute.ArrayClear();
    REQUIRE(attribute.GetArrayLen() == 0);
    REQUIRE(copy.GetArrayLen() == 2);

    // ArrayReserve() reserves more memory if needed but does not clear items
    copy.ArrayReserve(32);
    REQUIRE(copy.GetArrayLen() == 2);

    // InitArray() also clears if called on existing array
    copy.InitArray(16);
    REQUIRE(copy.GetArrayLen() == 0);
  }

  SECTION("M allow all operations W type is object") {
    // Given an object attribute value
    Attribute attribute = Attribute::Object(8);
    REQUIRE(attribute.GetType() == ValueType::Object);

    // SetObjectProperty() adds new properties to the object
    attribute.SetObjectProperty("foo", Attribute::Int(100));
    attribute.SetObjectProperty("bar", Attribute::Int(200));

    // SetObjectProperty() overwrites existing value on name conflict
    attribute.SetObjectProperty("foo", Attribute::Int(300));

    // DeleteObjectProperty() remove existing properties
    attribute.DeleteObjectProperty("bar");

    // DeleteObjectProperty() is a no-op if property does not exist
    attribute.DeleteObjectProperty("nothing");

    // Get<Type>() for all types returns zero; array funcs are no-op
    REQUIRE(attribute.GetBoolValue() == false);
    REQUIRE(attribute.GetIntValue() == 0ll);
    REQUIRE(attribute.GetUIntValue() == 0ull);
    REQUIRE(attribute.GetDoubleValue() == 0.0);
    REQUIRE(attribute.GetStringValue() == "");
    require_array_ops_are_noop(attribute);

    // GetObjectPropertyCount() reports number of properties
    REQUIRE(attribute.GetObjectPropertyCount() == 1);

    // FindObjectProperty() retuns index of property matching name, or -1
    REQUIRE(attribute.FindObjectProperty("foo") == 0);
    REQUIRE(attribute.FindObjectProperty("bar") == -1);

    // GetObjectPropertyNameAt() returns the name of the property stored at the
    // given index, or "" if out of bounds
    REQUIRE(attribute.GetObjectPropertyNameAt(0) == "foo");
    REQUIRE(attribute.GetObjectPropertyNameAt(1) == "");
    REQUIRE(attribute.GetObjectPropertyNameAt(-1) == "");

    // GetObjectPropertyValueAt() returns a copy of the property value stored at the
    // given index, or a null attribute if out of bounds
    REQUIRE(attribute.GetObjectPropertyValueAt(0).GetIntValue() == 300);
    REQUIRE(attribute.GetObjectPropertyValueAt(1).GetType() == ValueType::Null);
    REQUIRE(attribute.GetObjectPropertyValueAt(-1).GetType() == ValueType::Null);

    // GetObjectProperty() returns the value associated with the given property
    // name, or a null attribute if no such property exists
    REQUIRE(attribute.GetObjectProperty("foo").GetIntValue() == 300);
    REQUIRE(attribute.GetObjectProperty("bar").GetType() == ValueType::Null);

    // Copy-assignment creates another object attribute with the same values
    Attribute copy = attribute;
    REQUIRE(copy.GetType() == ValueType::Object);
    REQUIRE(copy.GetObjectPropertyCount() == 1);
    REQUIRE(copy.GetObjectProperty("foo").GetIntValue() == 300);

    // SetObjectProperty() on copy doesn't affect original
    copy.SetObjectProperty("baz", Attribute::Int(400));
    REQUIRE(copy.GetObjectPropertyCount() == 2);
    REQUIRE(attribute.GetObjectPropertyCount() == 1);

    // DeleteObjectProperty() on original doesn't affect copy
    attribute.DeleteObjectProperty("foo");
    REQUIRE(attribute.GetObjectPropertyCount() == 0);
    REQUIRE(copy.GetObjectPropertyCount() == 2);

    // ReserveObjectPropertyCapacity() reserves more memory if needed but does not
    // clear properties
    copy.ReserveObjectPropertyCapacity(32);
    REQUIRE(copy.GetObjectPropertyCount() == 2);

    // InitObject() deletes all properties if called on an existing object
    copy.InitObject(16);
    REQUIRE(copy.GetObjectPropertyCount() == 0);
  }

  SECTION("M accept SetObjectProperty() W name is empty string") {
    // Given an object attribute
    Attribute attribute = Attribute::Object(4);

    // When we attempt to set a property with an empty name, which is acceptable
    // since we accept any name that would be a valid JSON property name
    attribute.SetObjectProperty("", Attribute::Int(100));

    // Then the operation is successful
    REQUIRE(attribute.GetObjectPropertyCount() == 1);
    REQUIRE(attribute.FindObjectProperty("") == 0);
  }

  SECTION("M prevent massive allocation W initial array capacity is huge") {
    // Given a size_t value initialized from -1
    const size_t initial_capacity = static_cast<size_t>(-1);
    REQUIRE(initial_capacity == std::numeric_limits<size_t>::max());

    // When we attempt to create an array with that initial capacity
    Attribute attribute = Attribute::Array(initial_capacity);

    // Then we refrain from allocating SIZE_MAX * sizeof(Attribute) bytes, which
    // would surely fail
    REQUIRE(attribute.GetArrayLen() == 0);  // Still works

    // And the same clamping behavior is employed when we reinitialize our array
    attribute.InitArray(initial_capacity);
    REQUIRE(attribute.GetArrayLen() == 0);

    // And also when we change an existing value's type to array
    Attribute other = Attribute::String("so long, it's been good to know you");
    other.InitArray(initial_capacity);
    REQUIRE(other.GetArrayLen() == 0);
  }

  SECTION("M prevent massive allocation W initial object capacity is huge") {
    // Given a size_t value initialized from -1
    const size_t initial_capacity = static_cast<size_t>(-1);
    REQUIRE(initial_capacity == std::numeric_limits<size_t>::max());

    // When we attempt to create an object with that initial capacity
    Attribute attribute = Attribute::Object(initial_capacity);

    // Then we refrain from allocating SIZE_MAX * sizeof(pair<string, Attribute>)
    // bytes, which would surely fail
    REQUIRE(attribute.GetObjectPropertyCount() == 0);  // Still works

    // And the same clamping behavior is employed when we reinitialize our object
    attribute.InitObject(initial_capacity);
    REQUIRE(attribute.GetObjectPropertyCount() == 0);

    // And also when we change an existing value's type to object
    Attribute other = Attribute::String("so long, it's been good to know you");
    other.InitObject(initial_capacity);
    REQUIRE(other.GetObjectPropertyCount() == 0);
  }

  SECTION("M increment reference count W attribute is copied") {
    Attribute copy;
    {
      // Given two attributes referencing the same string value
      Attribute attribute = Attribute::String("hello");
      copy = attribute;

      // When the original attribute goes out of scope and releases its reference
      // to the string value
    }

    // Then we can still access the string value via the copy
    REQUIRE(copy.GetStringValue() == "hello");
    copy.SetString("world");
    REQUIRE(copy.GetStringValue() == "world");
  }

  SECTION("M increment reference count W array item is copied on access") {
    // Given an array attribute containing a string item
    Attribute array = Attribute::Array(4);
    array.ArrayPush(Attribute::String("hello"));

    // When we access the array and get a copy of our item
    Attribute copy = array.GetArrayItem(0);

    // And we clear the array, releasing its reference to the string value
    array.ArrayClear();

    // Then we can still access the copy
    REQUIRE(copy.GetStringValue() == "hello");
    copy.SetString("world");
    REQUIRE(copy.GetStringValue() == "world");
  }

  SECTION("M increment reference count W object property value is copied on lookup") {
    // Given an object attribute containing a string property
    Attribute obj = Attribute::Object(4);
    obj.SetObjectProperty("foo", Attribute::String("hello"));

    // When we access the object and get a copy of our property value
    Attribute copy = obj.GetObjectProperty("foo");

    // And we delete the property from the object, releasing its reference to the
    // string value
    obj.DeleteObjectProperty("foo");

    // Then we can still access the copy
    REQUIRE(copy.GetStringValue() == "hello");
    copy.SetString("world");
    REQUIRE(copy.GetStringValue() == "world");
  }

  SECTION("M increment reference count W object property value is copied on access") {
    // Given an object attribute containing a string property
    Attribute obj = Attribute::Object(4);
    obj.SetObjectProperty("foo", Attribute::String("hello"));

    // When we access the object and get a copy of our property value
    Attribute copy = obj.GetObjectPropertyValueAt(0);

    // And we delete the property from the object, releasing its reference to the
    // string value
    obj.DeleteObjectProperty("foo");

    // Then we can still access the copy
    REQUIRE(copy.GetStringValue() == "hello");
    copy.SetString("world");
    REQUIRE(copy.GetStringValue() == "world");
  }

  SECTION("M convert between data types W set functions are called") {
    // Given a description, for every type, of how we:
    // - create a new value of that type, initialized with a known value
    // - reinitialize an existing value to that type, with the same value
    // - verify that a value of that type has the expected value
    struct TypeTest {
      std::string_view name;
      std::function<Attribute()> init_func;
      std::function<void(Attribute&)> set_func;
      std::function<void(const Attribute&)> check_func;
    };
    std::vector<TypeTest> types = {
        {"null", Attribute::Null, [](Attribute& attr) { attr.SetNull(); },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::Null);
           REQUIRE(attr.GetIntValue() == 0);
         }},

        {"bool", []() { return Attribute::Bool(true); },
         [](Attribute& attr) { attr.SetBool(true); },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::Bool);
           REQUIRE(attr.GetBoolValue() == true);
         }},

        {"int", []() { return Attribute::Int(-100); },
         [](Attribute& attr) { attr.SetInt(-100); },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::Int);
           REQUIRE(attr.GetIntValue() == -100);
         }},

        {"uint", []() { return Attribute::UInt(999); },
         [](Attribute& attr) { attr.SetUInt(999); },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::UInt);
           REQUIRE(attr.GetUIntValue() == 999);
         }},

        {"timestamp",
         []() { return Attribute::TimestampFromNanoseconds(145296000000000000); },
         [](Attribute& attr) { attr.SetTimestampAsNanoseconds(145296000000000000); },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::Timestamp);
           REQUIRE(attr.GetTimestampValueAsNanoseconds() == 145296000000000000);
         }},

        {"double", []() { return Attribute::Double(5.6789); },
         [](Attribute& attr) { attr.SetDouble(5.6789); },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::Double);
           REQUIRE(attr.GetDoubleValue() == 5.6789);
         }},

        {"string", []() { return Attribute::String("hello"); },
         [](Attribute& attr) { attr.SetString("hello"); },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::String);
           REQUIRE(attr.GetStringValue() == "hello");
         }},

        {"array",
         []() {
           Attribute array = Attribute::Array(4);
           array.ArrayPush(Attribute::Int(100));
           array.ArrayPush(Attribute::String("hi"));
           return array;
         },
         [](Attribute& attr) {
           attr.InitArray(4);
           attr.ArrayPush(Attribute::Int(100));
           attr.ArrayPush(Attribute::String("hi"));
         },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::Array);
           REQUIRE(attr.GetArrayItem(0).GetIntValue() == 100);
           REQUIRE(attr.GetArrayItem(1).GetStringValue() == "hi");
         }},

        {"object",
         []() {
           Attribute obj = Attribute::Object(4);
           obj.SetObjectProperty("foo", Attribute::Int(100));
           obj.SetObjectProperty("bar", Attribute::String("hi"));
           return obj;
         },
         [](Attribute& attr) {
           attr.InitObject(4);
           attr.SetObjectProperty("foo", Attribute::Int(100));
           attr.SetObjectProperty("bar", Attribute::String("hi"));
         },
         [](const Attribute& attr) {
           REQUIRE(attr.GetType() == ValueType::Object);
           REQUIRE(attr.GetObjectPropertyValueAt(0).GetIntValue() == 100);
           REQUIRE(attr.GetObjectPropertyValueAt(1).GetStringValue() == "hi");
           REQUIRE(attr.GetObjectProperty("foo").GetIntValue() == 100);
           REQUIRE(attr.GetObjectProperty("bar").GetStringValue() == "hi");
         }},
    };

    // We should be able to create a value of any type, then convert it to a value
    // of any type (including itself), and get the expected value, across all N^2
    // permutations
    for (const auto& src_type : types) {
      for (const auto& dst_type : types) {
        DYNAMIC_SECTION(" {" << src_type.name << " -> " << dst_type.name << "}") {
          // Given a attribute created as the source type
          Attribute attribute = src_type.init_func();
          src_type.check_func(attribute);

          // When we change it to a value of the destination type
          dst_type.set_func(attribute);

          // Then it adopts the expected type and value
          dst_type.check_func(attribute);
        }
      }
    }
  }

  SECTION("M create a clone W array is pushed onto itself") {
    // Given an array `[1]`
    Attribute array = Attribute::Array(2);
    array.ArrayPush(Attribute::Int(1));

    // When we attempt to create `[1,[1]]`, passing `array` by reference
    array.ArrayPush(array);

    // Then we end up with our desired value, rather than crashing, because the
    // Attribute API implicitly clones the operand if it's `this`
    REQUIRE(array.GetType() == ValueType::Array);
    REQUIRE(array.GetArrayLen() == 2);
    REQUIRE(array.GetArrayItem(0).GetIntValue() == 1);
    REQUIRE(array.GetArrayItem(1).GetArrayLen() == 1);
    REQUIRE(array.GetArrayItem(1).GetArrayItem(0).GetIntValue() == 1);
  }

  SECTION("M create a clone W object is set as property on itself") {
    // Given an object `{"foo":1}`
    Attribute obj = Attribute::Object(2);
    obj.SetObjectProperty("foo", Attribute::Int(1));

    // When we attempt to create `{"foo":1,"bar":{"foo":1}}`, passing `obj` by
    // reference
    obj.SetObjectProperty("bar", obj);

    // Then we end up with that value, rather than crashing, because the Attribute
    // API implicitly clones the operand if it's `this`
    REQUIRE(obj.GetType() == ValueType::Object);
    REQUIRE(obj.GetObjectPropertyCount() == 2);
    REQUIRE(obj.GetObjectProperty("foo").GetIntValue() == 1);
    REQUIRE(obj.GetObjectProperty("bar").GetType() == ValueType::Object);
    REQUIRE(obj.GetObjectProperty("bar").GetObjectPropertyCount() == 1);
    REQUIRE(obj.GetObjectProperty("bar").GetObjectProperty("foo").GetIntValue() == 1);
  }

  SECTION("M guard against self-assignment W copy-assigned") {
    // When we assign an attribute to itself
    Attribute attribute = Attribute::String("hello");
    Attribute& attribute_ref = attribute;
    attribute = attribute_ref;

    // Then everything is still fine
    REQUIRE(attribute.GetType() == ValueType::String);
    REQUIRE(attribute.GetStringValue() == "hello");
  }

  SECTION("M guard against self-assignment W move-assigned") {
    // When we move an attribute to itself
    Attribute attribute = Attribute::String("hello");
    Attribute& attribute_ref = attribute;
    attribute = std::move(attribute_ref);

    // Then everything is still fine
    REQUIRE(attribute.GetType() == ValueType::String);
    REQUIRE(attribute.GetStringValue() == "hello");
  }
}

TEST_CASE("Attribute JSON example", "[unit][attribute][cpp-api]") {
  // This test validates the integration of multiple units of functionality: more
  // exhaustive JSON serialization tests can be found in the implementation layer's
  // `json_test.cpp`.

  // Essentially, this test validates that we can build a complex object using
  // Attribute and it will be properly serialized to JSON.

  // We want to build this JSON object:
  /*
      {
          "process": {
              "pid": 9238451,
              "name": "my-cool-program",
              "args": ["--mode", "good"],
              "started_at": "1974-08-09T16:00:00.000Z"
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
      R"({"process":{"pid":9238451,"name":"my-cool-program","args":["--mode","good"],"started_at":"1974-08-09T16:00:00.000Z"},"state":{"rect":[[0.03333,-12.3],[94,98.7001]],"state":[{}],"offset":-1,"active":true},"tags":["blue","meh",null]})";

  // Given the Attribute calls required to construct that object
  Attribute obj = Attribute::Object(3);

  // 'process' subobject
  {
    Attribute args = Attribute::Array(2);
    args.ArrayPush(Attribute::String("--mode"));
    args.ArrayPush(Attribute::String("good"));

    Attribute process = Attribute::Object(4);
    process.SetObjectProperty("pid", Attribute::UInt(9238451));
    process.SetObjectProperty("name", Attribute::String("my-cool-program"));
    process.SetObjectProperty("args", args);
    process.SetObjectProperty(
        "started_at", Attribute::TimestampFromNanoseconds(145296000000000000)
    );

    obj.SetObjectProperty("process", process);
  }

  // 'state' subobject
  {
    Attribute state = Attribute::Object(4);

    // 'rect' 2D array
    {
      Attribute coord_0 = Attribute::Array(2);
      coord_0.ArrayPush(Attribute::Double(0.03333));
      coord_0.ArrayPush(Attribute::Double(-12.3));
      Attribute coord_1 = Attribute::Array(2);
      coord_1.ArrayPush(Attribute::Double(94.0));
      coord_1.ArrayPush(Attribute::Double(98.7001));

      Attribute rect = Attribute::Array(2);
      rect.ArrayPush(coord_0);
      rect.ArrayPush(coord_1);

      state.SetObjectProperty("rect", rect);
    }

    // Nested 'state' array
    {
      Attribute inner_state = Attribute::Array(1);
      inner_state.ArrayPush(Attribute::Object());
      state.SetObjectProperty("state", inner_state);
    }

    state.SetObjectProperty("offset", Attribute::Int(-1));
    state.SetObjectProperty("active", Attribute::Bool(true));

    obj.SetObjectProperty("state", state);
  }

  // 'tags' array
  {
    Attribute tags = Attribute::Array(3);
    tags.ArrayPush(Attribute::String("blue"));
    tags.ArrayPush(Attribute::String("meh"));
    tags.ArrayPush(Attribute::Null());
    obj.SetObjectProperty("tags", tags);
  }

  // When we serialize that attribute to JSON
  std::vector<uint8_t> buffer;
  impl::AttributeSerialization::ToJSON(obj, buffer);

  // Then it's formatted exactly the same as our jq output
  std::string_view got_json{reinterpret_cast<char*>(buffer.data()), buffer.size()};
  REQUIRE(got_json == want_json);
}
