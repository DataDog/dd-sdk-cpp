// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/storage/datadog_file_system.h"

#include <cerrno>
#include <fstream>
#include <iostream>
#include "datadog_file_system.h"

namespace datadog::core::storage {

class StdDatadogFile : public IDatadogFile {
 public:
  explicit StdDatadogFile(std::fstream&& file) : file_{std::move(file)} {
    file_.seekg(0, file_.end);
    size_ = file_.tellg();
    file_.seekg(0, file_.beg);
  }
  ~StdDatadogFile() override { file_.close(); }

  StdDatadogFile(const StdDatadogFile&) = delete;
  StdDatadogFile& operator=(const StdDatadogFile&) = delete;
  StdDatadogFile(StdDatadogFile&&) = delete;
  StdDatadogFile& operator=(StdDatadogFile&&) = delete;

  uintmax_t GetSize() const override { return size_; };

  DatadogFileStatus Write(std::string_view buffer) override {
    file_.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto pos = file_.tellp();
    // We've overwritten the whole file. Stream position is now the file size.
    if (pos > size_) {
      size_ = pos;
    }

    if (file_.bad()) return DatadogFileStatus::BadState;
    if (file_.fail()) return DatadogFileStatus::OperationFailure;

    return DatadogFileStatus::Ok;
  }

  DatadogFileStatus Read(char* buffer, size_t& bytes) override {
    if (!buffer || !bytes) {
      bytes = 0;
      return DatadogFileStatus::OperationFailure;
    }

    file_.read(buffer, static_cast<std::streamsize>(bytes));
    bytes = file_.gcount();

    if (file_.bad()) return DatadogFileStatus::BadState;
    if (file_.eof()) return DatadogFileStatus::EndOfFile;
    if (file_.fail()) return DatadogFileStatus::OperationFailure;

    return DatadogFileStatus::Ok;
  }

 private:
  uintmax_t size_{};
  std::fstream file_;
};

StdDatadogFileSystem::StdDatadogFileSystem(
    const std::filesystem::path& base_cache_directory)
    : base_cache_directory_(base_cache_directory) {}

std::unique_ptr<IDatadogFile> StdDatadogFileSystem::Open(
    const std::filesystem::path& path) {
  const auto full_path = base_cache_directory_ / path;
  if (!IsInFileSystem(full_path)) return nullptr;

  if (!std::filesystem::exists(base_cache_directory_)) {
    if (!std::filesystem::create_directories(base_cache_directory_)) {
      return nullptr;
    }
  }

  const auto open_mode = std::fstream::in | std::fstream::out |
                         std::fstream::app | std::fstream::binary;
  std::fstream file{full_path, open_mode};
  if (!file.is_open()) return nullptr;

  return std::make_unique<StdDatadogFile>(std::move(file));
}

DatadogFileStatus StdDatadogFileSystem::Delete(
    const std::filesystem::path& path) {
  auto full_path = base_cache_directory_ / path;

  if (!IsInFileSystem(full_path)) return DatadogFileStatus::OperationFailure;

  if (!std::filesystem::exists(full_path)) {
    return DatadogFileStatus::NoOperation;
  }

  return std::filesystem::remove(full_path)
             ? DatadogFileStatus::Ok
             : DatadogFileStatus::OperationFailure;
}

std::vector<std::filesystem::path> StdDatadogFileSystem::ListPaths(
    const std::filesystem::path& in_dir) {
  const auto path = base_cache_directory_ / in_dir;
  std::vector<std::filesystem::path> ret;
  if (!IsInFileSystem(path)) return ret;

  if (std::filesystem::is_directory(path)) {
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
      ret.push_back(entry);
    }
  }

  return ret;
}

bool StdDatadogFileSystem::IsInFileSystem(const std::filesystem::path& path) {
  const auto normalized = path.lexically_normal();

  const auto [rootEnd, _] =
      std::mismatch(base_cache_directory_.begin(), base_cache_directory_.end(),
                    normalized.begin());

  return (rootEnd == base_cache_directory_.end());
}

}  // namespace datadog::core::storage
