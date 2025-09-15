// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#include <string_view>
#include <vector>

#include "core/tlv.hpp"
#include "mock/filesystem.hpp"

using namespace datadog;

/**
 * Helper struct used to build TLV file contents for use during testing.
 */
struct MockTLVFile {
  std::vector<char> buffer;

  explicit MockTLVFile(size_t initial_capacity = 256) {
    buffer.reserve(initial_capacity);
  }

  MockTLVFile& AppendMetadata(impl::Block data) {
    const uint32_t size = static_cast<uint32_t>(data.size());
    AppendHeader(impl::TLVBlockHeader{impl::TLVBlockType::Metadata, size});
    AppendBytes(data);
    return *this;
  }

  MockTLVFile& AppendEvent(impl::Block data) {
    const uint32_t size = static_cast<uint32_t>(data.size());
    AppendHeader(impl::TLVBlockHeader{impl::TLVBlockType::Event, size});
    AppendBytes(data);
    return *this;
  }

  MockTLVFile& AppendHeader(impl::TLVBlockHeader header) {
    char encoded[impl::TLVBlockHeader::SIZE];
    header.Encode(encoded);
    AppendBytes(std::string_view{encoded, impl::TLVBlockHeader::SIZE});
    return *this;
  }

  MockTLVFile& AppendBytes(std::string_view bytes) {
    buffer.reserve(buffer.size() + bytes.size());
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    return *this;
  }

  void WriteTo(MockStorageDirectory& storage, std::string_view relpath) {
    std::string_view contents{buffer.data(), buffer.size()};
    storage.WithExistingFile(relpath, contents);
  }

  std::string ToString() const { return std::string(buffer.begin(), buffer.end()); }
};
