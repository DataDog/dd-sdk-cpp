#pragma once

#include <cinttypes>
#include <string_view>
#include <vector>

#include "core/feature.hpp"
#include "core/queue.hpp"

namespace datadog::impl {

/**
 * Message representing an event that needs to be flushed to persistent storage.
 * Produced by a feature implementation in the main thread; handled in the storage
 * thread.
 */
struct WriteToStorage
{
    /**
     * Unique FourCC identifier of the feature that produced this message. The
     * accompanying event data will be written in the storage directory that corresponds
     * to this feature.
     */
    FeatureId feature_id;
    /**
     * The binary data representing the payload of this event, to be written to storage
     * as a TLV block of type 'Event'. The message stores a copy of the source data.
     */
    std::vector<uint8_t> event;
    /**
     * An optional payload describing the event, also copied. Will be written as a TLV
     * block of type 'Metadata', immediately following the Event block, if and only if
     * non-empty.
     */
    std::vector<uint8_t> event_metadata;

    WriteToStorage(FeatureId in_feature_id, Block in_event, Block in_event_metadata)
        : feature_id(in_feature_id)
        , event(in_event.begin(), in_event.end())
        , event_metadata(in_event_metadata.begin(), in_event_metadata.end())
    {
    }
};

void StorageThreadMain(Queue<WriteToStorage>& queue, std::vector<struct Feature>& features);

}
