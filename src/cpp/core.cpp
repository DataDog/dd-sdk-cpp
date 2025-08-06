#include "datadog/core.hpp"

#include "core/core.hpp"

namespace datadog {

std::shared_ptr<Core> Core::Create(const CoreConfig& config)
{
    auto impl = std::make_unique<impl::Core>(config);
    if (!impl->Init())
    {
        return nullptr;
    }
    const std::shared_ptr<Core> core = std::make_shared<Core>();
    core->_impl = std::move(impl);
    return core;
}

bool Core::Start()
{
    return _impl->Start();
}

void Core::Shutdown()
{
    _impl->Stop();
}

}
