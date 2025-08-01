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
    std::ofstream _outfile;

public:
    StdFileWriter(std::ofstream&& outfile)
        : _outfile(std::move(outfile))
    {
    }

     ~StdFileWriter()
     {
        // std::ofstream destructor closes _outfile
     }

    FilesystemResult<void> Write(const char* src, size_t n) override
    {
        // Attempt to write all n bytes to the file, then check error bits
        _outfile.write(src, n);
        if (_outfile.bad())
        {
            return nonstd::make_unexpected(FilesystemError::IOError);
        }
        if (_outfile.fail())
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
 * The path with which an StdDirectory is initialized need not exist initially. In cases
 * where `_path` does not point to a valid directory, StdDirectory exhibits the
 * following behaviors:
 * 
 * - In `ListFiles()`: successfully returns an empty list of filenames, unless `_path`
 *   points to an existing file that conflicts with the target directory name, in which
 *   case the result will be `FilesystemError::Failed`.
 * 
 * - In `OpenForRead()`: returns `FilesystemError::DoesNotExist` for any nonexistent
 *   file, regardless of whether the parent directory exists.
 * 
 * - In `OpenForWrite()`: if `_path` does not exist, it will be created along with all
 *   parent directories, a la `mkdir -p`. If any directory in the path could not be
 *   created, returns `FilesystemError::Failed`.
 * 
 * This behavior ensures that the SDK's default storage implementation is tolerant to
 * unexpected file deletions that occur during the lifetime of the program: if a storage
 * directory is removed, either externally or as the result of some error-recovery
 * routine elsewhere in the program, existing filesystem interfaces will be able to
 * continue writing files.
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

    FilesystemResult<std::unique_ptr<IFileWriter>> OpenForWrite(
        std::string_view name
    ) override
    {
        // Build the path to the target file
        assert(_is_clean_basename(name));
        const std::filesystem::path file_path = _path / name;

        // Ensure the parent directory exists (create if necessary)
        std::error_code ec;
        std::filesystem::create_directories(_path, ec);
        if (ec)
        {
            // Any failure to create the directories is a failure to open the file
            return nonstd::make_unexpected(FilesystemError::Failed);
        }

        // Open the file for write in binary mode, and wrap it in a StdFileWriter
        std::ofstream outfile(file_path, std::ios::binary);
        if (outfile.is_open())
        {
            return std::make_unique<StdFileWriter>(std::move(outfile));
        }

        // We failed to open the file for writing
        return nonstd::make_unexpected(FilesystemError::Failed);
    }

    FilesystemResult<std::unique_ptr<IDirectory>> GetOrCreateChild(
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
