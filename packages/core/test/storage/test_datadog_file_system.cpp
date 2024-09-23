// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include <datadog/storage/datadog_file_system.h>

#include <algorithm>
#include <fstream>

#include "test.h"

namespace {

using datadog::core::storage::DatadogFileStatus;
using datadog::core::storage::IDatadogFile;
using datadog::core::storage::StdDatadogFileSystem;

enum class FileDisposition : bool { remove_only, create_and_remove };

// Creates a file on construction deletes the file on destruction
class TempFile {
 public:
  explicit TempFile(
      const std::filesystem::path& path,
      FileDisposition file_disposition = FileDisposition::create_and_remove)
      : path_(path) {
    // Create the file and immediately close it.
    if (file_disposition == FileDisposition::create_and_remove) {
      std::fstream{path, std::fstream::out | std::fstream::trunc};
    }
  }

  ~TempFile() { std::filesystem::remove(path_); }

  const std::filesystem::path& GetPath() { return path_; }

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;

 private:
  const std::filesystem::path path_;
};

const std::filesystem::path base_filesystem_dir{"caches/datadog"};

TEST_CASE("M create file in base cache directory W OpenFile", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  auto expected_path = std::filesystem::path{"caches/datadog/test.tmp"};
  TempFile cleanup{expected_path, FileDisposition::remove_only};

  // When
  { auto file = file_system.Open("test.tmp"); }

  // Then
  REQUIRE(std::filesystem::exists(expected_path));
}

TEST_CASE("M return nullptr W OpenFile fails", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  auto expected_path = std::filesystem::path{"noexist/test.tmp"};

  // When
  auto file = file_system.Open("nexist/test.tmp");

  // Then
  REQUIRE_FALSE(file);
}

TEST_CASE("M not create file outside of its directory W OpenFile {relative}",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile cleanup{"caches/test.tmp", FileDisposition::remove_only};

  // When
  {
    auto file = file_system.Open("../test.tmp");
    REQUIRE(file == nullptr);
  }

  // Then
  REQUIRE(!std::filesystem::exists("caches/test.tmp"));
}

TEST_CASE("M not create file outside of its directory W OpenFile {rooted}",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile cleanup{"/tmp/test.tmp", FileDisposition::remove_only};

  // When
  {
    auto file = file_system.Open("/tmp/test.tmp");
    REQUIRE(file == nullptr);
  }

  // Then
  REQUIRE(!std::filesystem::exists("/tmp/test.tmp"));
}

TEST_CASE("M return empty list for empty directory W ListFilePaths",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  std::filesystem::path dir{"empty"};

  // When
  auto files = file_system.ListPaths(dir);

  // Then
  REQUIRE(files.empty());
}

TEST_CASE(
    "M return empty list for directory outside the filesystem W ListFilePaths "
    "{relative}",
    "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile file1(base_filesystem_dir / "../file1.tmp");
  std::filesystem::path dir{"../"};

  // When
  auto files = file_system.ListPaths(dir);

  // Then
  REQUIRE(files.empty());
}

TEST_CASE(
    "M return empty list for directory outside the filesystem W ListFilePaths "
    "{rooted}",
    "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  std::filesystem::path dir("/usr/bin");

  // When
  auto files = file_system.ListPaths(dir);

  // Then
  REQUIRE(files.empty());
}

TEST_CASE("M return file names for directory W ListFilePaths", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile file1(base_filesystem_dir / "file1.tmp");
  TempFile file2(base_filesystem_dir / "file2.tmp");

  // When
  auto files = file_system.ListPaths("");

  // Then
  REQUIRE(std::find(files.begin(), files.end(),
                    base_filesystem_dir / "file1.tmp") != files.end());
  REQUIRE(std::find(files.begin(), files.end(),
                    base_filesystem_dir / "file2.tmp") != files.end());
}

TEST_CASE("M not recurse directories W ListFilePaths", "[storage]") {
  // Given
  std::filesystem::create_directories(base_filesystem_dir / "subdir");
  TempFile file1(base_filesystem_dir / "subdir" / "file1.tmp");

  // When
  StdDatadogFileSystem file_system;
  auto files = file_system.ListPaths("");

  // Then
  REQUIRE(std::find(files.begin(), files.end(),
                    base_filesystem_dir / "subdir" / "file1.tmp") ==
          files.end());
}

TEST_CASE("M delete file W DeleteFile", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile file1(base_filesystem_dir / "file1.tmp");

  // When
  REQUIRE(DatadogFileStatus::Ok == file_system.Delete("file1.tmp"));

  // Then
  REQUIRE(!std::filesystem::exists(file1.GetPath()));
}

TEST_CASE("M return NoSuchFile W DeleteFile {nonexistant file}", "[storage]") {
  // When
  StdDatadogFileSystem file_system;
  DatadogFileStatus result{};
  REQUIRE_NOTHROW(result = file_system.Delete("noexist.tmp"));

  // Then
  REQUIRE(DatadogFileStatus::NoOperation == result);
}

TEST_CASE(
    "M not delete files outside of file system W DeleteFile {relative file}",
    "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile file1("caches/file1.tmp");

  // When
  DatadogFileStatus result{};
  REQUIRE_NOTHROW(result = file_system.Delete("../file1.tmp"));

  // Then
  REQUIRE(DatadogFileStatus::OperationFailure == result);
  REQUIRE(std::filesystem::exists("caches/file1.tmp"));
}

TEST_CASE("M not delete files outside of file system W DeleteFile {rooted}",
          "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile file1("/tmp/file1.tmp");

  // When
  DatadogFileStatus result{};
  REQUIRE_NOTHROW(result = file_system.Delete("/tmp/file1.tmp"));

  REQUIRE(DatadogFileStatus::OperationFailure == result);
  REQUIRE(std::filesystem::exists("/tmp/file1.tmp"));
}

TEST_CASE("M write file content W IDatadogFile::Write", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile file1(base_filesystem_dir / "write_file.tmp");
  std::string file_content{"file contents\n"};

  // When
  {
    std::unique_ptr<IDatadogFile> file = file_system.Open("write_file.tmp");
    REQUIRE(file);
    auto status = file->Write(file_content);
    REQUIRE(status == DatadogFileStatus::Ok);
  }

  // Then
  std::fstream file{file1.GetPath(), std::fstream::in};
  std::string actual_file_contents;
  std::getline(file, actual_file_contents);

  REQUIRE(actual_file_contents == "file contents");
}

TEST_CASE("M read file content W IDatadogFile::Read", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile temp_file(base_filesystem_dir / "read_file.tmp");
  {
    std::fstream write_file{base_filesystem_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  constexpr size_t buffer_size = 128;
  std::array<char, buffer_size> buffer{};
  {
    std::unique_ptr<IDatadogFile> file = file_system.Open("read_file.tmp");
    REQUIRE(file);
    size_t read = 0;
    auto result = file->ReadArray(buffer, read);

    // THen
    REQUIRE(result == DatadogFileStatus::EndOfFile);
    REQUIRE(std::string_view(buffer.data(), read) == "file contents\n");
  }
}

TEST_CASE("M partial file content W IDatadogFile::Read", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile temp_file(base_filesystem_dir / "read_file.tmp");
  {
    std::fstream write_file{base_filesystem_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  constexpr size_t buffer_size = 8;
  std::array<char, buffer_size> buffer{};
  {
    std::unique_ptr<IDatadogFile> file = file_system.Open("read_file.tmp");
    REQUIRE(file);
    size_t read = 0;
    auto result = file->ReadArray(buffer, read);
    REQUIRE(result == DatadogFileStatus::Ok);
  }

  // Then
  REQUIRE(buffer.size() == buffer_size);
  REQUIRE(std::string_view(buffer.data(), buffer.size()) == "file con");
}

TEST_CASE("M file contents looped W IDatadogFile::Read", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile temp_file(base_filesystem_dir / "read_file.tmp");

  {
    std::fstream write_file{base_filesystem_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  constexpr size_t buffer_size = 8;
  std::array<char, buffer_size> buffer{};
  {
    std::unique_ptr<IDatadogFile> file = file_system.Open("read_file.tmp");
    REQUIRE(file);
    size_t size = 0;
    while (DatadogFileStatus::Ok == file->ReadArray(buffer, size) &&
           size == buffer_size) {
    }
  }
}

TEST_CASE("M return Ok W IDatadogFile::Read {exact length}", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile temp_file(base_filesystem_dir / "read_file.tmp");

  {
    std::fstream write_file{base_filesystem_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  {
    std::unique_ptr<IDatadogFile> file = file_system.Open("read_file.tmp");
    REQUIRE(file);
    size_t file_size = file->GetSize();
    std::vector<char> buffer(file_size);
    size_t read_size{file_size};
    const auto result = file->Read(buffer.data(), read_size);

    // Then
    REQUIRE(result == DatadogFileStatus::Ok);
    REQUIRE(read_size == file_size);
  }
}

TEST_CASE("M return error W IDatadogFile::Read {zero size}", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile temp_file(base_filesystem_dir / "read_file.tmp");

  {
    std::fstream write_file{base_filesystem_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  constexpr size_t buffer_size = 0;
  std::array<char, buffer_size> buffer{};
  {
    std::unique_ptr<IDatadogFile> file = file_system.Open("read_file.tmp");
    REQUIRE(file);
    size_t read = 0;
    auto result = file->ReadArray(buffer, read);

    // THEN
    REQUIRE(result == DatadogFileStatus::OperationFailure);
    REQUIRE(read == 0);
  }
}

TEST_CASE("M return error W IDatadogFile::Read {nullptr}", "[storage]") {
  // Given
  StdDatadogFileSystem file_system;
  TempFile temp_file(base_filesystem_dir / "read_file.tmp");

  {
    std::fstream write_file{base_filesystem_dir / "read_file.tmp",
                            std::fstream::out | std::fstream::trunc};
    write_file << "file contents" << std::endl;
  }

  // When
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
  size_t read_size = 32;
  char* buffer = nullptr;
  {
    std::unique_ptr<IDatadogFile> file = file_system.Open("read_file.tmp");
    REQUIRE(file);
    auto result = file->Read(buffer, read_size);

    // THEN
    REQUIRE(result == DatadogFileStatus::OperationFailure);
    REQUIRE(read_size == 0);
  }
}

}  // namespace
