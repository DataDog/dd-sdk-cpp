// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>

#include "datadog/attribute.hpp"
#include "impl/attribute/reference.hpp"
#include "support/attribute_debug.hpp"

using namespace datadog;
using namespace datadog::impl;

// These tests allow us to sanity-check attribute refcounting and COW behavior, using
// the AttributeDebug helper to dump the current state of an attribute, including the
// number of extant references for non-primitive values

static void require_dump(const Attribute& attribute, std::string_view want_literal) {
  // Do some basic string manipulation to make our test cases more pleasant to read
  std::string want;
  if (want_literal.front() == '\n') {
    // If our input value starts with a newline, assume it's a multi-line string
    // that we've aligned with the gutter for readability, and use everything after
    // the initial newline
    want = want_literal.substr(1);
  } else {
    // Otherwise, treat it as a single-line value, and append a newline since our
    // AttributeDebug function always includes a trailing newline
    want = std::string(want_literal) + "\n";
  }
  std::string got = AttributeDebug::ToString(attribute);
  REQUIRE(got == want);
}

TEST_CASE("Attribute refcount", "[unit][attribute]") {
  SECTION("M count references W non-primitive values copied and modified") {
    // Given a single attribute with an initial refcount of 1
    Attribute attribute = Attribute::String("hi");
    require_dump(attribute, "1 hi");

    // When we create two copies referencing that value
    Attribute copy1 = attribute;
    Attribute copy2 = attribute;

    // Then the original attribute knows it's shared
    require_dump(attribute, "3 hi");
    require_dump(copy1, "3 hi");
    require_dump(copy2, "3 hi");

    // Next: When we modify one of the copies
    copy1.SetString("hello");

    // Then the original value's refcount is decremented
    require_dump(attribute, "2 hi");
    require_dump(copy1, "1 hello");
    require_dump(copy2, "2 hi");
  }

  SECTION("M count references W array items refer to same value") {
    // When we create an array with multiple values that share references
    Attribute array = Attribute::Array(4);
    {
      array.ArrayPush(Attribute::String("hello"));
      array.ArrayPush(Attribute::String("you world, you"));
      array.ArrayPush(array.GetArrayItem(0));
      array.ArrayPush(array.GetArrayItem(1));
    }

    // Then we end up with the expected shared references
    require_dump(array, R"(
1 [
  2 hello
  2 you world, you
  2 hello
  2 you world, you
  ]
)");

    // Next: When we access an item from the array, creating a copy
    Attribute item = array.GetArrayItem(0);

    // Then it shares a reference as well
    require_dump(item, "3 hello");
    require_dump(array, R"(
1 [
  3 hello
  2 you world, you
  3 hello
  2 you world, you
  ]
)");

    // Next: When we modify the item
    item.SetString("goodbye");

    // Then its reference to the original value is released
    require_dump(item, "1 goodbye");
    require_dump(array, R"(
1 [
  2 hello
  2 you world, you
  2 hello
  2 you world, you
  ]
)");

    // Next: When we get another copy, then clear the array by any means
    item = array.GetArrayItem(0);
    array.SetNull();

    // Then our copy retains a single reference to the original string value
    require_dump(item, "1 hello");
    require_dump(array, ". null");
  }

  SECTION("M count references W object properties refer to same value") {
    // When we create an object with two references to the same string
    Attribute obj = Attribute::Object(3);
    {
      Attribute int_100 = Attribute::Int(100);
      Attribute string_hi = Attribute::String("hi");

      obj.SetObjectProperty("foo", int_100);
      obj.SetObjectProperty("bar", string_hi);
      obj.SetObjectProperty("baz", string_hi);
    }

    // Then both properties point to the same value
    require_dump(obj, R"(
1 {
  foo: . 100ll
  bar: 2 hi
  baz: 2 hi
  }
)");

    // Next: When we replace the value stored in one of those properties
    obj.SetObjectProperty("baz", Attribute::Int(100));

    // Then the refcount of our original string value is decremented
    require_dump(obj, R"(
1 {
  foo: . 100ll
  bar: 1 hi
  baz: . 100ll
  }
)");

    // Next: When we access our string property value and get a copy
    Attribute property = obj.GetObjectProperty("bar");

    // Then our refcounts are adjusted accordingly
    require_dump(property, "2 hi");
    require_dump(obj, R"(
1 {
  foo: . 100ll
  bar: 2 hi
  baz: . 100ll
  }
)");
  }

  SECTION("M have 1 reference per value W reference object constructed") {
    // When we construct our reference object, leaving `obj` as the only Attribute
    // in scope on the stack
    Attribute obj = init_reference_value();

    // Then we end up with an object where every non-primitive value has 1 reference
    require_dump(obj, R"(
1 {
  process: 1 {
             pid: . 9238451ull
             guid: 1 ccb79084-bc2b-4549-bbc7-f27e153fd4b6
             name: 1 my-cool-program
             args: 1 [
                     1 --mode
                     1 good
                     ]
             }
  state: 1 {
           rect: 1 [
                   1 [
                     . 0.03333
                     . -12.3
                     ]
                   1 [
                     . 94
                     . 98.7001
                     ]
                   ]
           state: 1 [
                    1 {
                      }
                    ]
           offset: . -1ll
           active: . true
           }
  tags: 1 [
          1 blue
          1 meh
          . null
          ]
  }
)");
  }

  SECTION("M increment only root reference W object is cloned") {
    // Given an instance of our reference object
    Attribute obj = init_reference_value();

    // When we copy the attribute that holds its value
    Attribute copy = obj;

    // Then the root object has its reference count incremented, but the values
    // nested within are still referenced only once, because nested Attribute values
    // have not been deep-copied
    require_dump(obj, R"(
2 {
  process: 1 {
             pid: . 9238451ull
             guid: 1 ccb79084-bc2b-4549-bbc7-f27e153fd4b6
             name: 1 my-cool-program
             args: 1 [
                     1 --mode
                     1 good
                     ]
             }
  state: 1 {
           rect: 1 [
                   1 [
                     . 0.03333
                     . -12.3
                     ]
                   1 [
                     . 94
                     . 98.7001
                     ]
                   ]
           state: 1 [
                    1 {
                      }
                    ]
           offset: . -1ll
           active: . true
           }
  tags: 1 [
          1 blue
          1 meh
          . null
          ]
  }
)");
  }

  SECTION("M clone root object and share properties W copied object is modified") {
    // Given an instance of our reference object, and a shallow copy
    Attribute obj = init_reference_value();
    Attribute copy = obj;

    // When we modify the copy by adding an attribute
    copy.SetObjectProperty("blue", Attribute::Int(42));

    // Then refcount on our root object is no longer 2: both obj and copy have their
    // own distinct object CowValues, but the 'process', 'state', and 'tags'
    // subobjects are still shared between them, so they each have a refcount of 2.
    require_dump(copy, R"(
1 {
  process: 2 {
             pid: . 9238451ull
             guid: 1 ccb79084-bc2b-4549-bbc7-f27e153fd4b6
             name: 1 my-cool-program
             args: 1 [
                     1 --mode
                     1 good
                     ]
             }
  state: 2 {
           rect: 1 [
                   1 [
                     . 0.03333
                     . -12.3
                     ]
                   1 [
                     . 94
                     . 98.7001
                     ]
                   ]
           state: 1 [
                    1 {
                      }
                    ]
           offset: . -1ll
           active: . true
           }
  tags: 2 [
          1 blue
          1 meh
          . null
          ]
  blue: . 42ll
  }
)");

    // And our original object reflects the same shared references, but it has its
    // original, unmodified state with no other references to its root value
    require_dump(obj, R"(
1 {
  process: 2 {
             pid: . 9238451ull
             guid: 1 ccb79084-bc2b-4549-bbc7-f27e153fd4b6
             name: 1 my-cool-program
             args: 1 [
                     1 --mode
                     1 good
                     ]
             }
  state: 2 {
           rect: 1 [
                   1 [
                     . 0.03333
                     . -12.3
                     ]
                   1 [
                     . 94
                     . 98.7001
                     ]
                   ]
           state: 1 [
                    1 {
                      }
                    ]
           offset: . -1ll
           active: . true
           }
  tags: 2 [
          1 blue
          1 meh
          . null
          ]
  }
)");
  }
}
