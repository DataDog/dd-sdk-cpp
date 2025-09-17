// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "attribute/merge.hpp"

#include "assert.hpp"
#include "attribute/cow.hpp"

namespace datadog::impl {

void AttributeMerge::AssembleObject(
    Attribute& mut_obj, std::initializer_list<Attribute> attributes,
    AttributeMerge::FilterFunc filter
) {
  // Require that the target attribute is already of type Object
  if (mut_obj.GetType() != ValueType::Object) {
    DATADOG_ASSERT(false, "non-object value passed to AssembleObject");
    return;
  }

  // Determine the worst-case number of properties our result object will have, if there
  // are no conflicts (each property requires ~32 bytes, so overestimating is fine)
  size_t max_num_properties = mut_obj.GetObjectPropertyCount();
  for (const Attribute& attribute : attributes) {
    // Result will be 0 for non-object values
    max_num_properties += attribute.GetObjectPropertyCount();
  }

  // Ensure that our target object has enough space to hold all the values we might need
  // to set, preallocating if necessary
  mut_obj.ReserveObjectPropertyCapacity(max_num_properties);

  // Merge in all top-level properties from our input objects, with input objects that
  // appear later in the list taking precedence in case of name conflicts
  for (const Attribute& attribute : attributes) {
    // For non-object values, num_properties will be 0
    const size_t num_properties = attribute.GetObjectPropertyCount();
    for (int i = 0, n = static_cast<int>(num_properties); i < n; i++) {
      // If a non-root object has a property with a reserved name, ignore it
      std::string_view name = attribute.GetObjectPropertyNameAt(i);
      if (filter && !filter(name)) {
        continue;
      }

      // Property has a non-reserved name: merge its value into the root object
      mut_obj.SetObjectProperty(name, attribute.GetObjectPropertyValueAt(i));
    }
  }
}

}  // namespace datadog::impl
