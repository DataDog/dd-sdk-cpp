// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/attribute.hpp"

/**
 * Initializes the reference object value used in Attribute implementation tests.
 */
inline datadog::Attribute init_reference_value() {
  using datadog::Attribute;

  static const uint8_t bytes_ccb7[16] = {
      204, 183, 144, 132, 188, 43, 69, 73, 187, 199, 242, 126, 21, 63, 212, 182
  };

  Attribute obj = Attribute::Object(3);
  {
    Attribute args = Attribute::Array(2);
    args.ArrayPush(Attribute::String("--mode"));
    args.ArrayPush(Attribute::String("good"));

    Attribute process = Attribute::Object(4);
    process.SetObjectProperty("pid", Attribute::UInt(9238451));
    process.SetObjectProperty("guid", Attribute::UUID(bytes_ccb7));
    process.SetObjectProperty("name", Attribute::String("my-cool-program"));
    process.SetObjectProperty("args", args);

    obj.SetObjectProperty("process", process);
  }
  {
    Attribute state = Attribute::Object(4);
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
    {
      Attribute inner_state = Attribute::Array(1);
      inner_state.ArrayPush(Attribute::Object());
      state.SetObjectProperty("state", inner_state);
    }
    state.SetObjectProperty("offset", Attribute::Int(-1));
    state.SetObjectProperty("active", Attribute::Bool(true));
    obj.SetObjectProperty("state", state);
  }
  {
    Attribute tags = Attribute::Array(3);
    tags.ArrayPush(Attribute::String("blue"));
    tags.ArrayPush(Attribute::String("meh"));
    tags.ArrayPush(Attribute::Null());
    obj.SetObjectProperty("tags", tags);
  }
  return obj;
}
