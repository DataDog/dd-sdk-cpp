// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "datadog/attribute.hpp"

#include "datadog/impl/types/diagnostics.hpp"
#include "datadog/impl/types/rum.hpp"

// Helpers for the `_dd.error.*` attributes that callers attach to error reports. RUM
// errors and logs both honor these, so they live in types rather than in either
// feature.

namespace datadog::impl {

/**
 * Attribute key used by cross-platform wrapper SDKs (e.g. Unity, Flutter) to signal an
 * error's `source_type` when reporting through this SDK's native error/log APIs.
 */
inline constexpr std::string_view kErrorSourceTypeAttributeKey =
    "_dd.error.source_type";

/**
 * Extracts the `_dd.error.source_type` attribute out of the merged event `context`,
 * removing the key so it doesn't leak into the event's serialized `context`/custom
 * attributes, and returns the corresponding `RumErrorSourceType`, if any.
 *
 * Extraction only occurs if the attribute is present in one of `call_attribute_sets`
 * (the per-call attribute sets supplied directly alongside this specific error
 * report), not merely inherited from global or view-level attributes that happen to
 * share the same key — this mirrors the gating behavior of
 * `extract_resource_trace_attributes` in resource.cpp for `_dd.trace_id` et al. If the
 * attribute is present but its value doesn't match a known `RumErrorSourceType`, it is
 * dropped and an unrecognized-value diagnostic warning is emitted.
 */
inline std::optional<RumErrorSourceType> ExtractErrorSourceType(
    std::initializer_list<Attribute> call_attribute_sets,
    Attribute& context,
    const DiagnosticLogger& diagnostic_logger
) {
  const bool present = std::any_of(
      call_attribute_sets.begin(),
      call_attribute_sets.end(),
      [](const Attribute& attrs) {
        return attrs.FindObjectProperty(kErrorSourceTypeAttributeKey) >= 0;
      }
  );
  if (!present) {
    return std::nullopt;
  }

  const Attribute val = context.GetObjectProperty(kErrorSourceTypeAttributeKey);
  std::optional<RumErrorSourceType> result;
  if (val.GetType() == ValueType::String) {
    result = ParseRumErrorSourceType(val.GetStringValue());
    if (!result) {
      diagnostic_logger.Warning(
          "Ignoring _dd.error.source_type attribute: unrecognized value",
          {{"value", val.GetStringValue()}}
      );
    }
  } else {
    diagnostic_logger.Warning(
        "Ignoring _dd.error.source_type attribute: expected a string value"
    );
  }
  context.DeleteObjectProperty(kErrorSourceTypeAttributeKey);
  return result;
}

/**
 * Attribute key users set to attach a custom Error Tracking grouping fingerprint to a
 * RUM `AddError` call or a log error. See
 * `RumAttributes::ErrorCustomFingerprintAttributeKey` and
 * `LogAttributes::ErrorFingerprintAttributeKey`.
 */
inline constexpr std::string_view kErrorFingerprintAttributeKey =
    "_dd.error.fingerprint";

/**
 * Extracts the `_dd.error.fingerprint` attribute out of the merged event `context`,
 * removing the key so it doesn't leak into the event's serialized `context`/custom
 * attributes, and returns its string value, if any. If the attribute is present but
 * isn't a string, it is dropped and a diagnostic warning is emitted.
 */
inline std::optional<std::string> ExtractErrorFingerprint(
    Attribute& context, const DiagnosticLogger& diagnostic_logger
) {
  const Attribute val = context.GetObjectProperty(kErrorFingerprintAttributeKey);
  std::optional<std::string> result;
  if (val.GetType() == ValueType::String) {
    result = std::string(val.GetStringValue());
  } else if (val.GetType() != ValueType::Null) {
    diagnostic_logger.Warning(
        "Ignoring _dd.error.fingerprint attribute: expected a string value"
    );
  }
  context.DeleteObjectProperty(kErrorFingerprintAttributeKey);
  return result;
}

}  // namespace datadog::impl
