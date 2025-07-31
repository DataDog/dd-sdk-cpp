#pragma once

#include <string>
#include <functional>

#include "core/feature_id.hpp"

namespace datadog::impl {

struct Feature
{
    FeatureId id;
    std::string name;
};

}
