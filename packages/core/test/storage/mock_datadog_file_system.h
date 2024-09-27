// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/storage/datadog_file_system.h"

#include "test.h"

#include <catch2/trompeloeil.hpp>

namespace datadog::core::storage::mocks {

class MockDatadogFile : public DatadogFile {
 public:
  MockDatadogFile(const std::filesystem::path& path) : DatadogFile(path) {}

  MAKE_MOCK0(GetSize, uintmax_t(), const override);
  MAKE_MOCK1(Write, bool(std::string_view buffer), override);
  MAKE_MOCK2(Read, bool(char*, size_t&), override);
};

class MockDatadogFileSystem : public DatadogFileSystem {
 public:
  MAKE_MOCK1(Open,
             std::unique_ptr<DatadogFile>(const std::filesystem::path&),
             override);
  MAKE_MOCK1(Exists, bool(const std::filesystem::path&), override);
  MAKE_MOCK1(Delete, DatadogFileStatus(const std::filesystem::path&), override);
  MAKE_MOCK2(ListFiles,
             bool(const std::filesystem::path&,
                  std::vector<std::filesystem::path>&),
             override);
};

}  // namespace datadog::core::storage::mocks
