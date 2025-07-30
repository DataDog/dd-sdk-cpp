#include "platform/filesystem.h"

namespace datadog::platform {

std::unique_ptr<RootFilesystem> RootFilesystem::Init(std::string_view root_path)
{
    return nullptr;
}

}