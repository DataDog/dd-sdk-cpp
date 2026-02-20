// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/rum.h"
#include "datadog/rum.hpp"

namespace datadog {

inline RumConfig RumConfig_FromC(const dd_rum_config_t& config) {
  // Convert from dd_uuid_t to datadog::UUID
  const uint8_t* bytes = static_cast<const uint8_t*>(config.application_id.bytes);
  const UUID application_id(bytes);

  // Initialize a C++ config struct from our input values
  return RumConfig(application_id).SetSessionSampleRate(config.session_sample_rate);
}

inline RumActionType RumActionType_FromC(dd_rum_action_type_t value) {
  static_assert(static_cast<int>(RumActionType::Tap) == DD_RUM_ACTION_TYPE_TAP);
  static_assert(static_cast<int>(RumActionType::Click) == DD_RUM_ACTION_TYPE_CLICK);
  static_assert(static_cast<int>(RumActionType::Scroll) == DD_RUM_ACTION_TYPE_SCROLL);
  static_assert(static_cast<int>(RumActionType::Swipe) == DD_RUM_ACTION_TYPE_SWIPE);
  static_assert(static_cast<int>(RumActionType::Custom) == DD_RUM_ACTION_TYPE_CUSTOM);
  return static_cast<RumActionType>(value);
}

inline RumResourceMethod RumResourceMethod_FromC(dd_rum_resource_method_t value) {
  static_assert(static_cast<int>(RumResourceMethod::Get) == DD_RUM_RESOURCE_METHOD_GET);
  static_assert(
      static_cast<int>(RumResourceMethod::Head) == DD_RUM_RESOURCE_METHOD_HEAD
  );
  static_assert(
      static_cast<int>(RumResourceMethod::Post) == DD_RUM_RESOURCE_METHOD_POST
  );
  static_assert(static_cast<int>(RumResourceMethod::Put) == DD_RUM_RESOURCE_METHOD_PUT);
  static_assert(
      static_cast<int>(RumResourceMethod::Delete) == DD_RUM_RESOURCE_METHOD_DELETE
  );
  static_assert(
      static_cast<int>(RumResourceMethod::Connect) == DD_RUM_RESOURCE_METHOD_CONNECT
  );
  static_assert(
      static_cast<int>(RumResourceMethod::Options) == DD_RUM_RESOURCE_METHOD_OPTIONS
  );
  static_assert(
      static_cast<int>(RumResourceMethod::Trace) == DD_RUM_RESOURCE_METHOD_TRACE
  );
  static_assert(
      static_cast<int>(RumResourceMethod::Patch) == DD_RUM_RESOURCE_METHOD_PATCH
  );
  return static_cast<RumResourceMethod>(value);
}

inline RumResourceType RumResourceType_FromC(dd_rum_resource_type_t value) {
  static_assert(
      static_cast<int>(RumResourceType::Unknown) == DD_RUM_RESOURCE_TYPE_UNKNOWN
  );
  static_assert(
      static_cast<int>(RumResourceType::Beacon) == DD_RUM_RESOURCE_TYPE_BEACON
  );
  static_assert(static_cast<int>(RumResourceType::Fetch) == DD_RUM_RESOURCE_TYPE_FETCH);
  static_assert(static_cast<int>(RumResourceType::Xhr) == DD_RUM_RESOURCE_TYPE_XHR);
  static_assert(
      static_cast<int>(RumResourceType::Document) == DD_RUM_RESOURCE_TYPE_DOCUMENT
  );
  static_assert(
      static_cast<int>(RumResourceType::Native) == DD_RUM_RESOURCE_TYPE_NATIVE
  );
  static_assert(static_cast<int>(RumResourceType::Image) == DD_RUM_RESOURCE_TYPE_IMAGE);
  static_assert(static_cast<int>(RumResourceType::Js) == DD_RUM_RESOURCE_TYPE_JS);
  static_assert(static_cast<int>(RumResourceType::Font) == DD_RUM_RESOURCE_TYPE_FONT);
  static_assert(static_cast<int>(RumResourceType::Css) == DD_RUM_RESOURCE_TYPE_CSS);
  static_assert(static_cast<int>(RumResourceType::Media) == DD_RUM_RESOURCE_TYPE_MEDIA);
  static_assert(static_cast<int>(RumResourceType::Other) == DD_RUM_RESOURCE_TYPE_OTHER);
  return static_cast<RumResourceType>(value);
}

inline RumErrorSource RumErrorSource_FromC(dd_rum_error_source_t value) {
  static_assert(
      static_cast<int>(RumErrorSource::Network) == DD_RUM_ERROR_SOURCE_NETWORK
  );
  static_assert(static_cast<int>(RumErrorSource::Source) == DD_RUM_ERROR_SOURCE_SOURCE);
  static_assert(
      static_cast<int>(RumErrorSource::Console) == DD_RUM_ERROR_SOURCE_CONSOLE
  );
  static_assert(static_cast<int>(RumErrorSource::Logger) == DD_RUM_ERROR_SOURCE_LOGGER);
  static_assert(static_cast<int>(RumErrorSource::Agent) == DD_RUM_ERROR_SOURCE_AGENT);
  static_assert(
      static_cast<int>(RumErrorSource::Webview) == DD_RUM_ERROR_SOURCE_WEBVIEW
  );
  static_assert(static_cast<int>(RumErrorSource::Custom) == DD_RUM_ERROR_SOURCE_CUSTOM);
  static_assert(static_cast<int>(RumErrorSource::Report) == DD_RUM_ERROR_SOURCE_REPORT);
  return static_cast<RumErrorSource>(value);
}

inline RumOperationFailureReason RumOperationFailureReason_FromC(
    dd_rum_failure_reason_t value
) {
  static_assert(
      static_cast<int>(RumOperationFailureReason::Error) ==
      DD_RUM_FAILURE_REASON_ERROR
  );
  static_assert(
      static_cast<int>(RumOperationFailureReason::Abandoned) ==
      DD_RUM_FAILURE_REASON_ABANDONED
  );
  static_assert(
      static_cast<int>(RumOperationFailureReason::Other) ==
      DD_RUM_FAILURE_REASON_OTHER
  );
  return static_cast<RumOperationFailureReason>(value);
}

}  // namespace datadog
