// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_processing.hpp"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <string_view>
#include <vector>

#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/core/storage/util.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/crash_reporting/data/crash_context_read.hpp"
#include "datadog/impl/crash_reporting/data/crash_report_read.hpp"

namespace datadog::impl {

/**
 * Given the full paths to both crash_<timestamp>_<pid> and crash_<timestamp>_<pid>.ctx,
 * attempts to delete both files, logging a warning if deletion fails for any file that
 * exists, and logging a debug message for each file successfully deleted.
 *
 * The crash file is assumed to exist; the context file will be ignored if it does not
 * exist.
 *
 * Returns false if any error was encountered that prevented an existing file from being
 * deleted; true if one or both files were deleted without error.
 */
static bool delete_crash_files(
    DiagnosticLogger& logger,
    FilesystemWrapper& fsw,
    const StoragePath& crash_file_path,
    const StoragePath& context_file_path
) {
  // Result will remain true so long as no filesystem errors occur
  bool all_deletes_ok = true;

  // Attempt to delete the crash report file, logging a diagnostic warning if we fail
  // for any reason
  const FilesystemResult crash_file_res = fsw.Delete(crash_file_path.CStr());
  if (crash_file_res == FilesystemResult::OK) {
    logger.Debug("Deleted crash report file", {{"path", crash_file_path.Get()}});
  } else {
    logger.Warning(
        "Failed to delete crash report file",
        {{"path", crash_file_path.Get()},
         {"error", FilesystemResultStr(crash_file_res)}}
    );
    all_deletes_ok = false;
  }

  // Attempt to delete any existing <crash>.ctx file that describes this crash, silently
  // accepting DoesNotExist to indicate that there was nothing to delete, but logging a
  // warning on any other failure
  const FilesystemResult context_file_res = fsw.Delete(context_file_path.CStr());
  if (context_file_res == FilesystemResult::OK) {
    logger.Debug("Deleted crash context file", {{"path", context_file_path.Get()}});
  } else if (context_file_res != FilesystemResult::DoesNotExist) {
    logger.Warning(
        "Failed to delete crash context file",
        {{"path", context_file_path.Get()},
         {"error", FilesystemResultStr(context_file_res)}}
    );
    all_deletes_ok = false;
  }

  // Return true if we're OK; false if we're leaving files behind
  return all_deletes_ok;
}

/**
 * Result of calling read_crash_files. When data has been successfully read into crf,
 * crash_file retains an open file handle so that the caller can continue holding the
 * exclusive lock on that file.
 */
struct CrashReadResult {
  std::optional<File> crash_file;
  std::optional<CrashReportFile> crf;
  std::optional<CrashContextFile> ccf;
};

/**
 * Attempts to parse a pair of a files that represent a crash report:
 *
 * - crash_<timestamp>_<pid> - full path given by `crash_file_path`
 * - crash_<timestamp>_<pid>.ctx - full path given by `context_file_path`
 *
 * If successful, the result will contain a valid `File` handle for the crash file at
 * `crash_file_path`, as well as a valid `CrashReportFile` struct containing the data
 * parsed from that file.
 *
 * If no context file exists, no `CrashContextFile` value will be included.
 *
 * If a context file exists, it will be parsed to a `CrashContextFile` value and
 * included in the result. Failure to read or parse an existing context file that will
 * cause the entire operation to fail.
 *
 * On failure, all values in the result struct will be std::nullopt.
 */
static CrashReadResult read_crash_files(
    DiagnosticLogger& logger,
    FilesystemWrapper& fsw,
    const StoragePath& crash_file_path,
    const StoragePath& context_file_path
) {
  // We must acquire a lock on the crash file in order to read, process, or delete any
  // files for that crash. Once we've acquired a lock on the crash file, we have
  // exclusive access to the accompanying context file by extension.
  const bool hold_advisory_lock_on_crash_file = true;
  const bool hold_advisory_lock_on_context_file = false;

  // Attempt to open the crash file (crash_<timestamp>_<pid>) for read
  auto crash_file_open_res =
      fsw.OpenForRead(crash_file_path.CStr(), hold_advisory_lock_on_crash_file);

  // If we've failed to acquire a lock, another process already owns the file. Either:
  // 1. It's still in use by the process that originally created it (no crash yet), or
  // 2. Another process running this same routine is currently processing it
  if (crash_file_open_res.value == FilesystemResult::LockContention) {
    logger.Debug(
        "Crash report file is locked; ignoring it", {{"path", crash_file_path.Get()}}
    );
    return {};
  }

  // If we've failed to open the file for any other reason, log a warning and abort
  // processing this crash
  if (crash_file_open_res.value != FilesystemResult::OK) {
    logger.Warning(
        "Unable to process crash report: failed to open crash report file",
        {{"path", crash_file_path.Get()},
         {"error", FilesystemResultStr(crash_file_open_res.value)}}
    );
    return {};
  }

  // Crash file opened successfully: we now have an RAII struct that will keep our crash
  // file handle open until the function exits (or until we std::move it into our result
  // to persist it to the caller), retaining our exclusive lock on this pair of files
  auto crash_file = std::move(crash_file_open_res.file);

  // Parse the crash report file, buffering the essential details of the crash into an
  // in-memory CrashReportFile struct
  auto crash_file_read_res = ReadCrashReport(crash_file);
  switch (crash_file_read_res.GetStatus()) {
    case ReadCrashReportResult::Status::ReadError:
      // We failed to read the file due to a filesystem error: leave the files in place,
      // but log a warning and move on to the next crash
      logger.Warning(
          "Unable to process crash report: failed to read from crash file",
          {{"path", crash_file_path.Get()},
           {"error", FilesystemResultStr(crash_file_read_res.fs_result)}}
      );
      return {};
    case ReadCrashReportResult::Status::OK:
      // File parsed successfully; continue processing this crash
      break;
    case ReadCrashReportResult::Status::Empty:
      // File was empty: the in-process crash handler must open the crash file
      // preemptively, so a leftover empty file is a likely indicator that no crash
      // occurred, but the SDK failed to clean up the file. Ensure that all artifacts
      // from the crash are cleaned up, and then proceed no further.
      logger.Debug(
          "Crash report file is empty; no crash to report",
          {{"path", crash_file_path.Get()}}
      );
      delete_crash_files(logger, fsw, crash_file_path, context_file_path);
      return {};
    case ReadCrashReportResult::Status::Malformed:
      // We failed to parse a valid crash report from this file: attempt to delete it
      logger.Warning(
          "Unable to process crash report: crash file is truncated or malformed",
          {{"path", crash_file_path.Get()}}
      );
      delete_crash_files(logger, fsw, crash_file_path, context_file_path);
      return {};
  }

  // Attempt to open a context file (crash_<timestamp>_<pid>.ctx) that may exist
  // alongside the crash file
  auto context_file_open_res =
      fsw.OpenForRead(context_file_path.CStr(), hold_advisory_lock_on_context_file);

  // If no such context file exists, we can just assume that no context was ever written
  // to accompany this crash and process the crash report with no additional context
  if (context_file_open_res.value == FilesystemResult::DoesNotExist) {
    return CrashReadResult{
        std::move(crash_file), std::move(crash_file_read_res.data), std::nullopt
    };
  }

  // Otherwise, a context file _does_ exist: we MUST successfully parse it in order to
  // process this crash. If we failed to open the file, log a warning and abort
  if (context_file_open_res.value != FilesystemResult::OK) {
    logger.Warning(
        "Unable to process crash report: failed to open crash context file",
        {{"path", context_file_path.Get()},
         {"error", FilesystemResultStr(context_file_open_res.value)}}
    );
    // Allow crash_file to be closed; leave all files for this crash on the disk
    return {};
  }

  // Context file opened OK: parse its contents, aborting and deleting the files for
  // this crash if the context file is malformed
  auto context_file_read_res = ReadCrashContext(context_file_open_res.file);
  switch (context_file_read_res.GetStatus()) {
    case ReadCrashContextResult::Status::ReadError:
      // We failed to read the file due to a filesystem error: leave the files in place,
      // but log a warning and move on to the next crash
      logger.Warning(
          "Unable to process crash report: failed to read from context file",
          {{"path", context_file_path.Get()},
           {"error", FilesystemResultStr(context_file_read_res.fs_result)}}
      );
      return {};
    case ReadCrashContextResult::Status::OK:
      // File parsed successfully; continue processing this crash
      break;
    case ReadCrashContextResult::Status::Malformed:
      // We failed to parse valid crash context from this file: treat the entire crash
      // report as malformed, deleting both files and skipping this crash.
      logger.Warning(
          "Unable to process crash report: context file is truncated or malformed",
          {{"path", context_file_path.Get()}}
      );
      delete_crash_files(logger, fsw, crash_file_path, context_file_path);
      return {};
  }

  // We now have:
  // 1.) An open handle to the crash report file, with an exclusive lock
  // 2.) A struct parsed from that crash report file
  // 3.) A struct parsed from the accompanying context file, iff one existed
  // Return these values to the caller, handing off the file handle to retain the lock
  DATADOG_ASSERT(crash_file_read_res.data.has_value(), "invalid crash file data");
  DATADOG_ASSERT(context_file_read_res.data.has_value(), "invalid context file data");
  return CrashReadResult{
      std::move(crash_file),
      std::move(crash_file_read_res.data),
      std::move(context_file_read_res.data)
  };
}

CrashReport BuildCrashReport(
    const CrashReportFile& crf, const std::optional<CrashContextFile>& ccf
) {
  // Initialize a result struct and copy over the basic details of the crash
  CrashReport crash{};
  crash.fault_code = crf.fault_code;
  crash.fault_address = crf.fault_address;
  crash.fault_flags = crf.fault_flags;
  crash.pid = crf.pid;
  crash.tid = crf.tid;
  crash.timestamp = crf.timestamp;

  // If the crash has a valid context file, copy over the RUM UUIDs from that file
  // (if we have no context file, they'll remain default-initialized to UUID::Zero)
  if (ccf.has_value()) {
    crash.rum_application_id = ccf->rum_application_id;
    crash.rum_session_id = ccf->rum_session_id;
    crash.rum_view_id = ccf->rum_view_id;
    crash.rum_action_id = ccf->rum_action_id;
  }

  // The CrashReportFile contains the data written during the signal-safe path of the
  // crash handler, where our primary job was to flush everything to disk as quickly and
  // simply as possible: i.e. `crf` just has a list of raw stack addresses and an
  // exhaustive list of every module that was loaded (with their address ranges) at the
  // time of the crash.

  // Now that we're back in a stable process, we need to do a bit of post-processing to
  // enrich our stack trace with module information, as well as to pare down the report
  // to only the set of information that needs to be sent to intake - filenames instead
  // of full module paths, only modules that are actually implicated in the crash, etc.

  // Allocate a CrashReport::Frame for each of our original stack addresses
  crash.stack.reserve(crf.stack_addresses.size());

  // For each raw stack address, resolve the following:
  // - module_index: Index into crf.modules denoting which loaded module contained the
  //    code that was being executed at that stack frame, or -1 if no module can be
  //    resolved. These values will later be remapped from crf.modules to crash.modules.
  // - offset: Offset from that module's load address corresponding to raw_address.
  for (uint64_t raw_address : crf.stack_addresses) {
    // We allow for the possibility of modules with overlapping address ranges: e.g. on
    // macOS, system libraries in the dyld shared cache have distinct start addresses,
    // but can all have their end addresses reported as the end of the shared cache,
    // rather than the boundary of the library
    int64_t best_idx = -1;
    uint64_t best_start = 0;
    for (size_t i = 0; i < crf.modules.size(); ++i) {
      // Therefore, we find the module with the highest start_address that still
      // satisfies start_address <= raw_address < end_address
      const auto& mod = crf.modules[i];
      if (mod.start_address <= raw_address && raw_address < mod.end_address) {
        if (best_idx == -1 || mod.start_address > best_start) {
          best_idx = static_cast<int64_t>(i);
          best_start = mod.start_address;
        }
      }
    }

    // If we've identified a module, compute the offset into that module that
    // corresponds to this stack frame's address
    uint64_t offset = 0;
    if (best_idx >= 0) {
      offset = (raw_address - crf.modules[best_idx].start_address);
    }

    // Record a CrashReport::Frame value that encodes the original address along with
    // any resolved module information
    crash.stack.push_back({raw_address, best_idx, offset});
  }

  // Now that we've identified all modules that are references in the call stack, we
  // need to populate crash.modules with _only_ that subset of relevant modules, while
  // also condensing each module's path to a plain filename. Along the way, we'll build
  // a lookup table that maps from original crf.modules index to the corresponding index
  // in our crash.modules vector, so we can fix up the indices in crash.stack.
  std::vector<int64_t> remap(crf.modules.size(), -1);

  // Walk crash.stack to collect the set of referenced module indices, and push a new
  // CrashReport::Module value into crash.modules for each module
  for (const auto& frame : crash.stack) {
    // If we didn't resolve a module reference for this frame, skip it
    if (frame.module_index == -1) {
      continue;
    }

    // If this frame references a module that we've already handled in a previous frame,
    // skip it
    const size_t i = static_cast<size_t>(frame.module_index);
    if (remap[i] != -1) {
      continue;
    }
    const CrashReportFile::Module& mod = crf.modules[i];

    // name: extract a filename from the full module path, using either '\' or '/' as a
    // delimiter on Windows, and only '/' on all other platforms
    std::string_view name{mod.path};
    size_t slash_pos = name.rfind('/');
#ifdef _WIN32
    const size_t backslash_pos = name.rfind('\\');
    if (backslash_pos != std::string_view::npos) {
      if (slash_pos == std::string_view::npos || backslash_pos > slash_pos) {
        slash_pos = backslash_pos;
      }
    }
#endif
    if (slash_pos != std::string_view::npos) {
      name = name.substr(slash_pos + 1);
    }

    // arch: not currently used
    const std::string arch;

    // is_system: not currently used
    const bool is_system = false;

    // Update our result value with an entry for this module, while writing the index of
    // that new value into our module_index remap table
    remap[i] = static_cast<int64_t>(crash.modules.size());
    crash.modules.push_back(
        {std::string(name),
         mod.build_id,
         arch,
         is_system,
         mod.start_address,
         mod.end_address}
    );
  }

  // Use the remap table to fix up all module_index values in our stack frame entries
  for (auto& frame : crash.stack) {
    if (frame.module_index != -1) {
      frame.module_index = remap[static_cast<size_t>(frame.module_index)];
    }
  }

  return crash;
}

bool IsCrashReportFilename(const std::string& filename) {
  // Name must begin with crash_, meaning char 5 is an underscore
  if (filename.find("crash_") != 0) {
    return false;
  }
  const size_t first_delim_pos = 5;

  // Name must contain two underscores in total
  const size_t second_delim_pos = filename.find_first_of('_', first_delim_pos + 1);
  if (second_delim_pos == std::string::npos) {
    return false;
  }

  // Extract two character spans, both of which should be integers denoting:
  // 1. Between the first and second underscore: timestamp in milliseconds
  // 2. From second underscore to end of string: pid
  std::string_view timestamp_ms_str(
      filename.data() + first_delim_pos + 1, second_delim_pos - first_delim_pos - 1
  );
  std::string_view pid_str(
      filename.data() + second_delim_pos + 1, filename.size() - second_delim_pos - 1
  );

  // Both spans must be non-empty and contain only ASCII digits
  auto is_all_digits = [](std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
      return std::isdigit(c);
    });
  };
  return is_all_digits(timestamp_ms_str) && is_all_digits(pid_str);
}

void ProcessCrashReports(
    DiagnosticLogger& logger,
    IFilesystem& fs,
    StoragePath& storage_dir_path,
    const CrashReportCallback& on_process_callback
) {
  DATADOG_ASSERT(
      on_process_callback != nullptr, "No callback passed to ProcessCrashReports"
  );

  // Initialize an IFilesystem wrapper to handle path encoding
  FilesystemWrapper fsw(fs);

  // List all regular files in the crash storage directory
  std::vector<std::string> filenames;
  auto list_res = fsw.ListFiles(storage_dir_path.CStr(), filenames);
  if (list_res != FilesystemResult::OK) {
    // If the directory doesn't exist, there's nothing to process: treat this case the
    // same as a valid directory that contains 0 crash report files
    if (list_res == FilesystemResult::DoesNotExist) {
      logger.Status(
          "Crash report processing complete: storage directory does not exist",
          {{"path", storage_dir_path.Get()}}
      );
      return;
    }
    // If we failed to list files for any other reason, log a warning and abort
    logger.Warning(
        "Unable to process crash reports: failed to list files in storage directory",
        {{"path", storage_dir_path.Get()}, {"error", FilesystemResultStr(list_res)}}
    );
    return;
  }

  // Iterate over all filenames, removing the ones that don't conform to our
  // crash-report-file naming convention, so we can see how many crashes exist
  filenames.erase(
      std::remove_if(
          filenames.begin(),
          filenames.end(),
          [](const std::string& name) { return !IsCrashReportFilename(name); }
      ),
      filenames.end()
  );

  // Early-out with a status message if we have no crash reports to process
  if (filenames.empty()) {
    logger.Status(
        "Crash report processing complete: no crash reports found",
        {{"path", storage_dir_path.Get()}}
    );
    return;
  }

  // Sort crash reports in reverse lexicographical order for deterministic iteration:
  // format is 'crash_<timestamp>_<pid>', so this puts newer crashes first
  std::sort(filenames.rbegin(), filenames.rend());

  // Iterate over each crash report file, attempting to parse it (along with any
  // accompanying crash context file) and report the crash
  for (const std::string& filename : filenames) {
    // Build the full path to the crash file
    StoragePath crash_file_path;
    crash_file_path.MustSet(storage_dir_path);
    const char* join_msg = "Failed to build path to crash report file: path too long";
    if (!AppendPath(crash_file_path, filename, logger, join_msg)) {
      return;
    }

    // Build the full path to the accompanying crash context file
    StoragePath context_file_path;
    const char* ctx_join_msg =
        "Failed to build path to crash context file: path too long";
    context_file_path.MustSet(crash_file_path);
    if (!AppendExtensionToPath(context_file_path, ".ctx", logger, ctx_join_msg)) {
      // Note that these failures can only occur if the API layer doesn't adequately
      // validate the root application storage path (leaving enough headroom to build
      // all expected paths without hitting MAX_STORAGE_PATH_SIZE): if we can't build
      // paths to both files, fail hard rather than producing a potentially-incomplete
      // crash report
      return;
    }

    // Attempt to read a CrashReportFile and an optional CrashContextFile
    auto res = read_crash_files(logger, fsw, crash_file_path, context_file_path);
    if (!res.crash_file.has_value() || !res.crf.has_value()) {
      // read_crash_files handles logging and deletion/cleanup where needed; move on to
      // the next crash report
      continue;
    }
    // res.crash_file retains our exclusive lock on crash_file_path; to be released when
    // the file is closed via RAII

    // Using the raw data read from those files, build a single CrashReport struct that
    // describes our crash, with resolved module offsets etc.
    CrashReport report = BuildCrashReport(*res.crf, res.ccf);

    // Broadcast this crash report so it can be handled in other parts of the SDK (e.g.
    // the `CrashReporting` feature can push a CrashReportProcessedMessage to the
    // MessageBus, allowing `Rum` to respond by generating a RUM Error that describes
    // this crash)
    on_process_callback(std::move(report));
    logger.Status("Processed a valid crash report", {{"path", crash_file_path.Get()}});

    // We're done with this crash; delete all files before we release our lock. Note
    // that if the application crashes in the very brief window between now and when the
    // resulting RUM event is flushed to disk, the crash report will be lost: this is
    // an acceptable tradeoff
    delete_crash_files(logger, fsw, crash_file_path, context_file_path);

    // res.crash_file is closed via RAII; continue to next crash file
  }
}

}  // namespace datadog::impl
