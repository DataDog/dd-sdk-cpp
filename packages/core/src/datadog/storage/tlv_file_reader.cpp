// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "tlv_file_reader.h"

namespace datadog::core::storage {

DatadogFileStatus TLVFileReader::ReadBlock(TLVBlock& block) {
  StorageBlockType block_type{StorageBlockType::Event};
  size_t block_type_size = sizeof(StorageBlockType);
  uint32_t block_size{0};
  size_t block_size_size = sizeof(uint32_t);

  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  if (!file_->Read(reinterpret_cast<char*>(&block_type), block_type_size) ||
      !file_->Read(reinterpret_cast<char*>(&block_size), block_size_size)) {
    return file_->GetStatus();
  }
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  block.block_type = block_type;
  block.data.resize(block_size);

  size_t block_read_size = block_size;
  file_->Read(block.data.data(), block_read_size);

  return file_->GetStatus();
}

}  // namespace datadog::core::storage
