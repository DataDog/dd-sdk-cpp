// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

#include "datadog/impl/json.hpp"

/**
 * Given a value of any JSON-serializable type T, serializes it with `EncodeJson` and
 * verifies that the resulting JSON is byte-for-byte identical to the expected value.
 */
template <typename T>
void RequireJsonLiteral(const T& value, std::string_view want) {
  std::vector<uint8_t> buffer;
  datadog::impl::EncodeJson<T>(buffer, value);
  std::string_view got(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  REQUIRE(got == want);
}

/**
 * Given a value of any JSON-serializable type T, serializes it with `EncodeJson`, then
 * parses the encoded value with nlohmann:json to verify that it's a valid JSON object,
 * then verifies that it is equivalent to the provided JSON object literal.
 */
template <typename T>
void RequireJsonObject(const T& value, std::string_view want_text) {
  auto want = nlohmann::json::parse(want_text);
  REQUIRE(want.is_object());

  std::vector<uint8_t> buffer;
  datadog::impl::EncodeJson<T>(buffer, value);
  std::string_view text(reinterpret_cast<const char*>(buffer.data()), buffer.size());

  auto got = nlohmann::json::parse(text);
  REQUIRE(got == want);
}

/**
 * Wrapping a raw string literal in `DATADOG_RUM_EVENT_LITERAL` indicates to the
 * `validate-event-types` script that the value constitutes a valid RUM event payload.
 *
 * When `validate-event-types` is run, it will find all such values and validate them
 * against the `rum-events-format` schema. This complementary validation step ensures
 * that if all unit tests pass, then we can conclude that the events produced by the SDK
 * under test are valid RUM events.
 *
 * RawStringLiteral MUST be a raw-string literal (e.g. `R"({"type":"view",...})"`)
 * containing the text of a JSON object literal. A limited set of basic template
 * variables can be substituted in order to allow fuzzy matches: see TemplateVar below.
 *
 * @see tools/validate-event-types/main.py
 */
#define DATADOG_RUM_EVENT_LITERAL(RawStringLiteral) RawStringLiteral

/**
 * Template variables that may be used in RUM event literals. e.g. wherever we see a
 * leaf string property with the magic value "${__NONZERO_UUID__}", we'll treat that as
 * a placeholder for 'any UUID value other than zero' and pass the assertion as long as
 * the object we got from our test contains such a value at that path.
 */
enum class TemplateVar : uint8_t {
  /**
   * In tests, any valid UUID besides 00000000-0000-0000-0000-000000000000 will be
   * accepted. In event validation, a random UUIDv4 value will be substituted.
   */
  NONZERO_UUID
};

/**
 * Examines a JSON value to determine whether it represents a template placeholder.
 */
inline std::optional<TemplateVar> ParseTemplateVar(const nlohmann::json& value) {
  static const std::string_view prefix = "${__";
  static const std::string_view suffix = "__}";

  // Only consider string values that match ${__*__}
  if (!value.is_string()) {
    return std::nullopt;
  }
  auto str = value.get<std::string_view>();
  if (str.find(prefix) != 0) {
    return std::nullopt;
  }
  if (str.rfind(suffix) != str.size() - suffix.size()) {
    return std::nullopt;
  }
  const size_t var_name_len = str.size() - prefix.size() - suffix.size();
  std::string_view var_name = str.substr(prefix.size(), var_name_len);

  // Parse the placeholder variable name
  if (var_name == "NONZERO_UUID") {
    return TemplateVar::NONZERO_UUID;
  }
  FAIL("Invalid TemplateVar name: " << var_name);
  // FAIL() is [[noreturn]], so this return is unreachable. It is still required
  // to satisfy compilers that cannot deduce this. MSVC (correctly) warns about
  // the unreachable code, so we disable C4702 around the return statement.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
  return std::nullopt;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

/**
 * Recursively traverses `want`, checking for property values like "${__SOME_VAR__}".
 * Where those values are found, they will be parsed as template variables, and the
 * corresponding value in `got` will be resolved.
 *
 * Whenever a valid template variable is found, the value from `got` will be evaluated
 * to determine whether it satisifes the constriant expressed by that template variable.
 * If so, that literal value will be written to `want` in place of the original template
 * placeholder.
 *
 * If no corresponding value exists at the same path within `got`, or if the value found
 * does not satisfy the constraints of the template variable, then the template variable
 * will simply be left as-is.
 *
 * If a template variable name is unrecognized, the current Catch2 test will fail
 * explicitly.
 */
inline void EvaluateTemplateVars(const nlohmann::json& got, nlohmann::json& want) {
  // Iterate over all top-level properties in `want`
  for (auto& [key, want_val] : want.items()) {
    // Get the corresponding value at the same path within `got`, falling back to a JSON
    // null value if no such property exists
    nlohmann::json got_val = nlohmann::json{nullptr};
    if (got.is_object() && got.contains(key)) {
      got_val = got.at(key);
    }

    // Check whether the current value in `want` is a string representing a template var
    auto template_var_opt = ParseTemplateVar(want_val);
    if (template_var_opt) {
      // If so, evaluate the rules for the given template var, copying the actual value
      // from `got` into `want` if there's a corresponding value that satifies the
      // constraint
      switch (*template_var_opt) {
        case TemplateVar::NONZERO_UUID: {
          if (got_val.is_string()) {
            auto uuid_opt = datadog::UUID::Parse(got_val.get<std::string_view>());
            if (uuid_opt) {
              want_val = uuid_opt->ToString();
            }
          }
        } break;
      }
    }

    // Recurse into subobjects, fully traversing `want` regardless of whether we're
    // still finding matching properties in `got`. Note that we _don't_ recurse into
    // arrays.
    if (want_val.is_object()) {
      EvaluateTemplateVars(got_val, want_val);
    }
  }
}

inline void RequireEventMatch(
    const nlohmann::json& got, std::string_view template_literal
) {
  // Require that `got` is a JSON object
  REQUIRE(got.is_object());

  // Parse the text of the desired JSON object, which may contain magic string values
  // that indicate we want to perform some basic template substitution to allow fuzzy
  // matches on the equivalent property in the `got` object
  auto want = nlohmann::json::parse(template_literal);
  REQUIRE(want.is_object());

  // Traverse our `want` object to identify placeholders and validate the corresponding
  // properties of `got`: if the value in `got` passes the checks required for that
  // template var, we'll then copy the value into `want` so it will pass a strict
  // equality check
  EvaluateTemplateVars(got, want);

  // Perform our strict equality check on the full object
  REQUIRE(got == want);
}
