// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage/feature_event.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/core/storage/util.hpp"
#include "datadog/impl/core/util/assert.hpp"

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)

// Global version number applied to all event data stored persistently; may be bumped in
// the event of breaking changes in order to abandon previously-written events on disk.
// This versioning scheme applies to the storage implementation as a whole: individual
// features should implement their own versioning schemes internally if needed.
#define DATADOG_EVENT_STORAGE_VERSION "1"

// Use (e.g.) 'v1' to store events gathered while tracking consent is granted;
// 'intermediate-v1' for events gathered while tracking consent is pending
const char* PENDING_SUBDIRECTORY_NAME = "intermediate-v" DATADOG_EVENT_STORAGE_VERSION;
const char* GRANTED_SUBDIRECTORY_NAME = "v" DATADOG_EVENT_STORAGE_VERSION;

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(cppcoreguidelines-macro-usage)

namespace datadog::impl {

FeatureEventStorage::FeatureEventStorage(
    IFilesystem& in_fs, impl::DiagnosticLogger& in_logger
)
    : _fs(in_fs), _logger(in_logger) {}

bool FeatureEventStorage::Initialize(
    std::string_view process_root, std::string_view feature_name
) {
  const char* join_message =
      "Failed to initialize feature event storage directory from configured "
      "application storage path: path exceeds length limit";
  const char* mkdir_message =
      "Failed to initialize feature event storage directory from configured "
      "application storage path: unable to create directory";

  // Initialize a wrapper that will handle path encoding seamlessly
  FilesystemWrapper fsw(_fs);

  // process_root gives us the root directory for all events stored by this SDK
  // instance, i.e. <application-storage>/.datadog/<instance>/<pid>/ - we first need to
  // build a root directory for our feature within process_root
  if (!JoinPaths(_root, process_root, feature_name, _logger, join_message)) {
    return false;
  }
  if (!EnsureDirectoryExists(_root, fsw, _logger, mkdir_message)) {
    return false;
  }

  // Now that we have a root feature directory, we want two subdirectories: the first,
  // _pending_root, is for events gathered while tracking consent is pending
  if (!JoinPaths(
          _pending_root, _root.Get(), PENDING_SUBDIRECTORY_NAME, _logger, join_message
      )) {
    return false;
  }
  if (!EnsureDirectoryExists(_pending_root, fsw, _logger, mkdir_message)) {
    return false;
  }

  // The second, _granted_root, contains events that we have consent to upload
  if (!JoinPaths(
          _granted_root, _root.Get(), GRANTED_SUBDIRECTORY_NAME, _logger, join_message
      )) {
    return false;
  }
  if (!EnsureDirectoryExists(_granted_root, fsw, _logger, mkdir_message)) {
    return false;
  }

  // We can now permit the SDK to write events to pending and/or granted dirs; read from
  // the granted dir; and freely move/delete files in those directories
  return true;
}

bool FeatureEventStorage::DeletePendingBatches() {
  // Thread-safety/synchronization considerations:
  //
  // 1. We only delete from _pending_root, which the upload thread never reads from, so
  //    we don't have to worry about contention with other threads
  // 2. The storage thread performs deletion synchronously, i.e. there can be no file
  //    writes happening concurrently during deletion
  // 3. When the storage thread _does_ perform writes, it does so atomically and closes
  //    the file, so there are no open handles to any of the files we're deleting
  // 4. This function is called on the storage thread
  //
  // Therefore, we can safely assume that we have exclusive access to _pending_root
  // during this function call.

  // Use a FilesystemWrapper to handle path encoding transparently
  FilesystemWrapper fsw(_fs);

  // Get a list of all files in the pending directory
  std::vector<std::string> filenames;
  const FilesystemResult list_res = fsw.ListFiles(_pending_root.CStr(), filenames);
  if (list_res != FilesystemResult::OK) {
    _logger.Error(
        "Could not delete batch files on consent change: directory listing failed",
        {{"path", _pending_root.Get()}, {"error", FilesystemResultStr(list_res)}}
    );
    return false;
  }

  // Sort files alphabetically for deterministic ordering
  std::sort(filenames.begin(), filenames.end());

  // Iterate over each file, deleting any files with numeric names
  const char* msg =
      "Could not delete batch files on consent change: path exceeds length limit";
  for (const std::string& filename : filenames) {
    // Skip files with non-numeric names; these were not written by the SDK
    if (!std::all_of(filename.begin(), filename.end(), [](unsigned char c) {
          return std::isdigit(c);
        })) {
      continue;
    }

    // Build the full path to the file to be deleted
    StoragePath file_path;
    file_path.MustSet(_pending_root);
    if (!AppendPath(file_path, filename, _logger, msg)) {
      return false;
    }

    // Attempt to delete the batch file, aborting on failure
    const FilesystemResult delete_res = fsw.Delete(file_path.CStr());
    if (delete_res != FilesystemResult::OK) {
      _logger.Error(
          "Could not delete batch file on consent change",
          {{"path", file_path.Get()}, {"error", FilesystemResultStr(delete_res)}}
      );
      return false;
    }

    // Batch file deleted successfully
    _logger.Debug(
        "Deleted batch file due to consent change", {{"path", file_path.Get()}}
    );
  }

  // Success: all batch files in pending directory deleted
  return true;
}

bool FeatureEventStorage::MigratePendingBatchesToGranted() {
  // Thread-safety/synchronization considerations:
  //
  // 1. We move files *from* _pending_root: as with DeletePendingBatches(), this
  //    function call happens synchronously on the storage thread, so we don't have to
  //    worry about concurrent writes or open file handles
  // 2. We move files *into* _granted_root: we need to consider that the upload thread
  //    may be reading from this directory while we're moving files into it. However:
  // 3. _pending_root and _granted_root are guaranteed to be on the same filesystem
  //    (since they're subdirectories within an SDK-controlled directory), and therefore
  //    a file rename operation is atomic on both POSIX and Windows: the upload thread
  //    will never see a "partially-moved" file.
  //
  // Additional considerations re: filename conflicts:
  //
  // - The files we're moving are named with timestamps indicating file creation time,
  //   so if we're switching from pending to granted, it's extremely unlikely that
  //   _granted_directory will already contain a file with the same name as a file being
  //   moved from _pending_directory. However, it _is_ technically possibly in the event
  //   of system clock adjustments, external filesystem tampering, or extremely rapid
  //   tracking consent changes.
  //
  // - In the unlikely event of a conflict, we leave the file in _granted_directory
  //   intact, and we delete the file from _pending_directory. As long as we either move
  //   the file or delete it in response to a conflict, the operation continues
  //   successfully.

  // Use a FilesystemWrapper to handle path encoding transparently
  FilesystemWrapper fsw(_fs);

  // Get a list of all files in the pending directory
  std::vector<std::string> filenames;
  const FilesystemResult list_res = fsw.ListFiles(_pending_root.CStr(), filenames);
  if (list_res != FilesystemResult::OK) {
    _logger.Error(
        "Could not migrate batch files on consent change: directory listing failed",
        {{"path", _pending_root.Get()}, {"error", FilesystemResultStr(list_res)}}
    );
    return false;
  }

  // Sort files alphabetically for deterministic ordering
  std::sort(filenames.begin(), filenames.end());

  // Iterate over each file, relocating any files with numeric names
  const char* msg =
      "Could not migrate batch files on consent change: path exceeds length limit";
  for (const std::string& filename : filenames) {
    // Skip files with non-numeric names; these were not written by the SDK
    if (!std::all_of(filename.begin(), filename.end(), [](unsigned char c) {
          return std::isdigit(c);
        })) {
      continue;
    }

    // Build the full path to the file to be renamed and to the destination file path
    StoragePath src_file_path;
    src_file_path.MustSet(_pending_root);
    if (!AppendPath(src_file_path, filename, _logger, msg)) {
      return false;
    }
    StoragePath dst_file_path;
    dst_file_path.MustSet(_granted_root);
    if (!AppendPath(dst_file_path, filename, _logger, msg)) {
      return false;
    }

    // Attempt a rename that will relocate the file from _pending_root to _granted_root,
    // preserving its filename
    const auto rename_res = fsw.Rename(src_file_path.CStr(), dst_file_path.CStr());

    // If the rename succeeds, we've successfully migrated this file and we can continue
    // to the next one
    if (rename_res == FilesystemResult::OK) {
      _logger.Debug(
          "Migrated batch file due to consent change",
          {{"src_path", src_file_path.Get()}, {"dst_path", dst_file_path.Get()}}
      );
      continue;
    }

    // If the rename failed because a destination file exists with the same name,
    // attempt to delete the source file and continue
    if (rename_res == FilesystemResult::AlreadyExists) {
      auto delete_res = fsw.Delete(src_file_path.CStr());
      if (delete_res == FilesystemResult::OK) {
        // If we can successfully delete the source file (leaving the existing
        // destination file as-is), continue with the migration
        _logger.Warning(
            "Deleted pending-directory copy of duplicate batch file",
            {{"path", src_file_path.Get()}}
        );
        continue;
      }

      // Otherwise, we have a file conflict and we're unable to delete the source file:
      // leave the file in place, but log a warning and carry on with the migration
      _logger.Warning(
          "Could not delete pending-directory copy of duplicate batch file",
          {{"path", src_file_path.Get()}, {"error", FilesystemResultStr(delete_res)}}
      );
      continue;
    }

    // If the move failed for any other reason, abort the migration operation
    _logger.Error(
        "Could not migrate batch file on consent change: rename failed",
        {{"src_path", src_file_path.Get()},
         {"dst_path", dst_file_path.Get()},
         {"error", FilesystemResultStr(rename_res)}}
    );
    return false;
  }

  // Success: all batch files in _pending_root (whose names didn't conflict with
  // existing files in _granted_root) have been relocated
  return true;
}

}  // namespace datadog::impl
