// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_processing.hpp"

#include "datadog/impl/crash_reporting/data/crash_context_read.hpp"
#include "datadog/impl/crash_reporting/data/crash_report_read.hpp"

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("BuildCrashReport", "[unit][crash_reporting]") {
  // Given a basic crash report (as parsed from an in-process crash dump file) which
  // test cases can freely modify
  CrashReportFile crf{0xbeef, 0x10001000, 0xcf, 12345, 6789, 1710000000000, {}, {}};

  SECTION("M convey basic crash details faithfully") {
    // Given crash report with no module info, no stack, and no context file
    // When we build a CrashReport struct
    const CrashReport crash = BuildCrashReport(crf, std::nullopt);

    // The then result includes the essential data
    REQUIRE(crash.fault_code == 0xbeef);
    REQUIRE(crash.fault_address == 0x10001000);
    REQUIRE(crash.fault_flags == 0xcf);
    REQUIRE(crash.pid == 12345);
    REQUIRE(crash.tid == 6789);
    REQUIRE(crash.timestamp == 1710000000000);
  }

  SECTION("M convey module details") {
    // Given a single module and a single stack frame whose raw address is positioned
    // within the load range of that module
    crf.modules.push_back({0x100000, 0x200000, "something.lib", "my-build-id"});
    crf.stack_addresses.push_back({0x10c000});
    const CrashReport crash = BuildCrashReport(crf, std::nullopt);

    // Then the resulting crash report contains the essential data describing the module
    REQUIRE(crash.modules.size() == 1);
    REQUIRE(crash.modules[0].name == "something.lib");
    REQUIRE(crash.modules[0].build_id == "my-build-id");
    REQUIRE(crash.modules[0].start_address == 0x100000);
    REQUIRE(crash.modules[0].end_address == 0x200000);

    // TODO: arch is currently unused on all platforms
    REQUIRE(crash.modules[0].arch == "");

    // TODO: is_system is currently unused on all platforms
    REQUIRE(crash.modules[0].is_system == false);
  }

  SECTION("M strip module path down to filename-only name") {
    SECTION("{forward-slash is universal}") {
      // Given a single module with a forward-slash-delimited path
      crf.modules.push_back({0x100000, 0x200000, "/var/foo/libbar.so", "my-build-id"});
      crf.stack_addresses.push_back({0x10c000});
      const CrashReport crash = BuildCrashReport(crf, std::nullopt);

      // Then the resulting module entry is just the filename, on all platforms
      REQUIRE(crash.modules.size() == 1);
      REQUIRE(crash.modules[0].name == "libbar.so");
    }
    SECTION("{backslash is Windows-only}") {
      // Given a single module with a backslash-delimited path
      crf.modules.push_back({0x100000, 0x200000, "C:\\foo\\bar.lib", "my-build-id"});
      crf.stack_addresses.push_back({0x10c000});
      const CrashReport crash = BuildCrashReport(crf, std::nullopt);

      // Then on Windows only, the resulting module entry is just the filename, while on
      // all other platforms the backslashes are treated as any other character
      REQUIRE(crash.modules.size() == 1);
#ifdef _WIN32
      REQUIRE(crash.modules[0].name == "bar.lib");
#else
      REQUIRE(crash.modules[0].name == "C:\\foo\\bar.lib");
#endif
    }
  }

  SECTION("M resolve module references and offsets for stack frames") {
    // Given a single module and a single stack frame whose raw address is positioned
    // within the load range of that module
    crf.modules.push_back({0x100000, 0x200000, "something.lib", "my-build-id"});
    crf.stack_addresses.push_back({0x10c000});
    const CrashReport crash = BuildCrashReport(crf, std::nullopt);

    // Then the resulting stack frame is associated with the relevant module, with an
    // offset that corresponds to its raw address
    REQUIRE(crash.stack.size() == 1);
    REQUIRE(crash.stack[0].address == 0x10c000);
    REQUIRE(crash.stack[0].module_index == 0);
    REQUIRE(crash.stack[0].offset == 0xc000);
    REQUIRE(crash.modules.size() == 1);
    REQUIRE(crash.modules[0].name == "something.lib");
  }

  SECTION("M resolve module with highest start_address W address ranges overlap") {
    // Given two module with overlapping address ranges, and two stack frames, one
    // referencing each module
    crf.modules.push_back({0x100000, 0x300000, "libone", "build-id-one"});
    crf.modules.push_back({0x200000, 0x300000, "libtwo", "build-id-two"});
    crf.stack_addresses.push_back({0x10c000});  // libone + 0xc000
    crf.stack_addresses.push_back({0x20ffc0});  // libtwo + 0xffc0 (libone + 0x10ffc0)
    const CrashReport crash = BuildCrashReport(crf, std::nullopt);

    // Then the module resolved for each frame is the best match, not simply the first
    // valid match found
    REQUIRE(crash.modules.size() == 2);
    REQUIRE(crash.modules[0].name == "libone");
    REQUIRE(crash.modules[1].name == "libtwo");
    REQUIRE(crash.stack.size() == 2);
    REQUIRE(crash.stack[0].address == 0x10c000);
    REQUIRE(crash.stack[0].module_index == 0);
    REQUIRE(crash.stack[0].offset == 0xc000);
    REQUIRE(crash.stack[1].address == 0x20ffc0);
    REQUIRE(crash.stack[1].module_index == 1);
    REQUIRE(crash.stack[1].offset == 0xffc0);
  }

  SECTION("M set module_index = -1, offset = 0 W stack frame has unknown module") {
    // Given a single stack frame whose raw address does not correspond to any known
    // module
    crf.stack_addresses.push_back({0x10c000});
    const CrashReport crash = BuildCrashReport(crf, std::nullopt);

    // Then the resulting stack frame is conveys the correct raw address, but
    // module_index and offset are set to sentinel values
    REQUIRE(crash.stack.size() == 1);
    REQUIRE(crash.stack[0].address == 0x10c000);
    REQUIRE(crash.stack[0].module_index == -1);
    REQUIRE(crash.stack[0].offset == 0);
    REQUIRE(crash.modules.size() == 0);
  }

  SECTION("M filter modules to exclude libraries not present in stack") {
    // Given three modules, and a stack frame that only references two of them
    crf.modules.push_back({0x100000, 0x200000, "libone", "build-id-one"});
    crf.modules.push_back({0x200000, 0x300000, "libtwo", "build-id-two"});
    crf.modules.push_back({0x300000, 0x400000, "libtri", "build-id-tri"});
    crf.stack_addresses.push_back({0x10c000});  // libone + 0xc000
    crf.stack_addresses.push_back({0x30c000});  // libtri + 0xc000
    crf.stack_addresses.push_back({0x10cf60});  // libone + 0xcf60
    crf.stack_addresses.push_back({0x400000});  // ??? + 0
    const CrashReport crash = BuildCrashReport(crf, std::nullopt);

    // Then the resulting list of modules only includes the two that are referenced by
    // stack frames
    REQUIRE(crash.modules.size() == 2);
    REQUIRE(crash.modules[0].name == "libone");
    REQUIRE(crash.modules[0].start_address == 0x100000);
    REQUIRE(crash.modules[0].end_address == 0x200000);
    REQUIRE(crash.modules[1].name == "libtri");
    REQUIRE(crash.modules[1].start_address == 0x300000);
    REQUIRE(crash.modules[1].end_address == 0x400000);

    // And the resulting stack has its module references resolved as expected, with
    // module_index mapped to the actual result vector, not the position in the original
    // CrashReportFile vector
    REQUIRE(crash.stack.size() == 4);
    REQUIRE(crash.stack[0].address == 0x10c000);
    REQUIRE(crash.stack[0].module_index == 0);
    REQUIRE(crash.stack[0].offset == 0xc000);
    REQUIRE(crash.stack[1].address == 0x30c000);
    REQUIRE(crash.stack[1].module_index == 1);
    REQUIRE(crash.stack[1].offset == 0xc000);
    REQUIRE(crash.stack[2].address == 0x10cf60);
    REQUIRE(crash.stack[2].module_index == 0);
    REQUIRE(crash.stack[2].offset == 0xcf60);
    REQUIRE(crash.stack[3].address == 0x400000);
    REQUIRE(crash.stack[3].module_index == -1);
    REQUIRE(crash.stack[3].offset == 0);
  }

  SECTION("M include RUM context W context file has valid RUM UUIDs") {
    // Given a crash context file that describes a RUM action (within a RUM view, within
    // a RUM session, all owned by a RUM application) that was ongoing at the time of
    // the crash
    CrashContextFile ccf{
        *UUID::Parse("bb9e8db9-4dbd-4eb4-b40f-f276b4e922eb"),
        *UUID::Parse("620c3e19-7c4e-461c-853f-1c3d38f50ac8"),
        *UUID::Parse("21bce51b-4174-47a3-ad00-3b177634e954"),
        *UUID::Parse("83b0007a-a397-4f5b-957e-40a1ada06b38")
    };
    const CrashReport crash = BuildCrashReport(crf, ccf);

    // Then the resulting value records that RUM state
    REQUIRE(
        crash.rum_application_id == *UUID::Parse("bb9e8db9-4dbd-4eb4-b40f-f276b4e922eb")
    );
    REQUIRE(
        crash.rum_session_id == *UUID::Parse("620c3e19-7c4e-461c-853f-1c3d38f50ac8")
    );
    REQUIRE(crash.rum_view_id == *UUID::Parse("21bce51b-4174-47a3-ad00-3b177634e954"));
    REQUIRE(
        crash.rum_action_id == *UUID::Parse("83b0007a-a397-4f5b-957e-40a1ada06b38")
    );
  }

  SECTION("M clear RUM context W no context file exists") {
    // Given no additional crash context
    const CrashReport crash = BuildCrashReport(crf, std::nullopt);

    // Then the resulting value has UUID::Zero for all RUM UUIDs
    REQUIRE(crash.rum_application_id == UUID::Zero);
    REQUIRE(crash.rum_session_id == UUID::Zero);
    REQUIRE(crash.rum_view_id == UUID::Zero);
    REQUIRE(crash.rum_action_id == UUID::Zero);
  }
}
