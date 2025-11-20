// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "datadog/attribute.hpp"
#include "json.hpp"

namespace datadog::impl {

/**
 * A descriptive error message for use in diagnostic logging and/or telemetry.
 *
 * When some part of the SDK needs to propagate error details back to the caller, it can
 * return an `ErrorMessage`, optionally specifying a static list of named attribute
 * values:
 *
 * - ErrorMessage("failed to create directory", {
 *     {"path", Attribute::String(path)},
 *     {"errno", Attribute::Int(errno)}
 *   });
 *
 * When the caller receives some value `err`, it can wrap it with a prefix to provide
 * additional context:
 *
 * - err = err.AddPrefix("SDK init aborted")
 *
 * Finally, when we call `err.Format()` on the topmost error, we get a final string that
 * can be logged:
 *
 * - SDK init aborted: failed to create directory {"path":"/mnt/foo/bar","errno":7}
 *
 */
class ErrorMessage {
  const char* text;
  Attribute attributes;

  size_t num_prefixes;
  std::array<std::string_view, 4> prefixes;

  explicit ErrorMessage(const char* in_text, const Attribute& in_attributes);

 public:
  explicit ErrorMessage(
      const char* in_text,
      std::initializer_list<std::pair<std::string_view, Attribute>> in_attributes = {}
  );

  ErrorMessage& AddPrefix(const char* prefix);

  std::string Format() const;
};

}  // namespace datadog::impl
