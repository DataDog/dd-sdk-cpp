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
  explicit StdDatadogFile(std::fstream file);
  ~StdDatadogFile() override { file_.close(); }

  StdDatadogFile(const StdDatadogFile&) = delete;
  StdDatadogFile& operator=(const StdDatadogFile&) = delete;
  StdDatadogFile(StdDatadogFile&&) = delete;
  StdDatadogFile& operator=(StdDatadogFile&&) = delete;

  uintmax_t GetSize() const override { return size_; };
  DatadogFileStatus GetStatus() const override { return status_; }

  DatadogFileStatus Write(std::string_view buffer) override;
  DatadogFileStatus Read(std::vector<char>& buffer) override;

 private:
  uintmax_t size_;
  std::fstream file_;
  DatadogFileStatus status_;
};

StdDatadogFile::StdDatadogFile(std::fstream file)
    : IDatadogFile(),
      size_(0),
      file_(std::move(file)),
      status_(DatadogFileStatus::Ok) {
  file_.seekg(0, file_.end);
  size_ = file_.tellg();
  file_.seekg(0, file_.beg);
}

DatadogFileStatus StdDatadogFile::Write(std::string_view buffer) {
  try {
    file_ << buffer;
    auto pos = file_.tellp();
    // We've overwritten the whole file. Stream position is now the file size
    if (pos > size_) {
      size_ = pos;
    }
  } catch (...) {
  }

  if (file_.fail()) {
    status_ = DatadogFileStatus::OperationFailure;
  }
  if (file_.bad()) {
    status_ = DatadogFileStatus::BadState;
  }

  return status_;
}

DatadogFileStatus StdDatadogFile::Read(std::vector<char>& buffer) {
  if (buffer.capacity() == 0) {
    status_ = DatadogFileStatus::OperationFailure;
    return status_;
  }

  auto file_size = GetSize();
  auto remaining = static_cast<size_t>(file_size - file_.tellg());
  auto read_size =
      static_cast<std::streamsize>(std::min(buffer.capacity(), remaining));
  buffer.resize(read_size);
  try {
    file_.read(buffer.data(), read_size);
  } catch (...) {
  }

  if (file_.fail()) {
    status_ = DatadogFileStatus::OperationFailure;
  }
  if (file_.bad()) {
    status_ = DatadogFileStatus::BadState;
  }
  if (file_.eof() || read_size == remaining) {
    status_ = DatadogFileStatus::EndOfFile;
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
  auto full_path = base_cache_directory_ / path;
  if (!IsInFileSystem(full_path)) {
    return nullptr;
  }

  if (!std::filesystem::exists(base_cache_directory_)) {
    std::filesystem::create_directories(base_cache_directory_);
  }

  std::fstream::openmode open_mode = std::fstream::in | std::fstream::out |
                                     std::fstream::app | std::fstream::binary;
  std::fstream file{full_path, open_mode};
  if (!file.is_open()) {
    return nullptr;
  }

  return std::make_unique<StdDatadogFile>(std::move(file));
}

void StdDatadogFileSystem::DeleteFile(const std::filesystem::path& path) {
  if (auto full_path = base_cache_directory_ / path;
      IsInFileSystem(full_path)) {
    std::filesystem::remove(full_path);
  }
}

std::vector<std::filesystem::path> StdDatadogFileSystem::GetFiles(
    const std::filesystem::path& in_dir) {
  const auto path = base_cache_directory_ / in_dir;
  std::vector<std::filesystem::path> ret{};
  if (!IsInFileSystem(path)) {
    return ret;
  }

  if (std::filesystem::is_directory(path)) {
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
      ret.push_back(entry);
    }
  }

  return ret;
}

bool StdDatadogFileSystem::IsInFileSystem(const std::filesystem::path& path) {
  const auto normalized = path.lexically_normal();

  auto [rootEnd, _] =
      std::mismatch(base_cache_directory_.begin(), base_cache_directory_.end(),
                    normalized.begin());

  return rootEnd == base_cache_directory_.end();
}

}  // namespace datadog::core::storage
