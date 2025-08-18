#pragma once

#include <memory>

#include "core/core.hpp"

struct dd_core
{
    std::unique_ptr<datadog::impl::Core> impl;
};
