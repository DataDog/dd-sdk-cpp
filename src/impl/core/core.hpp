#pragma once

#include <vector>
#include <memory>

#include "core/types.hpp"
#include "core/feature.hpp"

namespace datadog::platform { class IHttpSubsystem; }
namespace datadog::platform { class IHttpClient; }

namespace datadog::impl {

using CoreConfig = datadog::CoreConfig;

struct Core
{
    explicit Core(const CoreConfig& config);
    ~Core();

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = default;
    Core& operator=(Core&&) = default;

    bool Start();
    void Shutdown();

    void RegisterFeature(Feature&& feature);

private:
    CoreConfig _config;
    std::vector<Feature> _features;

    std::unique_ptr<datadog::platform::IHttpSubsystem> _http;
    std::unique_ptr<datadog::platform::IHttpClient> _http_client;
};

}
