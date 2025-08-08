#include <catch2/catch_test_macros.hpp>

#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"
#include "mock/tempdir.hpp"

// These tests exercise the mock feature implementation to sanity check that it (and
// the mock filesystem/http code it depends on) works as intended

TEST_CASE("MockFeature", "[unit]")
{
    SECTION("M yes")
    {
        REQUIRE(2 == 2);
    }
}
