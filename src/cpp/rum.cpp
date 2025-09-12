#include "datadog/rum.hpp"

#include "core/core.hpp"
#include "core/feature.hpp"
#include "datadog/core.hpp"
#include "features/rum/rum.hpp"

namespace datadog {

std::shared_ptr<Rum> Rum::Register(Core& core, const RumConfig& config) {
  (void)config;  // Unused for now - this is a "do-nothing" API

  // Initialize our RUM feature implementation
  auto rum_impl = std::make_shared<impl::Rum>();

  // Register the feature with the core, aborting on failure
  if (!core._impl->RegisterFeature(rum_impl)) {
    // TODO: Return a no-op interface
    return nullptr;
  }

  // Initialize and return the API object that represents our user-facing interface for
  // the RUM feature
  const std::shared_ptr<Rum> rum = std::make_shared<Rum>();
  rum->_impl = std::move(rum_impl);
  return rum;
}

}  // namespace datadog
