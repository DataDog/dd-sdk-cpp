// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

// When the SDK is built with the CMake option DD_CRASH_MODE=crashpad, we'll be compiled
// with DATADOG_WITH_CRASHPAD=1. If enabled, we can use the crashpad client library, and
// we can expect the crashpad_handler executable to be deployed alongside the
// application.
#ifndef DATADOG_WITH_CRASHPAD
#define DATADOG_WITH_CRASHPAD 0
#endif

namespace datadog::impl {

/**
 * Entry point for temporary Crashpad code, intended to verify that we can successfully
 * call the Crashpad client library and launch the handler process.
 *
 * If handler_exe_path is empty, we'll fall back to the default (expecting the
 * crashpad_handler executable to be present in the same directory as the application
 * binary).
 *
 * If DATADOG_WITH_CRASHPAD=1, returns whether the handler process was started.
 * If DATADOG_WITH_CRASHPAD=0, does nothing and returns true.
 */
bool InitializeCrashHandler(std::string_view handler_exe_path);

}  // namespace datadog::impl
