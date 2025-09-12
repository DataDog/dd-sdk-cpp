#include "datadog/rum.h"

#include <memory>

#include "core/core.hpp"
#include "core_glue.hpp"
#include "datadog/core.h"
#include "features/rum/rum.hpp"
#include "rum_glue.hpp"

static const dd_rum_config_t DEFAULT_RUM_CONFIG;

// NOLINTBEGIN(cppcoreguidelines-owning-memory)

extern "C" {

dd_rum_config_t* dd_rum_config_create(void) { return new dd_rum_config_t(); }

void dd_rum_config_destroy(dd_rum_config_t* config) { delete config; }

dd_rum_t* dd_rum_init(dd_core_t* core, const dd_rum_config_t* config) {
  // Don't crash if user fails to give us a valid core, but don't give them a no-op
  // implementation either
  if (!core || !core->impl) {
    return nullptr;
  }

  if (!config) {
    config = &DEFAULT_RUM_CONFIG;
  }

  // Initialize our RUM feature implementation
  // Note: config->cpp_config will be used in future RUM work items
  (void)config;  // Suppress unused variable warning for now
  auto rum_impl = std::make_shared<datadog::impl::Rum>();

  // Register the feature with the core, aborting on failure
  if (!core->impl->RegisterFeature(rum_impl)) {
    // TODO: Return a no-op interface
    return nullptr;
  }

  // Initialize and return the API object that represents our user-facing interface
  // for the RUM feature
  dd_rum_t* rum = new dd_rum;
  rum->impl = std::move(rum_impl);
  return rum;
}

void dd_rum_destroy(dd_rum_t* rum) { delete rum; }
}

// NOLINTEND(cppcoreguidelines-owning-memory)
