// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/deterministic_sampler.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cinttypes>
#include <limits>
#include <random>

#include "datadog/impl/core/util/insecure_random_generator.hpp"

using namespace datadog;
using namespace datadog::impl;

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("DeterministicSampler", "[unit][tracing]") {
  SECTION("M derive the sampling seed W constructed with a UUID") {
    const UUID uuid = *UUID::Parse("458f9d68-e3ce-4540-b65a-b7834046fb18");
    constexpr uint64_t EXPECTED_SEED = 0xb7834046fb18;
    constexpr std::array<float, 7> SAMPLE_RATES = {
        1.0f,
        25.0f,
        50.0f,
        59.0f,
        60.0f,
        75.0f,
        99.0f,
    };

    for (const float sample_rate : SAMPLE_RATES) {
      DeterministicSampler uuid_sampler(uuid, sample_rate);
      DeterministicSampler seed_sampler(EXPECTED_SEED, sample_rate);

      REQUIRE(uuid_sampler.Sample() == seed_sampler.Sample());
    }
  }

  SECTION("M produce true W given known test values that should be sampled") {
    // These values are replicated from DeterministicSamplerTests.swift, originally
    // derived from dd-trace-go via browser SDK sampler.spec.ts
    REQUIRE(DeterministicSampler(5577006791947779410ull, 94.0509f).Sample() == true);
    REQUIRE(DeterministicSampler(15352856648520921629ull, 43.7714f).Sample() == true);
    REQUIRE(DeterministicSampler(3916589616287113937ull, 68.6823f).Sample() == true);
    REQUIRE(DeterministicSampler(894385949183117216ull, 30.0912f).Sample() == true);
    REQUIRE(DeterministicSampler(12156940908066221323ull, 46.889f).Sample() == true);
  }

  SECTION("M produce false W given known test values that should not be sampled") {
    REQUIRE(DeterministicSampler(9828766684487745566ull, 15.6519f).Sample() == false);
    REQUIRE(DeterministicSampler(4751997750760398084ull, 81.364f).Sample() == false);
    REQUIRE(DeterministicSampler(11199607447739267382ull, 38.0657f).Sample() == false);
    REQUIRE(DeterministicSampler(6263450610539110790ull, 21.8553f).Sample() == false);
    REQUIRE(DeterministicSampler(1874068156324778273ull, 36.0871f).Sample() == false);
  }

  SECTION("M unconditionally return true W sample rate is 100%") {
    REQUIRE(DeterministicSampler(0, 100.0f).Sample() == true);
    REQUIRE(
        DeterministicSampler(std::numeric_limits<uint64_t>::max(), 100.0f).Sample() ==
        true
    );
  }

  SECTION("M unconditionally return false W sample rate is 0%") {
    REQUIRE(DeterministicSampler(0, 0.0f).Sample() == false);
    REQUIRE(
        DeterministicSampler(std::numeric_limits<uint64_t>::max(), 0.0f).Sample() ==
        false
    );
  }

  SECTION("M sample in (fail-open) W seed is zero and rate > 0") {
    // A zero seed hashes to zero, which is below every positive threshold.
    REQUIRE(DeterministicSampler(0, 1.0f).Sample() == true);
    REQUIRE(DeterministicSampler(0, 50.0f).Sample() == true);
    REQUIRE(DeterministicSampler(0, 99.9f).Sample() == true);
  }

  SECTION("M return a RulePSR value derived from sample rate") {
    REQUIRE_THAT(
        DeterministicSampler(4ull, 100.0f).GetRulePSR(), WithinRel(1.0f, 0.001F)
    );
    REQUIRE_THAT(
        DeterministicSampler(56ull, 50.0f).GetRulePSR(), WithinRel(0.5f, 0.001F)
    );
    REQUIRE_THAT(
        DeterministicSampler(66ull, 42.91f).GetRulePSR(), WithinRel(0.4291f, 0.001F)
    );
    REQUIRE_THAT(
        DeterministicSampler(115ull, 0.0f).GetRulePSR(), WithinRel(0.0f, 0.001F)
    );
    REQUIRE_THAT(
        DeterministicSampler(156ull, 999999999.0f).GetRulePSR(), WithinRel(1.0f, 0.001F)
    );
    REQUIRE_THAT(
        DeterministicSampler(230ull, -888888888.0f).GetRulePSR(),
        WithinRel(0.0f, 0.001F)
    );
  }

  // Assuming independent uniformly distributed seeds, the worst-case probability
  // of a legitimate failure is approximately 2.54e-9 across all 10 sample rates.
  SECTION("M converge to rate W large enough seed universe") {
    constexpr std::size_t RUN_COUNT_PER_SAMPLE_RATE = 100000;
    constexpr std::size_t RANDOM_SAMPLE_RATES = 10;

    std::random_device random_device;
    std::mt19937 generator{random_device()};
    std::uniform_real_distribution<float> distribution{0.0F, 100.0F};

    for (std::size_t tries = 0; tries < RANDOM_SAMPLE_RATES; tries++) {
      const float sample_rate = distribution(generator);
      uint32_t sampled_count = 0;
      uint32_t not_sampled_count = 0;

      InsecureRandomUInt64Generator random;
      for (std::size_t i = 0; i < RUN_COUNT_PER_SAMPLE_RATE; i++) {
        if (DeterministicSampler(random.NextValue(), sample_rate).Sample()) {
          sampled_count++;
        } else {
          not_sampled_count++;
        }
      }

      float real_rate = static_cast<float>(sampled_count) /
                        static_cast<float>(sampled_count + not_sampled_count);
      INFO("Failure for sample rate: " << sample_rate);
      REQUIRE_THAT(real_rate, WithinAbs(sample_rate * 0.01f, 0.01F));
    }
  }

  SECTION("M generate expected sampler W combining with new sample rate") {
    // Seed has the sampling threshold at around 79.30.
    constexpr uint64_t SEED = 5577006791947779410ull;
    constexpr float PARENT_SAMPLE_RATE = 94.0509f;
    constexpr float REJECTING_CHILD_SAMPLE_RATE = 50.0f;
    constexpr float SAMPLING_CHILD_SAMPLE_RATE = 90.0f;

    DeterministicSampler parent(SEED, PARENT_SAMPLE_RATE);
    REQUIRE(parent.Sample() == true);

    const std::unique_ptr<ISampler> rejecting_combined =
        parent.CombineWith(REJECTING_CHILD_SAMPLE_RATE);
    const std::unique_ptr<ISampler> sampling_combined =
        parent.CombineWith(SAMPLING_CHILD_SAMPLE_RATE);

    REQUIRE(rejecting_combined != nullptr);
    REQUIRE(rejecting_combined->Sample() == false);
    REQUIRE_THAT(rejecting_combined->GetRulePSR(), WithinRel(0.4702545f, 0.001f));

    REQUIRE(sampling_combined != nullptr);
    REQUIRE(sampling_combined->Sample() == true);
    REQUIRE_THAT(sampling_combined->GetRulePSR(), WithinRel(0.8464581f, 0.001f));

    REQUIRE(parent.Sample() == true);
  }

  SECTION("M clamp child sample rate W combining with edge values") {
    constexpr uint64_t SEED = 5577006791947779410ull;
    constexpr float PARENT_SAMPLE_RATE = 94.0509f;

    DeterministicSampler parent(SEED, PARENT_SAMPLE_RATE);

    const std::unique_ptr<ISampler> zero = parent.CombineWith(0.0f);
    const std::unique_ptr<ISampler> hundred = parent.CombineWith(100.0f);
    const std::unique_ptr<ISampler> negative = parent.CombineWith(-100.0f);
    const std::unique_ptr<ISampler> maximum =
        parent.CombineWith(std::numeric_limits<float>::max());

    REQUIRE(zero->Sample() == false);
    REQUIRE_THAT(zero->GetRulePSR(), WithinAbs(0.0f, 0.0001f));

    REQUIRE(hundred->Sample() == true);
    REQUIRE_THAT(hundred->GetRulePSR(), WithinRel(0.940509f, 0.001f));

    REQUIRE(negative->Sample() == false);
    REQUIRE_THAT(negative->GetRulePSR(), WithinAbs(0.0f, 0.0001f));

    REQUIRE(maximum->Sample() == true);
    REQUIRE_THAT(maximum->GetRulePSR(), WithinRel(1.0f, 0.001f));
  }
}
