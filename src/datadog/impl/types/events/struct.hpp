// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/types/json.hpp"

namespace datadog::impl {

/**
 * Result returned by TryEncodeJson for types annotated with
 * DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES.
 *
 * `bytes_written` is the number of bytes written to the output buffer.
 * `truncated` is true if one or more extra attributes were dropped because they did
 * not fit within the buffer. Attributes dropped solely because their name conflicted
 * with a reserved struct field name do not count as truncation.
 */
struct StructEncodeResult {
  size_t bytes_written;
  bool truncated;
};

// Type trait to detect pair-like types (types with .first and .second members)
template <typename T, typename = void>
struct is_pair_like : std::false_type {};

template <typename T>
struct is_pair_like<
    T,
    std::void_t<decltype(std::declval<T>().first), decltype(std::declval<T>().second)>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_pair_like_v = is_pair_like<std::decay_t<T>>::value;

/**
 * This file implements JSON serialization support for arbitrary data structures.
 *
 * Given any data structure:
 *
 *   struct MyEvent {
 *     std::string type;
 *     UUID id;
 *     Timestamp timestamp;
 *     std::optional<std::string> tags;
 *   };
 *
 * You can separately define how a value of that type should be serialized as a JSON
 * object, given a set of JSON-serializable member values:
 *
 *   DATADOG_JSON_STRUCT(
 *     MyEvent,
 *     DATADOG_JSON_FIELD(type),
 *     DATADOG_JSON_FIELD(id),
 *     DATADOG_JSON_FIELD(timestamp),
 *     DATADOG_JSON_FIELD_NAME(tags, "ddtags")
 *   )
 *
 * Property names will match the member variable name exactly. To explicitly specify a
 * different name, use `DATADOG_JSON_FIELD_NAME` and provide a string literal. Property
 * names are NOT escaped: they are assumed to contain only printable ASCII characters,
 * with no backslashes, double-quotes, or control codes.
 *
 * Once a type is annotated with `DATADOG_JSON_STRUCT`, you can serialize any value of
 * that type as a JSON object by passing it to `EncodeJson`:
 *
 *   std::vector<uint8_t> bytes;
 *   MyEvent ev{"foo", UUID::Random(), std::chrono::system_clock::now(), std::nullopt};
 *   EncodeJson(bytes, ev);
 *   std::cout << std::string_view(bytes.data(), bytes.size()) << "\n";
 *
 * The example above will print something like:
 *
 *   {"type":"foo","id":"e9d4be4a-9063-40bc-977d-b073e39d4105","timestamp":"2025-10-24T12:46:17.301Z","ddtags":null}
 *
 * Properties are guaranteed to be serialized in the order in which they were declared.
 * JSON values are guaranteed to be minified: no whitespace or pretty-printing.
 */

/**
 * For use within `DATADOG_JSON_STRUCT`: defines a member variable that should be
 * serialized as a JSON object property. Evaluates to a `std::pair<std::string_view, T>`
 * value. The variadic list of types formed by all such values will match the
 * `template <typename... Fields>` overloads defined below.
 *
 * `Member` is the name of the chosen member variable.
 *
 * `Name` is a string literal denoting the JSON property name to associate with this
 * member. The string value must be unique among all fields of the same struct, and it
 * must contain no double-quotes, backslashes, or control codes.
 */
#define DATADOG_JSON_FIELD_NAME(Member, Name) \
  std::make_pair(std::string_view(Name), (obj.Member))

/**
 * Shorthand used in `DATADOG_JSON_STRUCT` to define JSON-serializable fields for member
 * variables whose names exactly match the desired JSON property name.
 */
#define DATADOG_JSON_FIELD(Member) DATADOG_JSON_FIELD_NAME(Member, #Member)

/**
 * May be added to a field list within `DATADOG_JSON_STRUCT` in order to reserve a field
 * name for future use. When used within `DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES`,
 * any extra attributes with this field's name will be filtered out of the final object.
 */
#define DATADOG_JSON_RESERVED_FIELD(Member) \
  std::make_pair(std::string_view(#Member), std::monostate{})

/**
 * Defines the JSON object format used to serialize a value of type `Type`.
 *
 * Declares inline definitions of `GetJsonSize` and `WriteJson` for the given type.
 * Those implementations forward to compiler-generated overloads of the
 * `template <typename... Fields>` functions defined below, based on the set of fields
 * defined via `DATADOG_JSON_FIELD`.
 */
#define DATADOG_JSON_STRUCT(Type, ...)                                            \
  inline size_t GetJsonSize(const Type& obj) { return GetJsonSize(__VA_ARGS__); } \
  inline size_t WriteJson(char* dst, size_t n, const Type& obj) {                 \
    return WriteJson(dst, n, __VA_ARGS__);                                        \
  }

/**
 * Given a set of fields declared via DATADOG_JSON_FIELD, with each field type having
 * the form `std::pair<std::string_view, T>`, returns the total number of bytes required
 * to encode the given values for those fields as a JSON object.
 */
template <
    typename... Fields,
    typename = std::enable_if_t<(is_pair_like_v<Fields> && ...)>>
size_t GetJsonSize(const Fields&&... fields) {
  // An empty struct is simply encoded as '{}'
  if constexpr (sizeof...(Fields) == 0) {
    return 2;
  }

  // 2 bytes for braces
  size_t size = 2;  // {}
  size_t num_properties_included = 0;

  // For each field (std::pair<std::string_view, T>), accumulate the size of the name
  // with enclosing quotes, and the size of the value when JSON-encoded, unless the
  // field's current value indicates that it should be entirely omitted
  ((([&] {
      if (HasJsonValue(fields.second)) {
        // Account for quoted name + colon + size of value (field names are assumed
        // to be plain ASCII and are not escaped; see file-level doc comment)
        size += 1 + fields.first.size() + 2 + GetJsonSize(fields.second);
        // Increment number of properties so we can account for commas when finished
        ++num_properties_included;
      }
    }()),
    ...));

  // Include commas between properties, if more than one property is present
  if (num_properties_included > 0) {
    size += num_properties_included - 1;
  }
  return size;
}

/**
 * Given a set of fields declared via DATADOG_JSON_FIELD, serializes a JSON object
 * containing each of those fields' names and values.
 */
template <
    typename... Fields,
    typename = std::enable_if_t<(is_pair_like_v<Fields> && ...)>>
size_t WriteJson(char* dst, size_t n, const Fields&&... fields) {
  char* ptr = dst;
  *ptr++ = '{';

  // Use a lambda fold expression to process each field sequentially
  bool first = true;
  (([&] {
     // If this field's current value is one that should be entirely omitted from the
     // JSON object, skip it
     if (!HasJsonValue(fields.second)) {
       return;
     }

     // Write a comma for all fields after the first one serialized
     if (!first) {
       *ptr++ = ',';
     }
     first = false;

     // Write the name of the field as a string, enclosed in quotes. As cautioned above,
     // we expect that field names do NOT require escaping, as this code deals
     // exclusively with internally-defined data types that should not have arbitrary
     // quotes/slashes/etc. in their property names.
     *ptr++ = '"';
     std::memcpy(ptr, fields.first.data(), fields.first.size());
     ptr += fields.first.size();
     *ptr++ = '"';

     // Write a colon to delimit name and value
     *ptr++ = ':';

     // Write the value as a JSON literal, resolving the appropriate overload for the
     // field's value type
     ptr += WriteJson(ptr, dst + n - ptr, fields.second);
   }()),
   ...);

  *ptr++ = '}';
  return ptr - dst;
}

/**
 * Defines the JSON object format used to serialize a value of type `Type`, merging any
 * custom user-specified property values specified via an Attribute member whose name
 * is given as `Extra`.
 *
 * The resulting serialization routines work identically to `DATADOG_JSON_STRUCT`,
 * except that if the member specified by `Extra` is an Attribute with one or more
 * object property values, those property values will be merged into the resulting JSON
 * object at top-level, alongside the ordinary struct members declared via
 * `DATADOG_JSON_FIELD`. If the `Extra` object contains any properties with names that
 * conflict with the names specified as struct fields, those values will be omitted.
 *
 * The extra `Attribute` field should _NOT_ be listed in the set of `DATADOG_JSON_FIELD`
 * declarations; it should instead be provided as `Extra`, without being wrapped in any
 * macro.
 */
#define DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(Type, Extra, ...)          \
  inline size_t GetJsonSize(const Type& obj) {                               \
    return GetJsonSizeWithExtraAttributes(obj.Extra, __VA_ARGS__);           \
  }                                                                          \
  inline size_t WriteJson(char* dst, size_t n, const Type& obj) {            \
    return WriteJsonWithExtraAttributes(obj.Extra, dst, n, __VA_ARGS__);     \
  }                                                                          \
  inline std::optional<StructEncodeResult> TryEncodeJson(                    \
      char* dst, size_t n, const Type& obj                                   \
  ) {                                                                        \
    return TryEncodeJsonWithExtraAttributes(obj.Extra, dst, n, __VA_ARGS__); \
  }

/**
 * Returns the worst-case buffer size required to JSON-serialize a set of struct fields
 * along with the values specified in the given `extra` object.
 */
template <
    typename... Fields,
    typename = std::enable_if_t<(is_pair_like_v<Fields> && ...)>>
size_t GetJsonSizeWithExtraAttributes(
    const Attribute& extra, const Fields&&... fields
) {
  // Compute the size required to encode all our struct fields as a JSON object, without
  // any extra attributes merged in; and use that value as-is if we have no extra fields
  const size_t base_size = GetJsonSize(std::forward<const Fields>(fields)...);
  if (extra.GetObjectPropertyCount() == 0) {
    return base_size;
  }

  // If we've been provided with extra fields, extend our reserved size to also fit the
  // full, JSON-encoded representation of those fields as their own object, with the
  // leading brace trimmed off: this does not account for any extra properties with
  // reserved field names (which will be filtered out in WriteJsonWithExtraAttributes),
  // but it's OK if we overshoot slightly in edge cases
  return base_size + GetJsonSize(extra) - 1;
}

/**
 * Serializes the given set of struct fields to JSON, merging in any `extra` attribute
 * attribute values that are safe to merge. A value is safe to merge if its property
 * name does not conflict with any of the struct field names given in `fields`.
 */
template <
    typename... Fields,
    typename = std::enable_if_t<(is_pair_like_v<Fields> && ...)>>
size_t WriteJsonWithExtraAttributes(
    const Attribute& extra, char* dst, size_t n, const Fields&&... fields
) {
  // If we have no extra attributes whatsoever, early-out: write the base object only.
  if (extra.GetObjectPropertyCount() == 0) {
    return WriteJson(dst, n, std::forward<const Fields>(fields)...);
  }

  // It's possible that the set of `extra` attributes includes one or more top-level
  // property values that overlap with the set of canonical property names specified in
  // `fields`. Prepare a constexpr function that will return true if a given attribute
  // name is safe for merging; false if it conflicts with a value in `fields`.
  auto is_safe_name = [&](std::string_view name) constexpr {
    return !((name == std::forward<const Fields>(fields).first) || ...);
  };

  // If none of the base fields have a value to emit, write the extra object directly
  // instead of writing an empty `{}` and then trying to merge into it.
  const bool has_base_fields = ((HasJsonValue(fields.second)) || ...);
  if (!has_base_fields) {
    return WriteFilteredJsonObject(dst, n, extra, is_safe_name);
  }

  // Use the normal struct serialization implementation to produce a JSON object with
  // the base fields of our struct, e.g. `{"id":"foo","name":"bar"}`.
  const size_t base_size = WriteJson(dst, n, std::forward<const Fields>(fields)...);
  DATADOG_ASSERT(
      base_size >= 2 && dst[0] == '{' && dst[base_size - 1] == '}',
      "WriteJson for struct produced non-object value"
  );

  // Advance to the last character of the base object to begin writing extra properties
  char* extra_start = dst + base_size - 1;
  DATADOG_ASSERT(*extra_start == '}', "Bad write position for extra properties");

  // Write extra properties, overlapping the original JSON value by one, then convert
  // the extra object's opening brace to a comma to concatenate the two
  const size_t extra_size =
      WriteFilteredJsonObject(extra_start, n - base_size + 1, extra, is_safe_name);
  DATADOG_ASSERT(
      extra_size >= 2 && *extra_start == '{' && *(extra_start + extra_size - 1) == '}',
      "WriteFilteredJsonObject produced non-object value"
  );

  // Note that in a case where `extra` contains only properties with reserved names, it
  // could write `{}`, producing `{"id":"foo","name":"bar"{}`, which would not be valid
  // JSON if we turned it into `{"id":"foo","name":"bar",}`. We need to handle this case
  // specifically, after we serialize the extra attributes, since catching it beforehand
  // would require iterating over all property values up-front to perform a separate
  // filtering pass.
  if (extra_size == 2) {
    // Revert the last character to a closing brace, then return the original size: JSON
    // values are not null-terminated, so we can leave the extra '}' alone
    *extra_start = '}';
    return base_size;
  }

  // It's guaranteed that both our base value and our extra value were encoded as JSON
  // object values with at least 1 property, so replacing the extra value's opening '{'
  // (which was originally the base value's closing '}') with a comma will correctly
  // concatenate the two values into a single object
  *extra_start = ',';

  // Return the total size of both values, accounting for the fact that we overlapped
  // them by one byte
  return base_size + extra_size - 1;
}

/**
 * Attempts to serialize the given set of struct fields and any safe extra attributes
 * into the fixed-size buffer `dst` (capacity `n`), returning a StructEncodeResult on
 * success, or std::nullopt on failure.
 *
 * If the base struct fields alone do not fit in `n` bytes, returns std::nullopt
 * immediately without modifying `dst`. Otherwise, includes as many extra attributes as
 * fit, dropping from the back (highest index first) until the value fits. Extra
 * attribute properties with names that conflict with struct field names are always
 * filtered out, consistent with WriteJsonWithExtraAttributes.
 *
 * The `truncated` field of the returned StructEncodeResult is true if one or more extra
 * attributes that would have been safe to include (i.e. whose names did not conflict
 * with any struct field) were dropped because they did not fit within `n` bytes.
 *
 * Assumption: WriteFilteredJsonObject iterates properties in strictly ascending index
 * order (0, 1, 2, …). The stateful lambda used below relies on this invariant to
 * enforce the prefix-count limit `k` without requiring changes to that function.
 */
template <
    typename... Fields,
    typename = std::enable_if_t<(is_pair_like_v<Fields> && ...)>>
std::optional<StructEncodeResult> TryEncodeJsonWithExtraAttributes(
    const Attribute& extra, char* dst, size_t n, const Fields&&... fields
) {
  // Build the is_safe_name predicate shared by size-computation and writing paths
  auto is_safe_name = [&](std::string_view name) constexpr {
    return !((name == std::forward<const Fields>(fields).first) || ...);
  };

  const size_t base_size = GetJsonSize(std::forward<const Fields>(fields)...);

  if (base_size > n) {
    // The base struct itself doesn't fit; nothing we can do
    return std::nullopt;
  }

  const size_t num_extra = extra.GetObjectPropertyCount();

  // No extra attributes: write the base struct directly
  if (num_extra == 0) {
    const size_t written = WriteJson(dst, n, std::forward<const Fields>(fields)...);
    DATADOG_ASSERT(
        written <= n,
        "unexpected overflow of JSON buffer in TryEncodeJsonWithExtraAttributes"
    );
    return StructEncodeResult{written, false};
  }

  // Needed by both the size loop below and the write path: determines whether the base
  // struct contributes any fields to the output. When false, extras are emitted as a
  // standalone object (WriteFilteredJsonObject only), so k safe extras require only
  // k-1 inter-property commas rather than k leading commas.
  const bool has_base_fields = ((HasJsonValue(fields.second)) || ...);

  // Determine the largest prefix k (0 <= k <= num_extra) such that the combined output
  // fits within n bytes. We start with the full set and shrink by one each iteration.
  // This is O(P^2) in the number of extra properties, which is acceptable given the
  // small sizes expected in practice.
  //
  // num_safe_at_k tracks how many of the k properties at the chosen k value pass the
  // safe-name filter. If zero safe properties remain, we skip the extra-attributes
  // write path entirely and emit the base struct only.
  size_t k = num_extra;
  size_t num_safe_at_k = 0;
  while (true) {
    // Compute the size contributed by the k extra properties at the front.
    // Each safe property is charged: 1 comma + encoded name + 1 colon + value.
    // When has_base_fields is false the extras form a standalone object, so the
    // first safe property has no leading comma; we deduct one comma at the end.
    size_t extra_bytes = 0;
    size_t num_safe = 0;
    for (size_t i = 0; i < k; ++i) {
      std::string_view prop_name = extra.GetObjectPropertyNameAt(static_cast<int>(i));
      if (!is_safe_name(prop_name)) {
        continue;
      }
      extra_bytes += 1 + GetJsonSize(prop_name) + 1 +
                     GetJsonSize(extra.GetObjectPropertyValueAt(static_cast<int>(i)));
      ++num_safe;
    }
    if (!has_base_fields && num_safe > 0) {
      // k safe entries in a standalone object need k-1 commas, not k
      --extra_bytes;
    }

    // If no safe properties are included, the total size is just the base size
    const size_t total = (num_safe == 0) ? base_size : (base_size + extra_bytes);
    if (total <= n) {
      num_safe_at_k = num_safe;
      break;
    }

    if (k == 0) {
      // Should not happen: we already verified base_size <= n above, and k == 0 means
      // extra_bytes == 0, so total == base_size which fits. Guard defensively.
      num_safe_at_k = 0;
      break;
    }
    --k;
  }

  // Determine whether any safe extras were dropped: there are safe extras beyond the
  // chosen prefix k if any property in [k, num_extra) passes is_safe_name.
  bool truncated = false;
  for (size_t i = k; i < num_extra; ++i) {
    if (is_safe_name(extra.GetObjectPropertyNameAt(static_cast<int>(i)))) {
      truncated = true;
      break;
    }
  }

  // Write: use a stateful lambda to limit WriteFilteredJsonObject to the first k
  // properties, while also applying the safe-name filter. The lambda is called once
  // per property in strictly ascending index order by WriteFilteredJsonObject.
  //
  // If k == 0 or no safe properties exist within the prefix, write the base struct
  // only.
  if (k == 0 || num_safe_at_k == 0) {
    const size_t written = WriteJson(dst, n, std::forward<const Fields>(fields)...);
    DATADOG_ASSERT(
        written <= n,
        "unexpected overflow of JSON buffer in TryEncodeJsonWithExtraAttributes"
    );
    return StructEncodeResult{written, truncated};
  }

  // Mirror the merge logic from WriteJsonWithExtraAttributes, using a prefix-limited
  // filter instead of the plain is_safe_name predicate
  int calls_seen = 0;
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  auto truncated_filter = [&](std::string_view name) {
    // calls_seen tracks how many times this lambda has been invoked, corresponding
    // to property indices 0, 1, 2, … in ascending order (invariant of
    // WriteFilteredJsonObject). Only include the property if its index is within [0, k)
    // AND its name passes the safe-name check.
    const int idx = calls_seen++;
    return (static_cast<size_t>(idx) < k) && is_safe_name(name);
  };

  if (!has_base_fields) {
    const size_t written = WriteFilteredJsonObject(dst, n, extra, truncated_filter);
    DATADOG_ASSERT(
        written <= n,
        "unexpected overflow of JSON buffer in TryEncodeJsonWithExtraAttributes"
    );
    return StructEncodeResult{written, truncated};
  }

  const size_t base_written = WriteJson(dst, n, std::forward<const Fields>(fields)...);
  DATADOG_ASSERT(
      base_written >= 2 && dst[0] == '{' && dst[base_written - 1] == '}',
      "WriteJson for struct produced non-object value"
  );

  char* extra_start = dst + base_written - 1;
  const size_t extra_written = WriteFilteredJsonObject(
      extra_start, n - base_written + 1, extra, truncated_filter
  );
  DATADOG_ASSERT(
      extra_written >= 2 && *extra_start == '{' &&
          *(extra_start + extra_written - 1) == '}',
      "WriteFilteredJsonObject produced non-object value"
  );

  if (extra_written == 2) {
    // All k extra properties were filtered out by the safe-name check; revert to the
    // base struct output
    *extra_start = '}';
    return StructEncodeResult{base_written, truncated};
  }

  *extra_start = ',';
  DATADOG_ASSERT(
      base_written + extra_written - 1 <= n,
      "unexpected overflow of JSON buffer in TryEncodeJsonWithExtraAttributes"
  );
  return StructEncodeResult{base_written + extra_written - 1, truncated};
}

}  // namespace datadog::impl
