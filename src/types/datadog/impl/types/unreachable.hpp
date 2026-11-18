// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

// DATADOG_UNREACHABLE is a static compiler hint used to indicate that a branch can
// genuinely never be reached, regardless of runtime invariants - e.g. exhaustive
// switch/case statements, which GCC and MSVC tend to be unable to reason about as well
// as Clang does. For debug-only runtime checks, see DATADOG_ASSERT.
#ifdef _MSC_VER
#define DATADOG_UNREACHABLE __assume(false)
#else
#define DATADOG_UNREACHABLE __builtin_unreachable()
#endif
