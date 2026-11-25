// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/http/body_writer.hpp"

namespace datadog::impl {

/**
 * One part of a `multipart/form-data` request body: a form field, which may be a file.
 *
 * `name` and `filename` are written inside double quotes, so neither may contain a
 * double quote, a carriage return, or a line feed, and `content_type` must not be
 * empty. If `filename` is empty, no filename is given for the part.
 *
 * `data` is not copied: the bytes it views must stay valid for as long as the
 * MultipartBodyWriter that is given this part is in use.
 */
struct MultipartPart {
  std::string_view name;
  std::string_view filename;
  std::string_view content_type;
  Block data;
};

/**
 * The boundary of one `multipart/form-data` request, along with the value of the
 * request's `Content-Type` header that names it, held in a fixed-size buffer so that
 * neither needs a heap allocation.
 *
 * The boundary is a fixed prefix followed by a random UUID, so that it is unique to the
 * request, and so that it is practically impossible for the bytes of a part to contain
 * it. It is made only of characters that are valid in a boundary, and is well within
 * the limit of 70 characters.
 */
class MultipartBoundary {
 public:
  /** Returns a new boundary, made unique by a random UUID. */
  static MultipartBoundary Random();

  /** Returns the boundary, which separates the parts of the body. */
  std::string_view Boundary() const;

  /**
   * Returns the value of the request's `Content-Type` header:
   * `multipart/form-data; boundary=<boundary>`.
   */
  std::string_view ContentType() const;

 private:
  MultipartBoundary() = default;

  static constexpr std::string_view CONTENT_TYPE_PREFIX =
      "multipart/form-data; boundary=";
  static constexpr std::string_view BOUNDARY_PREFIX = "dd-sdk-cpp-boundary-";

  // A UUID in its text form is 36 characters of hex digits and hyphens, which are valid
  // in a boundary
  static constexpr size_t UUID_TEXT_LEN = 36;

  static constexpr size_t BOUNDARY_LEN = BOUNDARY_PREFIX.size() + UUID_TEXT_LEN;
  static_assert(
      BOUNDARY_LEN <= 70, "multipart boundary is longer than RFC 2046 allows"
  );

  // The Content-Type value, which ends with the boundary:
  //
  //   offset 0                               30                     50          86
  //          | multipart/form-data; boundary= | dd-sdk-cpp-boundary- | <uuid>    |
  //          '------------- prefix (30) ------'---- prefix (20) -----'-- (36) ---'
  //          ^ ContentType()                  ^ Boundary()
  std::array<char, CONTENT_TYPE_PREFIX.size() + BOUNDARY_LEN> _content_type{};
};

/**
 * Implementation of HttpBodyWriter that streams a `multipart/form-data` body (RFC 7578)
 * made of the given parts, without copying the parts' data. The body has this form,
 * with CRLF line endings, and the `Content-Type` of the request must be
 * `multipart/form-data; boundary=<boundary>` using the same boundary:
 *
 *     --<boundary>
 *     Content-Disposition: form-data; name="<name>"; filename="<filename>"
 *     Content-Type: <content_type>
 *
 *     <data>
 *     --<boundary>
 *     ...next part...
 *     --<boundary>--
 *
 * A writer is copyable, and a copy continues from the same position as the original.
 */
class MultipartBodyWriter {
 public:
  /**
   * Prepares to write a body made of the `num_parts` parts that `parts` points to, in
   * order, separated by `boundary`, such as from a std::array. `parts` may be null if
   * `num_parts` is 0. Neither `boundary` nor `parts` need stay valid after this
   * returns, but see MultipartPart for how long each part's data must.
   */
  MultipartBodyWriter(
      std::string_view boundary, const MultipartPart* parts, size_t num_parts
  );

  /**
   * Function-call operator satisfying HttpBodyWriter:
   * - std::function<size_t(char* buffer, size_t num_bytes)>
   *
   * Writes the next available chunk of the body, up to `num_bytes` bytes, into
   * `buffer`. Returns the number of bytes written, which is only less than `num_bytes`
   * when the end of the body is reached, and is 0 (HTTP_WRITE_RESULT_EOF) once the
   * whole body has been written. Calling it with `num_bytes` of 0 writes nothing and
   * returns 0, which a caller must not mistake for the end of the body: the
   * HttpBodyWriter contract has no other way to say it, and no HTTP client does so.
   */
  size_t operator()(char* buffer, size_t num_bytes);

 private:
  /**
   * What is written for one part: its boundary line and headers, which the writer owns,
   * then its data, which belongs to the caller.
   */
  struct Section {
    std::string head;
    Block data;
  };

  /**
   * Returns the bytes of the segment that is being written. The segments, in order, are
   * the head of each section followed by its data, then the closing boundary. Returns
   * an empty view once every segment has been written.
   */
  std::string_view CurrentSegment() const;

  /**
   * Returns the number of segments in the body: two for each section, then one for the
   * closing boundary. The body has been completely written once `_segment` reaches it.
   */
  size_t NumSegments() const;

  std::vector<Section> _sections;
  std::string _tail;

  // The segment that is being written, and how many of its bytes have been written
  size_t _segment = 0;
  size_t _offset = 0;
};

static_assert(
    std::is_convertible_v<MultipartBodyWriter, HttpBodyWriter>,
    "MultipartBodyWriter does not implement HttpBodyWriter"
);

}  // namespace datadog::impl
