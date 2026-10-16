// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>

#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Given a UUID used as the basis for deterministic sampling decisions, returns a 48-bit
 * SamplingSeed value obtained by extracting the RFC 4122 "node" field (bytes 10-15).
 */
uint64_t ExtractSamplingSeed(const UUID& uuid);

/**
 * Given a seed value and a sample rate in the range [0.0f..100.0f], makes a sampling
 * decision returns the result.
 *
 * If `true` ("sampled in"), the data that hinges upon this decision should be retained
 * and ultimately uploaded. If `false` ("sampled out"), the data should be dropped.
 *
 * This function uses Knuth multiplicative hashing, scaling the seed by a consistent
 * factor and comparing it against a threshold scaled by `sample_rate`. This process is
 * deterministic: it will always return the same result given the same seed and sample
 * rate.
 */
bool ShouldSample_Deterministic(uint64_t seed, float sample_rate);

}  // namespace datadog::impl
