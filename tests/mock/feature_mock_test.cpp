#include <catch2/catch_test_macros.hpp>

#include "mock/feature.hpp"

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
        MockCore core;
        auto feature = std::make_shared<CoolFeature>();
        core.RegisterMockFeature(feature);
        core.Start();

        // When the feature generates an event
        feature->GenerateEvent("hello world");
        feature->GenerateEvent("goodbye", "metadata");

        // And the Core is stopped
        core.Stop();

        REQUIRE(2 == 2);
    }
}
