// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/storage/datadog_file_system.h"

#include <cerrno>
#include <fstream>
#include <iostream>

namespace datadog::core::storage {

class StdDatadogFile : public IDatadogFile {
 public:
  explicit StdDatadogFile(std::fstream file) : file_(std::move(file)) {}

  StdDatadogFile(const StdDatadogFile&) = delete;
  StdDatadogFile& operator=(const StdDatadogFile&) = delete;
  ~StdDatadogFile() override { file_.close(); }

  void Write(const char* buffer, size_t buffer_size) override {
    file_.write(buffer, static_cast<std::streamsize>(buffer_size));
  }

 private:
  std::fstream file_;
};

StdDatadogFileSystem::StdDatadogFileSystem(
    const std::filesystem::path& base_cache_directory)
    : base_cache_directory_(base_cache_directory) {}

const std::filesystem::path& StdDatadogFileSystem::GetBaseDirectory() {
  return base_cache_directory_;
}

std::unique_ptr<IDatadogFile> StdDatadogFileSystem::OpenFile(
    const std::filesystem::path& path) {
  if (!std::filesystem::exists(GetBaseDirectory())) {
    std::filesystem::create_directories(GetBaseDirectory());
  }

  std::fstream file{GetBaseDirectory() / path,
                    std::fstream::in | std::fstream::out | std::fstream::app};
  if (!file.is_open()) {
    return nullptr;
  }

  return std::make_unique<StdDatadogFile>(std::move(file));
}

std::vector<std::filesystem::path> StdDatadogFileSystem::GetFiles(
    const std::filesystem::path& in_dir) {
  const auto path = GetBaseDirectory() / in_dir;
  std::vector<std::filesystem::path> ret{};

  if (std::filesystem::is_directory(path)) {
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
      ret.push_back(entry);
    }
  }

  return ret;
}

}  // namespace datadog::core::storage
