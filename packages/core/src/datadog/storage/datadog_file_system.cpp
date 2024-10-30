// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/storage/datadog_file_system.h"

#include <cerrno>
#include <fstream>
#include <iostream>
#include "datadog_file_system.h"

namespace datadog::core::storage {

class StdDatadogFile : public DatadogFile {
 public:
  explicit StdDatadogFile(const std::filesystem::path& path,
                          std::fstream&& file)
      : DatadogFile{path}, file_{std::move(file)} {
    file_.seekg(0, file_.beg);
  }
  ~StdDatadogFile() override { file_.close(); }

  StdDatadogFile(const StdDatadogFile&) = delete;
  StdDatadogFile& operator=(const StdDatadogFile&) = delete;
  StdDatadogFile(StdDatadogFile&&) = delete;
  StdDatadogFile& operator=(StdDatadogFile&&) = delete;

  bool Write(std::string_view buffer) override {
    file_.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    if (file_.bad()) {
      return StatusReturn(DatadogFileStatus::BadState);
    }
    if (file_.fail()) {
      return StatusReturn(DatadogFileStatus::OperationFailure);
    }

    return StatusReturn(status_);
  }

  bool Read(char* buffer, size_t& bytes) override {
    // Read zero bytes, just return
    if (!bytes) {
      return true;
    }

    if (!buffer) {
      file_.seekg(static_cast<std::streamoff>(bytes), file_.cur);
    } else {
      file_.read(buffer, static_cast<std::streamsize>(bytes));
      bytes = file_.gcount();
    }

    if (file_.bad()) {
      return StatusReturn(DatadogFileStatus::BadState);
    }
    if (file_.eof()) {
      return StatusReturn(DatadogFileStatus::EndOfFile);
    }
    if (file_.fail()) {
      return StatusReturn(DatadogFileStatus::OperationFailure);
    }

    return StatusReturn(status_);
  }

 private:
  bool StatusReturn(DatadogFileStatus status) {
    status_ = status;
    return (status_ == DatadogFileStatus::Ok);
  }

  std::fstream file_;
};

StdDatadogFileSystem::StdDatadogFileSystem(
    const std::filesystem::path& base_cache_directory)
    : base_cache_directory_{base_cache_directory} {}

std::unique_ptr<DatadogFile> StdDatadogFileSystem::Open(
    const std::filesystem::path& path) {
  const auto full_path = base_cache_directory_ / path;
  if (!IsInFileSystem(full_path)) {
    return nullptr;
  }

  if (!std::filesystem::exists(base_cache_directory_)) {
    if (!std::filesystem::create_directories(base_cache_directory_)) {
      return nullptr;
    }
  }

  const auto open_mode = std::fstream::in | std::fstream::out |
                         std::fstream::app | std::fstream::binary;
  std::fstream file{full_path, open_mode};
  if (!file.is_open()) {
    return nullptr;
  }

  return std::make_unique<StdDatadogFile>(path, std::move(file));
}

bool StdDatadogFileSystem::Exists(const std::filesystem::path& path) {
  auto full_path = base_cache_directory_ / path;
  if (!IsInFileSystem(full_path)) {
    return false;
  }

  return std::filesystem::is_regular_file(full_path);
}

DatadogFileStatus StdDatadogFileSystem::Delete(
    const std::filesystem::path& path) {
  auto full_path = base_cache_directory_ / path;

  if (!IsInFileSystem(full_path)) {
    return DatadogFileStatus::OperationFailure;
  }

  if (!std::filesystem::exists(full_path)) {
    return DatadogFileStatus::NoOperation;
  }

  return std::filesystem::remove(full_path)
             ? DatadogFileStatus::Ok
             : DatadogFileStatus::OperationFailure;
}

bool StdDatadogFileSystem::ListFiles(
    const std::filesystem::path& in_dir,
    std::vector<std::filesystem::path>& files) {
  const auto path = base_cache_directory_ / in_dir;
  if (!IsInFileSystem(path)) {
    return false;
  }
  // If the path doesn't exist, assume it's an empty directory. This can happen
  // if no files have been created at that path yet, as we lazily create the
  // base parent directory.
  if (!std::filesystem::exists(path)) {
    return true;
  }
  if (!std::filesystem::is_directory(path)) {
    return false;
  }

  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    const auto& relative_entry =
        std::filesystem::relative(entry, base_cache_directory_);
    files.push_back(relative_entry);
  }

  return true;
}

std::shared_ptr<DatadogFileSystem> StdDatadogFileSystem::CreateChildFileSystem(
    std::string_view name) {
  const auto path = base_cache_directory_ / name;
  if (!IsInFileSystem(path)) {
    return nullptr;
  }

  return std::make_shared<StdDatadogFileSystem>(path);
}

bool StdDatadogFileSystem::IsInFileSystem(const std::filesystem::path& path) {
  const auto normalized = path.lexically_normal();

  const auto [rootEnd, _] =
      std::mismatch(base_cache_directory_.begin(), base_cache_directory_.end(),
                    normalized.begin(), normalized.end());

  return (rootEnd == base_cache_directory_.end());
}

}  // namespace datadog::core::storage
