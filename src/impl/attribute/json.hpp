// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <vector>

#include "datadog/attribute.hpp"

namespace datadog::impl {

/**
 * Lightweight implementation of JSON serialization for Attribute values.
 *
 * Forward-declared as a friend of Attribute to permit direct access to members.
 */
struct AttributeSerialization {
  static size_t ComputeValueLen(const Attribute& attribute);

  static size_t WriteValue(
      const Attribute& attribute, uint8_t* buffer, size_t buffer_size
  );

  static void ToJSON(const Attribute& attribute, std::vector<uint8_t>& out_buffer);
};

}  // namespace datadog::impl
