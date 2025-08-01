#include "core/feature.hpp"

namespace datadog::impl {

StorageWriter::StorageWriter(FeatureId feature_id)
    : _feature_id(feature_id)
{
}

void StorageWriter::EnqueueEventForWrite(Block event, Block event_metadata /*= {}*/)
{
}

}
