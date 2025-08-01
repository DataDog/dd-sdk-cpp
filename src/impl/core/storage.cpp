#include "core/storage.hpp"

#include <iostream>
#include <algorithm>

namespace datadog::impl {

void StorageThreadMain(Queue<StorageWriteMessage>& queue, std::vector<Feature>& features)
{
    std::cout << "[STORAGE] Started\n";

    // Perform continual blocking reads until we get std::nullopt, indicating that the
    // queue is drained and processing should stop
    while (const auto item = queue.Pop())
    {
        // Find the feature implementation identified in the message
        const auto feature = std::find_if(features.begin(), features.end(), [&](const Feature& f) {
            return f.id == item->feature_id;
        });
        if (feature == features.end())
        {
            // Ignore the message if no such feature exists
            continue;
        }

        std::cout << "[STORAGE] Got write for feature id " << item->feature_id << "\n";
    }

    std::cout << "[STORAGE] Finished\n";
}

}