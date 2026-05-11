// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>

#include "datadog/core.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_types/crash_reporting.hpp"
#include "datadog/impl/crash_reporting/data/crash_context.hpp"
#include "datadog/impl/crash_reporting/data/crash_report.hpp"

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
 */
static MockBinaryFile MOCK_CRASH_CONTEXT_V1 =
    MockBinaryFile{}
        .UInt64(datadog::impl::CrashContextHeaderMagic)
        .UInt64(1)                    // version
        .String("mock-service")       // service
        .String("mock-env")           // env
        .String("1.2.3")              // application_version
        .String("rum-cpp")            // source
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
        .UUID("a991ca10-4004-4004-4004-beefbeefbeef")  // rum_session_id
        .UInt8(1)                                      // rum_session_is_sampled
        .UInt8(1)                                      // rum_session_is_active
        .UInt8(0)                                      // rum_session_is_initial
        .UInt8(1)                                      // rum_session_has_any_view
        .String(R"({"type":"view"})")                  // last_view_event_json
        // global_rum_attributes ({"plan": "gold"})
        .UInt8(static_cast<uint8_t>(datadog::ValueType::Object))
        .UInt64(1)
        .String("plan")
        .UInt8(static_cast<uint8_t>(datadog::ValueType::String))
        .String("gold")
        .UInt64(datadog::impl::CrashContextFooterMagic);

/**
 * Returns a representative `CrashContext` value with all fields populated, suitable
 * for use as test fixture data for crash context serialization and deserialization
 * tests.
 */
static inline datadog::impl::CrashContext MakeMockCrashContext() {
  datadog::impl::CrashContext ctx;
  ctx.service = "mock-service";
  ctx.env = "mock-env";
  ctx.application_version = "1.2.3";
  ctx.source = "rum-cpp";
  ctx.sdk_version = "2.0.0";
  ctx.tracking_consent = datadog::TrackingConsent::Pending;
  ctx.os_name = "mock-os";
  ctx.os_version = "2.3.4";
  ctx.os_build = "mock-build-number";
  ctx.os_version_major = "2";
  ctx.device_type = "desktop";
  ctx.device_name = "mock-device";
  ctx.device_model = "mock-model";
  ctx.device_brand = "mock-brand";
  ctx.device_architecture = "x86_64";
  ctx.device_locale = "en-US";
  ctx.device_time_zone = "America/New_York";
  ctx.user_id = "usr-123";
  ctx.user_name = "Alice";
  ctx.user_email = "alice@example.com";
  ctx.user_extra = datadog::Attribute::Object();
  ctx.account_id = "acct-456";
  ctx.account_name = "Acme Corp";
  ctx.account_extra = datadog::Attribute::Object();
  ctx.rum_session_state.session_id =
      *datadog::UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
  ctx.rum_session_state.is_sampled = true;
  ctx.rum_session_state.is_active = true;
  ctx.rum_session_state.is_initial_session = false;
  ctx.rum_session_state.has_tracked_any_view = true;
  ctx.last_view_event_json = R"({"type":"view"})";
  datadog::Attribute global_rum_attributes = datadog::Attribute::Object();
  global_rum_attributes.SetObjectProperty("plan", datadog::Attribute::String("gold"));
  ctx.global_rum_attributes = global_rum_attributes;
  return ctx;
}
