// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/platform/system_info.hpp"

namespace datadog::impl {

/**
 * Utilities for enriching RUM event payloads with context from CoreContext.
 */
struct RumEventEnrichment {
  /**
   * Populates a RUM event's `os` field with OS properties retrieved from CoreContext.
   *
   * @param scope FeatureScope from which to access CoreContext. If null or if
   *  CoreContext lacks OS info, ev.os remains unchanged.
   * @param ev RUM event with an `OmitIfNoValue<RumOSProperties> os` field, to be
   *  modified in-place.
   */
  template <typename T>
  static void PopulateOsProperties(const FeatureScope* scope, T& ev) {
    // Abort if no scope (SDK is not active)
    if (!scope) {
      return;
    }

    // Obtain a read-only copy of the context
    const CoreContext ctx = scope->GetContext();
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
};

}  // namespace datadog::impl
