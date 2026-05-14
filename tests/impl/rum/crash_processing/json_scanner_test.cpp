// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/json_scanner.hpp"

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

std::string_view span_substr(const JsonScanner& scanner, JsonScanner::Span span) {
  if (!span.OK()) {
    return "";
  }
  REQUIRE(span.i < scanner.s.size());
  REQUIRE(span.i + span.len <= scanner.s.size());
  return scanner.s.substr(span.i, span.len);
}

TEST_CASE("JsonScanner", "[unit][rum]") {
  SECTION("SkipValue") {
    SECTION("M succeed W value is a JSON literal") {
      // Given a JsonScanner positioned at a variety of well-formed JSON literal values
      auto s = GENERATE(
          as<std::string_view>(),
          "null",
          "true",
          "false",
          "0",
          "-1.0",
          "3.333333333333333e+49",
          "\"hello\"",
          "\"\"",
          "\"he\\\"llo\"",
          "[null,true,[false]]",
          "[]",
          "{\"foo\":100,\"bar\":[{\"baz\":200}]}",
          "{}"
      );
      INFO("input value: " << s);
      JsonScanner scanner{s};

      // When we call SkipValue
      auto span = scanner.SkipValue();

      // Then the scanner remains in a valid state, the Span returned encompasses the
      // entire JSON literal value, and the scanner's position is advanced to just
      // beyond the value
      REQUIRE(scanner.OK());
      REQUIRE(span.OK());
      REQUIRE(span_substr(scanner, span) == s);
      REQUIRE(scanner.pos == s.size());
      REQUIRE(scanner.Peek() == '\0');
    }

    SECTION("M fail W value is not a JSON literal") {
      // Given a JsonScanner positioned to read a variety of strings that are not valid
      // JSON literals
      auto s = GENERATE(
          as<std::string_view>(),
          "",
          ",,,",
          ",{}",
          "}{",
          "tralse",
          "\"hello",
          "hello\"",
          "[null,true,",
          "{\"foo\":100,"
      );
      INFO("input value: " << s);
      JsonScanner scanner{s};

      // When we call SkipValue
      auto span = scanner.SkipValue();

      // Then the scanner is left in a failed state, and the returned Span is empty
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }
  }

  SECTION("SkipNameLiteral") {
    SECTION("M fail W value is not exact match") {
      // When we look for 'true', anything other than an exact match triggers failure
      auto s = GENERATE(as<std::string_view>(), "", "tru", "treu", "false");
      INFO("input value: " << s);
      JsonScanner scanner{s};
      auto span = scanner.SkipNameLiteral("true");
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }

    SECTION("M leave parser at next position W expected value appears as substring") {
      // When we look for true, we parse a match successfully regardless of what follows
      // it: in this case, the overall input string is not valid JSON, but that will be
      // determined by whatever checks for the next delimiter (e.g. ',', '}', ']') and
      // finds 'n' instead
      JsonScanner scanner{"truenull"};
      auto span = scanner.SkipNameLiteral("true");
      REQUIRE(scanner.OK());
      REQUIRE(span.OK());
      REQUIRE(span_substr(scanner, span) == "true");
      REQUIRE(scanner.Peek() == 'n');
    }
  }

  SECTION("SkipBoolLiteral") {
    SECTION("M succeed W value is true or false") {
      auto s = GENERATE(as<std::string_view>(), "true", "false");
      INFO("input value: " << s);
      JsonScanner scanner{s};
      auto span = scanner.SkipBoolLiteral();
      REQUIRE(scanner.OK());
      REQUIRE(span.OK());
      REQUIRE(span_substr(scanner, span) == s);
    }

    SECTION("M fail W value is anything other than true or false") {
      auto s = GENERATE(as<std::string_view>(), "null", "", "400", "\"true\"", "True");
      INFO("input value: " << s);
      JsonScanner scanner{s};
      auto span = scanner.SkipBoolLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }
  }

  SECTION("SkipNumberLiteral") {
    SECTION("M fail W number is structurally invalid") {
      auto s = GENERATE(
          as<std::string_view>(),
          "",    // empty
          ".",   // no integer part before decimal
          "-",   // bare minus, no digits
          "1.",  // decimal point with no following digit
          "1e",  // exponent keyword with no digits
          "1e+"  // exponent sign with no digits
      );
      INFO("input value: " << s);
      JsonScanner scanner{s};
      auto span = scanner.SkipNumberLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }

    SECTION("M succeed and advance past leading zero only W integer starts with zero") {
      // SkipNumberLiteral parses the minimal valid prefix and stops; it does not
      // reject leading zeros, leaving further delimiter validation to the caller
      JsonScanner scanner{"007"};
      auto span = scanner.SkipNumberLiteral();
      REQUIRE(scanner.OK());
      REQUIRE(span_substr(scanner, span) == "0");
      REQUIRE(scanner.Peek() == '0');
    }
  }

  SECTION("SkipStringLiteral") {
    SECTION("M fail W not positioned at a string literal") {
      // Literal `hello`, as opposed to `"hello"`, is not a valid string
      JsonScanner scanner{"hello"};
      auto span = scanner.SkipStringLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }

    SECTION("M not terminate on escaped quote") {
      // The sequence \" inside a string is an escape, not a closing quote
      JsonScanner scanner{R"("hel\"lo")"};
      auto span = scanner.SkipStringLiteral();
      REQUIRE(scanner.OK());
      REQUIRE(span_substr(scanner, span) == "\"hel\\\"lo\"");
      REQUIRE(scanner.Peek() == '\0');
    }

    SECTION("M not terminate after escaped backslash") {
      // \\ is a single escape unit (one backslash), so the " after it closes the string
      JsonScanner scanner{R"("\\")"};
      auto span = scanner.SkipStringLiteral();
      REQUIRE(scanner.OK());
      REQUIRE(span_substr(scanner, span) == "\"\\\\\"");
      REQUIRE(scanner.Peek() == '\0');
    }
  }

  SECTION("SkipArrayLiteral") {
    SECTION("M fail W not positioned at an array") {
      auto s = GENERATE(as<std::string_view>(), "", "{}", "null");
      INFO("input value: " << s);
      JsonScanner scanner{s};
      auto span = scanner.SkipArrayLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }

    SECTION("M fail W an array element is invalid") {
      // A failure inside SkipValue propagates out through the array loop
      JsonScanner scanner{"[tralse]"};
      auto span = scanner.SkipArrayLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }
  }

  SECTION("SkipObjectLiteral") {
    SECTION("M fail W not positioned at an object") {
      auto s = GENERATE(as<std::string_view>(), "", "[]", "null");
      INFO("input value: " << s);
      JsonScanner scanner{s};
      auto span = scanner.SkipObjectLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }

    SECTION("M fail W property key is not a string") {
      JsonScanner scanner{"{1:2}"};
      auto span = scanner.SkipObjectLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }

    SECTION("M fail W colon delimiter is absent") {
      JsonScanner scanner{R"({"a"2})"};
      auto span = scanner.SkipObjectLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }

    SECTION("M fail W property value is invalid") {
      // A failure inside SkipValue propagates out through the object loop
      JsonScanner scanner{R"({"a":tralse})"};
      auto span = scanner.SkipObjectLiteral();
      REQUIRE(!scanner.OK());
      REQUIRE(!span.OK());
    }
  }

  SECTION("EnterObject") {
    SECTION("M fail W not positioned at an object") {
      // If not positioned at an opening brace, returns false and triggers failure
      JsonScanner scanner{",{}"};
      auto ok = scanner.EnterObject();
      REQUIRE(!ok);
      REQUIRE(!scanner.OK());
    }
  }

  SECTION("TrySkipObjectPropertyKey") {
    SECTION("M return true and advance to value W exact key match found") {
      // Given a scanner positioned at the start of an object property named "foo"
      JsonScanner scanner{R"({"foo":100,"bar":200})"};
      scanner.Advance();

      // When we check to see if we're positioned at a property called "foo"
      const bool found = scanner.TrySkipObjectPropertyKey("foo");

      // Then we get a result of true, indicating a match
      REQUIRE(found);

      // And the scanner remains in a valid state
      REQUIRE(scanner.OK());

      // And the scanner has advanced beyond the key to the start of the value, allowing
      // us to parse the value associated with "foo"
      REQUIRE(scanner.Peek() == '1');
      auto value_span = scanner.SkipNumberLiteral();
      REQUIRE(scanner.OK());
      REQUIRE(value_span.OK());
      REQUIRE(span_substr(scanner, value_span) == "100");
      REQUIRE(scanner.Peek() == ',');
    }

    SECTION(
        "M return false, NOT trigger failure, NOT update position W no match found"
    ) {
      // Given a scanner positioned at index 1 in a variety of strings that do not match
      // the property key `"foo":`, regardless of whether they're valid JSON or not
      auto s = GENERATE(
          as<std::string_view>(),
          "{",
          "{\"bar\":100}",
          "{\"fooo\":100}",
          "{\"foo\"100}",
          "{\"foo:100}"
      );
      INFO("input value: " << s);
      JsonScanner scanner{s};
      scanner.Advance();

      // When we check to see if we're positioned at a property called "foo"
      const bool found = scanner.TrySkipObjectPropertyKey("foo");

      // Then we get a result of false, indicating no match
      REQUIRE(!found);

      // And the scanner remains in a valid state, still positioned at index 1
      REQUIRE(scanner.OK());
      REQUIRE(scanner.pos == 1);
    }
  }

  SECTION("Object value parsing") {
    SECTION(
        "M properly identify spans of literal values W checking specific properties"
    ) {
      // Given a JSON object value from which we want to parse the following properties:
      // - bar -> `"world"`
      // - baz -> `[{"ok":true}]`
      // - quux -> (not found)
      // - nested.x -> `100`
      // - nested.y -> `3.33333333333333333333333`
      JsonScanner scanner{
          R"({"foo":"hello","bar":"world","nested":{"x":100,"y":3.33333333333333333333333,"z":0},"baz":[{"ok":true}]})"
      };
      JsonScanner::Span got_bar{};
      JsonScanner::Span got_baz{};
      JsonScanner::Span got_quux{};
      JsonScanner::Span got_nested_x{};
      JsonScanner::Span got_nested_y{};

      // When we use BeginObject(), TrySkipObjectPropertyKey() branches, and
      // SkipObjectProperty() to traverse the value and identify the spans where various
      // values appear
      if (scanner.EnterObject()) {
        while (scanner.OK() && scanner.Peek() != '}') {
          if (scanner.TrySkipObjectPropertyKey("bar")) {
            got_bar = scanner.SkipStringLiteral();
          } else if (scanner.TrySkipObjectPropertyKey("baz")) {
            got_baz = scanner.SkipArrayLiteral();
          } else if (scanner.TrySkipObjectPropertyKey("quux")) {
            got_quux = scanner.SkipValue();
          } else if (scanner.TrySkipObjectPropertyKey("nested")) {
            // We expect "nested" to be an object; enter a nested loop to parse its
            // properties
            if (scanner.EnterObject()) {
              while (scanner.OK() && scanner.Peek() != '}') {
                if (scanner.TrySkipObjectPropertyKey("x")) {
                  got_nested_x = scanner.SkipNumberLiteral();
                } else if (scanner.TrySkipObjectPropertyKey("y")) {
                  got_nested_y = scanner.SkipNumberLiteral();
                } else {
                  // Unrecognized property of 'nested' object; skip it
                  scanner.SkipObjectProperty();
                }
                // Skip commas after properties of nested object
                scanner.SkipObjectPropertySeparator();
              }

              // If we've successfully reached the closing '}' of the nested object,
              // skip past it
              if (scanner.OK()) {
                scanner.Advance();
              }
            }
          } else {
            // Unrecognized top-level property; skip it
            scanner.SkipObjectProperty();
          }

          // Skip commas after properties of top-level object
          scanner.SkipObjectPropertySeparator();
        }

        // Skip past final '}'
        if (scanner.OK()) {
          scanner.Advance();
        }
      }

      // Then we identify the byte ranges for all expected literal values
      REQUIRE(span_substr(scanner, got_bar) == "\"world\"");
      REQUIRE(span_substr(scanner, got_baz) == "[{\"ok\":true}]");
      REQUIRE(!got_quux.OK());
      REQUIRE(span_substr(scanner, got_nested_x) == "100");
      REQUIRE(span_substr(scanner, got_nested_y) == "3.33333333333333333333333");
    }

    SECTION("M require delimiters around object properties") {
      // Use a helper function to traverse the given JSON value one object property at a
      // time, capturing no values, returning true if we successfully reach the end of
      // the string without triggering failure
      auto scan_object = [](std::string_view s) -> bool {
        JsonScanner scanner{s};
        if (scanner.EnterObject()) {
          while (scanner.OK() && scanner.Peek() != '}') {
            scanner.SkipObjectProperty();
            scanner.SkipObjectPropertySeparator();
          }
          if (scanner.OK()) {
            scanner.Advance();
          }
        }
        return scanner.OK() && scanner.pos == s.size();
      };

      // When we scan a valid object, Then we succeed
      REQUIRE(scan_object(R"({"foo":"hello","bar":"world"})") == true);

      // When we scan an object that's missing a closing brace, Then we fail
      REQUIRE(scan_object(R"({"foo":"hello","bar":"world")") == false);

      // When we scan an object that's missing commas between properties, Then we fail
      REQUIRE(scan_object(R"({"foo":"hello""bar":"world"})") == false);
    }
  }
}
