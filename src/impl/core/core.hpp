#pragma once

#include <vector>

#include "core/types.hpp"
#include "core/feature.hpp"

namespace datadog::impl {

using CoreConfig = datadog::CoreConfig;

struct Core {
    explicit Core(const CoreConfig& config);

    void Start();
    void Shutdown();

    void RegisterFeature(Feature&& feature);

private:
    CoreConfig _config;
    std::vector<Feature> _features;
};

}
