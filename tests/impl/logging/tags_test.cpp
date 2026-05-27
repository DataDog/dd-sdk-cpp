// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/tags.hpp"

#include "support/catch.hpp"

using namespace datadog::impl;

TEST_CASE("LoggerTags", "[unit][logging]") {
  using Result = LoggerTags::AddResult;

  // Given a LoggerTags buffer that initially holds no tags
  LoggerTags tags;
  REQUIRE(tags.Get() == "");

  // === Add() ===

  SECTION("M store key:value W Add is called with separate key and value") {
    REQUIRE(tags.Add("alpha", "1") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1");
  }

  SECTION("M concatenate entries with comma W Add called multiple times") {
    REQUIRE(tags.Add("alpha", "1") == Result::Accepted);
    REQUIRE(tags.Add("bravo", "2") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,bravo:2");
  }

  SECTION("M strip trailing colons W value passed to Add ends in one or more colons") {
    auto value = GENERATE("1", "1:", "1:::");
    REQUIRE(tags.Add("alpha", value) == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1");
  }

  SECTION("M strip trailing colons W key passed to Add ends in one or more colons") {
    auto key = GENERATE("alpha", "alpha:", "alpha:::");
    REQUIRE(tags.Add(key, "1") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1");
  }

  SECTION("M preserve colons W key passed to Add contains non-trailing colons") {
    REQUIRE(tags.Add("alpha:foo", "1") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:foo:1");
  }

  SECTION("M preserve colons W value passed to Add contains non-trailing colons") {
    REQUIRE(tags.Add("alpha", "foo:1") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:foo:1");
  }

  SECTION(
      "M convert to lowercase and sanitize to underscore W key passed to Add contains "
      "invalid characters"
  ) {
    REQUIRE(tags.Add("Alpha?!", "1") == Result::AcceptedWithSanitization);
    REQUIRE(tags.Get() == "alpha__:1");
  }

  SECTION(
      "M convert to lowercase and sanitize to underscore W value passed to Add "
      "contains invalid characters"
  ) {
    REQUIRE(tags.Add("alpha", "<One>") == Result::AcceptedWithSanitization);
    REQUIRE(tags.Get() == "alpha:_one_");
  }

  SECTION("M reject new entry W Add called with reserved key") {
    auto key = GENERATE(
        "service",
        "version",
        "env",
        "sdk_version",
        "variant",
        "host",
        "device",
        "source"
    );
    REQUIRE(tags.Add(key, "1") == Result::RejectedAsReserved);
    REQUIRE(tags.Get() == "");
  }

  SECTION(
      "M reject new entry W Add called with key whose first colon-delimited substring "
      "is a reserved key"
  ) {
    auto key = GENERATE("service:", "service:foo");
    REQUIRE(tags.Add(key, "1") == Result::RejectedAsReserved);
    REQUIRE(tags.Get() == "");
  }

  SECTION("M reject new entry W Add called with empty key") {
    auto key = GENERATE("", ":", "::");
    REQUIRE(tags.Add(key, "1") == Result::RejectedAsEmpty);
    REQUIRE(tags.Get() == "");
  }

  SECTION("M reject new entry W Add called with key not beginning with a letter") {
    REQUIRE(tags.Add("1", "alpha") == Result::RejectedAsNotBeginningAZ);
    REQUIRE(tags.Get() == "");
  }

  SECTION("M accept new entry W Add called with existing key but distinct value") {
    REQUIRE(tags.Add("alpha", "1") == Result::Accepted);
    REQUIRE(tags.Add("alpha", "2") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,alpha:2");
  }

  SECTION("M silently ignore new entry W Add called with existing key and value") {
    REQUIRE(tags.Add("alpha", "1") == Result::Accepted);
    REQUIRE(tags.Add("alpha", "1") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1");
  }

  SECTION("M store key-only tag W Add called with empty value") {
    auto key = GENERATE("alpha", "alpha:");
    REQUIRE(tags.Add(key, "") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha");

    SECTION(
        "M silentl ignore key-only tag W Add called with existing key, regardless of "
        "trailing colons"
    ) {
      REQUIRE(tags.Add("alpha:", "") == Result::Accepted);
      REQUIRE(tags.Add("alpha", ":") == Result::Accepted);
      REQUIRE(tags.AddEntry("alpha") == Result::Accepted);
      REQUIRE(tags.AddEntry("alpha:") == Result::Accepted);
      REQUIRE(tags.Get() == "alpha");
    }
  }

  // === AddEntry() ===

  SECTION("M store key:value W AddEntry is called with pre-formatted key:value") {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1");
  }

  SECTION("M concatenate entries with comma W AddEntry called multiple times") {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.AddEntry("bravo:2") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,bravo:2");
  }

  SECTION(
      "M strip trailing colons W value passed to AddEntry ends in one or more colons"
  ) {
    auto entry = GENERATE("alpha:1", "alpha:1:", "alpha:1:::");
    REQUIRE(tags.AddEntry(entry) == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1");
  }

  SECTION("M preserve colons W entry passed to AddEntry contains non-trailing colons") {
    REQUIRE(tags.AddEntry("alpha:foo:1") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:foo:1");
  }

  SECTION(
      "M convert to lowercase and sanitize to underscore W entry passed to AddEntry "
      "contains invalid characters"
  ) {
    REQUIRE(tags.AddEntry("Alpha?!:<One>") == Result::AcceptedWithSanitization);
    REQUIRE(tags.Get() == "alpha__:_one_");
  }

  SECTION("M reject new entry W AddEntry called with reserved key") {
    auto entry = GENERATE(
        "service",
        "service:foo:1",
        "service:1",
        "version:1",
        "env:1",
        "sdk_version:1",
        "variant:1",
        "host:1",
        "device:1",
        "source:1"
    );
    REQUIRE(tags.AddEntry(entry) == Result::RejectedAsReserved);
    REQUIRE(tags.Get() == "");
  }

  SECTION("M reject new entry W AddEntry called with empty key") {
    auto entry = GENERATE("", ":1", "::1");
    REQUIRE(tags.AddEntry(entry) == Result::RejectedAsEmpty);
    REQUIRE(tags.Get() == "");
  }

  SECTION("M reject new entry W AddEntry called with key not beginning with a letter") {
    REQUIRE(tags.AddEntry("1:alpha") == Result::RejectedAsNotBeginningAZ);
    REQUIRE(tags.Get() == "");
  }

  SECTION("M accept new entry W AddEntry called with existing key but distinct value") {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.AddEntry("alpha:2") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,alpha:2");
  }

  SECTION("M silently ignore new entry W AddEntry called with existing key and value") {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1");
  }

  SECTION("M store key-only tag W Entry called with valueless tag") {
    auto entry = GENERATE("alpha", "alpha:");
    REQUIRE(tags.AddEntry(entry) == Result::Accepted);
    REQUIRE(tags.Get() == "alpha");

    SECTION(
        "M silently ignore key-only tag W AddEntry called with existing value, "
        "regardless of trailing colons"
    ) {
      REQUIRE(tags.AddEntry("alpha") == Result::Accepted);
      REQUIRE(tags.AddEntry("alpha:") == Result::Accepted);
      REQUIRE(tags.Add("alpha:", "") == Result::Accepted);
      REQUIRE(tags.Add("alpha", ":") == Result::Accepted);
      REQUIRE(tags.Get() == "alpha");
    }
  }

  // === Limits on Add() / AddEntry() ===

  SECTION("M reject new tag W at capacity of 100 tags") {
    // Given a set of 100 existing tags
    for (size_t i = 0; i < 100; i++) {
      std::string entry = "tag-" + std::to_string(i) + ":foo";
      REQUIRE(tags.AddEntry(entry) == Result::Accepted);
    }

    // When we try to add a 101st entry via either Add or AddEntry
    Result res;
    auto with_add_entry = GENERATE(false, true);
    if (with_add_entry) {
      res = tags.AddEntry("tag-100:foo");
    } else {
      res = tags.Add("tag-100", "foo");
    }

    // Then the operation fails
    REQUIRE(res == Result::RejectedDueToTagLimit);
  }

  SECTION("M accept tags W length of entry is exactly 200 bytes") {
    // Adding a tag with 200 'a' characters and a tag with 200 'b' characters work fine
    std::string aaaa(200, 'a');
    std::string bbbb(200, 'b');
    REQUIRE(tags.AddEntry(aaaa) == Result::Accepted);
    REQUIRE(tags.AddEntry(bbbb) == Result::Accepted);
    REQUIRE(tags.Get() == aaaa + "," + bbbb);
  }

  SECTION("M truncate tags W length of entry exceeds 200 bytes") {
    // When we add a tag with 201 'a' characters, then the value is stored truncated at
    // 200 bytes
    std::string aaaa(201, 'a');
    REQUIRE(tags.AddEntry(aaaa) == Result::AcceptedWithTruncation);
    REQUIRE(tags.Get() == std::string_view{aaaa.data(), 200});

    // And a subsequent attempt to add a tag with the same initial 200 bytes will
    // consider the input values identical and leave the set unmodified, since the value
    // we _would_ store already exists
    REQUIRE(tags.AddEntry(aaaa + "bcdef") == Result::AcceptedWithTruncation);
    REQUIRE(tags.Get() == std::string_view{aaaa.data(), 200});
  }

  // === RemoveEntry() ===

  SECTION("M remove single entry W RemoveEntry is called with matching key:value") {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.AddEntry("bravo:2") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,bravo:2");

    tags.RemoveEntry("alpha:1");
    REQUIRE(tags.Get() == "bravo:2");
  }

  SECTION(
      "M leave existing entries with same key but different value intact W RemoveEntry "
      "is called"
  ) {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.AddEntry("bravo:2") == Result::Accepted);
    REQUIRE(tags.AddEntry("alpha:3") == Result::Accepted);
    REQUIRE(tags.AddEntry("alpha") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,bravo:2,alpha:3,alpha");

    tags.RemoveEntry("alpha:1");
    REQUIRE(tags.Get() == "bravo:2,alpha:3,alpha");
  }

  SECTION("M remove only exact match W RemoveEntry is called with valueless tag") {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.AddEntry("alpha") == Result::Accepted);
    REQUIRE(tags.AddEntry("alpha:2") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,alpha,alpha:2");

    tags.RemoveEntry("alpha");
    REQUIRE(tags.Get() == "alpha:1,alpha:2");
  }

  SECTION(
      "M remove matching entry W RemoveEntry is called with key:value that sanitizes "
      "to same stored value"
  ) {
    REQUIRE(tags.AddEntry("aLPhA?!:<one>") == Result::AcceptedWithSanitization);
    REQUIRE(tags.AddEntry("BRAVo:t w o") == Result::AcceptedWithSanitization);
    REQUIRE(tags.Get() == "alpha__:_one_,bravo:t_w_o");

    tags.RemoveEntry("AlpHa**:!ONE!");
    REQUIRE(tags.Get() == "bravo:t_w_o");
  }

  // === RemoveEntriesWithKey() ===

  SECTION("M remove entry W RemoveEntriesWithKey is called with matching key") {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.AddEntry("bravo:2") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,bravo:2");

    tags.RemoveEntriesWithKey("alpha");
    REQUIRE(tags.Get() == "bravo:2");
  }

  SECTION("M remove all entries with same key W RemoveEntriesWithKey is called") {
    REQUIRE(tags.AddEntry("alpha:1") == Result::Accepted);
    REQUIRE(tags.AddEntry("bravo:2") == Result::Accepted);
    REQUIRE(tags.AddEntry("alpha:3") == Result::Accepted);
    REQUIRE(tags.AddEntry("alpha") == Result::Accepted);
    REQUIRE(tags.Get() == "alpha:1,bravo:2,alpha:3,alpha");

    tags.RemoveEntriesWithKey("alpha");
    REQUIRE(tags.Get() == "bravo:2");
  }

  SECTION(
      "M remove all matching entries W RemoveEntriesWithKey is called with key that "
      "sanitizes to same stored key"
  ) {
    REQUIRE(tags.AddEntry("aLPhA?!:<one>") == Result::AcceptedWithSanitization);
    REQUIRE(tags.AddEntry("BRAVo:t w o") == Result::AcceptedWithSanitization);
    REQUIRE(tags.AddEntry("alpha  :222") == Result::AcceptedWithSanitization);
    REQUIRE(tags.AddEntry("alpha:333") == Result::Accepted);
    REQUIRE(tags.AddEntry("ALPHA**") == Result::AcceptedWithSanitization);
    REQUIRE(tags.Get() == "alpha__:_one_,bravo:t_w_o,alpha__:222,alpha:333,alpha__");

    tags.RemoveEntriesWithKey("AlpHa**");
    REQUIRE(tags.Get() == "bravo:t_w_o,alpha:333");
  }
}
