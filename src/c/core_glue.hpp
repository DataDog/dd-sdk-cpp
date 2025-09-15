// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#include <memory>

#include "core/core.hpp"

struct dd_core {
  std::unique_ptr<datadog::impl::Core> impl;
};
