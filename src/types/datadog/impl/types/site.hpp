// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/core.hpp"

namespace datadog {

/**
 * Returns the short name of the given Datadog site (e.g. "us1"), as used in intake
 * hostnames, or an empty string if `value` isn't a known site.
 */
inline const char* Site_ToString(Site value) {
  switch (value) {
    case Site::us1:
      return "us1";
    case Site::us3:
      return "us3";
    case Site::us5:
      return "us5";
    case Site::eu1:
      return "eu1";
    case Site::ap1:
      return "ap1";
    case Site::ap2:
      return "ap2";
    case Site::us1_fed:
      return "us1_fed";
    case Site::us2_fed:
      return "us2_fed";
    case Site::uk1:
      return "uk1";
    default:
      return "";
  }
}

}  // namespace datadog
