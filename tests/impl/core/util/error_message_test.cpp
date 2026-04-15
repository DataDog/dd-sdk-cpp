// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/util/error_message.hpp"

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

static ErrorMessage failing_func() {
  return ErrorMessage("oh no", {{"foo", Attribute::String("hello")}});
}

TEST_CASE("ErrorMessage", "[unit][error]") {
  SECTION("M render simple string W initialized without attributes") {
    ErrorMessage err("something went wrong");
    REQUIRE(err.Format() == "something went wrong");
  }

  SECTION("M append attribute values as JSON object W initialized with attributes") {
    ErrorMessage err(
        "something went wrong",
        {{"foo", Attribute::Int(100)},
         {"bar", Attribute::String("goodbye")},
         {"baz", Attribute::Bool(false)}}
    );
    REQUIRE(
        err.Format() ==
        R"(something went wrong {"foo":100,"bar":"goodbye","baz":false})"
    );
  }

  SECTION("M prepend prefix W existing message is wrapped via AddPrefix") {
    SECTION("{without attributes}") {
      ErrorMessage err("hello world");
      err = err.AddPrefix("Oh no");
      REQUIRE(err.Format() == "Oh no: hello world");
    }
    SECTION("{with attibutes}") {
      ErrorMessage err(
          "bad coordinates", {{"x", Attribute::Int(-1)}, {"y", Attribute::Int(-2)}}
      );
      err = err.AddPrefix("Failed");
      REQUIRE(err.Format() == R"(Failed: bad coordinates {"x":-1,"y":-2})");
    }
    SECTION("{multiple layers}") {
      ErrorMessage err = ErrorMessage("goodbye").AddPrefix("well").AddPrefix("hello");
      REQUIRE(err.Format() == "hello: well: goodbye");
    }
  }

  SECTION("M remain usable W stored from function return value") {
    ErrorMessage err = failing_func();
    REQUIRE(err.Format() == R"(oh no {"foo":"hello"})");
  }
}
