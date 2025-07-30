#include "datadog/logging.hpp"

#include "datadog/core.hpp"

#include "core/core.hpp"
#include "core/feature_id.hpp"
#include "core/feature.hpp"
#include "logging/logging.hpp"

namespace datadog {

void Logger::Log(LogLevel level, std::string_view message)
{
    _impl->Log(level, message);
}

std::shared_ptr<Logging> Logging::Register(Core& core)
{
    const std::shared_ptr<Logging> logging = std::make_shared<Logging>();
    logging->_impl = std::make_unique<impl::Logging>();
    logging->_impl->Register(*core._impl);
    return logging;
}

}
