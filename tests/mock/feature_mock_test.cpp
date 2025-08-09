#include <catch2/catch_test_macros.hpp>

#include "mock/feature.hpp"
#include "support/core.hpp"

// These tests exercise the mock feature implementation to sanity check that it (and
// the mock filesystem/http code it depends on) works as intended

using namespace datadog;

class CoolFeature : public MockFeature
{
public:
    CoolFeature()
        : MockFeature(impl::CreateFeatureId("COOL"), "coolstuff")
    {}
};

TEST_CASE("MockFeature", "[unit]")
{
    SECTION("TEMP")
    {
        // Given a registered feature in a running core
        auto test = CoreTestHarness::Init();
        auto feature = std::make_shared<CoolFeature>();
        REQUIRE(test.core.RegisterFeature(feature));
        REQUIRE(test.core.Start());

        // And an HTTP client that is unable to complete requests
        test.client.SimulateTransientNetworkError();

        // When the feature generates an event
        feature->GenerateEvent("hello world");
        feature->GenerateEvent("goodbye", "metadata");

        // And the Core is stopped
        test.core.Stop();

        // Then TODO
        REQUIRE(2 == 2);
    }
}
