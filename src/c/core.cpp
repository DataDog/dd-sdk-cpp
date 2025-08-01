#include "datadog/core.h"

#include "core/types.hpp"
#include "core/core.hpp"

#include "core_glue.hpp"

#include <memory>

extern "C" {

dd_core_t* dd_core_create(dd_core_config_t* config)
{
    if (!config) return nullptr;
    
    try {
        datadog::CoreConfig cpp_config = datadog::CoreConfig_FromC(*config);
        auto impl = std::make_unique<datadog::impl::Core>(cpp_config);
        if (!impl->Init())
        {
            return nullptr;
        }
        dd_core_t* core = new dd_core;
        core->impl = std::move(impl);
        return core;
    } catch (...) {
        return nullptr;
    }
}

void dd_core_destroy(dd_core_t* core)
{
    delete core;
}

bool dd_core_start(dd_core_t* core)
{
    if (core && core->impl)
    {
        try {
            return core->impl->Start();
        } catch (...) {
            // Silently ignore exceptions for now
        }
    }
    return false;
}

void dd_core_shutdown(dd_core_t* core)
{
    if (core && core->impl)
    {
        try {
            core->impl->Shutdown();
        } catch (...) {
            // Silently ignore exceptions for now
        }
    }
}

}
