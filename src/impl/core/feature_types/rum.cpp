// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/feature_types/rum.hpp"

namespace datadog::impl {

DATADOG_JSON_STRUCT_IMPL(RumUserProperties)
DATADOG_JSON_STRUCT_IMPL(RumAccountProperties)
DATADOG_JSON_STRUCT_IMPL(RumConnectivityProperties)
DATADOG_JSON_STRUCT_IMPL(RumViewportProperties)
DATADOG_JSON_STRUCT_IMPL(RumSyntheticsProperties)
DATADOG_JSON_STRUCT_IMPL(RumCITestProperties)
DATADOG_JSON_STRUCT_IMPL(RumOSProperties)
DATADOG_JSON_STRUCT_IMPL(RumDeviceProperties)
DATADOG_JSON_STRUCT_IMPL_NS(RumViewEvent::Application, RumViewEvent__Application)
DATADOG_JSON_STRUCT_IMPL_NS(RumViewEvent::Session, RumViewEvent__Session)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Incidence, RumViewEvent__View__Incidence
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::PerfMetric, RumViewEvent__View__PerfMetric
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Performance::CLS::Rect,
    RumViewEvent__View__Performance__CLS__Rect
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Performance::CLS, RumViewEvent__View__Performance__CLS
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Performance::FCP, RumViewEvent__View__Performance__FCP
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Performance::FID, RumViewEvent__View__Performance__FID
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Performance::INP, RumViewEvent__View__Performance__INP
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Performance::LCP, RumViewEvent__View__Performance__LCP
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Performance::FBC, RumViewEvent__View__Performance__FBC
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Performance, RumViewEvent__View__Performance
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::View::Accessibility, RumViewEvent__View__Accessibility
)
DATADOG_JSON_STRUCT_IMPL_NS(RumViewEvent::View, RumViewEvent__View)
DATADOG_JSON_STRUCT_IMPL_NS(RumViewEvent::Display, RumViewEvent__Display)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::Internal::Session, RumViewEvent__Internal__Session
)
DATADOG_JSON_STRUCT_IMPL_NS(
    RumViewEvent::Internal::Configuration, RumViewEvent__Internal__Configuration
)
DATADOG_JSON_STRUCT_IMPL_NS(RumViewEvent::Internal, RumViewEvent__Internal)
DATADOG_JSON_STRUCT_IMPL(RumViewEvent)

}  // namespace datadog::impl
