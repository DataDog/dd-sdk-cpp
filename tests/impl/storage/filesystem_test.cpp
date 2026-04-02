// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/filesystem.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "support/catch.hpp"
#include "support/tempdir.hpp"

using namespace datadog::impl;

// Helper to convert StoragePath to PlatformPath
static PlatformPath MakePlatformPath(const StoragePath& storage_path) {
  PlatformPath platform_path;
  const bool ok = platform_path.Encode(storage_path.CStr());
  REQUIRE(ok);
  return platform_path;
}

TEST_CASE("IFilesystem directory operations", "[unit][storage][filesystem]") {
  TempDirectory temp;
  auto fs = CreateFilesystem();

  SECTION("M return OK W creating new directory") {
    // Given a path to a non-existent directory
    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("newdir")));

    // When we create the directory
    const FilesystemResult result = fs->CreateDirectory(MakePlatformPath(dir_path));

    // Then the operation succeeds
    REQUIRE(result == FilesystemResult::OK);

    // And the directory exists
    REQUIRE(temp.DirectoryExists("newdir"));
  }

  SECTION(
      "M return AlreadyExistsAsDirectory W creating directory that already exists"
  ) {
    // Given a directory that already exists
    temp.Mkdirs("existingdir");

    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("existingdir")));

    // When we attempt to create it again
    const FilesystemResult result = fs->CreateDirectory(MakePlatformPath(dir_path));

    // Then the operation returns AlreadyExistsAsDirectory
    REQUIRE(result == FilesystemResult::AlreadyExistsAsDirectory);
  }

  SECTION(
      "M return AlreadyExists or PermissionDenied W creating directory over "
      "existing file"
  ) {
    // Given a file at a specific path
    temp.WriteFile("somefile", "content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("somefile")));

    // When we attempt to create a directory at that same path
    const FilesystemResult result = fs->CreateDirectory(MakePlatformPath(file_path));

    // Then the operation returns AlreadyExists or PermissionDenied
    REQUIRE(
        (result == FilesystemResult::AlreadyExists ||
         result == FilesystemResult::PermissionDenied)
    );
  }

  SECTION("M return DoesNotExist W creating directory without parent") {
    // Given a path with a non-existent parent directory
    StoragePath nonexistent_parent;
    REQUIRE(
        (nonexistent_parent.Set(temp.path) &&
         nonexistent_parent.Append("nonexistent/child"))
    );

    // When we attempt to create the child directory
    const FilesystemResult result =
        fs->CreateDirectory(MakePlatformPath(nonexistent_parent));

    // Then the operation returns DoesNotExist
    REQUIRE(result == FilesystemResult::DoesNotExist);
  }
}

TEST_CASE("IFilesystem file enumeration", "[unit][storage][filesystem]") {
  TempDirectory temp;
  auto fs = CreateFilesystem();

  SECTION("M return empty vector W listing empty directory") {
    // Given an empty directory
    temp.Mkdirs("emptydir");
    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("emptydir")));

    // When we list files in the directory
    std::vector<std::string> files;
    const FilesystemResult result = fs->ListFiles(MakePlatformPath(dir_path), files);

    // Then the operation succeeds
    REQUIRE(result == FilesystemResult::OK);

    // And the result is empty
    REQUIRE(files.empty());
  }

  SECTION("M list only files W listing directory with files only") {
    // Given a directory containing three files
    temp.Mkdirs("filesdir");
    temp.WriteFile("filesdir/file1.txt", "content1");
    temp.WriteFile("filesdir/file2.txt", "content2");
    temp.WriteFile("filesdir/file3.txt", "content3");
    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("filesdir")));

    // When we list files in the directory
    std::vector<std::string> files;
    const FilesystemResult result = fs->ListFiles(MakePlatformPath(dir_path), files);

    // Then the operation succeeds
    REQUIRE(result == FilesystemResult::OK);

    // And all three files are returned
    REQUIRE(files.size() == 3);
    std::sort(files.begin(), files.end());
    REQUIRE(files[0] == "file1.txt");
    REQUIRE(files[1] == "file2.txt");
    REQUIRE(files[2] == "file3.txt");
  }

  SECTION("M list only subdirectories W listing directory with subdirs only") {
    // Given a directory containing two subdirectories
    temp.Mkdirs("parentdir");
    temp.Mkdirs("parentdir/subdir1");
    temp.Mkdirs("parentdir/subdir2");
    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("parentdir")));

    // When we list subdirectories
    std::vector<std::string> subdirs;
    const FilesystemResult result =
        fs->ListSubdirectories(MakePlatformPath(dir_path), subdirs);

    // Then the operation succeeds
    REQUIRE(result == FilesystemResult::OK);

    // And both subdirectories are returned
    REQUIRE(subdirs.size() == 2);
    std::sort(subdirs.begin(), subdirs.end());
    REQUIRE(subdirs[0] == "subdir1");
    REQUIRE(subdirs[1] == "subdir2");
  }

  SECTION("M list correctly W directory has mixed content") {
    // Given a directory containing both files and subdirectories
    temp.Mkdirs("mixeddir");
    temp.Mkdirs("mixeddir/subdir1");
    temp.Mkdirs("mixeddir/subdir2");
    temp.WriteFile("mixeddir/file1.txt", "content1");
    temp.WriteFile("mixeddir/file2.txt", "content2");
    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("mixeddir")));

    // When we list files
    std::vector<std::string> files;
    const FilesystemResult files_result =
        fs->ListFiles(MakePlatformPath(dir_path), files);

    // Then only the files are returned
    REQUIRE(files_result == FilesystemResult::OK);
    REQUIRE(files.size() == 2);

    // When we list subdirectories
    std::vector<std::string> subdirs;
    const FilesystemResult subdirs_result =
        fs->ListSubdirectories(MakePlatformPath(dir_path), subdirs);

    // Then only the subdirectories are returned
    REQUIRE(subdirs_result == FilesystemResult::OK);
    REQUIRE(subdirs.size() == 2);
  }

  SECTION("M return DoesNotExist W ListFiles called on non-existent directory") {
    StoragePath nonexistent;
    REQUIRE((nonexistent.Set(temp.path) && nonexistent.Append("nonexistent")));

    std::vector<std::string> files;
    const FilesystemResult result = fs->ListFiles(MakePlatformPath(nonexistent), files);

    REQUIRE(result == FilesystemResult::DoesNotExist);
  }

  SECTION(
      "M return DoesNotExist W ListSubdirectories called on non-existent directory"
  ) {
    StoragePath nonexistent;
    REQUIRE((nonexistent.Set(temp.path) && nonexistent.Append("nonexistent")));

    std::vector<std::string> subdirs;
    const FilesystemResult result =
        fs->ListSubdirectories(MakePlatformPath(nonexistent), subdirs);

    REQUIRE(result == FilesystemResult::DoesNotExist);
  }

  SECTION("M return error W listing path that is a file, not a directory") {
    // Given a file at a specific path
    temp.WriteFile("afile.txt", "content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("afile.txt")));

    // When we attempt to list it as a directory, both ListFiles and
    // ListSubdirectories should fail — the path exists but is not a directory
    std::vector<std::string> files;
    const FilesystemResult files_result =
        fs->ListFiles(MakePlatformPath(file_path), files);
    REQUIRE(files_result != FilesystemResult::OK);

    std::vector<std::string> subdirs;
    const FilesystemResult subdirs_result =
        fs->ListSubdirectories(MakePlatformPath(file_path), subdirs);
    REQUIRE(subdirs_result != FilesystemResult::OK);
  }
}

TEST_CASE("IFilesystem file writing", "[unit][storage][filesystem]") {
  TempDirectory temp;
  auto fs = CreateFilesystem();

  SECTION("M create file with content W opening new file for write") {
    // Given a path to a new file
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("newfile.txt")));

    // When we open the file for writing
    const auto open_result =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);
    REQUIRE(open_result.value == FilesystemResult::OK);
    REQUIRE(open_result.handle != INVALID_FILE_HANDLE);

    // And write data to it
    const char* data = "Hello, World!";
    const auto write_result = fs->Write(open_result.handle, data, std::strlen(data));
    REQUIRE(write_result.value == FilesystemResult::OK);
    REQUIRE(write_result.bytes_written == std::strlen(data));

    // And close the file
    const FilesystemResult close_result = fs->Close(open_result.handle);
    REQUIRE(close_result == FilesystemResult::OK);

    // Then the file exists with the expected content
    REQUIRE(temp.FileExists("newfile.txt"));
    REQUIRE(temp.ReadFileContents("newfile.txt") == "Hello, World!");
  }

  SECTION("M truncate file W opening existing file in truncate mode") {
    // Given an existing file with content
    temp.WriteFile("existing.txt", "old content that will be truncated");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("existing.txt")));

    // When we open it in truncate mode and write new data
    const auto open_result =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);
    REQUIRE(open_result.value == FilesystemResult::OK);
    const char* new_data = "new";
    fs->Write(open_result.handle, new_data, std::strlen(new_data));
    fs->Close(open_result.handle);

    // Then the old content is replaced
    REQUIRE(temp.ReadFileContents("existing.txt") == "new");
  }

  SECTION("M preserve existing content W opening existing file in append mode") {
    // Given an existing file with content
    temp.WriteFile("appendfile.txt", "existing content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("appendfile.txt")));

    // When we open it in append mode and write additional data
    const auto open_result = fs->OpenForWrite(MakePlatformPath(file_path), true, false);
    REQUIRE(open_result.value == FilesystemResult::OK);
    const char* appended = " appended";
    fs->Write(open_result.handle, appended, std::strlen(appended));
    fs->Close(open_result.handle);

    // Then the new data is appended to the existing content
    REQUIRE(temp.ReadFileContents("appendfile.txt") == "existing content appended");
  }

  SECTION("M write binary data W file contains null bytes") {
    // Given a new binary file
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("binary.dat")));
    const auto open_result =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);
    REQUIRE(open_result.value == FilesystemResult::OK);

    // And binary data with null bytes and full byte range
    std::array<char, 256> binary_data;
    for (size_t i = 0; i < binary_data.size(); ++i) {
      binary_data[i] = static_cast<char>(i);
    }

    // When we write binary data including null bytes
    const auto write_result =
        fs->Write(open_result.handle, binary_data.data(), binary_data.size());
    REQUIRE(write_result.value == FilesystemResult::OK);
    REQUIRE(write_result.bytes_written == binary_data.size());
    fs->Close(open_result.handle);

    // Then all 256 bytes are written correctly
    const std::string contents = temp.ReadFileContents("binary.dat");
    REQUIRE(contents.size() == 256);
    for (size_t i = 0; i < 256; ++i) {
      REQUIRE(static_cast<unsigned char>(contents[i]) == i);
    }
  }

  SECTION("M handle zero-byte write W writing empty data") {
    // Given a new file opened for writing
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("zerobyte.txt")));
    const auto open_result =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);
    REQUIRE(open_result.value == FilesystemResult::OK);

    // When we write zero bytes
    const char* data = "dummy";
    const auto write_result = fs->Write(open_result.handle, data, 0);

    // Then the operation succeeds with zero bytes written
    REQUIRE(write_result.value == FilesystemResult::OK);
    REQUIRE(write_result.bytes_written == 0);

    fs->Close(open_result.handle);

    // And the file exists but is empty
    REQUIRE(temp.FileExists("zerobyte.txt"));
    REQUIRE(temp.ReadFileContents("zerobyte.txt") == "");
  }

  SECTION("M write data in multiple sequential calls W same file handle") {
    // Given a new file opened for writing
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("sequential.txt")));
    const auto open_result =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);
    REQUIRE(open_result.value == FilesystemResult::OK);

    // When we write data in multiple sequential calls
    const char* part1 = "part1";
    const char* part2 = "part2";
    const char* part3 = "part3";
    const auto write1 = fs->Write(open_result.handle, part1, std::strlen(part1));
    REQUIRE(write1.value == FilesystemResult::OK);
    REQUIRE(write1.bytes_written == std::strlen(part1));
    const auto write2 = fs->Write(open_result.handle, part2, std::strlen(part2));
    REQUIRE(write2.value == FilesystemResult::OK);
    REQUIRE(write2.bytes_written == std::strlen(part2));
    const auto write3 = fs->Write(open_result.handle, part3, std::strlen(part3));
    REQUIRE(write3.value == FilesystemResult::OK);
    REQUIRE(write3.bytes_written == std::strlen(part3));

    fs->Close(open_result.handle);

    // Then all parts are written sequentially
    REQUIRE(temp.ReadFileContents("sequential.txt") == "part1part2part3");
  }
}

TEST_CASE("IFilesystem file reading", "[unit][storage][filesystem]") {
  TempDirectory temp;
  auto fs = CreateFilesystem();

  SECTION("M read file contents W opening existing file for read") {
    // Given an existing file with content
    const std::string content = "file contents";
    temp.WriteFile("readable.txt", content);
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("readable.txt")));

    // When we open the file for reading
    const auto open_result = fs->OpenForRead(MakePlatformPath(file_path), false);
    REQUIRE(open_result.value == FilesystemResult::OK);
    REQUIRE(open_result.handle != INVALID_FILE_HANDLE);

    // And read from it
    char buffer[100];
    const auto read_result = fs->Read(open_result.handle, buffer, sizeof(buffer));

    // Then the content is read successfully
    REQUIRE(read_result.value == FilesystemResult::OK);
    REQUIRE(read_result.bytes_read == content.size());
    REQUIRE(std::string(buffer, read_result.bytes_read) == content);

    fs->Close(open_result.handle);
  }

  SECTION("M return DoesNotExist W opening non-existent file for read") {
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("nonexistent.txt")));

    const auto open_result = fs->OpenForRead(MakePlatformPath(file_path), false);

    REQUIRE(open_result.value == FilesystemResult::DoesNotExist);
    REQUIRE(open_result.handle == INVALID_FILE_HANDLE);
  }

  SECTION("M return fewer bytes W reading with large buffer from small file") {
    // Given a small file
    temp.WriteFile("small.txt", "small");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("small.txt")));

    // When we read with a buffer larger than the file size
    const auto open_result = fs->OpenForRead(MakePlatformPath(file_path), false);
    REQUIRE(open_result.value == FilesystemResult::OK);
    char buffer[1000];
    const auto read_result = fs->Read(open_result.handle, buffer, sizeof(buffer));

    // Then only the actual file size is returned
    REQUIRE(read_result.value == FilesystemResult::OK);
    REQUIRE(read_result.bytes_read == 5);

    fs->Close(open_result.handle);
  }

  SECTION("M return 0 bytes W reading from empty file") {
    // Given an empty file
    temp.WriteFile("empty.txt", "");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("empty.txt")));

    // When we read from it
    const auto open_result = fs->OpenForRead(MakePlatformPath(file_path), false);
    REQUIRE(open_result.value == FilesystemResult::OK);
    char buffer[100];
    const auto read_result = fs->Read(open_result.handle, buffer, sizeof(buffer));

    // Then zero bytes are returned
    REQUIRE(read_result.value == FilesystemResult::OK);
    REQUIRE(read_result.bytes_read == 0);

    fs->Close(open_result.handle);
  }

  SECTION("M read file in multiple chunks W sequential reads until EOF") {
    // Given a file with 16 bytes of content
    temp.WriteFile("chunks.txt", "0123456789ABCDEF");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("chunks.txt")));
    const auto open_result = fs->OpenForRead(MakePlatformPath(file_path), false);
    REQUIRE(open_result.value == FilesystemResult::OK);

    // When we read in chunks
    char buffer[10];
    const auto read1 = fs->Read(open_result.handle, buffer, 5);
    REQUIRE(read1.value == FilesystemResult::OK);
    REQUIRE(read1.bytes_read == 5);
    REQUIRE(std::string(buffer, read1.bytes_read) == "01234");

    const auto read2 = fs->Read(open_result.handle, buffer, 5);
    REQUIRE(read2.value == FilesystemResult::OK);
    REQUIRE(read2.bytes_read == 5);
    REQUIRE(std::string(buffer, read2.bytes_read) == "56789");

    const auto read3 = fs->Read(open_result.handle, buffer, 10);
    REQUIRE(read3.value == FilesystemResult::OK);
    REQUIRE(read3.bytes_read == 6);
    REQUIRE(std::string(buffer, read3.bytes_read) == "ABCDEF");

    // And read again at EOF
    const auto read4 = fs->Read(open_result.handle, buffer, 10);

    // Then we get EOF indication
    REQUIRE(read4.value == FilesystemResult::OK);
    REQUIRE(read4.bytes_read == 0);

    fs->Close(open_result.handle);
  }

  SECTION("M handle zero-byte read W reading zero bytes") {
    // Given a file with content
    temp.WriteFile("zerobytes.txt", "content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("zerobytes.txt")));
    const auto open_result = fs->OpenForRead(MakePlatformPath(file_path), false);
    REQUIRE(open_result.value == FilesystemResult::OK);

    // When we read zero bytes
    char buffer[100];
    const auto read_result = fs->Read(open_result.handle, buffer, 0);

    // Then the operation succeeds with zero bytes read
    REQUIRE(read_result.value == FilesystemResult::OK);
    REQUIRE(read_result.bytes_read == 0);

    fs->Close(open_result.handle);
  }
}

TEST_CASE("IFilesystem advisory locking", "[unit][storage][filesystem]") {
  TempDirectory temp;
  auto fs = CreateFilesystem();

  SECTION("M acquire lock W writing with advisory lock") {
    // Given a path for a new file
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("locked.txt")));

    // When we open the file with advisory locking enabled
    const auto open_result = fs->OpenForWrite(MakePlatformPath(file_path), false, true);
    REQUIRE(open_result.value == FilesystemResult::OK);
    REQUIRE(open_result.handle != INVALID_FILE_HANDLE);

    // And write data
    const char* data = "locked content";
    fs->Write(open_result.handle, data, std::strlen(data));
    fs->Close(open_result.handle);

    // Then the file is written successfully
    REQUIRE(temp.ReadFileContents("locked.txt") == "locked content");
  }

  SECTION("M return LockContention W second writer requests advisory lock") {
    // Given a file opened with an advisory lock held
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("contended.txt")));
    const auto first_open = fs->OpenForWrite(MakePlatformPath(file_path), false, true);
    REQUIRE(first_open.value == FilesystemResult::OK);

    // When a second writer attempts to acquire the lock
    const auto second_open = fs->OpenForWrite(MakePlatformPath(file_path), false, true);

    // Then the operation returns LockContention
    REQUIRE(second_open.value == FilesystemResult::LockContention);
    REQUIRE(second_open.handle == INVALID_FILE_HANDLE);

    fs->Close(first_open.handle);
  }

  SECTION("M acquire lock W reading with advisory lock") {
    // Given an existing file
    temp.WriteFile("readlocked.txt", "content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("readlocked.txt")));

    // When we open it for reading with advisory locking enabled
    const auto open_result = fs->OpenForRead(MakePlatformPath(file_path), true);

    // Then the operation succeeds
    REQUIRE(open_result.value == FilesystemResult::OK);
    REQUIRE(open_result.handle != INVALID_FILE_HANDLE);

    fs->Close(open_result.handle);
  }

  SECTION("M return LockContention W second reader requests advisory lock") {
    // Given a file opened for reading with an advisory lock held
    temp.WriteFile("readcontended.txt", "content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("readcontended.txt")));
    const auto first_open = fs->OpenForRead(MakePlatformPath(file_path), true);
    REQUIRE(first_open.value == FilesystemResult::OK);

    // When a second reader attempts to acquire the lock
    const auto second_open = fs->OpenForRead(MakePlatformPath(file_path), true);

    // Then the operation returns LockContention
    REQUIRE(second_open.value == FilesystemResult::LockContention);
    REQUIRE(second_open.handle == INVALID_FILE_HANDLE);

    fs->Close(first_open.handle);
  }

  SECTION("M allow second lock W first writer releases lock") {
    // Given a file that was previously locked and released
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("released.txt")));
    const auto first_open = fs->OpenForWrite(MakePlatformPath(file_path), false, true);
    REQUIRE(first_open.value == FilesystemResult::OK);

    fs->Close(first_open.handle);

    // When we attempt to acquire the lock again
    const auto second_open = fs->OpenForWrite(MakePlatformPath(file_path), false, true);

    // Then the operation succeeds
    REQUIRE(second_open.value == FilesystemResult::OK);

    fs->Close(second_open.handle);
  }

  SECTION("M allow multiple handles W opening without advisory lock") {
    // Given a file opened without advisory locking
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("nonlocked.txt")));
    const auto write1 = fs->OpenForWrite(MakePlatformPath(file_path), false, false);
    REQUIRE(write1.value == FilesystemResult::OK);

    // When we open it again without advisory locking
    const auto write2 = fs->OpenForWrite(MakePlatformPath(file_path), false, false);

    // Then both opens succeed
    REQUIRE(write2.value == FilesystemResult::OK);

    fs->Close(write1.handle);
    fs->Close(write2.handle);
  }

  SECTION("M allow multiple readers W opening for read without advisory lock") {
    // Given a file opened for reading without advisory locking
    temp.WriteFile("nonlockedread.txt", "content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("nonlockedread.txt")));
    const auto read1 = fs->OpenForRead(MakePlatformPath(file_path), false);
    REQUIRE(read1.value == FilesystemResult::OK);

    // When we open it again without advisory locking
    const auto read2 = fs->OpenForRead(MakePlatformPath(file_path), false);

    // Then both opens succeed
    REQUIRE(read2.value == FilesystemResult::OK);

    fs->Close(read1.handle);
    fs->Close(read2.handle);
  }

  SECTION("M return LockContention W read with lock when write holds lock") {
    // Given a file opened for writing with an advisory lock held
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("writelock.txt")));
    const auto write_open = fs->OpenForWrite(MakePlatformPath(file_path), false, true);
    REQUIRE(write_open.value == FilesystemResult::OK);

    // When we attempt to open it for reading with advisory locking
    const auto read_open = fs->OpenForRead(MakePlatformPath(file_path), true);

    // Then the operation returns LockContention
    REQUIRE(read_open.value == FilesystemResult::LockContention);

    fs->Close(write_open.handle);
  }

  SECTION("M return LockContention W write with lock when read holds lock") {
    // Given a file opened for reading with an advisory lock held
    temp.WriteFile("readlock.txt", "content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("readlock.txt")));
    const auto read_open = fs->OpenForRead(MakePlatformPath(file_path), true);
    REQUIRE(read_open.value == FilesystemResult::OK);

    // When we attempt to open it for writing with advisory locking
    const auto write_open = fs->OpenForWrite(MakePlatformPath(file_path), false, true);

    // Then the operation returns LockContention
    REQUIRE(write_open.value == FilesystemResult::LockContention);

    fs->Close(read_open.handle);
  }

  SECTION("M succeed W write without lock when advisory lock held") {
    // Given a file opened with an advisory lock held
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("advisoryignored.txt")));
    const auto locked_write =
        fs->OpenForWrite(MakePlatformPath(file_path), false, true);
    REQUIRE(locked_write.value == FilesystemResult::OK);

    // When we open the same file without requesting an advisory lock
    const auto unlocked_write =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);

    // Then the operation succeeds (advisory locks are voluntary)
    REQUIRE(unlocked_write.value == FilesystemResult::OK);

    fs->Close(locked_write.handle);
    fs->Close(unlocked_write.handle);
  }
}

TEST_CASE("IFilesystem file operations", "[unit][storage][filesystem]") {
  TempDirectory temp;
  auto fs = CreateFilesystem();

  SECTION("M delete file W file exists") {
    // Given an existing file
    temp.WriteFile("deleteme.txt", "content");
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("deleteme.txt")));

    // When we delete the file
    const FilesystemResult result = fs->Delete(MakePlatformPath(file_path));

    // Then the operation succeeds
    REQUIRE(result == FilesystemResult::OK);

    // And the file no longer exists
    REQUIRE(!temp.FileExists("deleteme.txt"));
  }

  SECTION("M return DoesNotExist W deleting non-existent file") {
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("nonexistent.txt")));

    const FilesystemResult result = fs->Delete(MakePlatformPath(file_path));

    REQUIRE(result == FilesystemResult::DoesNotExist);
  }

  SECTION("M move file atomically W renaming file") {
    // Given a source file
    temp.WriteFile("source.txt", "content to move");
    StoragePath src_path;
    REQUIRE((src_path.Set(temp.path) && src_path.Append("source.txt")));

    // When we rename it to a new location
    StoragePath dst_path;
    REQUIRE((dst_path.Set(temp.path) && dst_path.Append("destination.txt")));
    const FilesystemResult result =
        fs->Rename(MakePlatformPath(src_path), MakePlatformPath(dst_path));

    // Then the operation succeeds
    REQUIRE(result == FilesystemResult::OK);

    // And the source no longer exists
    REQUIRE(!temp.FileExists("source.txt"));

    // And the destination exists with the original content
    REQUIRE(temp.FileExists("destination.txt"));
    REQUIRE(temp.ReadFileContents("destination.txt") == "content to move");
  }

  SECTION("M return AlreadyExists W renaming to existing destination") {
    // Given a source file and an existing destination file
    temp.WriteFile("source.txt", "source content");
    temp.WriteFile("destination.txt", "destination content");
    StoragePath src_path;
    REQUIRE((src_path.Set(temp.path) && src_path.Append("source.txt")));
    StoragePath dst_path;
    REQUIRE((dst_path.Set(temp.path) && dst_path.Append("destination.txt")));

    // When we attempt to rename to the existing destination
    const FilesystemResult result =
        fs->Rename(MakePlatformPath(src_path), MakePlatformPath(dst_path));

    // Then the operation returns AlreadyExists
    REQUIRE(result == FilesystemResult::AlreadyExists);

    // And both files remain unchanged
    REQUIRE(temp.FileExists("source.txt"));
    REQUIRE(temp.ReadFileContents("source.txt") == "source content");
    REQUIRE(temp.ReadFileContents("destination.txt") == "destination content");
  }

  SECTION("M return DoesNotExist W renaming non-existent source") {
    StoragePath src_path;
    REQUIRE((src_path.Set(temp.path) && src_path.Append("nonexistent.txt")));
    StoragePath dst_path;
    REQUIRE((dst_path.Set(temp.path) && dst_path.Append("destination.txt")));

    const FilesystemResult result =
        fs->Rename(MakePlatformPath(src_path), MakePlatformPath(dst_path));

    REQUIRE(result == FilesystemResult::DoesNotExist);
  }

  SECTION("M rename across directories W source and dest in different directories") {
    // Given two directories with a file in the first
    temp.Mkdirs("dir1");
    temp.Mkdirs("dir2");
    temp.WriteFile("dir1/moveme.txt", "content to move");
    StoragePath src_path;
    REQUIRE((src_path.Set(temp.path) && src_path.Append("dir1/moveme.txt")));
    StoragePath dst_path;
    REQUIRE((dst_path.Set(temp.path) && dst_path.Append("dir2/moveme.txt")));

    // When we rename the file to the second directory
    const FilesystemResult result =
        fs->Rename(MakePlatformPath(src_path), MakePlatformPath(dst_path));

    // Then the operation succeeds
    REQUIRE(result == FilesystemResult::OK);

    // And the source no longer exists
    REQUIRE(!temp.FileExists("dir1/moveme.txt"));

    // And the destination exists with the original content
    REQUIRE(temp.FileExists("dir2/moveme.txt"));
    REQUIRE(temp.ReadFileContents("dir2/moveme.txt") == "content to move");
  }

  SECTION("M delete empty directory W DeleteDirectory is called") {
    // Given an empty directory
    temp.Mkdirs("emptydir");
    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("emptydir")));

    // When we delete the directory
    const FilesystemResult result = fs->DeleteDirectory(MakePlatformPath(dir_path));

    // Then the operation succeeds
    REQUIRE(result == FilesystemResult::OK);

    // And the directory no longer exists
    REQUIRE(!temp.DirectoryExists("emptydir"));
  }

  SECTION("M return DoesNotExist W DeleteDirectory called on non-existent path") {
    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("nonexistent")));

    const FilesystemResult result = fs->DeleteDirectory(MakePlatformPath(dir_path));

    REQUIRE(result == FilesystemResult::DoesNotExist);
  }

  SECTION(
      "M return DirectoryNotEmpty W DeleteDirectory called on non-empty directory"
  ) {
    // Given a directory with a file in it
    temp.Mkdirs("nonempty");
    temp.WriteFile("nonempty/file.txt", "content");
    StoragePath dir_path;
    REQUIRE((dir_path.Set(temp.path) && dir_path.Append("nonempty")));

    // When we attempt to delete the non-empty directory
    const FilesystemResult result = fs->DeleteDirectory(MakePlatformPath(dir_path));

    // Then the operation fails
    REQUIRE(result == FilesystemResult::DirectoryNotEmpty);
  }
}

TEST_CASE("IFilesystem path handling", "[unit][storage][filesystem]") {
  TempDirectory temp;
  auto fs = CreateFilesystem();

  SECTION("M handle UTF-8 paths W creating file with UTF-8 name") {
    // Given a path with UTF-8 characters
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("文件.txt")));

    // When we create and write to the file
    const auto open_result =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);
    REQUIRE(open_result.value == FilesystemResult::OK);
    const char* data = "UTF-8 content";
    fs->Write(open_result.handle, data, std::strlen(data));
    fs->Close(open_result.handle);

    // Then the file is created successfully
    REQUIRE(temp.FileExists("文件.txt"));
  }

  SECTION("M handle paths with spaces W creating file") {
    // Given a path with spaces
    StoragePath file_path;
    REQUIRE((file_path.Set(temp.path) && file_path.Append("file with spaces.txt")));

    // When we create the file
    const auto open_result =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);
    REQUIRE(open_result.value == FilesystemResult::OK);

    fs->Close(open_result.handle);

    // Then the file is created successfully
    REQUIRE(temp.FileExists("file with spaces.txt"));
  }

  SECTION("M handle long paths W path length is near limit but under 512 bytes") {
    // Given a long filename within the 512-byte limit
    std::string long_filename(200, 'a');
    long_filename += ".txt";

    StoragePath file_path;
    const bool set_ok = file_path.Set(temp.path) && file_path.Append(long_filename);
    REQUIRE(set_ok);

    // When we create the file
    const auto open_result =
        fs->OpenForWrite(MakePlatformPath(file_path), false, false);

    // Then the file is created successfully
#ifdef _WIN32
    // TODO(RUM-15442): On Windows, paths are effectively limited to MAX_PATH (260
    // chars), since Win32 file APIs will reject any paths exceeding that limit with
    // ERROR_PATH_NOT_FOUND. We can enable long path support by prepending `\\?\` in
    // PlatformPath::Encode(), but using `\\?\` requires absolute paths and disables
    // forward-slash normalization, so we'd want to take care when making those changes.
    // For now, we assert that Windows *can't* handle a path of this length.
    REQUIRE(open_result.value == FilesystemResult::DoesNotExist);
#else
    REQUIRE(open_result.value == FilesystemResult::OK);
    fs->Close(open_result.handle);
    REQUIRE(temp.FileExists(long_filename));
#endif
  }
}
