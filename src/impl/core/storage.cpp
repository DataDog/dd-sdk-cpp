#include "core/storage.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <string>
#include <string_view>

#include "core/core.hpp"

namespace datadog::impl {

// We only process one write a time, so reuse our vector of filenames to reduce
// allocation overhead
static thread_local std::vector<std::string> s_filenames;

static void _handle_write(platform::IDirectory& dir, Block event, Block event_metadata)
{
    s_filenames.clear();
    auto ls_result = dir.ListFiles(s_filenames);
    if (!ls_result)
    {
        std::cout << "ls failed:" << static_cast<int>(ls_result.error()) << "\n";
        return;
    }

    std::ostringstream oss;
    if (!event_metadata.empty())
    {
        oss << " <" << event_metadata << ">";
    }
    std::cout << "[STORAGE] Got write from feature: " << event << oss.str() << "\n";
}

void StorageThreadMain(Queue<WriteToStorage>& queue, std::vector<Feature>& features)
{
    std::cout << "[STORAGE] Started\n";

    s_filenames.reserve(64);    

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

        assert(feature->directory &&
            "Storage thread matched feature_id from message to registered feature with "
            "no storage directory"
        );

        _handle_write(
            *feature->directory,
            Block(
                reinterpret_cast<const char*>(item->event.data()),
                item->event.size()
            ),
            Block(
                reinterpret_cast<const char*>(item->event_metadata.data()),
                item->event_metadata.size()
            )
        );
    }

    std::cout << "[STORAGE] Finished\n";
}

}