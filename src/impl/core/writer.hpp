// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string_view>

#include "core/block.hpp"
#include "core/feature.hpp"
#include "platform/http.hpp"

namespace datadog::impl {

/**
 * Implementation of HttpBodyWriter that streams a string value into the HTTP request
 * body.
 */
struct StringWriter {
  std::string_view s;
  size_t offset{0};

  /**
   * Initializes a new StringWriter that will read the given string value and write it
   * into the request body when used as a functor.
   *
   * @param s The string data to write into the request body. The underlying storage
   *  for the string must remain stable throughout the lifetime of the StringWriter.
   */
  explicit StringWriter(std::string_view s) : s(s) {}

  size_t operator()(char* buffer, size_t num_bytes);
};
static_assert(
    std::is_convertible_v<StringWriter, platform::HttpBodyWriter>,
    "StringWriter does not implement HttpBodyWriter"
);

/**
 * Implementation of HttpBodyWriter that streams the data read from a batch of TLV
 * 'Event' blocks into an HTTP request body, concatenating them into a JSON array by
 * default.
 */
struct TLVBatchWriter {
  /**
   * Finite states that this writer can be in: at any given time, we're working to write
   * one of these things to the destination buffer.
   */
  enum class Mode : uint8_t {
    Prefix,     // Writing a static prefix, e.g. '['
    Event,      // Writing the data of the last TLV Event block read from the file
    Delimiter,  // Writing a static delimiter between events, e.g. ','
    Suffix,     // Writing a static suffix, e.g. ']'
  };

  /**
   * Keeps track of what we're writing and how much of it we've written.
   */
  struct State {
    Mode mode{Mode::Prefix};
    std::string_view s;
    size_t offset{0};

    /**
     * Prepares our initial state, in which we need to write a static prefix.
     */
    explicit State(std::string_view prefix) : s(prefix) {}

    /**
     * Writes up to the next `dst_size` bytes of `s`, starting from `offset`, into
     * `dst`, incrementing both `dst` and `offset` by the number of bytes written; then
     * returns the number of bytes written.
     *
     * If Done() returns true after a call to Write(), no more source data exists and
     * the caller should transition to the next state.
     */
    size_t Write(char*& dst, size_t dst_size);

    /**
     * Returns true if all data in `s` has been written, including the case where `s` is
     * empty and no writes have occurred.
     */
    bool Done() const { return offset >= s.size(); }

    /**
     * Changes to a new state, in preparation to write the given value.
     */
    void Enter(Mode new_mode, std::string_view new_s) {
      mode = new_mode;
      s = new_s;
      offset = 0;
    }
  };

  /**
   * Non-owning reference to the BatchReader interface that we will use to read TLV
   * blocks, and which wraps the buffer containing the data for the last-read block.
   */
  BatchReader& reader;

  std::string_view prefix;
  std::string_view delimiter;
  std::string_view suffix;

  /**
   * State machine used to keep track of what we're currently writing (prefix, event
   * data, delimiter, or suffix) and how much of it we've written, in the case of
   * partial writes.
   */
  State state;
  /**
   * View of the next event block that we should write once we're finished writing the
   * delimiter.
   */
  std::string_view next_event;

  explicit TLVBatchWriter(
      BatchReader& in_reader, std::string_view in_prefix = "[",
      std::string_view in_delimiter = ",", std::string_view in_suffix = "]"
  )
      : reader(in_reader),
        prefix(in_prefix),
        delimiter(in_delimiter),
        suffix(in_suffix),
        state(in_prefix) {}

  size_t operator()(char* buffer, size_t num_bytes);
};

}  // namespace datadog::impl
