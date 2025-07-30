#pragma once

#include <memory>
#include <string_view>

namespace datadog::platform {

enum class FileStatus
{
};

struct File
{
};

struct Filesystem
{
};

struct RootFilesystem : public Filesystem
{
    static std::unique_ptr<RootFilesystem> Init(std::string_view root_path);
};

}
