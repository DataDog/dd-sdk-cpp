#include "platform/filesystem.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace datadog::platform {

static bool _is_clean_basename(std::string_view name)
{
    return (
        !name.empty()
        && name != "."
        && name != ".."
        && name.find('/') == std::string_view::npos
        && name.find('\\') == std::string_view::npos
    );
}

enum class _check_path_result
{
    does_not_exist,
    exists_as_file,
    exists_as_directory,
    check_failed,
};

static _check_path_result _check_path(const std::filesystem::path& path)
{
    // We could do all of this in a single std::filesystem::status call, but platforms
    // have subtle variation in behavior for stat'ing a nonexistent path: some return a
    // valid status where type() == std::filesystem::file_type::not_found, some return a
    // standard error (std::errc::no_such_file_or_directory), some return nonstandard,
    // platform-specific errors (e.g. 'generic:2'). Supporting each platform's quirks
    // with conditional compilation may not be worth saving a system call given that we
    // access files infrequently, using background threads.

    // Make a call to std::filesystem::exists to see if it points to anything
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec)
    {
        return _check_path_result::check_failed;
    }
    if (!exists)
    {
        return _check_path_result::does_not_exist;
    }

    // Make a call to std::filesystem::is_directory to test if it's a directory or
    // something else 
    const bool is_directory = std::filesystem::is_directory(path, ec);
    if (ec)
    {
        return _check_path_result::check_failed;
    }
    if (is_directory)
    {
        return _check_path_result::exists_as_directory;
    }
    return _check_path_result::exists_as_file;
};

/**
 * Wraps a std::ifstream in the IFileReader interface.
 * 
 * Read operations that set the bad bit (indicating a catastrophic I/O problem) will
 * result in `FilesystemError::IOError`, all other failures will result in
 * `FilesystemError::Failed`.
 */
class StdFileReader : public IFileReader
{
private:
    std::ifstream _infile;

public:
    explicit StdFileReader(std::ifstream&& infile)
        :_infile(std::move(infile))
    {
    }

    ~StdFileReader()
    {
        // std::ifstream destructor closes _infile
    }

    FilesystemResult<void> Seek(int offset) override
    {
        // Seek from the current position and check error bits
        _infile.seekg(offset, std::ios_base::cur);
        if (_infile.bad())
        {
            return nonstd::make_unexpected(FilesystemError::IOError);
        }
        if (_infile.fail())
        {
            return nonstd::make_unexpected(FilesystemError::Failed);
        }

        // No error; return default expected<void>
        return {};
    }

    FilesystemResult<FileReadResult> Read(char* dst, size_t n) override
    {
        // Read up to n bytes from the file into dst
        _infile.read(dst, n);

        // Check error bits and fail if we couldn't read from the file
        if (_infile.bad())
        {
            return nonstd::make_unexpected(FilesystemError::IOError);
        }
        if (_infile.fail())
        {
            return nonstd::make_unexpected(FilesystemError::Failed);
        }

        // Read succeeded; return EOF bit and number of bytes actually read
        const bool eof = _infile.eof();
        const size_t num_bytes_read = _infile.gcount();
        return FileReadResult{num_bytes_read, eof};
    }
};

/**
 * Wraps a std::ofstream in the IFileWriter interface.
 * 
 * Write operations that set the bad bit (indicating a catastrophic I/O problem) will
 * result in `FilesystemError::IOError`, all other failures will result in
 * `FilesystemError::Failed`.
 */
class StdFileWriter : public IFileWriter
{
private:
    const std::filesystem::path _path;

public:
    explicit StdFileWriter(std::filesystem::path&& path)
        : _path(std::move(path))
    {
    }

     ~StdFileWriter()
     {
        // We hold no resources
     }

    FilesystemResult<void> Write(const char* src, size_t n) override
    {
        // Attempt to open the file for append
        const std::ios::openmode mode = std::ios::binary | std::ios::app;
        std::ofstream outfile(_path, mode);

        // If we couldn't open the file, it may be because some parent directory doesn't
        // yet exist or has been externally deleted
        if (!outfile)
        {
            // Attempt to create the required directories, then retry opening the file
            std::error_code ec;
            if (!std::filesystem::create_directories(_path, ec) || ec)
            {
                return nonstd::make_unexpected(FilesystemError::Failed);
            }
            outfile.open(_path, mode);
        }

        // If the file still isn't open, there's some other problem: abort
        if (!outfile)
        {
            return nonstd::make_unexpected(FilesystemError::Failed);
        }

        // File is open: attempt to write all n bytes to the file, then check error bits
        outfile.write(src, n);
        if (outfile.bad())
        {
            return nonstd::make_unexpected(FilesystemError::IOError);
        }
        if (outfile.fail())
        {
            return nonstd::make_unexpected(FilesystemError::Failed);
        }

        // No error; return default expected<void>
        return {};
    }
};

/**
 * Implements IDirectory using std::filesystem, providing access to files and
 * subdirectories that are direct children of the directory indicated by `_path`.
 *
 * The path with which an StdDirectory is initialized need not exist initially: write
 * operations will create parent directories automatically if needed, allowing lazy
 * directory creation and handling external deletion of storage directories gracefully.
 */
class StdDirectory : public virtual IDirectory
{
private:
    const std::filesystem::path _path;

public:
    explicit StdDirectory(const std::filesystem::path& path)
        : _path(path)
    {
    }

    ~StdDirectory() override
    {
        // We hold no resources
    }

    FilesystemResult<void> ListFiles(std::vector<std::string>& out_names) override
    {
        // Result vector should be cleared by the caller
        assert(out_names.empty() && "ListFiles called with non-empty result vector");

        // Initialize a directory iterator, and check to see if it was successful
        std::error_code ec;
        std::filesystem::directory_iterator it(_path, ec);
        if (ec)
        {
            // If our directory doesn't exist, that's fine: our result set is empty
            if (ec == std::errc::no_such_file_or_directory)
            {
                // No error; return default expected<void>
                return {};
            }

            // We failed to read from the directory for some other reason
            return nonstd::make_unexpected(FilesystemError::Failed);
        }

        // Our iterator is good; read the basenames of all regular files into the output
        // vector and return success
        for (const std::filesystem::directory_entry& entry : it)
        {
            // Only consider regular files
            if (!entry.is_regular_file())
            {
                continue;
            }

            // Add basename only
            out_names.emplace_back(entry.path().filename());
        }

        // No error; return default expected<void>
        return {};
    }

    FilesystemResult<std::unique_ptr<IFileReader>> OpenForRead(
        std::string_view name
    ) override
    {
        // Build the path to the target file
        assert(_is_clean_basename(name));
        const std::filesystem::path file_path = _path / name;

        // Open the file for read in binary mode, and wrap it in a StdFileReader
        std::ifstream infile(file_path, std::ios::binary);
        if (infile.is_open())
        {
            return std::make_unique<StdFileReader>(std::move(infile));
        }

        // We failed to open the file: determine whether that's because the file doesn't
        // exist or for some other reason
        switch (_check_path(file_path))
        {
            case _check_path_result::does_not_exist:
                return nonstd::make_unexpected(FilesystemError::DoesNotExist);

            case _check_path_result::check_failed:
            case _check_path_result::exists_as_directory:
            case _check_path_result::exists_as_file:
                return nonstd::make_unexpected(FilesystemError::Failed);
        }
    }

    FilesystemResult<std::unique_ptr<IFileWriter>> PrepareForWrite(
        std::string_view name
    ) override
    {
        // Initialize a wrapper for a file at the target path
        assert(_is_clean_basename(name));
        return std::make_unique<StdFileWriter>(_path / name);
    }

    FilesystemResult<std::unique_ptr<IDirectory>> PrepareSubdirectory(
        std::string_view name
    ) override
    {
        // Build the path to the target directory
        assert(_is_clean_basename(name));
        const std::filesystem::path subdir_path = _path / name;

        // Check to see if the target path is occupied
        switch (_check_path(subdir_path))
        {
            // If we can't stat the path, or if the target directory path is occupied by
            // an existing file, the operation failed
            case _check_path_result::check_failed:
            case _check_path_result::exists_as_file:
                return nonstd::make_unexpected(FilesystemError::Failed);

            // Whether the directory exists or not, we're fine to proceed: we'll create
            // the director(ies) lazily on the first file write if needed
            case _check_path_result::exists_as_directory:
            case _check_path_result::does_not_exist:
                break;
        }

        // Create a StdDirectory to wrap the requested subdirectory
        return std::make_unique<StdDirectory>(subdir_path);
    }
};

class StdStorageDirectory : public StdDirectory, public IStorageDirectory
{
public:
    explicit StdStorageDirectory(const std::filesystem::path& path)
        : StdDirectory(path)
    {
    }

    ~StdStorageDirectory()
    {
        // We hold no global state
    }
};

std::unique_ptr<IStorageDirectory> Filesystem::Init(std::string_view path)
{
    // Resolve the configured path for our root storage directory
    const std::filesystem::path root_path{path};

    // Check to see if the target directory exists
    switch (_check_path(root_path))
    {
        // If we failed to check the path, or if the target directory path is occupied
        // by an existing file, we can't initialize the filesystem
        case _check_path_result::check_failed:
        case _check_path_result::exists_as_file:
            return nullptr;

        // If the target directory doesn't yet exist, create it
        case _check_path_result::does_not_exist:
            {
                // Create the target directory, requiring that the parent already exist
                std::error_code ec;
                std::filesystem::create_directory(root_path, ec);
                if (ec)
                {
                    // Failed to create directory; we can't initialize the filesystem
                    return nullptr;
                }
            }
            break;

        // If it already exists, we're good to go
        case _check_path_result::exists_as_directory:
            // 
            break;
    }

    // root_path points to a valid directory; create our filesystem interface
    return std::make_unique<StdStorageDirectory>(root_path);
}

}
