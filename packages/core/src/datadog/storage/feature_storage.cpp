// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/storage/feature_storage.h"

namespace datadog::core::storage {

using datadog::core::DateTimeProvider;
using datadog::core::internal::PerformancePreset;

enum class StorageBlockType : uint16_t { Event = 0, Metadata = 1 };

FeatureStorage::FeatureStorage(const std::string& feature_name,
                               const PerformancePreset& performance_preset,
                               DateTimeProvider date_time_provider,
                               std::shared_ptr<IDatadogFileSystem> file_system)
    : feature_name_{feature_name},
      performance_preset_{performance_preset},
      date_time_provider_{date_time_provider},
      file_system_{std::move(file_system)},
      current_file_creation_time_{0},
      current_file_size_{0} {}

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
  std::shared_ptr<IDatadogFile> file;
  if (CanReuseCurrentFile(data.size() + block_overhead)) {
    file = file_system_->OpenFile(current_file_path_.value());
  } else {
    file = CreateNewWritableFile();
  }

  if (!file) {
    // TODO(jeff.ward): TELEMETRY - Failed to get writable file of size
    return false;
  }

  StorageBlockType type{StorageBlockType::Event};
  uint32_t event_size = data.size();

  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  if (DatadogFileStatus::Ok ==
      file->Write({reinterpret_cast<char*>(&type), sizeof(StorageBlockType)})) {
    // TODO(jeff.ward): TELEMETRY - Failed to write to file
    return false;
  }

  if (DatadogFileStatus::Ok ==
      file->Write({reinterpret_cast<char*>(&event_size), sizeof(event_size)})) {
    // TODO(jeff.ward): TELEMETRY - Failed to write to file
    return false;
  }

  if (DatadogFileStatus::Ok == file->Write(data)) {
    // TODO(jeff.ward): TELEMETRY - Failed to write to file
    return false;
  }
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  return true;
}

bool FeatureStorage::CanReuseCurrentFile(size_t write_size) {
  if (!current_file_path_) return false;

  auto expected_file_size = current_file_size_ + write_size;
  if (expected_file_size >= performance_preset_.max_file_size()) return false;

  auto current_time = date_time_provider_();
  auto file_age = current_time - current_file_creation_time_;

  if (file_age > performance_preset_.max_file_age_for_write().count()) {
    return false;
  }

  // TODO: object count

  return true;
}

std::shared_ptr<IDatadogFile> FeatureStorage::CreateNewWritableFile() {
  // File name is based on the file creation time, but if the file already
  // exists attempt to add a random spread to get a file that doesn't. Don't
  // try this for too long and fail if lots of these files already exist.
  std::shared_ptr<IDatadogFile> file;
  auto nowMs = date_time_provider_() / 1000;
  for (int i = 0; i < 5; ++i) {
    auto file_name = std::to_string(nowMs);
    if (!file_system_->Exists(file_name)) {
      // file = file_system_->OpenFile(file_name);
      break;
    }
    nowMs += std::rand() % 100;
  }

  return file;
}

}  // namespace datadog::core::storage
