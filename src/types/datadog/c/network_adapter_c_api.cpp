// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include "datadog/network_adapter.h"

#include "datadog/c/network_adapter_glue.hpp"

extern "C" {

void dd_network_adapter_destroy(dd_network_adapter_t* adapter) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete adapter;
}
}
