// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>

namespace datadog::impl {

/**
 * Reads up to num_bytes bytes of data from the payload representing the body of an
 * outgoing HTTP request, then writes it into buffer.
 *
 * @return The number of bytes written to buffer. If HTTP_WRITE_RESULT_EOF, no bytes
 *  were written, there is no more data to write, and the request should be finished
 *  successfully. If HTTP_WRITE_RESULT_ABORT, writing can not proceed due to an error,
 *  and the request should be aborted.
 */
using HttpBodyWriter = std::function<size_t(char* buffer, size_t num_bytes)>;
static const size_t HTTP_WRITE_RESULT_EOF = 0;
static const size_t HTTP_WRITE_RESULT_ABORT = 0xffffffff;

}  // namespace datadog::impl
