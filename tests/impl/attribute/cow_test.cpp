// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include "attribute/cow.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace datadog;
using namespace datadog::impl;

// These tests validate the implementation-layer behavior of CowValue, which implements
// the reference-counted, copy-on-write data structure used by both dd_attribute_t and
// datadog::Attribute to store string, array, and object values

TEST_CASE("CowValue", "[unit][attribute]") {
  SECTION("M increment and decrement refcount W AddRef/Release are called") {
    // Given a CowValue with an initial refcount of 1
    CowValue* value = CowValue::String("hello world");
    REQUIRE(value->IsShared() == false);

    // When we call AddRef() to indicate that a copy has occurred
    value->AddRef();

    // Then the value now knows that it's referenced in multiple places
    REQUIRE(value->IsShared() == true);

    // Next: When we call Release() to indicate that a copy has gone out of scope
    value->Release();

    // Then the value once again has a refcount of 1
    REQUIRE(value->IsShared() == false);

    // Cleanup: ensure that our dynamically-allocated CowValue is destroyed
    value->Release();
  }

  SECTION("M delete value W last reference is released") {
    // Given an array CowValue holding 3 ints, with a initial refcount of 2
    CowValue* value = CowValue::Array(4);
    REQUIRE(value->IsShared() == false);
    REQUIRE(value->Size() == 0);
    value->Push(Attribute::Int(10));
    value->Push(Attribute::Int(20));
    REQUIRE(value->Size() == 2);
    value->AddRef();
    value->Push(Attribute::Int(30));
    REQUIRE(value->IsShared() == true);
    REQUIRE(value->Size() == 3);

    // When we release a single reference
    value->Release();
    REQUIRE(value->IsShared() == false);

    // Then we can continue using the object normally, as it hasn't been deleted
    REQUIRE(value->Size() == 3);
    value->Push(Attribute::Int(40));
    REQUIRE(value->Size() == 4);
    value->Clear();
    REQUIRE(value->Size() == 0);

    // Next: When we release our final reference
    value->Release();

    // Then the CowValue is deleted, and accessing it would be undefined behavior
  }

  SECTION("M create a shallow copy W cloned") {
    // Given an object CowValue storing 1 property, "foo":100
    CowValue* original = CowValue::Object(4);
    original->SetProperty("foo", Attribute::Int(100));
    REQUIRE(original->Size() == 1);

    // When we create a copy via Clone()
    CowValue* clone = original->Clone();

    // Then we get a new CowValue pointer with a distinct address
    REQUIRE(clone != original);

    // And both CowValues are nevertheless identical
    REQUIRE(original->Size() == 1);
    REQUIRE(clone->Size() == 1);
    REQUIRE(original->IsShared() == false);
    REQUIRE(clone->IsShared() == false);

    // And if we modify the clone, the original is unaffected
    clone->SetProperty("foo", Attribute::Double(1.2345));
    REQUIRE(original->GetAt(0).GetIntValue() == 100);
    REQUIRE(clone->GetAt(0).GetDoubleValue() == 1.2345);
    clone->SetProperty("bar", Attribute::Null());
    REQUIRE(original->Size() == 1);
    REQUIRE(clone->Size() == 2);

    // And vice versa
    original->AddRef();
    REQUIRE(original->IsShared() == true);
    REQUIRE(clone->IsShared() == false);

    // Cleanup!
    original->Release();
    original->Release();
    clone->Release();
  }

  SECTION("M isolate writes W a string is cloned") {
    // Given a non-empty string value and its clone, initially identical
    CowValue* original = CowValue::String("hello");
    CowValue* clone = original->Clone();

    // When we clear the clone
    clone->Clear();

    // Then the original retains its value and the clone is now empty
    REQUIRE(std::string(original->CStr()) == "hello");
    REQUIRE(std::string(clone->CStr()) == "");

    // Cleanup
    original->Release();
    clone->Release();
  }

  SECTION("M isolate writes W an array is cloned") {
    // Given a non-empty array value and its clone, initially identical
    CowValue* original = CowValue::Array(8);
    original->Push(Attribute::Int(100));
    original->Push(Attribute::Int(200));
    CowValue* clone = original->Clone();

    // When we add an item the clone
    clone->Push(Attribute::Int(300));

    // Then the two values diverge
    REQUIRE(original->Size() == 2);
    REQUIRE(clone->Size() == 3);

    // Cleanup
    original->Release();
    clone->Release();
  }

  SECTION("M isolate writes W an object is cloned") {
    // Given a non-empty object value and its clone, initially identical
    CowValue* original = CowValue::Object(8);
    original->SetProperty("foo", Attribute::Int(100));
    original->SetProperty("bar", Attribute::Int(200));
    CowValue* clone = original->Clone();

    // When we add a property the clone
    clone->SetProperty("baz", Attribute::Int(300));

    // Then the two values diverge
    REQUIRE(original->Size() == 2);
    REQUIRE(clone->Size() == 3);

    // Cleanup
    original->Release();
    clone->Release();
  }

  SECTION("M handle all supported operations W storing a string") {
    // Given a string CowValue
    CowValue* value = CowValue::String("hello");

    // CStr returns the stored string value
    REQUIRE(std::string(value->CStr()) == "hello");

    // Size returns the length of the string
    REQUIRE(value->Size() == 5);

    // Capacity returns the capacity of the string
    REQUIRE(value->Capacity() >= 5);

    // These non-string write operations are a harmless no-op
    value->Push(Attribute::Int(1));
    value->SetProperty("foo", Attribute::Int(2));
    value->DeleteProperty("bar");

    // These non-string read operations return empty values
    REQUIRE(value->GetAt(0).GetType() == ValueType::Null);
    REQUIRE(std::string(value->GetPropertyNameCStr(0)) == "");
    REQUIRE(value->FindPropertyIndex("foo") == -1);

    // String state is unmodified after all the above
    REQUIRE(std::string(value->CStr()) == "hello");
    REQUIRE(value->Size() == 5);

    // SetString modifies the stored value
    value->SetString("ok, sure");
    REQUIRE(std::string(value->CStr()) == "ok, sure");
    REQUIRE(value->Size() == 8);

    // Reserve() does nothing if desired capacity is already satisfied; reallocates
    // to grow (retaining existing string value) if needed
    const size_t old_capacity = value->Capacity();
    REQUIRE(old_capacity < 32);
    value->Reserve(old_capacity);
    REQUIRE(value->Capacity() == old_capacity);
    value->Reserve(32);
    REQUIRE(value->Capacity() >= 32);
    REQUIRE(std::string(value->CStr()) == "ok, sure");

    // Clear() reverts the value to an empty string
    value->Clear();
    value->Clear(16);
    REQUIRE(std::string(value->CStr()) == "");
    REQUIRE(value->Size() == 0);

    // Cleanup
    value->Release();
  }

  SECTION("M handle all supported operations W storing an array") {
    // Given an array CowValue
    CowValue* value = CowValue::Array(8);

    // Capacity returns how many items we can fit without reallocating
    REQUIRE(value->Capacity() >= 8);

    // Size returns the number of items in the array
    REQUIRE(value->Size() == 0);

    // Push copies new items into the array
    value->Push(Attribute::Int(100));
    value->Push(Attribute::Int(200));
    REQUIRE(value->Size() == 2);

    // GetAt returns a copy of the item at the given index, or a new null value if
    // index is out of bounds
    REQUIRE(value->GetAt(0).GetIntValue() == 100);
    REQUIRE(value->GetAt(1).GetIntValue() == 200);
    REQUIRE(value->GetAt(2).GetType() == ValueType::Null);
    REQUIRE(value->GetAt(-1).GetType() == ValueType::Null);

    // These non-array write operations are a harmless no-op
    value->SetString("nothing");
    value->SetProperty("foo", Attribute::Int(2));
    value->DeleteProperty("bar");

    // These non-array read operations return empty values
    REQUIRE(std::string(value->CStr()) == "");
    REQUIRE(std::string(value->GetPropertyNameCStr(0)) == "");
    REQUIRE(value->FindPropertyIndex("foo") == -1);

    // Array state is unmodified after all the above
    REQUIRE(value->Size() == 2);
    REQUIRE(value->GetAt(0).GetIntValue() == 100);
    REQUIRE(value->GetAt(1).GetIntValue() == 200);
    REQUIRE(value->GetAt(2).GetType() == ValueType::Null);

    // Reserve() does nothing if desired capacity is already satisfied; reallocates
    // to grow (retaining existing item values) if needed
    const size_t old_capacity = value->Capacity();
    REQUIRE(old_capacity < 32);
    value->Reserve(old_capacity);
    REQUIRE(value->Capacity() == old_capacity);
    value->Reserve(32);
    REQUIRE(value->Capacity() >= 32);
    REQUIRE(value->Size() == 2);

    // Clear() empties the array
    value->Clear();
    value->Clear(16);
    REQUIRE(value->Size() == 0);
    REQUIRE(value->GetAt(0).GetType() == ValueType::Null);

    // Cleanup
    value->Release();
  }

  SECTION("M handle all supported operations W storing an object") {
    // Given an object CowValue
    CowValue* value = CowValue::Object(8);

    // Capacity returns how many properties we can fit without reallocating
    REQUIRE(value->Capacity() >= 8);

    // Size returns the number of properties in the object
    REQUIRE(value->Size() == 0);

    // When name is not yet used, SetProperty appends copies of values
    value->SetProperty("foo", Attribute::Int(100));
    value->SetProperty("bar", Attribute::Int(200));
    REQUIRE(value->Size() == 2);

    // FindPropertyIndex returns the index where a property of the given name is
    // stored, or -1 if the name is unrecognized
    REQUIRE(value->FindPropertyIndex("foo") == 0);
    REQUIRE(value->FindPropertyIndex("bar") == 1);
    REQUIRE(value->FindPropertyIndex("scrambo") == -1);

    // GetPropertyNameCStr returns the name of the property stored at the given
    // index, or empty string if index is out of bounds
    REQUIRE(std::string(value->GetPropertyNameCStr(0)) == "foo");
    REQUIRE(std::string(value->GetPropertyNameCStr(1)) == "bar");
    REQUIRE(std::string(value->GetPropertyNameCStr(2)) == "");
    REQUIRE(std::string(value->GetPropertyNameCStr(-1)) == "");

    // GetAt returns a copy of the value stored at the given index, or a new null
    // value if index is out of bounds
    REQUIRE(value->GetAt(0).GetIntValue() == 100);
    REQUIRE(value->GetAt(1).GetIntValue() == 200);
    REQUIRE(value->GetAt(2).GetType() == ValueType::Null);
    REQUIRE(value->GetAt(-1).GetType() == ValueType::Null);

    // When a property already exists with the given name, SetProperty copies the
    // new value into storage at its position, overwriting the original value
    value->SetProperty("foo", Attribute::Int(300));
    REQUIRE(value->FindPropertyIndex("foo") == 0);
    REQUIRE(std::string(value->GetPropertyNameCStr(0)) == "foo");
    REQUIRE(value->GetAt(0).GetIntValue() == 300);
    REQUIRE(value->GetAt(1).GetIntValue() == 200);
    REQUIRE(value->GetAt(2).GetType() == ValueType::Null);

    // These non-object write operations are a harmless no-op
    value->SetString("nothing");
    value->Push(Attribute::Int(400));

    // These non-object read operations return empty values
    REQUIRE(std::string(value->CStr()) == "");

    // Object state is unmodified after all the above
    REQUIRE(value->Size() == 2);
    REQUIRE(value->GetAt(0).GetIntValue() == 300);
    REQUIRE(value->GetAt(1).GetIntValue() == 200);
    REQUIRE(value->GetAt(2).GetType() == ValueType::Null);

    // DeleteProperty() removes the property with the given name, or does nothing if
    // the name does not correspond to an existing property
    value->DeleteProperty("foo");
    REQUIRE(value->Size() == 1);
    REQUIRE(std::string(value->GetPropertyNameCStr(0)) == "bar");
    REQUIRE(value->GetAt(0).GetIntValue() == 200);
    REQUIRE(std::string(value->GetPropertyNameCStr(1)) == "");
    REQUIRE(value->GetAt(1).GetType() == ValueType::Null);
    value->DeleteProperty("scrambo");
    REQUIRE(value->Size() == 1);

    // Reserve() does nothing if desired capacity is already satisfied; reallocates
    // to grow (retaining existing property values) if needed
    const size_t old_capacity = value->Capacity();
    REQUIRE(old_capacity < 32);
    value->Reserve(old_capacity);
    REQUIRE(value->Capacity() == old_capacity);
    value->Reserve(32);
    REQUIRE(value->Capacity() >= 32);
    REQUIRE(value->Size() == 1);

    // Clear() removes all properties from the object
    value->Clear();
    value->Clear(16);
    REQUIRE(value->Size() == 0);
    REQUIRE(value->FindPropertyIndex("bar") == -1);
    REQUIRE(std::string(value->GetPropertyNameCStr(0)) == "");
    REQUIRE(value->GetAt(0).GetType() == ValueType::Null);

    // Cleanup
    value->Release();
  }
}
