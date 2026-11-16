// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace datadog::impl {

/**
 * Contains a finite set of custom tag values applied to a Logger.
 *
 * Entries are stored pre-encoded in a comma-delimited string. Each entry is a single
 * string matching `[a-z][a-z0-9_:/.-]*`, with any trailing colons stripped. An entry's
 * key is its first colon-delimited token.
 *
 * Entries are unique: if the set already contains `foo:value1`, adding `foo:value1`
 * will be a no-op. Keys may appear multiple times: `foo:value1,foo:value2` is valid.
 * Keys need not be followed by a value: e.g. `foo` is valid.
 *
 * Values are preemptively sanitized on Add, e.g. adding `Foo:Hello world`, `foo:`, and
 * `foo!:100` will result in `foo:hello_world,foo,foo_:100`.
 */
class LoggerTags {
  std::string buf;     // Comma-delimited string of pre-formatted key:value pairs
  size_t num_tags{0};  // Total number of tags currently stored

 public:
  /**
   * Returns a view of the comma-delimited set of tags currently held in the set.
   */
  std::string_view Get() const { return buf; }

  /**
   * Result of an attempt to add a new tag to the set.
   */
  enum class AddResult : uint8_t {
    Accepted,                  // Value accepted as-is (incl. no-op for existing value)
    AcceptedWithTruncation,    // Value was truncated at length limit but accepted
    AcceptedWithSanitization,  // Value was modified to fit acceptable charset
    RejectedAsEmpty,           // Value rejected because no key was specified
    RejectedAsNotBeginningAZ,  // Value rejected because key did not begin with [a-z]
    RejectedAsReserved,        // Value rejected because key is reserved
    RejectedDueToTagLimit,     // Value rejected: too many tags are already stored
  };

  /**
   * Sanitizes `key` and `value`, attempting to add a new entry `<key>:<value>`.
   */
  AddResult Add(std::string_view key, std::string_view value);

  /**
   * Sanitizes the given entry and attempts to add it to the set.
   */
  AddResult AddEntry(std::string_view entry);

  /**
   * Removes any existing entry whose sanitized form matches the sanitized form of the
   * given string.
   */
  void RemoveEntry(std::string_view entry);

  /**
   * Removes all existing entries whose sanitized key matches the sanitized form of the
   * given key.
   */
  void RemoveEntriesWithKey(std::string_view key);
};

}  // namespace datadog::impl
