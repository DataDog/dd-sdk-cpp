// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/http/body_writer_multipart.hpp"

#include <algorithm>
#include <cstring>

#include "datadog/uuid.hpp"

#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

// The fixed pieces of text that make up the headers of a part
static constexpr std::string_view DISPOSITION_PREFIX =
    "Content-Disposition: form-data; name=\"";
static constexpr std::string_view FILENAME_PREFIX = "\"; filename=\"";
static constexpr std::string_view CONTENT_TYPE_PREFIX = "Content-Type: ";

MultipartBoundary MultipartBoundary::Random() {
  // Fill the buffer in order: the Content-Type prefix, the boundary prefix, then the
  // UUID (see the layout of _content_type)
  MultipartBoundary result;
  char* const content_type_start = result._content_type.data();
  char* const boundary_start = content_type_start + CONTENT_TYPE_PREFIX.size();
  char* const uuid_start = boundary_start + BOUNDARY_PREFIX.size();
  std::memcpy(
      content_type_start, CONTENT_TYPE_PREFIX.data(), CONTENT_TYPE_PREFIX.size()
  );
  std::memcpy(boundary_start, BOUNDARY_PREFIX.data(), BOUNDARY_PREFIX.size());
  UUID::Random().ToBytes(uuid_start, UUID_TEXT_LEN);
  return result;
}

std::string_view MultipartBoundary::Boundary() const {
  return {_content_type.data() + CONTENT_TYPE_PREFIX.size(), BOUNDARY_LEN};
}

std::string_view MultipartBoundary::ContentType() const {
  return {_content_type.data(), _content_type.size()};
}

/**
 * Returns the boundary line and headers that begin a part. Every part but the first
 * follows the previous part's data, which must first be ended with a CRLF.
 */
static std::string make_part_head(
    std::string_view boundary, const MultipartPart& part, bool is_first
) {
  DATADOG_ASSERT(
      part.name.find_first_of("\"\r\n") == std::string_view::npos &&
          part.filename.find_first_of("\"\r\n") == std::string_view::npos,
      "multipart part name or filename is not safe to quote"
  );
  DATADOG_ASSERT(!part.content_type.empty(), "multipart part has no content type");

  // Compute the exact size of the head so that we allocate it once. Each CRLF is 2
  // bytes, as is '--', and a double quote is 1.
  const bool has_filename = !part.filename.empty();

  // CRLF ending the previous part's data, if there is one
  const size_t leading_crlf_len = is_first ? 0 : 2;

  // '--' + boundary + CRLF
  const size_t boundary_line_len = 2 + boundary.size() + 2;

  // '"; filename="' + filename, if there is one
  const size_t filename_len =
      has_filename ? FILENAME_PREFIX.size() + part.filename.size() : 0;

  // 'Content-Disposition: form-data; name="' + name + filename + '"' + CRLF
  const size_t disposition_line_len =
      DISPOSITION_PREFIX.size() + part.name.size() + filename_len + 1 + 2;

  // 'Content-Type: ' + content_type + CRLF
  const size_t content_type_line_len =
      CONTENT_TYPE_PREFIX.size() + part.content_type.size() + 2;

  // All of the lines above, then the CRLF of the blank line that ends the headers
  const size_t head_len = leading_crlf_len + boundary_line_len + disposition_line_len +
                          content_type_line_len + 2;

  std::string head;
  head.reserve(head_len);
  if (!is_first) {
    head += "\r\n";
  }
  head += "--";
  head += boundary;
  head += "\r\n";
  head += DISPOSITION_PREFIX;
  head += part.name;
  if (has_filename) {
    head += FILENAME_PREFIX;
    head += part.filename;
  }
  head += "\"\r\n";
  head += CONTENT_TYPE_PREFIX;
  head += part.content_type;
  head += "\r\n\r\n";
  DATADOG_ASSERT(head.size() == head_len, "multipart part head size miscomputed");
  return head;
}

/**
 * Returns the closing boundary, which follows the last part's data. If there are any
 * parts, that data must first be ended with a CRLF; with no parts there is no data to
 * end.
 */
static std::string make_tail(std::string_view boundary, bool has_parts) {
  // CRLF ending the last part's data, if there is one
  const size_t leading_crlf_len = has_parts ? 2 : 0;

  // That CRLF, then '--' + boundary + '--' + CRLF
  const size_t tail_len = leading_crlf_len + 2 + boundary.size() + 2 + 2;

  std::string tail;
  tail.reserve(tail_len);
  if (has_parts) {
    tail += "\r\n";
  }
  tail += "--";
  tail += boundary;
  tail += "--\r\n";
  DATADOG_ASSERT(tail.size() == tail_len, "multipart tail size miscomputed");
  return tail;
}

MultipartBodyWriter::MultipartBodyWriter(
    std::string_view boundary, const MultipartPart* parts, size_t num_parts
)
    : _tail(make_tail(boundary, num_parts > 0)) {
  DATADOG_ASSERT(parts != nullptr || num_parts == 0, "multipart parts are null");

  _sections.reserve(num_parts);
  for (size_t i = 0; i < num_parts; i++) {
    const MultipartPart& part = parts[i];
    const bool is_first = (i == 0);
    _sections.push_back(Section{make_part_head(boundary, part, is_first), part.data});
  }
}

size_t MultipartBodyWriter::operator()(char* buffer, size_t num_bytes) {
  size_t num_bytes_written = 0;

  // Fill the buffer from the segments in turn, moving on as each is finished, until the
  // buffer is full or every segment has been written, including the closing boundary. A
  // segment can be empty, such as the data of an empty part, and is skipped.
  const size_t num_segments = NumSegments();
  while (num_bytes_written < num_bytes && _segment < num_segments) {
    const std::string_view segment = CurrentSegment();
    if (_offset >= segment.size()) {
      _segment++;
      _offset = 0;
      continue;
    }

    const size_t num_bytes_to_copy =
        std::min(num_bytes - num_bytes_written, segment.size() - _offset);
    std::memcpy(
        buffer + num_bytes_written, segment.data() + _offset, num_bytes_to_copy
    );
    _offset += num_bytes_to_copy;
    num_bytes_written += num_bytes_to_copy;
  }

  return num_bytes_written;
}

std::string_view MultipartBodyWriter::CurrentSegment() const {
  // Each section is two segments, its head then its data, so the section that segment
  // N belongs to is N / 2, and N is its head if even or its data if odd. For example,
  // with 2 sections:
  //
  //   segment:  0       1       2       3       4
  //             head 0  data 0  head 1  data 1  tail
  //
  // The closing boundary comes right after the last section's data.
  const size_t tail_segment = _sections.size() * 2;
  if (_segment < tail_segment) {
    const Section& section = _sections[_segment / 2];
    const bool is_head = (_segment % 2 == 0);
    return is_head ? std::string_view(section.head) : section.data;
  }
  if (_segment == tail_segment) {
    return _tail;
  }

  // Past the closing boundary; every segment has been written
  return {};
}

size_t MultipartBodyWriter::NumSegments() const {
  // A head and a data segment for each section (see CurrentSegment()), then the
  // closing boundary
  return (_sections.size() * 2) + 1;
}

}  // namespace datadog::impl
