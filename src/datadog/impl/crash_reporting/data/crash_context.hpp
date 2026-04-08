// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>

/**
 * Example layout:
 *
 * 0x0000: <CrashContextHeaderMagic>
 * 0x0008: version
 * 0x0010: rum_application_id
 * 0x0020: rum_session_id
 * 0x0030: rum_view_id
 * 0x0040: rum_action_id
 * 0x0050: <CrashContextFooterMagic>
 */

namespace datadog::impl {

static const uint64_t CrashContextHeaderMagic = 0xdc01;
static const uint64_t CrashContextFileVersion = 1;

static const uint64_t CrashContextFooterMagic = 0xdcff;

}  // namespace datadog::impl
