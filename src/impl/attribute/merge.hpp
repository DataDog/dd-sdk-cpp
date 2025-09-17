// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/attribute.hpp"

namespace datadog::impl {

struct AttributeMerge {
  /**
   * Returns true if a property with the given name should be accepted as a valid user
   * attribute; returns false is the given name is reserved.
   */
  using FilterFunc = bool (*)(std::string_view);

  /**
   * Given a target object `mut_obj` and any number of other object attributes, mutates
   * the target object such that it contains the union of all top-level properties found
   * in both `mut_obj` and `attributes`.
   *
   * If two or more objects contain properties with the same name, the value that
   * appears last will take precedence. For example, merging `{"foo":1,"bar":1}` and
   * `{"bar":"two"}` will result in `{"foo":1,"bar":"two"}`.
   *
   * If a `filter` function is provided, each property value being merged into `mut_obj`
   * will first have its name checked. If `filter(name)` returns false, the incoming
   * value will be entirely ignored. If `mut_obj` has an existing value with that name,
   * it will be retained. This function does not remove any existing properties from
   * `mut_obj`.
   *
   * No recursive merging or reconciliation on nested objects is performed: e.g merging
   * `{"obj":{"foo":1,"bar":2}}` with `{"obj":{"bar":3,"baz":4}}` will result in
   * `{"obj":{"bar":3,"baz":4}}`.
   *
   * If `attributes` contains any non-object values, they will be ignored. If
   * `attributes` contains no object values, `mut_obj` will not be modified.
   */
  static void AssembleObject(
      Attribute& mut_obj, std::initializer_list<Attribute> attributes,
      FilterFunc filter = nullptr
  );
};

}  // namespace datadog::impl
