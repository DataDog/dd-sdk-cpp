#pragma once

#include "platform/filesystem.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace datadog;

/**
 * Mock data structure used to simulate a file entry. Our code accesses the filesystem
 * from separate threads concurrently, so we need to simulate actual reads and writes to
 * disk.
 *
 * POSIX systems are more permissive that Windows w.r.t. file locking: POSIX allows
 * multiple open handles for the same file by default, whereas Windows defaults to
 * exclusively locking files (only one handle may be open at a time) unless told
 * otherwise via FILE_SHARE_READ / FILE_SHARE_WRITE. Since the default std::filesystem
 * implementation doesn't have access to these platform-specific file open modes, we're
 * subject to the worst-case behavior for now, and so our mocking code simulates the
 * Windows behavior for portability.
 */
struct MockFileEntry
{
    std::string data; // Current file contents, mutated by writers
    int reader_fd{0}; // If nonzero, file is currently open for read with this fd
    int writer_fd{0}; // If nonzero, file is currently open for write with this fd
    bool bad{false};  // If set, any read or write will result in an I/O error
    bool fail{false}; // If set, any read or write will fail

    std::mutex mutex; // Synchronizes reads/writes to the same file across threads

    MockFileEntry(std::string_view in_data)
        : data(in_data)
    {
    }
};

/**
 * Mock implementation of IFileReader.
 *
 * Simulates fopen('rb') on ctor, fseek()/fread() on demand, fclose() on dtor.
 */
class MockFileReader : public platform::IFileReader
{
public:
    std::shared_ptr<MockFileEntry> f;
    int fd;
    size_t read_offset;

    explicit MockFileReader(std::shared_ptr<MockFileEntry> in_f, int in_fd)
        : f(in_f)
        , fd(in_fd)
        , read_offset(0)
    {
    }

    ~MockFileReader()
    {
        std::lock_guard lock(f->mutex);

        // Simulate fclose, iff we had the file open
        if (f->reader_fd == fd)
        {
            f->reader_fd = 0;
        }
    }

    virtual platform::FilesystemResult<void> Seek(int offset) override
    {
        std::lock_guard lock(f->mutex);

        // If we don't have exclusive access to the file, fail
        if (f->reader_fd != fd || f->writer_fd != 0)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // If file is flagged bad, simulate I/O error
        if (f->bad)
        {
            return nonstd::make_unexpected(platform::FilesystemError::IOError);
        }

        // If file is flagged fail, simulate problem with file handle
        if (f->fail)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // Check if seeking backward past start of file
        if (offset < 0 && static_cast<size_t>(-offset) > read_offset)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // Increment our read offset
        read_offset = static_cast<size_t>(static_cast<int>(read_offset) + offset);

        return {};
    }

    virtual platform::FilesystemResult<platform::FileReadResult>
    Read(char* dst, size_t n) override
    {
        std::lock_guard lock(f->mutex);

        // If we don't have exclusive access to the file, fail
        if (f->reader_fd != fd || f->writer_fd != 0)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // If file is flagged bad, simulate I/O error
        if (f->bad)
        {
            return nonstd::make_unexpected(platform::FilesystemError::IOError);
        }

        // If file is flagged fail, simulate problem with file handle
        if (f->fail)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // Otherwise, prepare to read from the file
        const std::string& s = f->data;
        const char* read_ptr = s.data() + read_offset;
        const char* end = s.data() + s.size();

        // If already at EOF before read, read nothing and return
        if (read_ptr >= end)
        {
            return platform::FileReadResult{0, true};
        }

        // Read up to N bytes, bounded by remaining size
        const size_t max_bytes_to_read = end - read_ptr;
        const size_t num_bytes_read = std::min(n, max_bytes_to_read);
        std::memcpy(dst, read_ptr, num_bytes_read);

        // Increment our read offset
        read_offset += num_bytes_read;

        // Check if we've hit EOF and return
        const bool eof = read_offset >= s.size();
        return platform::FileReadResult{num_bytes_read, eof};
    }
};

/**
 * Mock implementation of IFileWriter.
 *
 * Simulates fopen('ab') -> fwrite() -> fclose() on write, without holding a handle.
 */
class MockFileWriter : public platform::IFileWriter
{
public:
    std::shared_ptr<MockFileEntry> f;
    int fd;

    explicit MockFileWriter(std::shared_ptr<MockFileEntry> in_f, int in_fd)
        : f(in_f)
        , fd(in_fd)
    {
        // File writes occur atomically, with close-after-write: keep no handle open
    }

    virtual platform::FilesystemResult<void> Write(const char* src, size_t n) override
    {
        std::lock_guard lock(f->mutex);

        // If anyone else has this file open, fail
        if (f->reader_fd != 0 || f->writer_fd != 0)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // If file is flagged bad, simulate I/O error
        if (f->bad)
        {
            return nonstd::make_unexpected(platform::FilesystemError::IOError);
        }

        // If file is flagged fail, simulate problem with file handle
        if (f->fail)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // Otherwise, open the file for exclusive write, simulating fopen with 'ab';
        // append all data; then simulate close() and immediate flush
        f->writer_fd = fd; // For clarity; mutex makes this irrelevant in mock impl
        f->data += std::string_view{src, n};
        f->writer_fd = 0;
        return {};
    }
};

struct MockDirEntry
{
    bool bad{false};  // If set, any directory operation will result in an I/O error
    bool fail{false}; // If set, any directory operation will fail

    std::mutex mutex;
};

struct MockFilesystem
{
    int next_fd{1};

    // Keep underlying filesystem state in memory
    std::unordered_map<std::filesystem::path, std::shared_ptr<MockFileEntry>> files;
    std::unordered_map<std::filesystem::path, std::shared_ptr<MockDirEntry>> dirs;

    // Synchronize access to these maps (separate from file-level synchronization)
    std::mutex mutex;

    /**
     * Creates directory entries for all path components in relpath. For example,
     * "foo/bar/baz" creates entries for "foo", "foo/bar", and "foo/bar/baz".
     */
    void Mkdirs(std::filesystem::path relpath)
    {
        if (relpath.empty())
        {
            return;
        }

        std::scoped_lock lock(mutex);

        std::filesystem::path current_path;
        for (const auto& component : relpath)
        {
            current_path /= component;
            if (dirs.find(current_path) == dirs.end())
            {
                dirs[current_path] = std::make_shared<MockDirEntry>();
            }
        }
    }

    /**
     * Ensures that a file exists at the given path, creating it if it doesn't exist.
     */
    void Touch(std::filesystem::path relpath, std::string_view data = "")
    {
        std::scoped_lock lock(mutex);

        if (files.find(relpath) == files.end())
        {
            files[relpath] = std::make_shared<MockFileEntry>(data);
        }
    }

    /**
     * Handles IDirectory::ListFiles given the relevant directory path.
     */
    platform::FilesystemResult<void>
    HandleListFiles(std::filesystem::path relpath, std::vector<std::string>& out_names)
    {
        std::scoped_lock lock(mutex);

        // Simulate error if directory is flagged bad or fail
        auto dir = dirs.find(relpath);
        if (dir != dirs.end())
        {
            std::scoped_lock dir_lock(dir->second->mutex);

            if (dir->second->bad)
            {
                return nonstd::make_unexpected(platform::FilesystemError::IOError);
            }

            if (dir->second->fail)
            {
                return nonstd::make_unexpected(platform::FilesystemError::Failed);
            }
        }

        // Populate out_names with the name of every file entry whose parent is the
        // given directory
        for (const auto& [file_path, file] : files)
        {
            if (file_path.parent_path() == relpath)
            {
                out_names.push_back(file_path.filename().string());
            }
        }
        return {};
    }

    /**
     * Handles IDirectory::DeleteFile given the relevant directory path.
     */
    platform::FilesystemResult<void> HandleDeleteFile(std::filesystem::path relpath)
    {
        // Acquire filesystem mutex
        std::scoped_lock lock(mutex);

        // Check for an existing file entry, propagating IOError from bad directory
        auto file_result = GetFileEntry(relpath);
        if (!file_result.has_value())
        {
            return nonstd::make_unexpected(file_result.error());
        }

        // If FileEntry is null, file does not exist
        auto file = *file_result;
        if (!file)
        {
            return nonstd::make_unexpected(platform::FilesystemError::DoesNotExist);
        }

        // Acquire file mutex
        std::unique_lock file_lock(file->mutex);

        // If file is flagged bad, fail with I/O error
        if (file->bad)
        {
            return nonstd::make_unexpected(platform::FilesystemError::IOError);
        }

        // If file is flagged fail, fail
        if (file->fail)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // If any handles to the file are open, fail
        if (file->reader_fd != 0 || file->writer_fd != 0)
        {
            return nonstd::make_unexpected(platform::FilesystemError::Failed);
        }

        // Remove the file entry and return success
        file_lock.release();
        files.erase(relpath);
        return {};
    }

    /**
     * Handles IDirectory::OpenForRead given the relevant directory path.
     */
    platform::FilesystemResult<std::unique_ptr<platform::IFileReader>>
    HandleOpenForRead(std::filesystem::path relpath)
    {
        // Acquire filesystem mutex
        std::scoped_lock lock(mutex);

        // Check for an existing file entry, propagating IOError from bad directory
        auto file_result = GetFileEntry(relpath);
        if (!file_result.has_value())
        {
            return nonstd::make_unexpected(file_result.error());
        }

        // If FileEntry is null, file does not exist
        auto file = *file_result;
        if (!file)
        {
            return nonstd::make_unexpected(platform::FilesystemError::DoesNotExist);
        }

        // Acquire mutex and check file state before initializing a MockFileReader
        int fd = -1;
        {
            std::lock_guard file_lock(file->mutex);

            // Ensure exclusivity (Windows-style locking)
            if (file->reader_fd != 0 || file->writer_fd != 0)
            {
                return nonstd::make_unexpected(platform::FilesystemError::Failed);
            }

            // If bad flag is set for file, return IOError
            if (file->bad)
            {
                return nonstd::make_unexpected(platform::FilesystemError::IOError);
            }

            // If fail flag is set for file, return Failed
            if (file->fail)
            {
                return nonstd::make_unexpected(platform::FilesystemError::Failed);
            }

            // Assign the file handle to the FileEntry while we have it locked;
            // MockFileReader will take ownership when constructed below
            fd = next_fd++;
            file->reader_fd = fd;
        }

        // Create a reader and give it a shared_ptr to the FileEntry
        return std::make_unique<MockFileReader>(file, fd);
    }

    /**
     * Handles IDirectory::PrepareForWrite given the relevant directory path.
     */
    platform::FilesystemResult<std::unique_ptr<platform::IFileWriter>>
    HandlePrepareForWrite(std::filesystem::path relpath)
    {
        // Acquire filesystem mutex
        std::scoped_lock lock(mutex);

        // Check for an existing file entry, propagating IOError from bad directory
        auto file_result = GetFileEntry(relpath);
        if (!file_result.has_value())
        {
            return nonstd::make_unexpected(file_result.error());
        }

        // If FileEntry is null, file does not yet exist: create a new FileEntry with no
        // data and no open handles
        std::shared_ptr<MockFileEntry> file = *file_result;
        if (!file)
        {
            file = std::make_shared<MockFileEntry>("");
            files[relpath] = file;
        }

        // Create a writer and give it a shared_ptr to the FileEntry; it'll acquire the
        // file mutex as needed
        return std::make_unique<MockFileWriter>(file, next_fd++);
    }

private:
    /**
     * Retrieves the MockFileEntry at the given path, or nullptr if no such file is
     * known. Returns unexpected IOError if the parent directory entry has its bad flag
     * set; otherwise returns no error.
     *
     * @note MockFilesystem::mutex MUST already be held by the caller.
     */
    platform::FilesystemResult<std::shared_ptr<MockFileEntry>> GetFileEntry(
        std::filesystem::path relpath
    )
    {
        // Simulate error if directory is flagged bad or fail
        auto dir = dirs.find(relpath.parent_path());
        if (dir != dirs.end())
        {
            std::scoped_lock dir_lock(dir->second->mutex);

            if (dir->second->bad)
            {
                return nonstd::make_unexpected(platform::FilesystemError::IOError);
            }

            if (dir->second->fail)
            {
                return nonstd::make_unexpected(platform::FilesystemError::Failed);
            }
        }

        // Check for existing file
        auto file = files.find(relpath);
        if (file != files.end())
        {
            return file->second;
        }

        // Don't signal DoesNotExist; return nullptr for internal usage
        return nullptr;
    }
};

/**
 * Mock implementation of IDirectory. Uses a reference to the MockFilesystem for
 * visibility into the subset of the directory tree that it manages.
 */
class MockDirectory : public platform::IDirectory
{
public:
    MockFilesystem& fs;
    std::filesystem::path relpath;

    MockDirectory(MockFilesystem& in_fs, std::filesystem::path in_relpath)
        : fs(in_fs)
        , relpath(in_relpath)
    {
    }

    virtual platform::FilesystemResult<void> ListFiles(
        std::vector<std::string>& out_names
    ) override
    {
        return fs.HandleListFiles(relpath, out_names);
    }

    virtual platform::FilesystemResult<void> DeleteFile(std::string_view name) override
    {
        return fs.HandleDeleteFile(relpath / name);
    }

    virtual platform::FilesystemResult<std::unique_ptr<platform::IFileReader>>
    OpenForRead(std::string_view name) override
    {
        return fs.HandleOpenForRead(relpath / name);
    }

    virtual platform::FilesystemResult<std::unique_ptr<platform::IFileWriter>>
    PrepareForWrite(std::string_view name) override
    {
        return fs.HandlePrepareForWrite(relpath / name);
    }

    virtual platform::FilesystemResult<std::unique_ptr<platform::IDirectory>>
    PrepareSubdirectory(std::string_view name) override
    {
        return std::make_unique<MockDirectory>(fs, relpath / name);
    }
};

/**
 * Mock implementation of IStorageDirectory. Wraps a MockFilesystem and simulates the
 * interface to the root event storage directory.
 */
class MockStorageDirectory : public platform::IStorageDirectory
{
public:
    MockFilesystem fs;

    MockStorageDirectory()
    {
    }

    // IDirectory interface implementation - delegate to MockFilesystem
    virtual platform::FilesystemResult<void> ListFiles(
        std::vector<std::string>& out_names
    ) override
    {
        return fs.HandleListFiles("", out_names);
    }

    virtual platform::FilesystemResult<void> DeleteFile(std::string_view name) override
    {
        return fs.HandleDeleteFile(name);
    }

    virtual platform::FilesystemResult<std::unique_ptr<platform::IFileReader>>
    OpenForRead(std::string_view name) override
    {
        return fs.HandleOpenForRead(name);
    }

    virtual platform::FilesystemResult<std::unique_ptr<platform::IFileWriter>>
    PrepareForWrite(std::string_view name) override
    {
        return fs.HandlePrepareForWrite(name);
    }

    virtual platform::FilesystemResult<std::unique_ptr<platform::IDirectory>>
    PrepareSubdirectory(std::string_view name) override
    {
        return std::make_unique<MockDirectory>(fs, name);
    }

    /**
     * Called during test setup to initialize the mock filesystem with files.
     */
    void WithExistingFile(std::string_view relpath, std::string_view data)
    {
        const std::filesystem::path path{relpath};
        fs.Mkdirs(path.parent_path());
        fs.Touch(path, data);
    }

    /**
     * Sets the bad flag for any file and/or directory at the given path, ensuring that
     * all subsequent operations on that file and/or directory will result in an I/O
     * error.
     */
    void Corrupt(std::string_view relpath)
    {
        auto file = fs.files.find(relpath);
        if (file != fs.files.end())
        {
            std::scoped_lock lock(file->second->mutex);
            file->second->bad = true;
        }
        auto dir = fs.dirs.find(relpath);
        if (dir != fs.dirs.end())
        {
            std::scoped_lock lock(dir->second->mutex);
            dir->second->bad = true;
        }
    }

    /**
     * Sets or clears the 'fail' flag for any file and/or directory at the given path.
     * If the flag is set, all subsequent operations targeting that file and/or
     * directory will fail until the flag is cleared.
     */
    void SetFail(std::string_view relpath, bool fail)
    {
        auto file = fs.files.find(relpath);
        if (file != fs.files.end())
        {
            std::scoped_lock lock(file->second->mutex);
            file->second->fail = fail;
        }
        auto dir = fs.dirs.find(relpath);
        if (dir != fs.dirs.end())
        {
            std::scoped_lock lock(dir->second->mutex);
            dir->second->fail = fail;
        }
    }

    /**
     * Given the path to a directory, collects the full set of files that have been
     * written to that directory and returns a vector containing their full paths. For
     * examination of filesystem state after a test; does not exhibit simulated
     * filesystem behavior or acquire locks.
     */
    std::vector<std::string> FindFiles(std::filesystem::path relpath)
    {
        std::vector<std::string> result;
        for (const auto& file : fs.files)
        {
            if (file.first.parent_path() == relpath)
            {
                result.push_back(file.first);
            }
        }
        return result;
    }

    /**
     * Retrieves the current contents of the file at the given path, if any. This
     * function is intended for the validation of filesystem state at the end of a test;
     * it does not acquire file-level mutexes, read or modify file descriptors, or
     * respect bad/fail flags.
     */
    std::optional<std::string> Cat(std::string_view relpath)
    {
        auto file = fs.files.find(relpath);
        if (file != fs.files.end())
        {
            return file->second->data;
        }
        return std::nullopt;
    }
};
