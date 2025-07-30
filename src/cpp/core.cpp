#include "datadog/core.hpp"

#include "core/core.hpp"

namespace datadog {

std::shared_ptr<Core> Core::Create(const CoreConfig& config)
{
    const std::shared_ptr<Core> core = std::make_shared<Core>();
    core->_impl = std::make_unique<impl::Core>(config);
    return core;
}

void Core::Start()
{
    _impl->Start();
}

void Core::Shutdown()
{
    _impl->Shutdown();
}

}
