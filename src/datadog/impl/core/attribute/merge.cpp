// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/attribute/merge.hpp"

#include "datadog/impl/core/attribute/cow.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

void AttributeMerge::AssembleObject(
    Attribute& mut_obj, std::initializer_list<Attribute> attributes
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

  // Clear the destination object of all existing properties, while ensuring that it has
  // enough space to hold all the values we might need to set, preallocating if needed
  mut_obj.InitObject(max_num_properties);

  // Merge in all top-level properties from our input objects, with input objects that
  // appear later in the list taking precedence in case of name conflicts
  for (const Attribute& attribute : attributes) {
    // For non-object values, num_properties will be 0
    const size_t num_properties = attribute.GetObjectPropertyCount();
    for (int i = 0, n = static_cast<int>(num_properties); i < n; i++) {
      mut_obj.SetObjectProperty(
          attribute.GetObjectPropertyNameAt(i), attribute.GetObjectPropertyValueAt(i)
      );
    }
  }
}

}  // namespace datadog::impl
