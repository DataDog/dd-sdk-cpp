// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/storage/feature_storage.h"

#include <algorithm>

#include "datadog/internal/utils.h"

namespace datadog::core::storage {

using datadog::core::DateTimeProvider;
using datadog::core::internal::NanoToMillis;
using datadog::core::internal::PerformancePreset;

// This assumes all of our filenames in FeatureStorage are numbers for the
// purpose of comparison, but thankfully doesn't fail if it encounters a
// non-number, and doesn't spend extra time parsing the number.
static bool numerical_string_comparitor(const std::string& a,
                                        const std::string& b) {
  if (a.size() == b.size()) return a < b;

  return a.size() < b.size();
}

FeatureStorage::FeatureStorage(const std::string& feature_name,
                               const PerformancePreset& performance_preset,
                               DateTimeProvider date_time_provider,
                               std::shared_ptr<DatadogFileSystem> file_system)
    : feature_name_{feature_name},
      performance_preset_{performance_preset},
      date_time_provider_{date_time_provider},
      file_system_{std::move(file_system)},
      random_generator_(std::random_device{}()),
      current_file_creation_time_{0},
      current_file_bytes_written_{0} {}

bool FeatureStorage::Write(std::string_view data) {
  // Write the given data, encrypting it if encryption is available.
  //
  // The written data format is:
  //
  //     +- 2 bytes -+-  4 bytes -+- n bytes  -|
  //     |    0x00   | block size | block data |
  //     +-----------+------------+------------+
  //
  // Where the 2 first bytes represents the `block type` of
  // an event. (data or metadata)
  constexpr auto block_overhead = sizeof(StorageBlockType) + sizeof(uint32_t);

  // TODO(jeff.ward): Determine / create encrypted string first if
  // encryption is available.
  auto full_write_size = data.size() + block_overhead;
  if (full_write_size > performance_preset_.max_file_size()) {
    // TODO(jeff.ward): TELEMETRY - Attempting to write too much data to storage
    return false;
  }

  if (!CanReuseCurrentFile(full_write_size)) {
    CreateNewWritableFile();
  }

  if (!current_file_) {
    // TODO(jeff.ward): TELEMETRY - Failed to get writable file of size
    return false;
  }

  StorageBlockType type{StorageBlockType::Event};
  uint32_t event_size = data.size();

  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  if (!current_file_->Write(
          {reinterpret_cast<char*>(&type), sizeof(StorageBlockType)}) ||
      !current_file_->Write(
          {reinterpret_cast<char*>(&event_size), sizeof(event_size)}) ||
      !current_file_->Write(data)) {
    // TODO(jeff.ward): TELEMETRY - Failed to write to file
    return false;
  }
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  current_file_bytes_written_ += full_write_size;

  return true;
}

bool FeatureStorage::ListReadableFiles(
    std::vector<std::filesystem::path>& files) {
  if (!file_system_->ListFiles("", files)) return false;

  std::sort(files.begin(), files.end(), numerical_string_comparitor);

  // Remove the current file, which should be near the end, so doing a find
  // using the reverse iterator and erasing should have a minimal impact
  if (current_file_) {
    const auto current_file_pos =
        std::find(files.rbegin(), files.rend(), current_file_->GetPath());
    if (current_file_pos != files.rend()) {
      files.erase(std::next(current_file_pos).base());
    }
  }

  return true;
}

std::unique_ptr<TLVFileReader> FeatureStorage::GetReadableFile(
    const std::filesystem::path& path) {
  auto file = file_system_->Open(path);
  if (!file) return nullptr;

  return std::make_unique<TLVFileReader>(std::move(file));
}

bool FeatureStorage::DeleteReadableFile(std::unique_ptr<TLVFileReader> file) {
  if (!file) return false;

  auto path = file->GetPath();
  // Clear the file, closing it before deleting it
  file.reset();

  return file_system_->Delete(path) == DatadogFileStatus::Ok;
}

bool FeatureStorage::CanReuseCurrentFile(size_t write_size) {
  if (!current_file_) return false;

  auto expected_file_size = current_file_bytes_written_ + write_size;
  if (expected_file_size >= performance_preset_.max_file_size()) return false;

  auto current_time = Nanoseconds{date_time_provider_()};
  auto file_age = current_time - current_file_creation_time_;

  if (file_age > performance_preset_.max_file_age_for_write()) {
    return false;
  }

  // TODO(jeff.ward): object count

  return true;
}

bool FeatureStorage::CreateNewWritableFile() {
  constexpr auto kAttempts = 5;
  constexpr auto kMaxSpread = 100;
  static std::uniform_int_distribution<> random_spread(1, kMaxSpread);

  // File name is based on the file creation time, but if the file already
  // exists attempt to add a random spread to get a file that doesn't. Don't
  // try this for too long and fail if lots of these files already exist.
  auto nowMs = NanoToMillis(date_time_provider_());
  for (int i = 0; i < kAttempts; ++i) {
    auto file_name = std::to_string(nowMs);
    if (!file_system_->Exists(file_name)) {
      current_file_ = file_system_->Open(file_name);
      if (!current_file_) {
        // TODO(jeff.ward) Telemetry: Failed to open file
        return false;
      }

      current_file_creation_time_ = Nanoseconds{date_time_provider_()};
      current_file_bytes_written_ = 0;
      return true;
    }
    nowMs += random_spread(random_generator_);
  }

  return false;
}

}  // namespace datadog::core::storage
