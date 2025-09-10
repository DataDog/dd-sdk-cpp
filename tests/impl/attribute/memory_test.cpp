#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>

#include "datadog/attribute.hpp"
#include "impl/attribute/reference.hpp"
#include "support/allocation_tracker.hpp"
#include "support/attribute_debug.hpp"
#include "support/memory_checks.hpp"

using namespace datadog;
using namespace datadog::impl;

// These tests document the expected heap memory usage of Attribute values, ensuring
// that we don't accidentally introduce regressions that impact Attribute's memory
// overhead or heap-allocation performance. These tests are intended to supplement (not
// replace) profiling and dynamic analysis steps within our CI pipeline.

// We use two methods to validate Attribute memory usage in these tests:

// 1. `AttributeDebug::ComputeHeapSize` (and `require_heap_size` et al.) examines an
//    `Attribute` value to estimate the current number of heap bytes it occupies, across
//    all allocations. These checks are performed unconditionally, although we don't
//    presume to know the exact expected sizes on every compiler/platform, so checks are
//    strict for environments where we author the tests, and they're a bit looser in
//    other build environments.

// 2. `AllocationTracker` (and `require_allocation_tracker_stats`) uses the code in
//    `support/allocation_tracker.hpp` to instrument all calls to `operator new` and
//    `operator delete`, giving us stats on all allocations/frees that occur in the
//    scope of a given test. These checks only run if WITH_DATADOG_ALLOCATION_TRACKING
//    is enabled for the build. If these checks run, they are comprehensive when
//    WITH_DATADOG_STRICT_MEMORY_CHECKS is enabled; otherwise they simply assert that no
//    memory was leaked and no double-frees occurred.

/**
 * Computes the total heap usage of a given Attribute (across all allocations, including
 * both CowValue structs and STL container, recursively) and verifies that it is exactly
 * the specified size.
 */
static size_t require_exact_heap_size(const Attribute& attribute, size_t want) {
  const size_t got = AttributeDebug::ComputeHeapSize(attribute);
  REQUIRE(got == want);
  return got;
}

/**
 * Validates that the current heap usage of an attribute is the specified size, or close
 * enough to it.
 */
static size_t require_heap_size(const Attribute& attribute, size_t want) {
#ifdef WITH_DATADOG_STRICT_MEMORY_CHECKS
  // If we're building in the same environment in which we observed the expected size,
  // require an exact match
  return require_exact_heap_size(attribute, want);
#else
  // On other platforms/compilers: just do a rough order-of-magnitude check
  const size_t want_min = static_cast<size_t>(static_cast<double>(want) * 0.1);
  const size_t want_max = static_cast<size_t>(static_cast<double>(want) * 10.0);
  const size_t got = AttributeDebug::ComputeHeapSize(attribute);
  REQUIRE(got >= want_min);
  REQUIRE(got <= want_max);
  return got;
#endif
}

/**
 * Validates that the current heap usage of an attribute is near enough to the specified
 * size, and also strictly validates that its current size is larger than the last-known
 * size indicated by `prev`.
 */
static size_t require_increased_heap_size(
    const Attribute& attribute, size_t want, size_t prev
) {
  const size_t got = require_heap_size(attribute, want);
  REQUIRE(got > prev);
  return got;
}

/**
 * Validates the expected heap usage of code executed during a single test, in terms of
 * actual observed calls to new and delete operators.
 *
 * Whereas `AttributeDebug::ComputeHeapSize` takes an estimated snapshot of an
 * attribute's current heap usage, these stats are gathered via
 * `allocation_tracker.hpp`, which overrides operator new and delete in order to profile
 * all allocations and frees that happen during the test (with the notable exception
 * that direct calls to malloc/free/etc will not be profiled).
 *
 * If WITH_DATADOG_ALLOCATION_TRACKING is false for this build, no allocation-tracking
 * overrides are present in the build, and this function is a complete no-op.
 *
 * Otherwise, this function stops the gathering of allocation profiling data, then
 * enforces some basic checks to ensure that the tracker had enough space to record all
 * allocations/frees, and that no leaks or double-frees occurred. These checks occur
 * strictly on all platforms.
 *
 * Additionally, if we're using the canonical platform and compiler for strict memory
 * checks, and a function `f` is provided: the checks implemented in the function will
 * be run as well.
 */
static void require_allocation_tracker_stats(
    AllocationTracker& tracker,
    std::function<void(const AllocationTracker::Stats&)> f = nullptr
) {
  // In a non-test-tracker build, AllocationTracker will do nothing and return an
  // empty stats object: do nothing in that case
  const auto stats = tracker.Stop();
  if (!stats.enabled) {
    return;
  }

  // Validate basic assertions about allocations and frees
  REQUIRE(stats.num_events_dropped == 0);
  REQUIRE(stats.num_bytes_freed == stats.num_bytes_allocated);
  REQUIRE(stats.num_frees == stats.num_allocs);

  // Assume that the provided callback validates precise compiler-specific behavior:
  // only run it on the platform where we're authoring tests
#ifdef WITH_DATADOG_STRICT_MEMORY_CHECKS
  if (f) {
    f(stats);
  }
#else
  (void)f;
#endif
}

TEST_CASE("Attribute memory", "[unit][attribute]") {
  SECTION("M use no heap memory W type is primitive") {
    AllocationTracker tracker;
    {
      // Primitives require no heap memory
      require_exact_heap_size(Attribute::Null(), 0);
      require_exact_heap_size(Attribute::Bool(true), 0);
      require_exact_heap_size(Attribute::Int(-1), 0);
      require_exact_heap_size(Attribute::UInt(1), 0);
      require_exact_heap_size(Attribute::Double(1.01), 0);
    }
    const AllocationTracker::Stats stats = tracker.Stop();
    REQUIRE(stats.num_allocs == 0);
  }

  SECTION("M use heap memory W type is string") {
    AllocationTracker tracker;
    {
      // Small strings typically fit inside the CowValue thanks to SSO; larger
      // strings will heap-allocate based on default std::string behavior
      require_heap_size(Attribute::String(""), 32);
      require_heap_size(Attribute::String("hi"), 32);
      require_heap_size(Attribute::String("hello world"), 32);
      require_heap_size(Attribute::String("goodbye small string optimization"), 71);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          REQUIRE(stats.num_allocs == 13);
          REQUIRE(stats.min_alloc_size == 16);
          REQUIRE(stats.mean_alloc_size == 25);
          REQUIRE(stats.max_alloc_size == 40);
          REQUIRE(stats.num_bytes_allocated == 328);
        }
    );
  }

  SECTION("M allocate 1x W creating a single SSO string") {
    AllocationTracker tracker;
    {
      // Given a string attribute initialized from a value that's small enough to
      // employ small string optimization (SSO)
      Attribute::String("tiny string!");
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // One 32-byte allocation for the CowValue
          REQUIRE(stats.num_allocs == 1);
          REQUIRE(stats.num_bytes_allocated == 32);
        }
    );
  }

  SECTION("M allocate 2x W creating a single non-SSO string") {
    AllocationTracker tracker;
    {
      // Given a string that's too long to fit in stack space
      Attribute::String("string of thirty-two characters.");
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the string CowValue
          // 40 bytes to initialize std::string from our literal
          REQUIRE(stats.num_allocs == 2);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 40);
          REQUIRE(stats.num_bytes_allocated == 72);
        }
    );
  }

  SECTION("M not reallocate W updating string value with len <= existing") {
    AllocationTracker tracker;
    {
      // Given a string that's too long to fit in stack space
      Attribute attribute = Attribute::String("string of thirty-two characters.");

      // When we modify the string value
      attribute.SetString("a different 32-character string.");
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // Then we have no additional allocations for the update, just:
          // - 32 bytes for the string CowValue
          // - 40 bytes to initialize std::string from our literal
          REQUIRE(stats.num_allocs == 2);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 40);
          REQUIRE(stats.num_bytes_allocated == 72);
        }
    );
  }

  SECTION("M reallocate W updating string value with len > existing") {
    AllocationTracker tracker;
    {
      // Given a string that's too long to fit in stack space
      Attribute attribute = Attribute::String("string of thirty-two characters.");

      // When we modify the string value with a longer value
      attribute.SetString(
          "This string, I'm sorry to say, exceeds the thirty-two character "
          "threshold quite easily, weighing in at a total of 128 characters"
      );
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // Then we have our initial two allocations:
          // - 32 bytes for the string CowValue
          // - 40 bytes to initialize std::string from our literal
          // Following by another allocation when the value is changed:
          // - 136 bytes from std::string operator=
          REQUIRE(stats.num_allocs == 3);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 136);
          REQUIRE(stats.num_bytes_allocated == 208);
        }
    );
  }

  SECTION("M use heap memory W type is array") {
    AllocationTracker tracker;
    {
      // An empty array is sizeof(CowValue); specifiying an initial capacity
      // reserves N * sizeof(Attribute) bytes
      require_heap_size(Attribute::Array(), 32);
      require_heap_size(Attribute::Array(4), 96);
      require_heap_size(Attribute::Array(10), 192);

      // Adding an attribute beyond the initial capacity will reallocate according
      // to the default behavior of std::vector
      Attribute attribute = Attribute::Array(2);
      require_heap_size(attribute, 64);
      attribute.ArrayPush(Attribute::Int(100));
      require_heap_size(attribute, 64);
      attribute.ArrayPush(Attribute::Int(200));
      require_heap_size(attribute, 64);
      attribute.ArrayPush(Attribute::Int(300));
      require_heap_size(attribute, 96);

      // Clearing the array will not shrink the underlying vector
      attribute.ArrayClear();
      require_heap_size(attribute, 96);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          REQUIRE(stats.num_allocs == 24);
          REQUIRE(stats.min_alloc_size == 16);
          REQUIRE(stats.mean_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 768);
        }
    );
  }

  SECTION("M reuse existing buffer W array is reinitialized") {
    AllocationTracker tracker;
    {
      // Given an array that has already allocated some space
      Attribute attribute = Attribute::Array(4);
      const size_t initial = require_heap_size(attribute, 96);
      attribute.ArrayPush(Attribute::Int(100));
      attribute.ArrayPush(Attribute::Int(101));
      attribute.ArrayPush(Attribute::Int(102));
      attribute.ArrayPush(Attribute::Int(103));
      require_exact_heap_size(attribute, initial);
      attribute.ArrayPush(Attribute::Int(104));
      const size_t hwm = require_heap_size(attribute, 160);

      // When we reinitialize the array value with a capacity <= existing capacity
      REQUIRE(attribute.GetArrayLen() == 5);
      attribute.InitArray(4);
      REQUIRE(attribute.GetArrayLen() == 0);

      // Then the array retains its existing buffer
      require_exact_heap_size(attribute, hwm);
      attribute.ArrayPush(Attribute::Int(100));
      attribute.ArrayPush(Attribute::Int(101));
      attribute.ArrayPush(Attribute::Int(102));
      attribute.ArrayPush(Attribute::Int(103));
      attribute.ArrayPush(Attribute::Int(104));
      require_exact_heap_size(attribute, hwm);

      // Next: When we reinitialize the same array with a greater capacity
      REQUIRE(attribute.GetArrayLen() == 5);
      attribute.InitArray(16);
      REQUIRE(attribute.GetArrayLen() == 0);

      // Then our existing container reserves more memory
      require_increased_heap_size(attribute, 288, hwm);

      // Next: When we change the attribute's type
      attribute.SetNull();
      require_exact_heap_size(attribute, 0);

      // And then change it back to an array with a different initial capacity
      attribute.InitArray(4);

      // Then the new array has a brand new buffer which reflects that initial
      // capacity, as the original buffer was released when the attribute changed
      // type
      require_heap_size(attribute, initial);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          REQUIRE(stats.num_allocs == 20);
          REQUIRE(stats.min_alloc_size == 16);
          REQUIRE(stats.mean_alloc_size == 42);
          REQUIRE(stats.max_alloc_size == 256);
          REQUIRE(stats.num_bytes_allocated == 856);
        }
    );
  }

  SECTION("M allocate 2x W populating an array of primitives w known size") {
    AllocationTracker tracker;
    {
      // Guven an array that contains four primitive values
      Attribute attribute = Attribute::Array(4);
      attribute.ArrayPush(Attribute::Null());
      attribute.ArrayPush(Attribute::UInt(100));
      attribute.ArrayPush(Attribute::Double(99.9));
      attribute.ArrayPush(Attribute::Bool(false));
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the array CowValue
          // 64 bytes to reserve space for 4 Attribute values
          REQUIRE(stats.num_allocs == 2);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 64);
          REQUIRE(stats.num_bytes_allocated == 96);
        }
    );
  }

  SECTION("M allocate 4x W populating an array with one string value") {
    AllocationTracker tracker;
    {
      // Given an array that contains four references to the same string value
      Attribute attribute = Attribute::Array(4);
      attribute.ArrayPush(Attribute::String("string of thirty-two characters."));
      attribute.ArrayPush(attribute.GetArrayItem(0));
      attribute.ArrayPush(attribute.GetArrayItem(0));
      attribute.ArrayPush(attribute.GetArrayItem(0));
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the array CowValue
          // 64 bytes to reserve space for 4 Attribute values
          // 32 bytes for the string CowValue
          // 40 bytes to initialize std::string from our literal
          REQUIRE(stats.num_allocs == 4);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.mean_alloc_size == 42);
          REQUIRE(stats.max_alloc_size == 64);
          REQUIRE(stats.num_bytes_allocated == 168);
        }
    );
  }

  SECTION("M not reallocate W reserving array capacity <= existing") {
    AllocationTracker tracker;
    {
      // Given an array that's initialized with 64 primitive values
      Attribute attribute = Attribute::Array(64);
      for (int i = 0; i < 64; i++) {
        attribute.ArrayPush(Attribute::Int(i));
      }

      // When we attempt to reserve space for  32, 48, or 64 items
      attribute.ArrayReserve(32);
      attribute.ArrayReserve(48);
      attribute.ArrayReserve(64);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // Then we make an initial couple of allocations:
          // - 32 bytes for the array CowValue
          // - 1024 bytes to reserve space for the initial 64 Attribute values
          // And no allocations thereafter
          REQUIRE(stats.num_allocs == 2);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 1024);
          REQUIRE(stats.num_bytes_allocated == 1056);
        }
    );
  }

  SECTION("M reallocate W reserving array capacity > existing") {
    AllocationTracker tracker;
    {
      // Given an array that's initialized with 64 primitive values
      Attribute attribute = Attribute::Array(64);
      for (int i = 0; i < 64; i++) {
        attribute.ArrayPush(Attribute::Int(i));
      }

      // When we attempt to reserve space for 128 items
      attribute.ArrayReserve(128);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // Then we make the same two allocations initially:
          // - 32 bytes for the array CowValue
          // - 1024 bytes to reserve space for the initial 64 Attribute values
          // And another on reserve:
          // - 2048 bytes to reserve a larger buffer to fit 128 Attribute values
          REQUIRE(stats.num_allocs == 3);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 2048);
          REQUIRE(stats.num_bytes_allocated == 3104);
        }
    );
  }

  SECTION("M use heap memory W type is object") {
    AllocationTracker tracker;
    {
      // An empty object is sizeof(CowValue); specifiying an initial capacity
      // reserves N * sizeof(std::pair<std::string,Attribute>) bytes
      require_heap_size(Attribute::Object(), 32);
      require_heap_size(Attribute::Object(4), 192);
      require_heap_size(Attribute::Object(10), 432);

      // Adding an attribute beyond initial capacity will reallocate according to
      // default behavior of std::vector
      Attribute attribute = Attribute::Object();
      require_heap_size(attribute, 32);
      attribute.SetObjectProperty("foo", Attribute::Int(10));
      require_heap_size(attribute, 72);
      attribute.SetObjectProperty("bar", Attribute::Int(20));
      require_heap_size(attribute, 112);

      // Deleting an attribute will not shrink the underlying vector
      attribute.DeleteObjectProperty("foo");
      require_heap_size(attribute, 112);

      // Keys that fit into SSO storage will not allocate additional heap memory
      attribute.SetObjectProperty("hi-sso", Attribute::Int(30));
      require_heap_size(attribute, 112);

      // Keys that require heap memory will increase the total heap size used by
      // the attribute (n.b. string memory and Attribute/CowValue memory are not
      // necessarily colocated)
      attribute.DeleteObjectProperty("hi-sso");
      attribute.SetObjectProperty(
          "goodbye small string optimization", Attribute::Int(40)
      );
      require_heap_size(attribute, 151);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          REQUIRE(stats.num_allocs == 27);
          REQUIRE(stats.min_alloc_size == 16);
          REQUIRE(stats.mean_alloc_size == 44);
          REQUIRE(stats.max_alloc_size == 400);
          REQUIRE(stats.num_bytes_allocated == 1208);
        }
    );
  }

  SECTION("M reuse existing buffer W object is reinitialized") {
    AllocationTracker tracker;
    {
      // Given an object that has already allocated some space
      Attribute attribute = Attribute::Object(4);
      const size_t initial = require_heap_size(attribute, 192);
      attribute.SetObjectProperty("foo0", Attribute::Int(100));
      attribute.SetObjectProperty("foo1", Attribute::Int(101));
      attribute.SetObjectProperty("foo2", Attribute::Int(102));
      attribute.SetObjectProperty("foo3", Attribute::Int(103));
      require_exact_heap_size(attribute, initial);
      attribute.SetObjectProperty("foo4", Attribute::Int(104));
      const size_t hwm = require_heap_size(attribute, 352);

      // When we reinitialize the object value with a capacity <= existing
      // capacity
      REQUIRE(attribute.GetObjectPropertyCount() == 5);
      attribute.InitObject(4);
      REQUIRE(attribute.GetObjectPropertyCount() == 0);

      // Then the object retains its existing buffer
      require_exact_heap_size(attribute, hwm);
      attribute.SetObjectProperty("foo0", Attribute::Int(100));
      attribute.SetObjectProperty("foo1", Attribute::Int(101));
      attribute.SetObjectProperty("foo2", Attribute::Int(102));
      attribute.SetObjectProperty("foo3", Attribute::Int(103));
      attribute.SetObjectProperty("foo4", Attribute::Int(104));
      require_exact_heap_size(attribute, hwm);

      // Next: When we reinitialize the same object with a greater capacity
      REQUIRE(attribute.GetObjectPropertyCount() == 5);
      attribute.InitObject(16);
      REQUIRE(attribute.GetObjectPropertyCount() == 0);

      // Then our existing container reserves more memory
      require_increased_heap_size(attribute, 672, hwm);

      // Next: When we change the attribute's type
      attribute.SetNull();
      require_exact_heap_size(attribute, 0);

      // And then change it back to an object with a different initial capacity
      attribute.InitObject(4);

      // Then the new object has a brand new buffer which reflects that initial
      // capacity, as the original buffer was released when the attribute changed
      // type
      require_heap_size(attribute, initial);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          REQUIRE(stats.num_allocs == 20);
          REQUIRE(stats.min_alloc_size == 16);
          REQUIRE(stats.mean_alloc_size == 81);
          REQUIRE(stats.max_alloc_size == 640);
          REQUIRE(stats.num_bytes_allocated == 1624);
        }
    );
  }

  SECTION("M allocate 2x W populating an object of primitives w known size") {
    AllocationTracker tracker;
    {
      // Given an object with four primitive values, indexed with names that are
      // short enough to fit within the SSO threshold
      Attribute attribute = Attribute::Object(4);
      attribute.SetObjectProperty("short0", Attribute::Null());
      attribute.SetObjectProperty("short1", Attribute::UInt(100));
      attribute.SetObjectProperty("short2", Attribute::Double(99.9));
      attribute.SetObjectProperty("short3", Attribute::Bool(false));
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the object CowValue
          // 160 bytes to reserve space for 4 pairs of std::string and Attribute
          // 0 bytes from std::string, as keys are short enough for SSO
          REQUIRE(stats.num_allocs == 2);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 192);
        }
    );
  }

  SECTION("M allocate 6x W populating primitives with long property names") {
    AllocationTracker tracker;
    {
      // Given an object with four primitive values, indexed with long names that
      // exceed the SSO threshold
      Attribute attribute = Attribute::Object(4);
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong0", Attribute::Null()
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong1", Attribute::UInt(100)
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong2", Attribute::Double(99.9)
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong3", Attribute::Bool(false)
      );
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the object CowValue
          // 160 bytes to reserve space for 4 pairs of std::string and Attribute
          // 40 for property 0's name
          // 40 for property 1's name
          // 40 for property 2's name
          // 40 for property 3's name
          REQUIRE(stats.num_allocs == 6);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.mean_alloc_size == 58);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 352);
        }
    );
  }

  SECTION("M allocate 14x W populating unique strings with long property names") {
    AllocationTracker tracker;
    {
      // Given an object that contains four unique string values that are
      // independently initialized, not copied using shared references
      Attribute attribute = Attribute::Object(4);
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong0",
          Attribute::String(
              "a string that's so long, it has exactly sixty-four characters!!!"
          )
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong1",
          Attribute::String(
              "a string that's so long, it has exactly sixty-four characters!!!"
          )
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong2",
          Attribute::String(
              "a string that's so long, it has exactly sixty-four characters!!!"
          )
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong3",
          Attribute::String(
              "a string that's so long, it has exactly sixty-four characters!!!"
          )
      );
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the object CowValue
          // 160 bytes to reserve space for 4 pairs of std::string and Attribute
          // 32 bytes for property 0's string CowValue
          // 72 bytes for property 0's underlying std::string
          // 40 bytes for property 0's name
          // 32 bytes for property 1's string CowValue
          // 72 bytes for property 1's underlying std::string
          // 40 bytes for property 1's name
          // 32 bytes for property 2's string CowValue
          // 72 bytes for property 2's underlying std::string
          // 40 bytes for property 2's name
          // 32 bytes for property 3's string CowValue
          // 72 bytes for property 3's underlying std::string
          // 40 bytes for property 3's name
          REQUIRE(stats.num_allocs == 14);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.mean_alloc_size == 54);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 768);
        }
    );
  }

  SECTION("M allocate 8x W populating string copies with long property names") {
    AllocationTracker tracker;
    {
      // Given an object which contains four shared references to a single string
      Attribute attribute = Attribute::Object(4);
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong0",
          Attribute::String(
              "a string that's so long, it has exactly sixty-four characters!!!"
          )
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong1", attribute.GetObjectPropertyValueAt(0)
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong2", attribute.GetObjectPropertyValueAt(0)
      );
      attribute.SetObjectProperty(
          "loooooooooooooooooooooooooooong3", attribute.GetObjectPropertyValueAt(0)
      );
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the object CowValue
          // 160 bytes to reserve space for 4 pairs of std::string and Attribute
          // 32 bytes for property 0's string CowValue
          // 72 bytes for property 0's underlying std::string
          // 40 bytes for property 0's name
          // 40 bytes for property 1's name
          // 40 bytes for property 2's name
          // 40 bytes for property 3's name
          REQUIRE(stats.num_allocs == 8);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.mean_alloc_size == 57);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 456);
        }
    );
  }

  SECTION("M allocate 4x W populating string copies with short property names") {
    AllocationTracker tracker;
    {
      Attribute attribute = Attribute::Object(4);
      attribute.SetObjectProperty(
          "short0",
          Attribute::String(
              "a string that's so long, it has exactly sixty-four characters!!!"
          )
      );
      attribute.SetObjectProperty("short1", attribute.GetObjectPropertyValueAt(0));
      attribute.SetObjectProperty("short2", attribute.GetObjectPropertyValueAt(0));
      attribute.SetObjectProperty("short3", attribute.GetObjectPropertyValueAt(0));
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the object CowValue
          // 160 bytes to reserve space for 4 pairs of std::string and Attribute
          // 32 bytes for property 0's string CowValue
          // 72 bytes for property 0's underlying std::string
          REQUIRE(stats.num_allocs == 4);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.mean_alloc_size == 74);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 296);
        }
    );
  }

  SECTION("M not reallocate W reserving object capacity <= existing") {
    AllocationTracker tracker;
    {
      // Given an object that's initialized with 64 properties with short names
      // and primitive values
      Attribute attribute = Attribute::Object(64);
      for (int i = 0; i < 64; i++) {
        char buf[16] = {0};
        auto res = std::to_chars(buf, buf + sizeof(buf), i);
        REQUIRE(res.ec == std::errc{});
        const size_t num_bytes_written = static_cast<size_t>(res.ptr - buf);
        const std::string_view name{buf, num_bytes_written};
        attribute.SetObjectProperty(name, Attribute::Int(i));
      }
      REQUIRE(attribute.GetObjectProperty("9").GetIntValue() == 9);
      REQUIRE(attribute.GetObjectProperty("10").GetIntValue() == 10);

      // When we attempt to reserve space for 32, 48, or 64 properties
      attribute.ReserveObjectPropertyCapacity(32);
      attribute.ReserveObjectPropertyCapacity(48);
      attribute.ReserveObjectPropertyCapacity(64);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // Then we make an initial couple of allocations:
          // - 32 bytes for the array CowValue
          // - 2560 bytes to reserve space for the initial 64 properties
          // And no allocations thereafter
          REQUIRE(stats.num_allocs == 2);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 2560);
          REQUIRE(stats.num_bytes_allocated == 2592);
        }
    );
  }

  SECTION("M reallocate W reserving object capacity > existing") {
    AllocationTracker tracker;
    {
      // Given an object that's initialized with 64 properties with short names
      // and primitive values
      Attribute attribute = Attribute::Object(64);
      for (int i = 0; i < 64; i++) {
        char buf[16] = {0};
        auto res = std::to_chars(buf, buf + sizeof(buf), i);
        REQUIRE(res.ec == std::errc{});
        const size_t num_bytes_written = static_cast<size_t>(res.ptr - buf);
        const std::string_view name{buf, num_bytes_written};
        attribute.SetObjectProperty(name, Attribute::Int(i));
      }
      REQUIRE(attribute.GetObjectProperty("9").GetIntValue() == 9);
      REQUIRE(attribute.GetObjectProperty("10").GetIntValue() == 10);

      // When we attempt to reserve space for 128 properties;
      attribute.ReserveObjectPropertyCapacity(128);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // Then we make the same two allocations initially:
          // - 32 bytes for the array CowValue
          // - 2560 bytes to reserve space for the initial 64 properties
          // And one more on reserve:
          // - 5120 bytes to reserve a larger buffer to fit 128 properties
          REQUIRE(stats.num_allocs == 3);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 5120);
          REQUIRE(stats.num_bytes_allocated == 7712);
        }
    );
  }

  SECTION("M not allocate W cow value is copied on access") {
    AllocationTracker tracker;
    {
      Attribute attribute = Attribute::Object(4);
      attribute.SetObjectProperty(
          "short0",
          Attribute::String(
              "a string that's so long, it has exactly sixty-four characters!!!"
          )
      );
      attribute.SetObjectProperty("short1", attribute.GetObjectPropertyValueAt(0));
      attribute.SetObjectProperty("short2", attribute.GetObjectPropertyValueAt(0));
      attribute.SetObjectProperty("short3", attribute.GetObjectPropertyValueAt(0));

      // This Attribute is just 16 bytes on the stack: a type tag and a pointer to
      // the same CowValue held by the object's properties
      Attribute copy = attribute.GetObjectProperty("short1");
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the object CowValue
          // 160 bytes to reserve space for 4 pairs of std::string and Attribute
          // 32 bytes for property 0's string CowValue
          // 72 bytes for property 0's underlying std::string
          REQUIRE(stats.num_allocs == 4);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.mean_alloc_size == 74);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 296);
        }
    );
  }

  SECTION("M allocate 2x W copied cow value is modified") {
    AllocationTracker tracker;
    {
      Attribute attribute = Attribute::Object(4);
      attribute.SetObjectProperty(
          "short0",
          Attribute::String(
              "a string that's so long, it has exactly sixty-four characters!!!"
          )
      );
      attribute.SetObjectProperty("short1", attribute.GetObjectPropertyValueAt(0));
      attribute.SetObjectProperty("short2", attribute.GetObjectPropertyValueAt(0));
      attribute.SetObjectProperty("short3", attribute.GetObjectPropertyValueAt(0));

      Attribute copy = attribute.GetObjectProperty("short1");

      // This copy shares its CowValue reference with the object properties, so
      // modifying it creates a copy, resulting in a CowValue allocation and
      // another std::string allocation (if the string value is long enough)
      copy.SetString("string of thirty-two characters.");
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // 32 bytes for the object CowValue
          // 160 bytes to reserve space for 4 pairs of std::string and Attribute
          // 32 bytes for property 0's string CowValue
          // 72 bytes for property 0's underlying std::string
          // 32 bytes for the copy's new string CowValue
          // 72 bytes for the copy's std::string
          REQUIRE(stats.num_allocs == 6);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.mean_alloc_size == 66);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 400);
        }
    );
  }

  SECTION("M not allocate W cow value is copied") {
    AllocationTracker tracker;
    {
      // Given an array that will hold four references to the same value, with
      // space preallocated for three items
      Attribute array = Attribute::Array(3);
      const size_t initial = require_heap_size(array, 80);

      // When we add an initial item to our array
      array.ArrayPush(Attribute::String("string of thirty-two characters."));

      // Then the overall heap usage of our array increases
      const size_t w_property = require_increased_heap_size(array, 151, initial);

      // Next: When we add another reference to the exact same value
      array.ArrayPush(array.GetArrayItem(0));

      // Then our heap size doesn't change, because we already preallocated space
      // for 3 items, and our new Attribute at index 1 just contains a copy of the
      // string CowValue's address
      require_exact_heap_size(array, w_property);

      // Next: When we add a third reference, then we also don't grow
      array.ArrayPush(array.GetArrayItem(0));
      require_exact_heap_size(array, w_property);

      // Next: If we add a fourth reference to the same value, then we probably
      // _will_ grow, because we're exceeding our preallocated capacity and the
      // underlying vector probably needs to reallocate (but it's possible for
      // std::vector::reserve(3) to allocate space for more than 3 items, so don't
      // strictly enforce this assertion on all platforms)
      array.ArrayPush(array.GetArrayItem(0));
      require_heap_size(array, 199);
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          REQUIRE(stats.num_allocs == 19);
          REQUIRE(stats.min_alloc_size == 16);
          REQUIRE(stats.mean_alloc_size == 28);
          REQUIRE(stats.max_alloc_size == 96);
          REQUIRE(stats.num_bytes_allocated == 544);
        }
    );
  }

  SECTION("M allocate as expected W reference object constructed") {
    AllocationTracker tracker;
    {
      // Construct our reference object
      Attribute obj = init_reference_value();
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // This reasonably representative example shows us that our most
          // significant issue w.r.t. allocation is the frequency of small
          // allocations, and that the lowest-hanging fruit would probably be to
          // implement a CowValue pool allocator:
          //  32 from CowValue::Object()
          // 120 from vector<pair<string,Attribute>>::reserve()
          //  32 from CowValue::Array()
          //  32 from vector<Attribute>::reserve()
          //  32 from CowValue::String()
          //  32 from CowValue::String()
          //  32 from CowValue::Object()
          // 120 from vector<pair<string,Attribute>>::reserve()
          //  32 from CowValue::String()
          //  32 from CowValue::Object()
          // 160 from vector<pair<string,Attribute>>::reserve()
          //  32 from CowValue::Array()
          //  32 from vector<Attribute>::reserve()
          //  32 from CowValue::Array()
          //  32 from vector<Attribute>::reserve()
          //  32 from CowValue::Array()
          //  32 from vector<Attribute>::reserve()
          //  32 from CowValue::Array()
          //  16 from vector<Attribute>::reserve()
          //  32 from CowValue::Object()
          //  32 from CowValue::Array()
          //  48 from vector<Attribute>::reserve()
          //  32 from CowValue::String()
          //  32 from CowValue::String()
          REQUIRE(stats.num_allocs == 24);
          REQUIRE(stats.min_alloc_size == 16);
          REQUIRE(stats.mean_alloc_size == 44);
          REQUIRE(stats.max_alloc_size == 160);
          REQUIRE(stats.num_bytes_allocated == 1072);
        }
    );
  }

  SECTION("M not allocate additional memory W object is copied") {
    AllocationTracker tracker;
    {
      // Construct our reference object, then make a copy
      Attribute obj = init_reference_value();
      Attribute copy = obj;
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // Observed allocation stats are exactly identical; copying our value
          // just involved incrementing a reference count and populating 16 bytes
          // on the stack
          REQUIRE(stats.num_allocs == 24);
          REQUIRE(stats.num_bytes_allocated == 1072);
        }
    );
  }

  SECTION("M allocate additional CowValue W shared reference is cloned") {
    AllocationTracker tracker;
    {
      // Given a non-primitive value that's referenced by two different Attributes
      Attribute attribute = Attribute::String("string of thirty-two characters.");
      Attribute copy = attribute;

      // When either Attribute updates its value
      copy.SetString("a different 32-character string.");

      // Then the value is cloned
      REQUIRE(attribute.GetStringValue() == "string of thirty-two characters.");
      REQUIRE(copy.GetStringValue() == "a different 32-character string.");
    }
    require_allocation_tracker_stats(
        tracker, [](const AllocationTracker::Stats& stats) {
          // And we see the results of initializing our value to begin with:
          // - 32 bytes for the string CowValue
          // - 40 bytes to initialize std::string from our literal
          // Followed by the results of cloning a new value:
          // - 32 bytes for the string CowValue
          // - 40 bytes to initialize std::string from our literal
          REQUIRE(stats.num_allocs == 4);
          REQUIRE(stats.min_alloc_size == 32);
          REQUIRE(stats.max_alloc_size == 40);
          REQUIRE(stats.num_bytes_allocated == 144);
        }
    );
  }
}
