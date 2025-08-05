#include "core/feature.hpp"

#include <cassert>

namespace datadog::impl {

void Feature::OnCoreStarted(StorageWriter writer)
{
    // We're now permitted to write events; store a reference to our writer callback
    assert(!_writer && "Feature received OnCoreStop with non-null writer");
    _writer = writer;

    // Notify the feature that the core is started
    Start();
}

void Feature::OnCoreStopping()
{
    // Notify the feature that the core is about to stop, while it's still able to write
    // events
    Stop();

    // Clear the writer callback; we're no longer permitted to write anything
    assert(_writer && "Feature received OnCoreStop with null writer");
    _writer = nullptr;
}

bool Feature::WriteEvent(Block event, Block event_metadata)
{
    if (_writer)
    {
        return _writer(event, event_metadata);
    }
    return false;
}

bool Feature::IsRunning() const
{
    return _writer != nullptr;
}

}
