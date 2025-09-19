// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/rum.h"
#include "datadog/rum.hpp"

namespace datadog::impl {
class Rum;
}  // namespace datadog::impl

struct dd_rum {
  std::shared_ptr<datadog::impl::Rum> impl;
};
