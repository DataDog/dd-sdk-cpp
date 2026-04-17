// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>

#include "datadog/uuid.hpp"

namespace datadog::impl {

class File;

/**
 * In-memory representation of the complete contents of a crash context file.
 */
struct CrashContextFile {
  UUID rum_application_id;
  UUID rum_session_id;
  UUID rum_view_id;
  UUID rum_action_id;
};

/**
 * Given a handle to an open crash context file, reads the file and attempts to parse
 * its contents according to the format specified in crash_context.hpp.
 */
std::optional<CrashContextFile> ReadCrashContext(File& file);

}  // namespace datadog::impl
