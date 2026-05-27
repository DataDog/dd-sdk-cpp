// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/tags.hpp"

#include <algorithm>
#include <array>

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

// A single logger may have no more custom tags stored than this amount
static constexpr size_t MAX_TAGS_PER_LOGGER = 100;

// A custom tag's length in bytes (inclusive of '<key>:<value>', w/o terminator) may not
// exceed this value
static constexpr size_t MAX_TAG_ENTRY_LEN = 200;

/**
 * Defines the acceptable subset of characters for use in Logger tags. Tag values
 * (inclusive of both key and value) must consist only of [a-z0-9_:/.-].
 *
 * Given any character, returns a character contained within the acceptable subset.
 * Uppercase letters will be converted to lowercase; all invalid characters will be
 * converted to '_'.
 */
static char sanitize(const char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c + 32);
  }
  if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
    return c;
  }
  switch (c) {
    case '_':
    case ':':
    case '-':
    case '.':
    case '/':
      return c;
    default:
      return '_';
  }
}

/**
 * Given a sanitized (all-lowercase, trailing colons stripped) version of a key for a
 * custom tag, returns true if that key is reserved for internal use.
 */
static bool is_reserved_key(std::string_view sanitized_key) {
  // clang-format off
  // Internal values used in per-event ddtags
  if (sanitized_key == "service") { return true; }
  if (sanitized_key == "version") { return true; }
  if (sanitized_key == "env") { return true; }
  if (sanitized_key == "sdk_version") { return true; }
  if (sanitized_key == "variant") { return true; }
  // Additional values treated as reserved by mobile SDKs
  if (sanitized_key == "host") { return true; }
  if (sanitized_key == "device") { return true; }
  if (sanitized_key == "source") { return true; }
  // clang-format on
  return false;
}

/**
 * Scratch buffer used to hold a single input tag entry, formatted as either '<key>' or
 * '<key>:<value>'.
 *
 * Holding input values in a buffer allows us to sanitize them prior to performing
 * lookups. For example, when `AddTag("Foo:")` stores a sanitized value of `"foo"`, a
 * subsequent call to `RemoveTag("Foo:")` will properly sanitize the input string to
 * `"foo"` so that it matches the stored value as expected.
 *
 * Since tag entries are limited to 200 characters in total length, we can hold these
 * values on the stack.
 */
struct TagBuffer {
  std::array<char, MAX_TAG_ENTRY_LEN> s{};  // String data for our <key>:<value> entry
  size_t n{};                               // Length of that string

  /**
   * Returns the pre-formatted tag entry currently stored in this buffer.
   */
  std::string_view GetEntry() const { return std::string_view{s.data(), n}; }

  /**
   * Returns the key for the currently-stored tag, consisting of the first
   * colon-delimited token.
   */
  std::string_view GetKey() const {
    std::string_view entry = GetEntry();
    return entry.substr(0, entry.find(':'));
  }

  /**
   * Writes a sanitized version of the given <key>:<value> pair into the buffer,
   * returning either Accepted, AcceptedWithTruncation, or AcceptedWithSanitization.
   */
  LoggerTags::AddResult SanitizeAndStore(std::string_view key, std::string_view value) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)

    // Strip any trailing colons from the input string provided for the key: if the
    // application calls `AddTag("foo:", "bar")`, we want to store `foo:bar`, not
    // `foo::bar`
    while (!key.empty() && key.back() == ':') {
      key = key.substr(0, key.size() - 1);
    }

    // Stream key, ':', and value through the same per-character transformation,
    // stopping when our buffer is full
    n = 0;
    bool did_truncate = false;
    bool did_sanitize = false;
    auto append = [&](std::string_view sv) {
      for (const char c : sv) {
        if (n == s.size()) {
          did_truncate = true;
          break;
        }
        const char sanitized = sanitize(c);
        if (sanitized != c) {
          did_sanitize = true;
        }
        s[n++] = sanitized;
      }
    };
    append(key);
    if (n < s.size()) {
      s[n++] = ':';
    }
    append(value);

    // Strip any trailing colons
    while (n > 0 && s[n - 1] == ':') {
      --n;
    }

    // Return the appropriate status code so warnings can be propagated (we can only
    // accept values; all other validation belongs to LoggerTags)
    if (did_truncate) {
      return LoggerTags::AddResult::AcceptedWithTruncation;
    }
    if (did_sanitize) {
      return LoggerTags::AddResult::AcceptedWithSanitization;
    }
    return LoggerTags::AddResult::Accepted;

    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  }

  /**
   * Writes a sanitized version of a pre-formatted "<key>:<value>" entry into the
   * buffer, returning either Accepted, AcceptedWithTruncation, or
   * AcceptedWithSanitization.
   */
  LoggerTags::AddResult SanitizeAndStore(std::string_view entry) {
    // Split on colon and defer to our two-argument SanitizeAndStore overload
    const size_t first_colon_pos = entry.find(':');
    if (first_colon_pos == std::string_view::npos) {
      const std::string_view& key = entry;
      return SanitizeAndStore(key, "");
    }
    const std::string_view key = entry.substr(0, first_colon_pos);
    const std::string_view value = entry.substr(first_colon_pos + 1);
    return SanitizeAndStore(key, value);
  }
};

/**
 * Given a mutable std::string_view pointing to a set of comma-delimited tag values,
 * consumes the next entry from the set, while updating the view in the process to
 * advance to the start of the next entry.
 *
 * Once all values in the input string have been consumed, the view will be empty, and
 * subsequent calls will return an empty view as well.
 */
static std::string_view next_entry(std::string_view& mut_view) {
  // If our string_view is empty, there are no entries remaining
  if (mut_view.empty()) {
    return {};
  }

  // The next entry is the substring from the start of string up to the next comma, or
  // up to the end of the string if this is the last entry (no commas remain)
  const size_t comma_pos = mut_view.find(',');
  const std::string_view entry = mut_view.substr(0, comma_pos);

  // Mutate the string_view to advance it past the entry we just consumed
  if (comma_pos == std::string_view::npos) {
    // No entries remain
    mut_view = {};
  } else {
    // Next entry begins after the comma we just stopped at
    mut_view = mut_view.substr(comma_pos + 1);
  }

  // Return the <key>:<value> entry that we just consumed
  return entry;
}

/**
 * Given a mutable std::string containing a set of comma-delimited tag values, iterates
 * through those entries, mutating the string in the process such that any entries where
 * `pred(entry)` returns true are removed, decrementing `num_tags` for each entry
 * removed.
 *
 * Template-based predicate allows this same function to be specialized for matching on
 * key as well as matching on exact value.
 */
template <typename Pred>
static void remove_matching(std::string& buf, size_t& num_tags, Pred pred) {
  // Create a snapshot of our string before we've peformed any string mutation
  std::string_view view = buf;

  // As we iterate through the string looking for matches, we'll make two state changes
  // at each iteration:
  // 1. `next_entry` will advance `view` to the next comma-delimited entry
  // 2. Entries that are retained will be written back to `buf` contiguously, skipping
  //    any removed entries
  size_t buf_write_pos = 0;
  while (!view.empty()) {
    // Determine if this entry matches our criteria for removal
    const std::string_view entry = next_entry(view);
    const bool should_remove = pred(entry);

    // If so, skip the entry and decrement the total entry count
    if (should_remove) {
      DATADOG_ASSERT(num_tags > 0, "Attempting to decrement num_tags past 0");
      --num_tags;
      continue;
    }

    // Otherwise, the entry should remain in our set of tags: write it back to buf at
    // the current offset: this can never write beyond the original bounds or into the
    // region that we're still iterating over
    if (buf_write_pos > 0) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      buf[buf_write_pos++] = ',';
    }
    std::copy(entry.begin(), entry.end(), buf.data() + buf_write_pos);
    buf_write_pos += entry.size();
  }

  // Shrink the string to contain the newly-filtered contents
  const size_t new_buf_size = buf_write_pos;
  DATADOG_ASSERT(new_buf_size <= buf.size(), "tag removal caused buffer to grow");
  buf.resize(new_buf_size);
}

LoggerTags::AddResult LoggerTags::Add(std::string_view key, std::string_view value) {
  // Truncate and sanitize the input value so it conforms to our formatting requirements
  TagBuffer to_add;
  const auto sanitize_result = to_add.SanitizeAndStore(key, value);
  DATADOG_ASSERT(
      sanitize_result == AddResult::Accepted ||
          sanitize_result == AddResult::AcceptedWithTruncation ||
          sanitize_result == AddResult::AcceptedWithSanitization,
      "unexpected result from TagBuffer::SanitizeAndStore"
  );

  // Inspect the key under which the value will be stored, and validate that it meets
  // our requirements: it must be non-empty
  const std::string_view sanitized_key = to_add.GetKey();
  if (sanitized_key.empty()) {
    return AddResult::RejectedAsEmpty;
  }

  // A valid key must also begin with a letter A-Z (case-insensitive)
  const char first_char = sanitized_key.front();
  if (first_char < 'a' || first_char > 'z') {
    return AddResult::RejectedAsNotBeginningAZ;
  }

  // Custom tags may not use tag names that are used internally within ddtags
  // ('service', 'env', etc.)
  if (is_reserved_key(sanitized_key)) {
    return AddResult::RejectedAsReserved;
  }

  // We're mimicking a set where each '<key>:<value>' entry is a distinct item, so while
  // we permit multiple entries with the same key but different values, we need to
  // reject any attempt to add an entry that already exists verbatim
  const std::string_view sanitized_entry = to_add.GetEntry();
  std::string_view view = buf;
  while (!view.empty()) {
    const std::string_view existing_entry = next_entry(view);
    if (existing_entry == sanitized_entry) {
      // Tag already stored as described; consider it accepted as a no-op
      return sanitize_result;
    }
  }

  // Reject any attempt to add new tags beyond the limit
  if (num_tags >= MAX_TAGS_PER_LOGGER) {
    return AddResult::RejectedDueToTagLimit;
  }

  // Append the sanitized entry and increment our entry count
  if (!buf.empty()) {
    buf += ',';
  }
  buf += sanitized_entry;
  num_tags++;

  // Value accepted; propagate result of sanitization
  return sanitize_result;
}

LoggerTags::AddResult LoggerTags::AddEntry(std::string_view entry) {
  // Split key and value substrings from `entry` and defer to Add()
  const size_t first_colon_pos = entry.find(':');
  if (first_colon_pos == std::string_view::npos) {
    const std::string_view& key = entry;
    return Add(key, "");
  }
  const std::string_view key = entry.substr(0, first_colon_pos);
  const std::string_view value = entry.substr(first_colon_pos + 1);
  return Add(key, value);
}

void LoggerTags::RemoveEntry(std::string_view entry) {
  // Sanitize the input value so we're comparing against the normalized, lowercase,
  // trailing-colon-stripped version that would actually be stored if AddEntry() were
  // called with this input value
  TagBuffer to_remove;
  to_remove.SanitizeAndStore(entry);
  entry = to_remove.GetEntry();

  // Remove any entries that match that exact string
  remove_matching(buf, num_tags, [&entry](std::string_view e) { return e == entry; });
}

void LoggerTags::RemoveEntriesWithKey(std::string_view key) {
  // Sanitize the input key according to the same rules used to store it
  TagBuffer to_remove;
  to_remove.SanitizeAndStore(key);
  key = to_remove.GetKey();

  // Remove any entries that share the specified key
  remove_matching(buf, num_tags, [&key](std::string_view e) {
    std::string_view entry_key = e.substr(0, e.find(':'));
    return entry_key == key;
  });
}

}  // namespace datadog::impl
