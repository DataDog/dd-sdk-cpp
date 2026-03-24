// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/platform/filesystem.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <vector>

#include "support/tempdir.hpp"

using namespace datadog;

// Tag [platform-filesystem] describes tests used to validate that a platform-specific
// or user-provided implementation of platform/filesystem.hpp behaves as expected

TEST_CASE("Filesystem", "[unit][platform-filesystem]") {
  TempDirectory tempdir;

  SECTION("M create a valid IStorageDirectory W static Init is called") {
    auto storage_result = platform::Filesystem::Init(tempdir.path);
    REQUIRE(storage_result.has_value());
    auto storage = std::move(*storage_result);
    REQUIRE(storage != nullptr);
  }
}

TEST_CASE("IStorageDirectory", "[unit][platform-filesystem]") {
  // Given a storage directory
  TempDirectory tempdir;
  auto storage_result = platform::Filesystem::Init(tempdir.path);
  REQUIRE(storage_result.has_value());
  auto storage = std::move(*storage_result);
  REQUIRE(storage != nullptr);

  SECTION("M list empty directory W ListFiles is called on empty directory") {
    // When listing files in empty directory
    std::vector<std::string> files;
    auto result = storage->ListFiles(files);

    // Then operation succeeds and list is empty
    REQUIRE(result.has_value());
    REQUIRE(files.empty());
  }

  SECTION("M list files W ListFiles is called on directory with files") {
    // Given some files exist in the directory
    tempdir.WriteFile("test1.dat", "content1");
    tempdir.WriteFile("test2.txt", "content2");

    // When listing files
    std::vector<std::string> files;
    auto result = storage->ListFiles(files);

    // Then operation succeeds and contains expected files
    REQUIRE(result.has_value());
    REQUIRE(files.size() == 2);
    REQUIRE(std::find(files.begin(), files.end(), "test1.dat") != files.end());
    REQUIRE(std::find(files.begin(), files.end(), "test2.txt") != files.end());
  }

  SECTION("M successfully delete file W RemoveFile is called on existing file") {
    // Given a file exists
    tempdir.WriteFile("to_delete.txt", "delete me");
    REQUIRE(tempdir.FileExists("to_delete.txt"));

    // When deleting the file
    auto result = storage->RemoveFile("to_delete.txt");

    // Then operation succeeds and file is gone
    REQUIRE(result.has_value());
    REQUIRE_FALSE(tempdir.FileExists("to_delete.txt"));
  }

  SECTION("M return DoesNotExist error W RemoveFile is called on non-existent file") {
    // When deleting non-existent file
    auto result = storage->RemoveFile("nonexistent.txt");

    // Then operation fails with DoesNotExist error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == platform::FilesystemError::DoesNotExist);
  }

  SECTION("M create subdirectory W PrepareSubdirectory is called") {
    // When preparing a subdirectory
    auto result = storage->PrepareSubdirectory("testdir");

    // Then operation succeeds (directory creation is lazy)
    REQUIRE(result.has_value());
    REQUIRE(result.value() != nullptr);
    // Directory won't exist until first write operation
  }

  SECTION(
      "M return existing subdirectory W PrepareSubdirectory is called on existing "
      "directory"
  ) {
    // Given subdirectory is prepared and has a file written to it (to actually
    // create it)
    tempdir.Mkdirs("existing");
    tempdir.WriteFile("existing/dummy.txt", "content");
    REQUIRE(tempdir.DirectoryExists("existing"));

    // When preparing the same subdirectory again
    auto result2 = storage->PrepareSubdirectory("existing");

    // Then operation succeeds and returns valid directory handle
    REQUIRE(result2.has_value());
    REQUIRE(result2.value() != nullptr);
  }
}

TEST_CASE("IFileReader", "[unit][platform-filesystem]") {
  // Given a storage directory with a test file
  TempDirectory tempdir;
  auto storage_result = platform::Filesystem::Init(tempdir.path);
  REQUIRE(storage_result.has_value());
  auto storage = std::move(*storage_result);
  REQUIRE(storage != nullptr);

  SECTION("M return DoesNotExist error W OpenForRead is called on non-existent file") {
    // When opening non-existent file for read
    auto result = storage->OpenForRead("nonexistent.txt");

    // Then operation fails with DoesNotExist error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == platform::FilesystemError::DoesNotExist);
  }

  SECTION("M open file for read W OpenForRead is called on existing file") {
    // Given a file exists
    const char* test_content = "Test file content";
    tempdir.WriteFile("readable.txt", test_content);

    // When opening file for read
    auto result = storage->OpenForRead("readable.txt");

    // Then operation succeeds and returns valid reader
    REQUIRE(result.has_value());
    REQUIRE(result.value() != nullptr);
  }

  SECTION("M read full contents W read size is equal to file size") {
    // Given a file with 16 bytes of binary data
    tempdir.WriteFile("16a", std::string(16, 'A'));

    // When we open the file and try to read 16 bytes exactly
    auto reader = storage->OpenForRead("16a");
    REQUIRE(reader.has_value());
    char read_buffer[16];
    auto num_bytes_read = (*reader)->Read(read_buffer, sizeof(read_buffer));

    // Then the read is successful
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == 16);
    char expected[16];
    std::memset(expected, 'A', sizeof(expected));
    REQUIRE(std::memcmp(read_buffer, expected, 16) == 0);

    // And a subsequent read will do nothing and return 0 bytes read
    std::memset(read_buffer, '_', sizeof(read_buffer));
    num_bytes_read = (*reader)->Read(read_buffer, sizeof(read_buffer));
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == 0);
    REQUIRE(read_buffer[0] == '_');
  }

  SECTION("M read full contents W read size is greater than file size") {
    // Given a file with 16 bytes of binary data
    tempdir.WriteFile("16b", std::string(16, 'B'));

    // When we open the file and try to read 32 bytes
    auto reader = storage->OpenForRead("16b");
    REQUIRE(reader.has_value());
    char read_buffer[32];
    auto num_bytes_read = (*reader)->Read(read_buffer, sizeof(read_buffer));

    // Then the read is successful, with a size of 16
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == 16);
    char expected[16];
    std::memset(expected, 'B', sizeof(expected));
    REQUIRE(std::memcmp(read_buffer, expected, 16) == 0);

    // And a subsequent read will do nothing and return 0 bytes read
    std::memset(read_buffer, '_', sizeof(read_buffer));
    num_bytes_read = (*reader)->Read(read_buffer, sizeof(read_buffer));
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == 0);
    REQUIRE(read_buffer[0] == '_');
  }

  SECTION("M read 0 bytes W read size is 0") {
    // Given an empty file
    tempdir.WriteFile("empty.txt", "");

    // When we open the file and try to read 16 bytes
    auto reader = storage->OpenForRead("empty.txt");
    REQUIRE(reader.has_value());
    char read_buffer[16];
    auto num_bytes_read = (*reader)->Read(read_buffer, sizeof(read_buffer));

    // Then operation succeeds with zero bytes read
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == 0);
  }

  SECTION("M seek forward in file W Seek is called with positive offset") {
    // Given a file with known content
    const char* test_content = "0123456789abcdef";
    tempdir.WriteFile("seek_test.txt", test_content);

    // When opening file and seeking forward
    auto reader_result = storage->OpenForRead("seek_test.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    auto seek_result = reader->Seek(5);
    REQUIRE(seek_result.has_value());

    // Then subsequent read starts from seeked position
    char buffer[5];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == sizeof(buffer));
    REQUIRE(std::memcmp(buffer, "56789", sizeof(buffer)) == 0);
  }

  SECTION(
      "M seek backward in file W Seek is called with negative offset after forward "
      "seek"
  ) {
    // Given a file with known content
    const char* test_content = "0123456789abcdef";
    tempdir.WriteFile("seek_back_test.txt", test_content);

    auto reader_result = storage->OpenForRead("seek_back_test.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    auto seek_forward = reader->Seek(8);
    REQUIRE(seek_forward.has_value());

    // When seeking backward
    auto seek_back = reader->Seek(-3);
    REQUIRE(seek_back.has_value());

    // Then subsequent read starts from new position
    char buffer[3];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == sizeof(buffer));
    REQUIRE(std::memcmp(buffer, "567", sizeof(buffer)) == 0);
  }

  SECTION("M return Failed error W Seek is called with offset before beginning") {
    // Given a file
    tempdir.WriteFile("seek_error_test.txt", "content");

    auto reader_result = storage->OpenForRead("seek_error_test.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    // When seeking before beginning of file
    auto seek_result = reader->Seek(-10);

    // Then operation fails with Failed error
    REQUIRE_FALSE(seek_result.has_value());
    REQUIRE(seek_result.error() == platform::FilesystemError::Failed);
  }

  SECTION("M handle seek past EOF correctly W Seek is called beyond file end") {
    // Given a short file
    tempdir.WriteFile("seek_eof_test.txt", "short");

    auto reader_result = storage->OpenForRead("seek_eof_test.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    // When seeking past end of file
    auto seek_result = reader->Seek(100);
    REQUIRE(seek_result.has_value());  // Should not error

    // Then subsequent read returns EOF
    char buffer[10];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == 0);
  }

  SECTION("M read binary data correctly W Read is called on file with binary content") {
    // Given a file with binary data (including null bytes)
    const char binary_data[] = {
        0x00, 0x01, 0x02, static_cast<char>(0xFF), static_cast<char>(0xFE), 0x00, 0x03
    };
    size_t data_len = sizeof(binary_data);
    tempdir.WriteFile(
        "binary_read_test.dat", std::string_view(binary_data, sizeof(binary_data))
    );

    // When opening and reading binary file
    auto reader_result = storage->OpenForRead("binary_read_test.dat");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    char buffer[10];
    auto num_bytes_read = reader->Read(buffer, sizeof(buffer));

    // Then operation succeeds and reads correct binary data
    REQUIRE(num_bytes_read.has_value());
    REQUIRE(num_bytes_read.value() == data_len);
    REQUIRE(std::memcmp(buffer, binary_data, data_len) == 0);
  }
}

TEST_CASE("Subdirectory Operations", "[unit][platform-filesystem]") {
  // Given a storage directory with a subdirectory
  TempDirectory tempdir;
  auto storage_result = platform::Filesystem::Init(tempdir.path);
  REQUIRE(storage_result.has_value());
  auto storage = std::move(*storage_result);
  REQUIRE(storage != nullptr);

  auto subdir_result = storage->PrepareSubdirectory("subdir");
  REQUIRE(subdir_result.has_value());
  auto subdir = std::move(subdir_result.value());

  SECTION("M operate on files in subdirectory W using IDirectory operations") {
    // Given a file in the subdirectory, written directly to disk
    tempdir.Mkdirs("subdir");
    const char* test_data = "Subdirectory content";
    tempdir.WriteFile("subdir/sub_file.txt", test_data);

    // Then file exists in subdirectory and can be read via IDirectory
    REQUIRE(tempdir.FileExists("subdir/sub_file.txt"));

    auto reader_result = subdir->OpenForRead("sub_file.txt");
    REQUIRE(reader_result.has_value());
    auto reader = std::move(reader_result.value());

    char buffer[50];
    auto read_result = reader->Read(buffer, sizeof(buffer));
    REQUIRE(read_result.has_value());
    REQUIRE(std::memcmp(buffer, test_data, std::strlen(test_data)) == 0);
  }

  SECTION("M list files in subdirectory W ListFiles is called on subdirectory") {
    // Given files in subdirectory, written directly to disk
    tempdir.Mkdirs("subdir");
    tempdir.WriteFile("subdir/file1.dat", "content1");
    tempdir.WriteFile("subdir/file2.dat", "content2");

    // When listing files in subdirectory
    std::vector<std::string> files;
    auto list_result = subdir->ListFiles(files);

    // Then operation succeeds and contains expected files
    REQUIRE(list_result.has_value());
    REQUIRE(files.size() == 2);
    REQUIRE(std::find(files.begin(), files.end(), "file1.dat") != files.end());
    REQUIRE(std::find(files.begin(), files.end(), "file2.dat") != files.end());
  }

  SECTION(
      "M create nested subdirectories W PrepareSubdirectory is called recursively"
  ) {
    // When creating a nested subdirectory handle and checking that a file written
    // directly to disk is visible through it
    auto nested_result = subdir->PrepareSubdirectory("nested");
    REQUIRE(nested_result.has_value());
    auto nested = std::move(nested_result.value());

    // Given a file written directly to disk in the nested directory
    tempdir.Mkdirs("subdir/nested");
    const char* nested_content = "Nested content";
    tempdir.WriteFile("subdir/nested/nested_file.txt", nested_content);

    // Then the file is accessible through the nested IDirectory handle
    REQUIRE(tempdir.DirectoryExists("subdir/nested"));
    REQUIRE(tempdir.FileExists("subdir/nested/nested_file.txt"));

    auto reader_result = nested->OpenForRead("nested_file.txt");
    REQUIRE(reader_result.has_value());
  }
}
