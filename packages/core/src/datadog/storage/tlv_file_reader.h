// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "datadog/storage/datadog_file_system.h"

namespace datadog::core::storage {

enum class StorageBlockType : uint16_t { Event = 0, Metadata = 1 };

struct TLVBlock {
  StorageBlockType block_type;
  std::vector<char> data;
};

// A wrapper around DatadogFile that provides methods for reading
// Type, Length, Value blocks from the file.
class TLVFileReader {
 public:
  explicit TLVFileReader(std::unique_ptr<DatadogFile> file)
      : file_(std::move(file)) {}

  const std::filesystem::path& GetPath() const { return file_->GetPath(); }

  DatadogFileStatus ReadBlock(TLVBlock& block);

 private:
  std::unique_ptr<DatadogFile> file_;
};

}  // namespace datadog::core::storage
