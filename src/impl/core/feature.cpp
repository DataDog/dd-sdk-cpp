#include "core/feature.hpp"

#include <cassert>

namespace datadog::impl {

void Feature::OnCoreStarted(EventGeneratedFunc event_callback)
{
    // We're now permitted to write events; store a reference to our writer callback
    assert(!_event_callback && "Feature has non-null _event_callback in OnCoreStarted");
    assert(event_callback && "Feature received null event_callback in OnCoreStarted");
    _event_callback = event_callback;

    // Notify the feature that the core is started
    Start();
}

void Feature::OnCoreStopping()
{
    // Notify the feature that the core is about to stop, while it's still able to write
    // events
    Stop();

    // Clear the writer callback; we're no longer permitted to write anything
    assert(_event_callback && "Feature has null _event_callback in OnCoreStop");
    _event_callback = nullptr;
}

bool Feature::WriteEvent(Block event, Block event_metadata)
{
    if (_event_callback)
    {
        return _event_callback(event, event_metadata);
    }
    return false;
}

bool Feature::IsRunning() const
{
    return _event_callback != nullptr;
}

}
