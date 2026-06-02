// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>

#include "datadog/attribute.hpp"
#include "datadog/logging.hpp"
#include "datadog/timestamp.hpp"

namespace datadog::impl {

/**
 * Data derived from LoggerConfig that affects how log events are generated for a given
 * logger. Initialized once on Logger init.
 */
struct LoggerConfigDetails {
  std::string service_override;  // Value used in lieu of SDK-configured service, if set
  std::string name;              // Value used for logger.name, if set
  bool enrich_with_rum_context;  // If true, add session_id, view.id, etc. if RUM active
};

/**
 * Captured data describing a log call initiated by the application on a calling thread;
 * used to copy the relevant per-message details to the context thread.
 */
struct LogCallDetails {
  LogLevel level;
  std::string message;
  std::string error_kind;
  std::string error_stack;
  Timestamp timestamp;
  Attribute merged_attributes;
  std::string logger_tags;
};

}  // namespace datadog::impl
