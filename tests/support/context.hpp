// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/platform/system_info.hpp"

static const datadog::platform::OsInfo MOCK_OS_INFO{
    "mock-os", "2.3.4", "mock-build-number", "2"
};

static const datadog::platform::DeviceInfo MOCK_DEVICE_INFO{
    "desktop",
    "mock-device",
    "mock-model",
    "mock-brand",
    "x86_64",
    "en-US",
    "America/New_York"
};

static const datadog::CoreConfig MOCK_CONTEXT_CONFIG =
    datadog::CoreConfig{"mock-client-token", "mock-service", "mock-env"}
        .Internal_SetSource("mock-source")
        .Internal_SetSdkVersion("1.2.3");

static const datadog::impl::ImmutableContext MOCK_IMMUTABLE_CONTEXT{
    MOCK_CONTEXT_CONFIG, MOCK_OS_INFO, MOCK_DEVICE_INFO, "mock", "0.0.0"
};

static const datadog::impl::CoreContext MOCK_CONTEXT{
    MOCK_IMMUTABLE_CONTEXT, datadog::TrackingConsent::Pending
};
