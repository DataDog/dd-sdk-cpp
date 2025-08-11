#include "datadog/logging.hpp"

#include "datadog/core.hpp"

#include "core/core.hpp"
#include "core/feature.hpp"
#include "features/logging/logging.hpp"

namespace datadog {

void Logger::Log(LogLevel level, std::string_view message)
{
    _impl->Log(level, message);
}

std::shared_ptr<Logging> Logging::Register(Core& core)
{
    auto impl = std::make_shared<impl::Logging>();
    if (!core._impl->RegisterFeature(impl))
    {
        return nullptr;
    }
    const std::shared_ptr<Logging> logging = std::make_shared<Logging>();
    logging->_impl = std::move(impl);
    return logging;
}

} // namespace datadog
