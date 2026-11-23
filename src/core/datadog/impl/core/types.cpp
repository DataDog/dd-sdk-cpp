// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/types.hpp"

namespace datadog::impl {

DiagnosticLogger DiagnosticLogger_FromC(
    dd_diagnostic_handler_t c_handler,
    void* c_handler_userdata,
    dd_diagnostic_level_t c_threshold
) {
  // Capture the callback and the userdata value to bind a std::function that will
  // handle messages emitted from the C++ implementation layer, translating them to
  // equivalent C type and invoking the C callback
  DiagnosticHandler cpp_handler = [=](const DiagnosticMessage& cpp_message) {
    if (c_handler) {
      dd_diagnostic_message_t c_message = DiagnosticMessage_ToC(cpp_message);
      c_handler(&c_message, c_handler_userdata);
    }
  };

  // Threshold check occurs _before_ the callback is invoked, in DiagnosticLogger
  DiagnosticLevel cpp_threshold = DiagnosticLevel_FromC(c_threshold);
  return DiagnosticLogger{cpp_handler, cpp_threshold};
}

}  // namespace datadog::impl
