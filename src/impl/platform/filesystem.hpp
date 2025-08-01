#pragma once

#include <cinttypes>
#include <memory>
#include <string_view>

#include "nonstd/expected.hpp"

namespace datadog::platform {

enum class FilesystemError : uint8_t
{
    /**
     * The target file or directory does not exist.
     */
    DoesNotExist,
    /**
     * The operation failed, but the target file or directory is stil in a usable state.
     */
    Failed,
    /**
     * A low-level file I/O error occurred, and the target file or directory is not in a
     * usable state.
     */
    IOError,
};

/**
 * Result of a filesystem operation. If the operation succeeded, `has_value()` evaluates
 * true, and dereferencing the result yields the return value T. Otherwise, `error()`
 * yields a FilesystemError value indicating the nature of the failure.
 */
template<typename T>
using FilesystemResult = nonstd::expected<T, FilesystemError>;

/**
 * Result of a successful file read operation.
 */
struct FileReadResult
{
    size_t num_bytes_read;
    bool eof;
};

/**
 * Handle to a binary file that's currently open for read.
 */
class IFileReader
{
public:
    /**
     * Closes the file handle held by this reader.
     */
    virtual ~IFileReader() = default;

    /**
     * Seeks forward or backward in the file by the given number of bytes.
     * 
     * If the operation seeks forward past the end of the file, no error will result
     * until the next read, and Seek will return without error.
     * 
     * If the operation would seek backward past the _beginning_ of the file, Seek
     * will return FilesystemError::Failed.
     * 
     * @param offset Number of bytes to move relative to the current position in the
     *  file. May be negative to indicate a backward offset.
     */
    virtual FilesystemResult<void> Seek(int offset) = 0;

    /**
     * Reads up to `n` bytes from the file into `dst`.
     * 
     * @returns On successful read, the number of bytes actually read from the file,
     *  and a flag indicating whether the read operation encountered the end of the
     *  file. If the read operation failed, returns a FilesystemError.
     */
    virtual FilesystemResult<FileReadResult> Read(char* dst, size_t n) = 0;
};

/**
 * Handle to a binary file that's currently open for write.
 */
class IFileWriter
{
public:
    /**
     * Closes the file handle held by this writer.
     */
    virtual ~IFileWriter() = default;

    /**
     * Writes the provided n bytes into the file.
     */
    virtual FilesystemResult<void> Write(const char* src, size_t n) = 0;
};

/**
 * Handle to a directory within the Datadog storage root. Permits access to files and
 * subdirectories that are direct children of this directory. All `name` parameters are
 * given as basename only, e.g. "foo.dat" is valid; "foo/bar.dat", "../foo.dat",
 * "/foo.dat", etc. are invalid and will never be supplied.
 */
class IDirectory
{
public:
    virtual ~IDirectory() = default;

    /**
     * Populates the provided vector with the names of all regular files that exist in
     * this directory.
     * 
     * @param out_names Reference to an empty vector to be populated with filenames.
     */
    virtual FilesystemResult<void> ListFiles(std::vector<std::string>& out_names) = 0;

    /**
     * Opens an existing file for read, in binary mode, a la `fopen(name, "rb")`. If the
     * file does not exist, returns FilesystemError::DoesNotExist.
     */
    virtual FilesystemResult<std::unique_ptr<IFileReader>> OpenForRead(
        std::string_view name
    ) = 0;

    /**
     * Opens a new or existing file for write, in binary mode, a la `fopen(name, "wb")`.
     * If the file does not exist, it will be created. If the file already exists, it
     * will be truncated to 0 bytes and overwritten.
     */
    virtual FilesystemResult<std::unique_ptr<IFileWriter>> OpenForWrite(
        std::string_view name
    ) = 0;

    /**
     * Returns a handle to a new or existing subdirectory with the given name.
     */
    virtual FilesystemResult<std::unique_ptr<IDirectory>> GetOrCreateChild(
        std::string_view name
    ) = 0;
};

/**
 * Handle to the root directory where Datadog is permitted to write files.
 */
class IStorageDirectory : public virtual IDirectory
{
public:
    /**
     * Cleans up any global state maintained by the filesystem implementation.
     */
    virtual ~IStorageDirectory() = default;
};

struct Filesystem
{
    static std::unique_ptr<IStorageDirectory> Init(std::string_view path);
};

}
