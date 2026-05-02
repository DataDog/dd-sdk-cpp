// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/platform/system_info.hpp"

namespace datadog::impl {

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
    // If SystemInfo details are available, populate 'os' and 'device' properties
    PopulateOsProperties(context, ev);
    PopulateDeviceProperties(context, ev);
    PopulateUserProperties(context, ev);
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
    if (ctx.user_info.IsEmpty()) {
      return;
    }
    RumUserProperties& u = ev.usr.value.emplace();
    u.id = ctx.user_info.id;
    u.name = ctx.user_info.name;
    u.email = ctx.user_info.email;
    u.extra = ctx.user_info.extra;
  }
};

}  // namespace datadog::impl
