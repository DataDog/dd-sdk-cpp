#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "mock/tempdir.hpp"
#include "platform/filesystem.hpp"

using namespace datadog;

// Tag [platform-filesystem] describes tests used to validate that a platform-specific
// or user-provided implementation of platform/filesystem.hpp behaves as expected

TEST_CASE("Filesystem", "[unit][platform-filesystem]")
{
    TempDirectory tempdir;

    SECTION("M create a valid IStorageDirectory W static Init is called")
    {
        auto storage = platform::Filesystem::Init(tempdir.path);
        REQUIRE(storage != nullptr);
    }
}

TEST_CASE("IStorageDirectory", "[unit][platform-filesystem]")
{
    // Given a storage directory
    TempDirectory tempdir;
    auto storage = platform::Filesystem::Init(tempdir.path);
    REQUIRE(storage != nullptr);

    SECTION("M list empty directory W ListFiles is called on empty directory")
    {
        // When listing files in empty directory
        std::vector<std::string> files;
        auto result = storage->ListFiles(files);

        // Then operation succeeds and list is empty
        REQUIRE(result.has_value());
        REQUIRE(files.empty());
    }

    SECTION("M list files W ListFiles is called on directory with files")
    {
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

    SECTION("M successfully delete file W DeleteFile is called on existing file")
    {
        // Given a file exists
        tempdir.WriteFile("to_delete.txt", "delete me");
        REQUIRE(tempdir.FileExists("to_delete.txt"));

        // When deleting the file
        auto result = storage->DeleteFile("to_delete.txt");

        // Then operation succeeds and file is gone
        REQUIRE(result.has_value());
        REQUIRE_FALSE(tempdir.FileExists("to_delete.txt"));
    }

    SECTION("M return success W DeleteFile is called on non-existent file")
    {
        // When deleting non-existent file
        auto result = storage->DeleteFile("nonexistent.txt");

        // Then operation succeeds (std::filesystem::remove returns success for
        // non-existent files)
        REQUIRE(result.has_value());
    }

    SECTION("M create subdirectory W PrepareSubdirectory is called")
    {
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
    )
    {
        // Given subdirectory is prepared and has a file written to it (to actually
        // create it)
        auto result1 = storage->PrepareSubdirectory("existing");
        REQUIRE(result1.has_value());
        auto writer = result1.value()->PrepareForWrite("dummy.txt");
        REQUIRE(writer.has_value());
        writer.value()->Write("content", 7);
        REQUIRE(tempdir.DirectoryExists("existing"));

        // When preparing the same subdirectory again
        auto result2 = storage->PrepareSubdirectory("existing");

        // Then operation succeeds and returns valid directory handle
        REQUIRE(result2.has_value());
        REQUIRE(result2.value() != nullptr);
    }
}

TEST_CASE("IFileWriter", "[unit][platform-filesystem]")
{
    // Given a storage directory
    TempDirectory tempdir;
    auto storage = platform::Filesystem::Init(tempdir.path);
    REQUIRE(storage != nullptr);

    SECTION("M create file writer W PrepareForWrite is called")
    {
        // When preparing a file for write
        auto result = storage->PrepareForWrite("test.dat");

        // Then operation succeeds and returns valid writer
        REQUIRE(result.has_value());
        REQUIRE(result.value() != nullptr);
    }

    SECTION("M write data to file W Write is called")
    {
        // Given a file writer
        auto writer_result = storage->PrepareForWrite("write_test.dat");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());

        // When writing data
        const char* test_data = "Hello, World!";
        size_t data_len = std::strlen(test_data);
        auto result = writer->Write(test_data, data_len);

        // Then operation succeeds and file contains expected data
        REQUIRE(result.has_value());
        REQUIRE(tempdir.FileExists("write_test.dat"));
        REQUIRE(tempdir.ReadFileContents("write_test.dat") == test_data);
        REQUIRE(tempdir.GetFileSize("write_test.dat") == data_len);
    }

    SECTION("M append data to existing file W Write is called multiple times")
    {
        // Given a file writer
        auto writer_result = storage->PrepareForWrite("append_test.dat");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());

        // When writing data multiple times
        const char* data1 = "First";
        const char* data2 = "Second";
        auto result1 = writer->Write(data1, std::strlen(data1));
        auto result2 = writer->Write(data2, std::strlen(data2));

        // Then both operations succeed and file contains concatenated data
        REQUIRE(result1.has_value());
        REQUIRE(result2.has_value());
        REQUIRE(tempdir.ReadFileContents("append_test.dat") == "FirstSecond");
    }

    SECTION("M write binary data W Write is called with binary content")
    {
        // Given a file writer
        auto writer_result = storage->PrepareForWrite("binary_test.dat");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());

        // When writing binary data (including null bytes)
        const char binary_data[] = {
            0x00, 0x01, 0x02, static_cast<char>(0xFF), static_cast<char>(0xFE),
            0x00, 0x03
        };
        size_t data_len = sizeof(binary_data);
        auto result = writer->Write(binary_data, data_len);

        // Then operation succeeds and file contains expected binary data
        REQUIRE(result.has_value());
        REQUIRE(tempdir.GetFileSize("binary_test.dat") == data_len);

        std::string file_contents = tempdir.ReadFileContents("binary_test.dat");
        REQUIRE(file_contents.size() == data_len);
        REQUIRE(std::memcmp(file_contents.data(), binary_data, data_len) == 0);
    }
}

TEST_CASE("IFileReader", "[unit][platform-filesystem]")
{
    // Given a storage directory with a test file
    TempDirectory tempdir;
    auto storage = platform::Filesystem::Init(tempdir.path);
    REQUIRE(storage != nullptr);

    SECTION("M return DoesNotExist error W OpenForRead is called on non-existent file")
    {
        // When opening non-existent file for read
        auto result = storage->OpenForRead("nonexistent.txt");

        // Then operation fails with DoesNotExist error
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == platform::FilesystemError::DoesNotExist);
    }

    SECTION("M open file for read W OpenForRead is called on existing file")
    {
        // Given a file exists
        const char* test_content = "Test file content";
        tempdir.WriteFile("readable.txt", test_content);

        // When opening file for read
        auto result = storage->OpenForRead("readable.txt");

        // Then operation succeeds and returns valid reader
        REQUIRE(result.has_value());
        REQUIRE(result.value() != nullptr);
    }

    SECTION("M read full file content W Read is called with sufficient buffer")
    {
        // Given a file with known content created via filesystem writer
        const char* test_content = "Hello, File Reading!";
        size_t content_len = std::strlen(test_content);
        auto writer_result = storage->PrepareForWrite("read_test.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());
        auto write_result = writer->Write(test_content, content_len);
        REQUIRE(write_result.has_value());

        // When opening and reading the file
        auto reader_result = storage->OpenForRead("read_test.txt");
        REQUIRE(reader_result.has_value());
        auto reader = std::move(reader_result.value());

        char buffer[100];
        auto read_result = reader->Read(buffer, sizeof(buffer));

        // Then operation succeeds and reads expected content
        REQUIRE(read_result.has_value());
        REQUIRE(read_result.value().num_bytes_read == content_len);
        REQUIRE(read_result.value().eof == true);
        REQUIRE(std::memcmp(buffer, test_content, content_len) == 0);
    }

    SECTION("M read partial content W Read is called with small buffer")
    {
        // Given a file with content larger than buffer created via filesystem writer
        const char* test_content =
            "This is a longer test content that will require multiple reads";
        auto writer_result = storage->PrepareForWrite("partial_read_test.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());
        auto write_result = writer->Write(test_content, std::strlen(test_content));
        REQUIRE(write_result.has_value());

        // When opening and reading with small buffer
        auto reader_result = storage->OpenForRead("partial_read_test.txt");
        REQUIRE(reader_result.has_value());
        auto reader = std::move(reader_result.value());

        char buffer[10];
        auto read_result = reader->Read(buffer, sizeof(buffer));

        // Then operation succeeds and reads partial content
        REQUIRE(read_result.has_value());
        REQUIRE(read_result.value().num_bytes_read == sizeof(buffer));
        REQUIRE(read_result.value().eof == false);
        REQUIRE(std::memcmp(buffer, test_content, sizeof(buffer)) == 0);
    }

    SECTION("M handle EOF correctly W Read is called on empty file")
    {
        // Given an empty file created via filesystem writer
        auto writer_result = storage->PrepareForWrite("empty.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());
        auto write_result = writer->Write("", 0);
        REQUIRE(write_result.has_value());

        // When opening and reading the empty file
        auto reader_result = storage->OpenForRead("empty.txt");
        REQUIRE(reader_result.has_value());
        auto reader = std::move(reader_result.value());

        char buffer[10];
        auto read_result = reader->Read(buffer, sizeof(buffer));

        // Then operation succeeds with zero bytes read and EOF
        REQUIRE(read_result.has_value());
        REQUIRE(read_result.value().num_bytes_read == 0);
        REQUIRE(read_result.value().eof == true);
    }

    SECTION("M seek forward in file W Seek is called with positive offset")
    {
        // Given a file with known content created via filesystem writer
        const char* test_content = "0123456789abcdef";
        auto writer_result = storage->PrepareForWrite("seek_test.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());
        auto write_result = writer->Write(test_content, std::strlen(test_content));
        REQUIRE(write_result.has_value());

        // When opening file and seeking forward
        auto reader_result = storage->OpenForRead("seek_test.txt");
        REQUIRE(reader_result.has_value());
        auto reader = std::move(reader_result.value());

        auto seek_result = reader->Seek(5);
        REQUIRE(seek_result.has_value());

        // Then subsequent read starts from seeked position
        char buffer[5];
        auto read_result = reader->Read(buffer, sizeof(buffer));
        REQUIRE(read_result.has_value());
        REQUIRE(read_result.value().num_bytes_read == sizeof(buffer));
        REQUIRE(std::memcmp(buffer, "56789", sizeof(buffer)) == 0);
    }

    SECTION(
        "M seek backward in file W Seek is called with negative offset after forward "
        "seek"
    )
    {
        // Given a file with known content created via filesystem writer
        const char* test_content = "0123456789abcdef";
        auto writer_result = storage->PrepareForWrite("seek_back_test.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());
        auto write_result = writer->Write(test_content, std::strlen(test_content));
        REQUIRE(write_result.has_value());

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
        auto read_result = reader->Read(buffer, sizeof(buffer));
        REQUIRE(read_result.has_value());
        REQUIRE(read_result.value().num_bytes_read == sizeof(buffer));
        REQUIRE(std::memcmp(buffer, "567", sizeof(buffer)) == 0);
    }

    SECTION("M return Failed error W Seek is called with offset before beginning")
    {
        // Given a file created via filesystem writer
        auto writer_result = storage->PrepareForWrite("seek_error_test.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());
        auto write_result = writer->Write("content", 7);
        REQUIRE(write_result.has_value());

        auto reader_result = storage->OpenForRead("seek_error_test.txt");
        REQUIRE(reader_result.has_value());
        auto reader = std::move(reader_result.value());

        // When seeking before beginning of file
        auto seek_result = reader->Seek(-10);

        // Then operation fails with Failed error
        REQUIRE_FALSE(seek_result.has_value());
        REQUIRE(seek_result.error() == platform::FilesystemError::Failed);
    }

    SECTION("M handle seek past EOF correctly W Seek is called beyond file end")
    {
        // Given a short file created via filesystem writer
        const char* test_content = "short";
        auto writer_result = storage->PrepareForWrite("seek_eof_test.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());
        auto write_result = writer->Write(test_content, std::strlen(test_content));
        REQUIRE(write_result.has_value());

        auto reader_result = storage->OpenForRead("seek_eof_test.txt");
        REQUIRE(reader_result.has_value());
        auto reader = std::move(reader_result.value());

        // When seeking past end of file
        auto seek_result = reader->Seek(100);
        REQUIRE(seek_result.has_value()); // Should not error

        // Then subsequent read returns EOF
        char buffer[10];
        auto read_result = reader->Read(buffer, sizeof(buffer));
        REQUIRE(read_result.has_value());
        REQUIRE(read_result.value().num_bytes_read == 0);
        REQUIRE(read_result.value().eof == true);
    }

    SECTION("M read binary data correctly W Read is called on file with binary content")
    {
        // Given a file with binary data
        const char binary_data[] = {
            0x00, 0x01, 0x02, static_cast<char>(0xFF), static_cast<char>(0xFE),
            0x00, 0x03
        };
        size_t data_len = sizeof(binary_data);

        // Create file using filesystem writer to ensure proper binary handling
        auto writer_result = storage->PrepareForWrite("binary_read_test.dat");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());
        auto write_result = writer->Write(binary_data, data_len);
        REQUIRE(write_result.has_value());

        // When opening and reading binary file
        auto reader_result = storage->OpenForRead("binary_read_test.dat");
        REQUIRE(reader_result.has_value());
        auto reader = std::move(reader_result.value());

        char buffer[10];
        auto read_result = reader->Read(buffer, sizeof(buffer));

        // Then operation succeeds and reads correct binary data
        REQUIRE(read_result.has_value());
        REQUIRE(read_result.value().num_bytes_read == data_len);
        REQUIRE(read_result.value().eof == true);
        REQUIRE(std::memcmp(buffer, binary_data, data_len) == 0);
    }
}

TEST_CASE("Subdirectory Operations", "[unit][platform-filesystem]")
{
    // Given a storage directory with a subdirectory
    TempDirectory tempdir;
    auto storage = platform::Filesystem::Init(tempdir.path);
    REQUIRE(storage != nullptr);

    auto subdir_result = storage->PrepareSubdirectory("subdir");
    REQUIRE(subdir_result.has_value());
    auto subdir = std::move(subdir_result.value());

    SECTION("M operate on files in subdirectory W using IDirectory operations")
    {
        // When writing to file in subdirectory (this will create the directory)
        auto writer_result = subdir->PrepareForWrite("sub_file.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());

        const char* test_data = "Subdirectory content";
        auto write_result = writer->Write(test_data, std::strlen(test_data));
        REQUIRE(write_result.has_value());

        // Then file exists in subdirectory and can be read
        REQUIRE(tempdir.FileExists("subdir/sub_file.txt"));

        auto reader_result = subdir->OpenForRead("sub_file.txt");
        REQUIRE(reader_result.has_value());
        auto reader = std::move(reader_result.value());

        char buffer[50];
        auto read_result = reader->Read(buffer, sizeof(buffer));
        REQUIRE(read_result.has_value());
        REQUIRE(std::memcmp(buffer, test_data, std::strlen(test_data)) == 0);
    }

    SECTION("M list files in subdirectory W ListFiles is called on subdirectory")
    {
        // Given files in subdirectory
        auto writer1 = subdir->PrepareForWrite("file1.dat");
        REQUIRE(writer1.has_value());
        writer1.value()->Write("content1", 8);

        auto writer2 = subdir->PrepareForWrite("file2.dat");
        REQUIRE(writer2.has_value());
        writer2.value()->Write("content2", 8);

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
    )
    {
        // When creating nested subdirectory and writing to it
        auto nested_result = subdir->PrepareSubdirectory("nested");
        REQUIRE(nested_result.has_value());
        auto nested = std::move(nested_result.value());

        auto writer_result = nested->PrepareForWrite("nested_file.txt");
        REQUIRE(writer_result.has_value());
        auto writer = std::move(writer_result.value());

        const char* nested_content = "Nested content";
        auto write_result = writer->Write(nested_content, std::strlen(nested_content));
        REQUIRE(write_result.has_value());

        // Then nested directory exists after file write and file is created
        REQUIRE(tempdir.DirectoryExists("subdir/nested"));
        REQUIRE(tempdir.FileExists("subdir/nested/nested_file.txt"));
    }
}
