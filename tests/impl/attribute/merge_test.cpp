// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/attribute/merge.hpp"

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>

#include "datadog/attribute.hpp"

using namespace datadog;
using namespace datadog::impl;

static bool is_valid_property_name(std::string_view name) {
  // clang-format off
  // Filter out reserved property names
  if (name == "foo") return false;
  if (name == "_secret") return false;
  return true;
  // clang-format on
}

TEST_CASE("AttributeMerge", "[unit][attribute]") {
  SECTION("M produce new object with all properties W called with many objects") {
    // Given three object attributes, each with a unique property name
    Attribute obj_with_foo = Attribute::Object(1);
    Attribute obj_with_bar = Attribute::Object(1);
    Attribute obj_with_baz = Attribute::Object(1);
    obj_with_foo.SetObjectProperty("foo", Attribute::Int(100));
    obj_with_bar.SetObjectProperty("bar", Attribute::Int(200));
    obj_with_baz.SetObjectProperty("baz", Attribute::Int(300));

    // When we create a new attribute merged from those three
    Attribute merged = Attribute::Object();
    AttributeMerge::AssembleObject(merged, {obj_with_foo, obj_with_bar, obj_with_baz});

    // Then we get a new object with all three properties
    REQUIRE(merged.GetType() == ValueType::Object);
    REQUIRE(merged.GetObjectPropertyCount() == 3);
    REQUIRE(merged.GetObjectProperty("foo").GetIntValue() == 100);
    REQUIRE(merged.GetObjectProperty("bar").GetIntValue() == 200);
    REQUIRE(merged.GetObjectProperty("baz").GetIntValue() == 300);

    // And the new object's properties are ordered deterministically
    REQUIRE(merged.GetObjectPropertyNameAt(0) == "foo");
    REQUIRE(merged.GetObjectPropertyNameAt(1) == "bar");
    REQUIRE(merged.GetObjectPropertyNameAt(2) == "baz");
  }

  SECTION("M take value from later object W property names conflict") {
    // Given an object attribute, obj_a, with values 'foo' and 'bar'
    Attribute obj_a = Attribute::Object(2);
    obj_a.SetObjectProperty("foo", Attribute::String("hello"));
    obj_a.SetObjectProperty("bar", Attribute::String("original"));

    // And another object attribute, obj_b, with values 'bar' and 'baz'
    Attribute obj_b = Attribute::Object(2);
    obj_b.SetObjectProperty("bar", Attribute::String("updated"));
    obj_a.SetObjectProperty("baz", Attribute::Int(-10));

    // When we merge obj_a and obj_b into a single result object
    Attribute result = Attribute::Object();
    AttributeMerge::AssembleObject(result, {obj_a, obj_b});

    // Then result contains three properties
    REQUIRE(result.GetObjectPropertyCount() == 3);

    // And the 'bar' value from obj_b prevails, since it appeared later in the list
    REQUIRE(result.GetObjectProperty("foo").GetStringValue() == "hello");
    REQUIRE(result.GetObjectProperty("bar").GetStringValue() == "updated");
    REQUIRE(result.GetObjectProperty("baz").GetIntValue() == -10);

    // And the new object's properties are ordered deterministically
    REQUIRE(result.GetObjectPropertyNameAt(0) == "foo");
    REQUIRE(result.GetObjectPropertyNameAt(1) == "bar");
    REQUIRE(result.GetObjectPropertyNameAt(2) == "baz");
  }

  SECTION("M ignore incoming values W property name is reserved") {
    // Given a root object {"foo":100} that we want to merge our values into
    Attribute root = Attribute::Object(1);
    root.SetObjectProperty("foo", Attribute::Int(100));

    // And another object {"foo":200,"bar":300,"_secret":400}
    Attribute other = Attribute::Object(3);
    other.SetObjectProperty("foo", Attribute::Int(200));
    other.SetObjectProperty("bar", Attribute::Int(300));
    other.SetObjectProperty("_secret", Attribute::Int(400));

    // And a filter function that excludes "foo" and "_secret" as reserved property
    // names
    AttributeMerge::FilterFunc filter = is_valid_property_name;

    // When we merge our two objects, specifying the filter function as a callback
    Attribute merged = Attribute::Object();
    AttributeMerge::AssembleObject(merged, {root, other}, filter);

    // Then 'foo' and '_secret' are not present in the result object, as our filter
    // function excludes them as reserved property names
    REQUIRE(merged.FindObjectProperty("foo") == -1);
    REQUIRE(merged.FindObjectProperty("_secret") == -1);

    // And 'bar' was merged into our root object just fine, because it's not a
    // reserved property
    REQUIRE(merged.GetObjectPropertyCount() == 1);
    REQUIRE(merged.GetObjectProperty("bar").GetIntValue() == 300);
  }

  SECTION("M preserve nested objects as-is W nested objects have conflicting names") {
    // Given two objects, alice and bob
    // alice: {"name":"Alice","age":35}
    // bob:   {"name":"Bob","height":182.8}
    Attribute alice = Attribute::Object(2);
    alice.SetObjectProperty("name", Attribute::String("Alice"));
    alice.SetObjectProperty("age", Attribute::Int(35));
    Attribute bob = alice;
    bob.DeleteObjectProperty("age");
    bob.SetObjectProperty("name", Attribute::String("Bob"));
    bob.SetObjectProperty("height", Attribute::Double(182.8));
    REQUIRE(alice.GetObjectPropertyCount() == 2);
    REQUIRE(alice.GetObjectProperty("name").GetStringValue() == "Alice");
    REQUIRE(alice.GetObjectProperty("age").GetIntValue() == 35);
    REQUIRE(alice.FindObjectProperty("height") == -1);
    REQUIRE(bob.GetObjectPropertyCount() == 2);
    REQUIRE(bob.GetObjectProperty("name").GetStringValue() == "Bob");
    REQUIRE(bob.GetObjectProperty("height").GetDoubleValue() == 182.8);
    REQUIRE(bob.FindObjectProperty("age") == -1);

    // And two objects, obj_a and obj_b, into which alice and bob are respectively
    // nested under the key 'subobject', i.e.:
    // obj_a: {"foo": 100, "subobject": {"name":"Alice","age":35}}
    // obj_b: {"bar": 200, "subobject": {"name":"Bob","height":182.8}}
    Attribute obj_a = Attribute::Object(2);
    obj_a.SetObjectProperty("foo", Attribute::Int(100));
    obj_a.SetObjectProperty("subobject", alice);
    Attribute obj_b = Attribute::Object(2);
    obj_b.SetObjectProperty("bar", Attribute::Int(200));
    obj_b.SetObjectProperty("subobject", bob);

    // When we create a new attribute merged from obj_a and obj_b
    Attribute merged = Attribute::Object();
    AttributeMerge::AssembleObject(merged, {obj_a, obj_b});

    // Then we get a new object with 'foo', 'bar' and 'subobject' values
    REQUIRE(merged.GetObjectPropertyCount() == 3);
    REQUIRE(merged.GetObjectProperty("foo").GetIntValue() == 100);
    REQUIRE(merged.GetObjectProperty("bar").GetIntValue() == 200);
    Attribute merged_subobject = merged.GetObjectProperty("subobject");
    REQUIRE(merged_subobject.GetType() == ValueType::Object);

    // And for 'subobject', we simply take the object value that appeared in obj_b;
    // we don't attempt any recursive merging of nested objects
    REQUIRE(merged_subobject.GetObjectProperty("name").GetStringValue() == "Bob");
    REQUIRE(merged_subobject.GetObjectProperty("height").GetDoubleValue() == 182.8);
    REQUIRE(merged_subobject.FindObjectProperty("age") == -1);

    // And properties are ordered deterministically
    REQUIRE(merged.FindObjectProperty("foo") == 0);
    REQUIRE(merged.FindObjectProperty("subobject") == 1);
    REQUIRE(merged.FindObjectProperty("bar") == 2);
    REQUIRE(merged_subobject.FindObjectProperty("name") == 0);
    REQUIRE(merged_subobject.FindObjectProperty("height") == 1);
  }

  SECTION("M produce empty object W input list is empty") {
    // Given a target object that has no properties to begin with
    Attribute merged = Attribute::Object();

    // When we call AssembleObject with a list of 0 attribute values
    AttributeMerge::AssembleObject(merged, {});

    // Then we get an object value with no properties
    REQUIRE(merged.GetType() == ValueType::Object);
    REQUIRE(merged.GetObjectPropertyCount() == 0);
  }

  SECTION("M ignore non-object values W input list has non-object values") {
    // Given obj_a: {"foo":100} and obj_b: {"foo":200}
    Attribute obj_a = Attribute::Object(1);
    Attribute obj_b = Attribute::Object(1);
    obj_a.SetObjectProperty("foo", Attribute::Int(100));
    obj_b.SetObjectProperty("foo", Attribute::Int(200));

    // When we call AssembleObject with a list that includes obj_b and obj_a (in
    // that order), but also a bunch of other non-object values
    Attribute merged = Attribute::Object();
    AttributeMerge::AssembleObject(
        merged,
        {Attribute::Null(),
         obj_b,
         Attribute::String("foo"),
         Attribute::Bool(true),
         obj_a,
         Attribute::Array()}
    );

    // Then we get an object value with a single property: obj_a's "foo" value
    // prevails since it appeared last; all non-object values were ignored
    REQUIRE(merged.GetType() == ValueType::Object);
    REQUIRE(merged.GetObjectPropertyCount() == 1);
    REQUIRE(merged.GetObjectProperty("foo").GetIntValue() == 100);
  }

  SECTION("M clear existing properties W destination object is not empty") {
    // Given an object with values for 'foo' and 'bar'
    Attribute obj = Attribute::Object(2);
    obj.SetObjectProperty("foo", Attribute::Int(100));
    obj.SetObjectProperty("bar", Attribute::Int(200));

    // And an object we want to merge in, with values for 'bar' and 'baz'
    Attribute other = Attribute::Object(2);
    other.SetObjectProperty("bar", Attribute::Int(300));
    other.SetObjectProperty("baz", Attribute::Int(400));

    // When we merge the properties from {other} into obj
    AttributeMerge::AssembleObject(obj, {other});

    // Then obj only contains the properties that were present in other ('bar' and
    // 'baz'); 'foo' is no longer present since obj was preemptively cleared
    REQUIRE(obj.GetType() == ValueType::Object);
    REQUIRE(obj.GetObjectPropertyCount() == 2);
    REQUIRE(obj.GetObjectProperty("bar").GetIntValue() == 300);
    REQUIRE(obj.GetObjectProperty("baz").GetIntValue() == 400);
  }
}
