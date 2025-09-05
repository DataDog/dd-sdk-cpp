#include "datadog/core.hpp"

#include "core/core.hpp"

namespace datadog {

std::shared_ptr<Core> Core::Create(const CoreConfig& config) {
  // Create core subsystems using default implementations
  auto subsystems = impl::CoreSubsystems::Init(config);
  if (!subsystems) {
    return nullptr;
  }

  // Create the core implementation
  auto impl = std::make_unique<impl::Core>(config, std::move(*subsystems));
  if (!impl->Init()) {
    return nullptr;
  }

  // Wrap the implementation in a C++ struct that exposes the public API for the core,
  // using the pimpl idiom, with automatic cleanup
  const std::shared_ptr<Core> core = std::make_shared<Core>();
  core->_impl = std::move(impl);
  return core;
}

bool Core::Start() { return _impl->Start(); }

void Core::Stop() { _impl->Stop(); }

}  // namespace datadog
