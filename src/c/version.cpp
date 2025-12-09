// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/version.h"

#include "core/version.hpp"

extern "C" {

dd_version_info_t dd_get_version_info() {
  return dd_version_info_t{
      DATADOG_BUILD_VERSION, DATADOG_GIT_REVISION_ID, DATADOG_BUILD_ARTIFACT_NAME
  };
}
}
