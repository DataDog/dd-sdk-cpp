// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/version.hpp"

#include "core/version.hpp"

namespace datadog {

VersionInfo GetVersionInfo() {
  return VersionInfo{
      impl::SDK_VERSION, impl::GIT_REVISION_ID, impl::BUILD_ARTIFACT_NAME
  };
}

}  // namespace datadog
