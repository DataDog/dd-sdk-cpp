#pragma once

#include <iostream>
#include <string>
#include <unordered_set>

#include "datadog/attribute.hpp"

namespace datadog::impl {

/**
 * Test-only helper functions used to verify the expected internal state of Attribute
 * objects.
 */
struct AttributeDebug {
  /**
   * Generates a string dump that lists all values held within a given Attribute, nested
   * hierarchically. Each primitive value is prefixed with '.' to indicate inline
   * storage; each non-primitive value held via CowValue is prefixed with a number
   * indicating the current reference count of that CowValue.
   */
  static void Dump(const Attribute& a, std::ostream& out, const std::string& prefix);
  static std::string ToString(const Attribute& a);

  /**
   * Returns an estimate of the total number of heap bytes occupied by this Attribute
   * and all its values, across all allocations.
   *
   * Accounts for the size of all unique CowValue pointers referenced within this
   * Attribute, as well as any additional heap memory held by the underlying STL
   * containers used in those unique CowValues. Does not include the size of the
   * Attribute itself.
   */
  static size_t ComputeHeapSize(const Attribute& a);

 private:
  static size_t ComputeHeapSizeImpl(
      const Attribute& a, std::unordered_set<const CowValue*>& visited
  );
};

}  // namespace datadog::impl
