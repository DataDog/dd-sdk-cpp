#include "datadog/core.h"

#include "core/core.hpp"
#include "core/types.hpp"
#include "platform/filesystem.hpp"
#include "platform/http.hpp"

#include "core_glue.hpp"

#include <memory>

// NOLINTBEGIN(cppcoreguidelines-owning-memory)

extern "C" {

dd_core_t* dd_core_create(dd_core_config_t* config)
{
    // Initialize a C++ config struct, which is used at the implementation layer
    if (!config)
    {
        return nullptr;
    }
    datadog::CoreConfig cpp_config = datadog::CoreConfig_FromC(*config);

    // Initialize core subsystems using the platform-specific implementations compiled
    // in this build
    auto subsystems = datadog::impl::CoreSubsystems::Init(cpp_config);
    if (!subsystems)
    {
        return nullptr;
    }

    // Create the impl::Core object
    auto impl =
        std::make_unique<datadog::impl::Core>(cpp_config, std::move(*subsystems));

    // Perform mandatory initialization routines that might fail
    if (!impl->Init())
    {
        return nullptr;
    }

    // Wrap the core in a dynamically-allocated dd_core struct, which will own our
    // implementation via unique_ptr, ensuring cleanup as long as we delete the dd_core
    dd_core_t* core = new dd_core;
    core->impl = std::move(impl);
    return core;
}

void dd_core_destroy(dd_core_t* core)
{
    delete core;
}

bool dd_core_start(dd_core_t* core)
{
    if (core && core->impl)
    {
        return core->impl->Start();
    }
    return false;
}

void dd_core_stop(dd_core_t* core)
{
    if (core && core->impl)
    {
        core->impl->Stop();
    }
}
}

// NOLINTEND(cppcoreguidelines-owning-memory)
