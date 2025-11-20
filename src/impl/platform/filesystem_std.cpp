// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <filesystem>
#include <fstream>
#include <limits>

#include "assert.hpp"
#include "platform/filesystem.hpp"

namespace datadog::platform {

static datadog::impl::ErrorMessage _fs_error(
    const char* text, const std::filesystem::path& path, const std::error_code& ec
) {
  if (ec) {
    return datadog::impl::ErrorMessage(
        text,
        {{"path", Attribute::String(path.string())},
         {"message", Attribute::String(ec.message())},
         {"category", Attribute::String(ec.category().name())},
         {"value", Attribute::Int(ec.value())}}
    );
  }
  return datadog::impl::ErrorMessage(
      text, {{"path", Attribute::String(path.string())}}
  );
}

static bool _is_clean_basename(std::string_view name) {
  return (
      !name.empty() && name != "." && name != ".." &&
      name.find('/') == std::string_view::npos &&
      name.find('\\') == std::string_view::npos
  );
}

enum class _check_path_result : uint8_t {
  does_not_exist,
  exists_as_file,
  exists_as_directory,
  check_failed,
};

static _check_path_result _check_path(
    const std::filesystem::path& path, std::error_code& ec
) {
  // We could do all of this in a single std::filesystem::status call, but platforms
  // have subtle variation in behavior for stat'ing a nonexistent path: some return a
  // valid status where type() == std::filesystem::file_type::not_found, some return a
  // standard error (std::errc::no_such_file_or_directory), some return nonstandard,
  // platform-specific errors (e.g. 'generic:2'). Supporting each platform's quirks with
  // conditional compilation may not be worth saving a system call given that we access
  // files infrequently, using background threads.

  // Make a call to std::filesystem::exists to see if it points to anything
  const bool exists = std::filesystem::exists(path, ec);
  if (ec) {
    return _check_path_result::check_failed;
  }
  if (!exists) {
    return _check_path_result::does_not_exist;
  }

  // Make a call to std::filesystem::is_directory to test if it's a directory or
  // something else
  const bool is_directory = std::filesystem::is_directory(path, ec);
  if (ec) {
    return _check_path_result::check_failed;
  }
  if (is_directory) {
    return _check_path_result::exists_as_directory;
  }
  return _check_path_result::exists_as_file;
}

/**
 * Wraps a std::ifstream in the IFileReader interface.
 *
 * Read operations that set the bad bit (indicating a catastrophic I/O problem) will
 * result in `FilesystemError::IOError`, all other failures will result in
 * `FilesystemError::Failed`.
 */
class StdFileReader final : public IFileReader {
 private:
  std::ifstream _infile;  // Default destructor closes file if open

 public:
  explicit StdFileReader(std::ifstream&& infile) : _infile(std::move(infile)) {}

  FilesystemResult<void> Seek(int offset) override {
    // Seek from the current position and check error bits
    _infile.seekg(offset, std::ios_base::cur);
    if (_infile.bad()) {
      return nonstd::make_unexpected(FilesystemError::IOError);
    }
    if (_infile.fail()) {
      return nonstd::make_unexpected(FilesystemError::Failed);
    }

    // No error; return default expected<void>
    return {};
  }

  FilesystemResult<size_t> Read(char* dst, size_t n) override {
    // Read up to n bytes from the file into dst
    DATADOG_ASSERT(
        static_cast<std::streamsize>(n) <= std::numeric_limits<std::streamsize>::max(),
        "unexpected truncation of file read size"
    );
    _infile.read(dst, static_cast<std::streamsize>(n));

    // If bad bit is set, an I/O error occurred
    if (_infile.bad()) {
      return nonstd::make_unexpected(FilesystemError::IOError);
    }

    // If we read anything, or if we've successfully reached the end of the file, the
    // operation succeeded (even if fail bit was set, as this happens on EOF)
    const size_t num_bytes_read = _infile.gcount();
    const bool read_ok = num_bytes_read > 0 || _infile.eof();

    // If the read operation failed, return an error
    if (_infile.fail() && !read_ok) {
      return nonstd::make_unexpected(FilesystemError::Failed);
    }

    // Read succeeded; return number of bytes actually read
    return num_bytes_read;
  }
};

/**
 * Wraps a std::ofstream in the IFileWriter interface.
 *
 * Write operations that set the bad bit (indicating a catastrophic I/O problem) will
 * result in `FilesystemError::IOError`, all other failures will result in
 * `FilesystemError::Failed`.
 */
class StdFileWriter final : public IFileWriter {
 private:
  const std::filesystem::path _path;

 public:
  explicit StdFileWriter(std::filesystem::path&& path) : _path(std::move(path)) {}

  FilesystemResult<void> Write(const char* src, size_t n) override {
    // Default-initialize std::ofstream
    std::ofstream outfile;

    // Attempt to open the file for append
    const std::ios::openmode mode = std::ios::binary | std::ios::app;
    outfile.open(_path, mode);

    // If we couldn't open the file, it may be because some parent directory doesn't
    // yet exist or has been externally deleted
    if (!outfile) {
      // Attempt to create the required directories, then retry opening the file
      std::error_code ec;
      if (!std::filesystem::create_directories(_path.parent_path(), ec) || ec) {
        return nonstd::make_unexpected(FilesystemError::Failed);
      }
      outfile.open(_path, mode);
    }

    // If the file still isn't open, there's some other problem: abort
    if (!outfile) {
      return nonstd::make_unexpected(FilesystemError::Failed);
    }

    // File is open: attempt to write all n bytes to the file, then check error bits
    DATADOG_ASSERT(
        static_cast<std::streamsize>(n) <= std::numeric_limits<std::streamsize>::max(),
        "unexpected truncation of file write size"
    );
    outfile.write(src, static_cast<std::streamsize>(n));
    if (outfile.bad()) {
      return nonstd::make_unexpected(FilesystemError::IOError);
    }
    if (outfile.fail()) {
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
class StdDirectory : public virtual IDirectory {
 private:
  const std::filesystem::path _path;

 public:
  explicit StdDirectory(const std::filesystem::path& path) : _path(path) {}

  FilesystemResult<void> ListFiles(std::vector<std::string>& out_names) override {
    // Result vector should be cleared by the caller
    DATADOG_ASSERT(out_names.empty(), "ListFiles called with non-empty result vector");

    // Initialize a directory iterator, and check to see if it was successful
    std::error_code ec;
    std::filesystem::directory_iterator it(_path, ec);
    if (ec) {
      // If our directory doesn't exist, that's fine: our result set is empty
      if (ec == std::errc::no_such_file_or_directory) {
        // No error; return default expected<void>
        return {};
      }

      // We failed to read from the directory for some other reason
      return nonstd::make_unexpected(FilesystemError::Failed);
    }

    // Our iterator is good; read the basenames of all regular files into the output
    // vector and return success
    for (const std::filesystem::directory_entry& entry : it) {
      // Only consider regular files
      if (!entry.is_regular_file()) {
        continue;
      }

      // Add basename only
      out_names.emplace_back(entry.path().filename().string());
    }

    // No error; return default expected<void>
    return {};
  }

  FilesystemResult<void> RemoveFile(std::string_view name) override {
    // Build the path to the target file
    DATADOG_ASSERT(_is_clean_basename(name), "invalid filename");
    const std::filesystem::path file_path = _path / name;

    // Attempt to delete the file
    std::error_code ec;
    const bool removed = std::filesystem::remove(file_path, ec);
    if (!removed || ec) {
      if (!removed || ec == std::errc::no_such_file_or_directory) {
        return nonstd::make_unexpected(FilesystemError::DoesNotExist);
      }
      return nonstd::make_unexpected(FilesystemError::Failed);
    }

    // No error; return default expected<void>
    return {};
  }

  FilesystemResult<std::unique_ptr<IFileReader>> OpenForRead(
      std::string_view name
  ) override {
    // Build the path to the target file
    DATADOG_ASSERT(_is_clean_basename(name), "invalid filename");
    const std::filesystem::path file_path = _path / name;

    // Open the file for read in binary mode, and wrap it in a StdFileReader
    std::ifstream infile(file_path, std::ios::binary);
    if (infile.is_open()) {
      return std::make_unique<StdFileReader>(std::move(infile));
    }

    // We failed to open the file: determine whether that's because the file doesn't
    // exist or for some other reason
    std::error_code ec;
    switch (_check_path(file_path, ec)) {
      case _check_path_result::does_not_exist:
        return nonstd::make_unexpected(FilesystemError::DoesNotExist);

      case _check_path_result::check_failed:
      case _check_path_result::exists_as_directory:
      case _check_path_result::exists_as_file:
        return nonstd::make_unexpected(FilesystemError::Failed);
    }
    DATADOG_ASSERT(false, "unhandled _check_path_result enum value");
    return nonstd::make_unexpected(FilesystemError::Failed);
  }

  FilesystemResult<std::unique_ptr<IFileWriter>> PrepareForWrite(
      std::string_view name
  ) override {
    // Initialize a wrapper for a file at the target path
    DATADOG_ASSERT(_is_clean_basename(name), "invalid filename");
    return std::make_unique<StdFileWriter>(_path / name);
  }

  FilesystemResult<std::unique_ptr<IDirectory>> PrepareSubdirectory(
      std::string_view name
  ) override {
    // Build the path to the target directory
    DATADOG_ASSERT(_is_clean_basename(name), "invalid directory name");
    const std::filesystem::path subdir_path = _path / name;

    // Check to see if the target path is occupied
    std::error_code ec;
    switch (_check_path(subdir_path, ec)) {
      // If we can't stat the path, or if the target directory path is occupied by an
      // existing file, the operation failed
      case _check_path_result::check_failed:
      case _check_path_result::exists_as_file:
        return nonstd::make_unexpected(FilesystemError::Failed);

      // Whether the directory exists or not, we're fine to proceed: we'll create the
      // director(ies) lazily on the first file write if needed
      case _check_path_result::exists_as_directory:
      case _check_path_result::does_not_exist:
        break;
    }

    // Create a StdDirectory to wrap the requested subdirectory
    return std::make_unique<StdDirectory>(subdir_path);
  }
};

// IStorageDirectory extends IDirectory, so we derived from the concrete Directory class
// when implementing IStorageDirectory. We don't invite the multiple-inheritance diamond
// problem, because there's still only one concrete base class, and the compiler
// resolves the ambiguity by preferring the concrete base class over the pure-virtual
// one ("via dominance"). MSVC emits a warning for this case; Clang and GCC don't.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4250)  // 'class' : inherits 'member' via dominance
#endif
class StdStorageDirectory final : public StdDirectory, public IStorageDirectory {
 public:
  explicit StdStorageDirectory(const std::filesystem::path& path)
      : StdDirectory(path) {}
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

Filesystem::InitResult Filesystem::Init(std::string_view path) {
  // Resolve the configured path for our root storage directory
  const std::filesystem::path root_path{path};

  // Check to see if the target directory exists
  std::error_code ec;
  switch (_check_path(root_path, ec)) {
    // If we failed to check the path, or if the target directory path is occupied by an
    // existing file, we can't initialize the filesystem
    case _check_path_result::check_failed:
      DATADOG_ASSERT(ec, "_check_path returned check_failed w/ successful error_code");
      return nonstd::make_unexpected(
          _fs_error("failed to stat root storage path", path, ec)
      );

    case _check_path_result::exists_as_file:
      return nonstd::make_unexpected(
          _fs_error("root storage path is occupied by a file", path, {})
      );

    // If the target directory doesn't yet exist, create it
    case _check_path_result::does_not_exist: {
      // Create the target directory, requiring that the parent already exist
      std::filesystem::create_directory(root_path, ec);
      if (ec) {
        // Failed to create directory; we can't initialize the filesystem
        return nonstd::make_unexpected(
            _fs_error("failed to create root storage directory", path, ec)
        );
      }
    } break;

    // If it already exists, we're good to go
    case _check_path_result::exists_as_directory:
      break;
  }

  // root_path points to a valid directory; create our filesystem interface
  return std::make_unique<StdStorageDirectory>(root_path);
}

}  // namespace datadog::platform
