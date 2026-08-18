// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <optional>
#include <string_view>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

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
 * attributes, and returns its string value, if any.
 *
 * Extraction only occurs if the attribute is present in one of `call_attribute_sets`
 * (the per-call attribute sets supplied directly alongside this specific error
 * report), not merely inherited from global or view-level attributes that happen to
 * share the same key. If the attribute is present but isn't a string, it is dropped and
 * a diagnostic warning is emitted.
 */
inline std::optional<std::string> ExtractErrorFingerprint(
    std::initializer_list<Attribute> call_attribute_sets,
    Attribute& context,
    const DiagnosticLogger& diagnostic_logger
) {
  const bool present = std::any_of(
      call_attribute_sets.begin(),
      call_attribute_sets.end(),
      [](const Attribute& attrs) {
        return attrs.FindObjectProperty(kErrorFingerprintAttributeKey) >= 0;
      }
  );
  if (!present) {
    return std::nullopt;
  }

  const Attribute val = context.GetObjectProperty(kErrorFingerprintAttributeKey);
  std::optional<std::string> result;
  if (val.GetType() == ValueType::String) {
    result = std::string(val.GetStringValue());
  } else {
    diagnostic_logger.Warning(
        "Ignoring _dd.error.fingerprint attribute: expected a string value"
    );
  }
  context.DeleteObjectProperty(kErrorFingerprintAttributeKey);
  return result;
}

/**
 * Utilities for enriching RUM event payloads with context from CoreContext.
 */
struct RumEventEnrichment {
 public:
  /**
   * Populates a RUM event's `os` and `device` fields with properties retrieved from
   * CoreContext.
   *
   * @param context CoreContext containing OS and device info. If CoreContext lacks
   *  OS/device info, corresponding event fields remain unchanged.
   * @param ev RUM event with `OmitIfNoValue<RumOSProperties> os` and
   *  `OmitIfNoValue<RumDeviceProperties> device` fields, to be modified in-place.
   */
  template <typename T>
  static void PopulateCommonProperties(const CoreContext& context, T& ev) {
    // Set 'ddtags' to reflect config: 'service:<service>,env:<env>' etc.
    ev.ddtags = context.per_event_ddtags;

    // If SystemInfo details are available, populate 'os' and 'device' properties
    PopulateOsProperties(context, ev);
    PopulateDeviceProperties(context, ev);

    // If the SDK has been configured with UserInfo, populate 'usr'
    PopulateUserProperties(context, ev);

    // If we have AccountInfo, populate 'account'
    PopulateAccountProperties(context, ev);
  }

 private:
  /**
   * Populates a RUM event's `os` field with OS properties from CoreContext.
   *
   * @param ctx CoreContext containing OS info. If ctx.os is null, ev.os remains
   *  unchanged.
   * @param ev RUM event with an `OmitIfNoValue<RumOSProperties> os` field, to be
   *  modified in-place.
   */
  template <typename T>
  static void PopulateOsProperties(const CoreContext& ctx, T& ev) {
    if (!ctx.os) {
      return;
    }

    // Construct RumOSProperties with required fields, and conditionally set optional
    // 'build' field
    ev.os.value.emplace(ctx.os->name, ctx.os->version, ctx.os->version_major);
    if (!ctx.os->build.empty()) {
      ev.os.value->build = ctx.os->build;
    }
  }

  /**
   * Populates a RUM event's `device` field with device properties from CoreContext.
   *
   * @param ctx CoreContext containing device info. If ctx.device is null, ev.device
   *  remains unchanged.
   * @param ev RUM event with an `OmitIfNoValue<RumDeviceProperties> device` field, to
   * be modified in-place.
   */
  template <typename T>
  static void PopulateDeviceProperties(const CoreContext& ctx, T& ev) {
    if (!ctx.device) {
      return;
    }

    // Construct RumDeviceProperties and set all fields
    RumDeviceProperties& device = ev.device.value.emplace();
    if (!ctx.device->type.empty()) {
      DATADOG_ASSERT(
          ctx.device->type == "desktop",
          "DeviceInfo specifies non-desktop platform; RUM event serialization code "
          "must be updated"
      );
      device.type = RumDeviceType::Desktop;
    }
    device.name = ctx.device->name;
    device.model = ctx.device->model;
    device.brand = ctx.device->brand;
    device.architecture = ctx.device->architecture;
    device.locale = ctx.device->locale;
    device.time_zone = ctx.device->time_zone;
  }

  template <typename T>
  static void PopulateUserProperties(const CoreContext& ctx, T& ev) {
    if (ctx.user_info.IsEmpty() && !ctx.anonymous_id_enabled) {
      return;
    }
    RumUserProperties& u = ev.usr.value.emplace();
    u.id = ctx.user_info.id;
    u.name = ctx.user_info.name;
    u.email = ctx.user_info.email;
    u.extra = ctx.user_info.extra;
    if (ctx.anonymous_id_enabled) {
      u.anonymous_id = ctx.anonymous_id;
    }
  }

  template <typename T>
  static void PopulateAccountProperties(const CoreContext& ctx, T& ev) {
    if (ctx.account_info.IsEmpty()) {
      return;
    }
    RumAccountProperties& a = ev.account.value.emplace(ctx.account_info.id);
    a.name = ctx.account_info.name;
    a.extra = ctx.account_info.extra;
  }
};

}  // namespace datadog::impl
