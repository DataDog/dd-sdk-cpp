#include "core/core.hpp"

#include <catch2/catch_test_macros.hpp>

#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"
#include "support/core.hpp"

using namespace datadog;
using namespace datadog::impl;

static impl::Core _make_core() {
  return impl::Core(
      CoreConfig{
          TrackingConsent::Granted, Site::us1, "test-client-token", "initial-service",
          "initial-env", "1.0.0", BatchSize::Small, UploadFrequency::Frequent,
          BatchProcessingLevel::Low, 0
      },
      CoreSubsystems(
          std::make_unique<MockClock>(), std::make_unique<MockStorageDirectory>(),
          std::make_unique<MockHttpSubsystem>()
      )
  );
}

class TestFeature : public MockFeature {
 public:
  TestFeature() : MockFeature(CreateFeatureId("TEST"), "testfeature") {}
};

class NameConflictFeature : public MockFeature {
 public:
  NameConflictFeature()
      : MockFeature(CreateFeatureId("NAME"), "testfeature")  // same name as above
  {}
};

TEST_CASE("Core Lifecycle", "[unit]") {
  // CoreConfig struct to be used in tests
  const CoreConfig config{
      TrackingConsent::Granted,
      Site::us1,
      "test-client-token",
      "test-service",
      "test-env",
      "1.0.0",
      BatchSize::Small,
      UploadFrequency::Frequent,
      BatchProcessingLevel::Low,
      0,
  };

  SECTION("M create Core in Uninitialized state W constructor called") {
    // Given CoreSubsystems
    auto clock = std::make_unique<MockClock>();
    auto storage_root = std::make_unique<MockStorageDirectory>();
    auto http = std::make_unique<MockHttpSubsystem>();
    CoreSubsystems subsystems(
        std::move(clock), std::move(storage_root), std::move(http)
    );

    // When Core is constructed
    impl::Core core(config, std::move(subsystems));

    // Then the core should be in Uninitialized state
    // Note: We can't directly access _state, but we can infer state from behavior
    // Init() should succeed only from Uninitialized state
    REQUIRE(core.Init());
  }

  SECTION("M transition to Initialized state W Init called on Uninitialized core") {
    // Given an uninitialized core
    impl::Core core = _make_core();

    // When Init() is called
    bool result = core.Init();

    // Then Init should succeed
    REQUIRE(result);

    // And core should be in Initialized state (can register features)
    auto feature = std::make_shared<TestFeature>();
    REQUIRE(core.RegisterFeature(feature));
  }

  SECTION("M register feature successfully W core is Initialized") {
    // Given an initialized core
    impl::Core core = _make_core();
    REQUIRE(core.Init());

    // When a feature is registered
    auto feature = std::make_shared<TestFeature>();
    bool result = core.RegisterFeature(feature);

    // Then registration should succeed
    REQUIRE(result);
  }

  SECTION("M reject feature registration W core is Uninitialized") {
    // Given an uninitialized core
    impl::Core core = _make_core();

    // When a feature is registered
    auto feature = std::make_shared<TestFeature>();
    bool result = core.RegisterFeature(feature);

    // Then registration should fail
    REQUIRE_FALSE(result);
  }

  SECTION("M reject feature registration W same feature ID registered twice") {
    // Given an initialized core with one feature
    impl::Core core = _make_core();
    REQUIRE(core.Init());

    auto feature1 = std::make_shared<TestFeature>();
    REQUIRE(core.RegisterFeature(feature1));

    // When another feature with the same ID is registered
    auto feature2 = std::make_shared<TestFeature>();
    bool result = core.RegisterFeature(feature2);

    // Then registration should fail
    REQUIRE_FALSE(result);
  }

  SECTION("M reject feature registration W same feature name registered twice") {
    // Given an initialized core with one feature
    impl::Core core = _make_core();
    REQUIRE(core.Init());

    auto feature1 = std::make_shared<TestFeature>();
    REQUIRE(core.RegisterFeature(feature1));

    // When another feature with the same name is registered
    auto feature2 = std::make_shared<NameConflictFeature>();
    bool result = core.RegisterFeature(feature2);

    // Then registration should fail
    REQUIRE_FALSE(result);
  }

  SECTION("M start successfully W core is Initialized with registered features") {
    // Given an initialized core with a registered feature
    impl::Core core = _make_core();
    REQUIRE(core.Init());

    auto feature = std::make_shared<TestFeature>();
    REQUIRE(core.RegisterFeature(feature));

    // When Start() is called
    bool result = core.Start();

    // Then Start should succeed
    REQUIRE(result);

    // And core should be in Started state (features can't be registered)
    auto another_feature = std::make_shared<TestFeature>();
    REQUIRE_FALSE(core.RegisterFeature(another_feature));

    // Clean up
    core.Stop();
  }

  SECTION("M fail to start W core is Uninitialized") {
    // Given an uninitialized core
    impl::Core core = _make_core();

    // When Start() is called
    bool result = core.Start();

    // Then Start should fail
    REQUIRE_FALSE(result);
  }

  SECTION("M fail to start W no features registered") {
    // Given an initialized core with no features
    impl::Core core = _make_core();
    REQUIRE(core.Init());

    // When Start() is called
    bool result = core.Start();

    // Then Start should fail
    REQUIRE_FALSE(result);
  }

  SECTION("M return to Initialized state W Stop called on Started core") {
    // Given a started core
    impl::Core core = _make_core();
    REQUIRE(core.Init());

    auto feature = std::make_shared<TestFeature>();
    REQUIRE(core.RegisterFeature(feature));
    REQUIRE(core.Start());

    // When Stop() is called
    core.Stop();

    // Then core should return to Initialized state (can start again)
    REQUIRE(core.Start());
    core.Stop();
  }

  SECTION("M handle multiple Stop calls safely W Stop called repeatedly") {
    // Given a stopped core
    impl::Core core = _make_core();
    REQUIRE(core.Init());

    auto feature = std::make_shared<TestFeature>();
    REQUIRE(core.RegisterFeature(feature));
    REQUIRE(core.Start());
    core.Stop();

    // When Stop() is called again
    core.Stop();

    // Then it should have no effect and not crash
    // (This is documented behavior in the header)
  }

  SECTION("M handle Stop safely W called before Start") {
    // Given an initialized core that was never started
    impl::Core core = _make_core();
    REQUIRE(core.Init());

    // When Stop() is called
    core.Stop();

    // Then it should have no effect and not crash
  }

  SECTION("M accept tracking consent changes W called in any state") {
    // Given a core in various states
    impl::Core core = _make_core();

    // When SetTrackingConsent is called before Init
    core.SetTrackingConsent(TrackingConsent::NotGranted);
    // Then it should not crash

    REQUIRE(core.Init());

    // When SetTrackingConsent is called after Init
    core.SetTrackingConsent(TrackingConsent::Pending);
    // Then it should not crash

    auto feature = std::make_shared<TestFeature>();
    REQUIRE(core.RegisterFeature(feature));
    REQUIRE(core.Start());

    // When SetTrackingConsent is called while running
    core.SetTrackingConsent(TrackingConsent::Granted);
    // Then it should not crash

    core.Stop();
  }

  SECTION("M call Stop automatically W destructor invoked") {
    // Given a started core in a scope
    {
      impl::Core core = _make_core();
      REQUIRE(core.Init());

      auto feature = std::make_shared<TestFeature>();
      REQUIRE(core.RegisterFeature(feature));
      REQUIRE(core.Start());

      // When the core goes out of scope, destructor should call Stop()
      // This should not hang or crash
    }

    // Then we reach this point without issues
    REQUIRE(true);
  }

  SECTION("M notify registered features W starting and stopping") {
    // Given a core with a registered feature
    impl::Core core = _make_core();
    REQUIRE(core.Init());
    auto feature = std::make_shared<TestFeature>();
    REQUIRE(core.RegisterFeature(feature));

    REQUIRE(feature->num_start_calls == 0);
    REQUIRE(feature->num_stop_calls == 0);

    // When the core is started
    REQUIRE(core.Start());

    // Then OnCoreStarted() should be called
    REQUIRE(feature->num_start_calls == 1);
    REQUIRE(feature->num_stop_calls == 0);

    // Next: When the core is stopped
    core.Stop();

    // Then OnCoreStopping() should be called
    REQUIRE(feature->num_start_calls == 1);
    REQUIRE(feature->num_stop_calls == 1);
  }
}
