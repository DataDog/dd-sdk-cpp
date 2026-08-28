// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>

/**
 * Binary layout of the crash context file (all integers encoded in native byte order):
 *
 * 0x0000: CrashContextHeaderMagic  uint64
 * 0x0008: version = 2              uint64
 *         service                  length-prefixed string
 *         env                      length-prefixed string
 *         application_version      length-prefixed string
 *         source                   length-prefixed string
 *         sdk_version              length-prefixed string
 *         tracking_consent         uint8 (0=Granted, 1=NotGranted, 2=Pending)
 *         os_name                  length-prefixed string
 *         os_version               length-prefixed string
 *         os_build                 length-prefixed string
 *         os_version_major         length-prefixed string
 *         device_type              length-prefixed string
 *         device_name              length-prefixed string
 *         device_model             length-prefixed string
 *         device_brand             length-prefixed string
 *         device_architecture      length-prefixed string
 *         device_locale            length-prefixed string
 *         device_time_zone         length-prefixed string
 *         user_id                  length-prefixed string (empty if not set)
 *         user_name                length-prefixed string (empty if not set)
 *         user_email               length-prefixed string (empty if not set)
 *         user_anonymous_id        16 raw bytes (UUID::Zero if not enabled or not set)
 *         user_extra               (see AttributeBinarySerialization)
 *         account_id               length-prefixed string (empty if not set)
 *         account_name             length-prefixed string (empty if not set)
 *         account_extra            (see AttributeBinarySerialization)
 *         rum_application_id       16 raw bytes (Zero if RUM state never broadcast)
 *         rum_session_sample_rate  float (IEEE 754, 4 bytes)
 *         rum_session_id           16 raw bytes (UUID::Zero if no session)
 *         rum_session_is_sampled   uint8
 *         rum_session_is_active    uint8
 *         rum_session_is_initial   uint8
 *         rum_session_has_any_view uint8
 *         rum_session_has_replay   uint8
 *         last_view_event_json     length-prefixed string (empty if no view)
 *         global_rum_attributes    (see AttributeBinarySerialization)
 *         CrashContextFooterMagic  uint64
 *
 * A length-prefixed string is a uint64 byte count followed by that many UTF-8 bytes
 * with no null terminator.
 */

namespace datadog::impl {

static const uint64_t CrashContextHeaderMagic = 0xdc01;
static const uint64_t CrashContextFileVersion = 2;

static const uint64_t CrashContextFooterMagic = 0xdcff;

}  // namespace datadog::impl
