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
  explicit StdDatadogFile(std::fstream&& file);
  ~StdDatadogFile() override { file_.close(); }

  StdDatadogFile(const StdDatadogFile&) = delete;
  StdDatadogFile& operator=(const StdDatadogFile&) = delete;
  StdDatadogFile(StdDatadogFile&&) = delete;
  StdDatadogFile& operator=(StdDatadogFile&&) = delete;

  uintmax_t GetSize() const override { return size_; };
  DatadogFileStatus GetStatus() const override { return status_; }

  DatadogFileStatus Write(std::string_view buffer) override;
  DatadogFileStatus Read(char* buffer, size_t& buffer_size) override;

 private:
  uintmax_t size_ = 0;
  DatadogFileStatus status_ = DatadogFileStatus::Ok;
  std::fstream file_;
};

StdDatadogFile::StdDatadogFile(std::fstream&& file) : file_{std::move(file)} {
  file_.seekg(0, file_.end);
  size_ = file_.tellg();
  file_.seekg(0, file_.beg);
}

DatadogFileStatus StdDatadogFile::Write(std::string_view buffer) {
  file_.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  auto pos = file_.tellp();
  // We've overwritten the whole file. Stream position is now the file size
  if (pos > size_) {
    size_ = pos;
  }

  if (file_.bad()) {
    status_ = DatadogFileStatus::BadState;
  } else if (file_.fail()) {
    status_ = DatadogFileStatus::OperationFailure;
  }

  return status_;
}

DatadogFileStatus StdDatadogFile::Read(char* buffer, size_t& bytes) {
  if (!buffer || bytes == 0) {
    status_ = DatadogFileStatus::OperationFailure;
    bytes = 0;
    return status_;
  }

  file_.read(buffer, static_cast<std::streamsize>(bytes));
  bytes = file_.gcount();

  if (file_.bad()) {
    status_ = DatadogFileStatus::BadState;
  } else if (file_.eof()) {
    status_ = DatadogFileStatus::EndOfFile;
  } else if (file_.fail()) {
    status_ = DatadogFileStatus::OperationFailure;
  }

  return status_;
}

StdDatadogFileSystem::StdDatadogFileSystem(
    const std::filesystem::path& base_cache_directory)
    : base_cache_directory_(base_cache_directory) {}

const std::filesystem::path& StdDatadogFileSystem::GetBaseDirectory() {
  return base_cache_directory_;
}

std::unique_ptr<IDatadogFile> StdDatadogFileSystem::OpenFile(
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
  if (!file.is_open()) {
    return nullptr;
  }

  return std::make_unique<StdDatadogFile>(std::move(file));
}

bool StdDatadogFileSystem::DeleteFile(const std::filesystem::path& path) {
  if (auto full_path = base_cache_directory_ / path;
      IsInFileSystem(full_path)) {
    return std::filesystem::remove(full_path);
  }
  return false;
}

std::vector<std::filesystem::path> StdDatadogFileSystem::GetFiles(
    const std::filesystem::path& in_dir) {
  const auto path = base_cache_directory_ / in_dir;
  std::vector<std::filesystem::path> ret{};
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

  return rootEnd == base_cache_directory_.end();
}

}  // namespace datadog::core::storage
