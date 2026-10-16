// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>

#include "datadog/core.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/crash_reporting/data/crash_context.hpp"
#include "datadog/impl/crash_reporting/data/crash_report.hpp"
#include "datadog/impl/types/crash_reporting.hpp"

#include "mock/binary.hpp"

/**
 * Binary data of a crash report file written when CrashReportFileVersion == 1.
 */
static MockBinaryFile MOCK_CRASH_REPORT_V1 =
    MockBinaryFile{}
        .UInt64(datadog::impl::CrashReportHeaderMagic)
        .UInt64(1)              // version
        .UInt64(11)             // fault_code (SIGSEGV)
        .UInt64(0x0)            // fault_address (null deference)
        .UInt64(0)              // fault_flags
        .UInt64(100)            // pid
        .UInt64(101)            // tid
        .UInt64(1700000000000)  // timestamp
        .UInt64(datadog::impl::CrashReportModuleMagic)
        .UInt64(0x100000)  // start_address
        .UInt64(0x200000)  // end_address
        .String("/foo")    // path
        .String("abc")     // build_id
        .UInt64(datadog::impl::CrashReportModuleMagic)
        .UInt64(0x300000)  // start_address
        .UInt64(0x400000)  // end_address
        .String("/bar")    // path
        .String("")        // build_id
        .UInt64(datadog::impl::CrashReportStackFrameMagic)
        .UInt64(0x100100)  // raw_address
        .UInt64(datadog::impl::CrashReportStackFrameMagic)
        .UInt64(0x100200)  // raw_address
        .UInt64(datadog::impl::CrashReportStackFrameMagic)
        .UInt64(0x100300)  // raw_address
        .UInt64(datadog::impl::CrashReportStackFrameMagic)
        .UInt64(0x100400)  // raw_address
        .UInt64(datadog::impl::CrashReportFooterMagic);

/**
 * Binary data of a crash context file written when CrashContextFileVersion == 1.
 *
 * Used to verify that the reader accepts V1 files and defaults user_anonymous_id to
 * UUID::Zero.
 */
static MockBinaryFile MOCK_CRASH_CONTEXT_V1 =
    MockBinaryFile{}
        .UInt64(datadog::impl::CrashContextHeaderMagic)
        .UInt64(1)                    // version
        .String("mock-service")       // service
        .String("mock-env")           // env
        .String("1.2.3")              // application_version
        .String("Debug")              // variant
        .String("cpp")                // source
        .String("2.0.0")              // sdk_version
        .UInt8(2)                     // tracking_consent (2: Pending)
        .String("mock-os")            // os_name
        .String("2.3.4")              // os_version
        .String("mock-build-number")  // os_build
        .String("2")                  // os_version_major
        .String("desktop")            // device_type
        .String("mock-device")        // device_name
        .String("mock-model")         // device_model
        .String("mock-brand")         // device_brand
        .String("x86_64")             // device_architecture
        .String("en-US")              // device_locale
        .String("America/New_York")   // device_time_zone
        .String("usr-123")            // user_id
        .String("Alice")              // user_name
        .String("alice@example.com")  // user_email
        // user_extra (empty object)
        .UInt8(static_cast<uint8_t>(datadog::ValueType::Object))
        .UInt64(0)
        .String("acct-456")   // account_id
        .String("Acme Corp")  // account_name
        // account_extra: (empty object)
        .UInt8(static_cast<uint8_t>(datadog::ValueType::Object))
        .UInt64(0)
        .UUID("a991ca10-4004-4004-4004-beefbeefbeef")  // rum_application_id
        .UUID("5e551017-4114-4114-4114-beeeefbeeeef")  // rum_session_id
        .UInt8(1)                                      // rum_session_is_sampled
        .UInt8(1)                                      // rum_session_is_active
        .UInt8(0)                                      // rum_session_is_initial
        .UInt8(1)                                      // rum_session_has_any_view
        .UInt8(0)                                      // rum_session_has_replay
        .String(R"({"type":"view"})")                  // last_view_event_json
        // global_rum_attributes ({"plan": "gold"})
        .UInt8(static_cast<uint8_t>(datadog::ValueType::Object))
        .UInt64(1)
        .String("plan")
        .UInt8(static_cast<uint8_t>(datadog::ValueType::String))
        .String("gold")
        .UInt64(datadog::impl::CrashContextFooterMagic);

/**
 * Binary data of a crash context file written when CrashContextFileVersion == 2.
 *
 * Identical to V1 except the version field is 2 and user_anonymous_id (16 bytes) is
 * encoded immediately after user_email.
 */
static MockBinaryFile MOCK_CRASH_CONTEXT_V2 =
    MockBinaryFile{}
        .UInt64(datadog::impl::CrashContextHeaderMagic)
        .UInt64(2)                                     // version
        .String("mock-service")                        // service
        .String("mock-env")                            // env
        .String("1.2.3")                               // application_version
        .String("Debug")                               // variant
        .String("cpp")                                 // source
        .String("2.0.0")                               // sdk_version
        .UInt8(2)                                      // tracking_consent (2: Pending)
        .String("mock-os")                             // os_name
        .String("2.3.4")                               // os_version
        .String("mock-build-number")                   // os_build
        .String("2")                                   // os_version_major
        .String("desktop")                             // device_type
        .String("mock-device")                         // device_name
        .String("mock-model")                          // device_model
        .String("mock-brand")                          // device_brand
        .String("x86_64")                              // device_architecture
        .String("en-US")                               // device_locale
        .String("America/New_York")                    // device_time_zone
        .String("usr-123")                             // user_id
        .String("Alice")                               // user_name
        .String("alice@example.com")                   // user_email
        .UUID("f47ac10b-58cc-4372-a567-0e02b2c3d479")  // user_anonymous_id
        // user_extra (empty object)
        .UInt8(static_cast<uint8_t>(datadog::ValueType::Object))
        .UInt64(0)
        .String("acct-456")   // account_id
        .String("Acme Corp")  // account_name
        // account_extra: (empty object)
        .UInt8(static_cast<uint8_t>(datadog::ValueType::Object))
        .UInt64(0)
        .UUID("a991ca10-4004-4004-4004-beefbeefbeef")  // rum_application_id
        .Float(55.5f)                                  // rum_session_sample_rate
        .UUID("5e551017-4114-4114-4114-beeeefbeeeef")  // rum_session_id
        .UInt8(1)                                      // rum_session_is_sampled
        .UInt8(1)                                      // rum_session_is_active
        .UInt8(0)                                      // rum_session_is_initial
        .UInt8(1)                                      // rum_session_has_any_view
        .UInt8(0)                                      // rum_session_has_replay
        .String(R"({"type":"view"})")                  // last_view_event_json
        // global_rum_attributes ({"plan": "gold"})
        .UInt8(static_cast<uint8_t>(datadog::ValueType::Object))
        .UInt64(1)
        .String("plan")
        .UInt8(static_cast<uint8_t>(datadog::ValueType::String))
        .String("gold")
        .UInt64(datadog::impl::CrashContextFooterMagic);
