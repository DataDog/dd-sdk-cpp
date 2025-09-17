// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdlib>

inline void Exit(int status_code) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  std::exit(status_code);
}
