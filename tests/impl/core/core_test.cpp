// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/core.hpp"

#include <atomic>
#include <utility>

#include "datadog/impl/core/feature_message.hpp"

#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"
#include "mock/system_info.hpp"
#include "support/catch.hpp"
#include "support/core.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

static impl::Core _make_core(
    DiagnosticMessageBuffer& diagnostics,
    TrackingConsent initial_tracking_consent = TrackingConsent::Granted
) {
  auto fs = std::make_unique<MockFilesystem>();
  fs->Mkdirs("app");
  return impl::Core(
      CoreConfig("test-client-token", "initial-service", "initial-env")
          .SetApplicationStoragePath("app")
          .SetDiagnosticHandler(diagnostics.CreateHandler())
          .SetDiagnosticThreshold(datadog::DiagnosticLevel::Debug)
          .SetVersion("1.0.0")
          .SetBatchSize(BatchSize::Small)
          .SetUploadFrequency(UploadFrequency::Frequent)
          .SetBatchProcessingLevel(BatchProcessingLevel::Low),
      initial_tracking_consent,
      CoreSubsystems(
          std::make_unique<MockClock>(),
          std::move(fs),
          std::make_unique<MockHttpSubsystem>(),
          std::make_unique<MockSystemInfo>()
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
  // Given a buffer where we'll accumulate all diagnostic messages emitted
  DiagnosticMessageBuffer diagnostics;

  SECTION("M create Core in Uninitialized state W constructor called") {
    // When Core is constructed
    impl::Core core = _make_core(diagnostics);

    // Then the core should be in Uninitialized state
    // Note: We can't directly access _state, but we can infer state from behavior
    // Init() should succeed only from Uninitialized state
    REQUIRE(core.Init());
  }

  SECTION("M transition to Initialized state W Init called on Uninitialized core") {
    // Given an uninitialized core
    impl::Core core = _make_core(diagnostics);

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
    impl::Core core = _make_core(diagnostics);
    REQUIRE(core.Init());

    // When a feature is registered
    auto feature = std::make_shared<TestFeature>();
    bool result = core.RegisterFeature(feature);

    // Then registration should succeed
    REQUIRE(result);
  }

  SECTION("M reject feature registration W core is Uninitialized") {
    // Given an uninitialized core
    impl::Core core = _make_core(diagnostics);

    // When a feature is registered
    auto feature = std::make_shared<TestFeature>();
    bool result = core.RegisterFeature(feature);

    // Then registration should fail
    REQUIRE_FALSE(result);
  }

  SECTION("M reject feature registration W same feature ID registered twice") {
    // Given an initialized core with one feature
    impl::Core core = _make_core(diagnostics);
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
    impl::Core core = _make_core(diagnostics);
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
    impl::Core core = _make_core(diagnostics);
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
    impl::Core core = _make_core(diagnostics);

    // When Start() is called
    bool result = core.Start();

    // Then Start should fail
    REQUIRE_FALSE(result);
  }

  SECTION("M fail to start W no features registered") {
    // Given an initialized core with no features
    impl::Core core = _make_core(diagnostics);
    REQUIRE(core.Init());

    // When Start() is called
    bool result = core.Start();

    // Then Start should fail
    REQUIRE_FALSE(result);
  }

  SECTION("M return to Initialized state W Stop called on Started core") {
    // Given a started core
    impl::Core core = _make_core(diagnostics);
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
    impl::Core core = _make_core(diagnostics);
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
    impl::Core core = _make_core(diagnostics);
    REQUIRE(core.Init());

    // When Stop() is called
    core.Stop();

    // Then it should have no effect and not crash
  }

  SECTION("M accept tracking consent changes W called in any state") {
    // Given a core in various states
    impl::Core core = _make_core(diagnostics);

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
      impl::Core core = _make_core(diagnostics);
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
    impl::Core core = _make_core(diagnostics);
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

/**
 * Feature that counts received ContextChangedMessages via MakeMessageHandler(), and
 * triggers a context update (causing one such message to be dispatched) each time the
 * core starts.
 */
class MessageHandlerFeature : public MockFeature {
 public:
  std::atomic<int> messages_received{0};
  std::optional<CoreContext> initial_context;
  std::optional<CoreContext> final_context;

  MessageHandlerFeature()
      : MockFeature(CreateFeatureId("MHFT"), "message_handler_test") {}

  std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() override {
    auto weak = weak_from_this();
    return [weak](const FeatureMessage& msg) {
      if (auto self = std::static_pointer_cast<MessageHandlerFeature>(weak.lock())) {
        self->messages_received.fetch_add(1);

        // On ContextChangedMessage, cache the CoreContext snapshot in our feature state
        const auto* context_changed = std::get_if<ContextChangedMessage>(&msg);
        if (!context_changed) {
          return;
        }
        if (!self->initial_context.has_value()) {
          self->initial_context.emplace(context_changed->context);
        }
        self->final_context.emplace(context_changed->context);
      }
    };
  }

  void Start() override {
    MockFeature::Start();
    // Trigger a context update so that a ContextChangedMessage is dispatched to
    // all registered handlers on each start
    _scope->UpdateContext([](CoreContext& ctx) { ctx.rum.emplace(); });
  }
};

TEST_CASE("Core Messaging", "[unit]") {
  DiagnosticMessageBuffer diagnostics;

  SECTION(
      "M deliver messages to handler after restart W core is stopped and "
      "started again"
  ) {
    impl::Core core = _make_core(diagnostics);
    REQUIRE(core.Init());

    auto feature = std::make_shared<MessageHandlerFeature>();
    REQUIRE(core.RegisterFeature(feature));

    // First run: Core::Start() buffers an initial ContextChangedMessage before
    // OnCoreStarted(); MessageHandlerFeature::Start() enqueues a second via
    // UpdateContext(). Both are delivered before Stop() returns.
    REQUIRE(core.Start());
    core.Stop();
    REQUIRE(feature->messages_received.load() == 2);
    REQUIRE(feature->initial_context.has_value());
    REQUIRE(feature->initial_context->tracking_consent == TrackingConsent::Granted);

    // Second run: both messages repeat; the handler remains registered across restarts
    REQUIRE(core.Start());
    core.Stop();
    REQUIRE(feature->messages_received.load() == 4);
  }

  SECTION("{tracking consent changes}") {
    // Given a variety of scenarios in which the TrackingConsent value meaningfully
    // changes from the initially-configured value
    auto [initial_tracking_consent, new_tracking_consent] = GENERATE(
        std::make_pair(TrackingConsent::Pending, TrackingConsent::Granted),
        std::make_pair(TrackingConsent::Pending, TrackingConsent::NotGranted),
        std::make_pair(TrackingConsent::Granted, TrackingConsent::NotGranted),
        std::make_pair(TrackingConsent::Granted, TrackingConsent::Pending),
        std::make_pair(TrackingConsent::NotGranted, TrackingConsent::Pending),
        std::make_pair(TrackingConsent::NotGranted, TrackingConsent::Granted)
    );

    // And a Core with the initial tracking consent value, and a single Feature
    impl::Core core = _make_core(diagnostics, initial_tracking_consent);
    REQUIRE(core.Init());
    auto feature = std::make_shared<MessageHandlerFeature>();
    REQUIRE(core.RegisterFeature(feature));

    SECTION("M broadcast initially-configured TrackingConsent value upon Start") {
      // When the core is started and then stopped (which flushes the message bus)
      REQUIRE(core.Start());
      core.Stop();

      // Then the feature has received a ContextChangedMessage that reflects the
      // tracking consent value we were originally configured with
      REQUIRE(feature->initial_context.has_value());
      REQUIRE(feature->initial_context->tracking_consent == initial_tracking_consent);
      REQUIRE(feature->final_context.has_value());
      REQUIRE(feature->final_context->tracking_consent == initial_tracking_consent);
    }

    SECTION("M broadcast new TrackingConsent value W changed before Start") {
      // When we call Core::SetTrackingConsent() prior to core start, changing to our
      // new tracking consent value
      core.SetTrackingConsent(new_tracking_consent);

      // And the core is started and then stopped (which flushes the message bus)
      REQUIRE(core.Start());
      core.Stop();

      // Then the feature has received a ContextChangedMessage that reflects the
      // up-to-date tracking consent value
      REQUIRE(feature->final_context.has_value());
      REQUIRE(feature->final_context->tracking_consent == new_tracking_consent);

      // And no prior message was sent with our initially-configured value, as the
      // tracking consent change took place before the core was started
      REQUIRE(feature->initial_context.has_value());
      REQUIRE(feature->initial_context->tracking_consent == new_tracking_consent);
    }

    SECTION("M broadcast new TrackingConsent value W changed after Start") {
      // When we call Core::SetTrackingConsent() after the core has started running
      REQUIRE(core.Start());
      core.SetTrackingConsent(new_tracking_consent);

      // And then stop the core (which flushes the message bus)
      core.Stop();

      // Then the feature has received a ContextChangedMessage that reflects the
      // up-to-date tracking consent value
      REQUIRE(feature->final_context.has_value());
      REQUIRE(feature->final_context->tracking_consent == new_tracking_consent);

      // And that message arrived after the original ContextChangedMessage that
      // signalled our originally-configured TrackingConsent value at SDK start
      REQUIRE(feature->initial_context.has_value());
      REQUIRE(feature->initial_context->tracking_consent == initial_tracking_consent);
    }
  }
}
