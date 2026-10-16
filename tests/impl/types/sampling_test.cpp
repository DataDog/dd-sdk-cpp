// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/sampling.hpp"

#include <cinttypes>
#include <limits>

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ExtractSamplingSeed", "[unit][sampling]") {
  const UUID uuid = *UUID::Parse("458f9d68-e3ce-4540-b65a-b7834046fb18");
  REQUIRE(ExtractSamplingSeed(uuid) == 0xb7834046fb18);
}

TEST_CASE("ShouldSample_Deterministic", "[unit][sampling]") {
  SECTION("M produce true W given known test values that should be sampled") {
    // These values are replicated from DeterministicSamplerTests.swift, originally
    // derived from dd-trace-go via browser SDK sampler.spec.ts
    REQUIRE(ShouldSample_Deterministic(5577006791947779410ull, 94.0509f) == true);
    REQUIRE(ShouldSample_Deterministic(15352856648520921629ull, 43.7714f) == true);
    REQUIRE(ShouldSample_Deterministic(3916589616287113937ull, 68.6823f) == true);
    REQUIRE(ShouldSample_Deterministic(894385949183117216ull, 30.0912f) == true);
    REQUIRE(ShouldSample_Deterministic(12156940908066221323ull, 46.889f) == true);
  }

  SECTION("M produce false W given known test values that should not be sampled") {
    REQUIRE(ShouldSample_Deterministic(9828766684487745566ull, 15.6519f) == false);
    REQUIRE(ShouldSample_Deterministic(4751997750760398084ull, 81.364f) == false);
    REQUIRE(ShouldSample_Deterministic(11199607447739267382ull, 38.0657f) == false);
    REQUIRE(ShouldSample_Deterministic(6263450610539110790ull, 21.8553f) == false);
    REQUIRE(ShouldSample_Deterministic(1874068156324778273ull, 36.0871f) == false);
  }

  SECTION("M unconditionally return true W sample rate is 100%") {
    REQUIRE(ShouldSample_Deterministic(0, 100.0f) == true);
    REQUIRE(
        ShouldSample_Deterministic(std::numeric_limits<uint64_t>::max(), 100.0f) == true
    );
  }

  SECTION("M unconditionally return false W sample rate is 0%") {
    REQUIRE(ShouldSample_Deterministic(0, 0.0f) == false);
    REQUIRE(
        ShouldSample_Deterministic(std::numeric_limits<uint64_t>::max(), 0.0f) == false
    );
  }

  SECTION("M sample in (fail-open) W seed is zero and rate > 0") {
    // A zero seed hashes to zero, which is below every positive threshold.
    REQUIRE(ShouldSample_Deterministic(0, 1.0f) == true);
    REQUIRE(ShouldSample_Deterministic(0, 50.0f) == true);
    REQUIRE(ShouldSample_Deterministic(0, 99.9f) == true);
  }
}
