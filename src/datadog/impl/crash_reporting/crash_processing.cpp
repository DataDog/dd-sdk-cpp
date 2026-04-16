// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_processing.hpp"

#include <cinttypes>
#include <string>
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

}  // namespace datadog::impl
