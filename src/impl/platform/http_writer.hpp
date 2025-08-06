#pragma once

#include "platform/http.hpp"

namespace datadog::platform {

/**
 * Implementation of HttpBodyWriter that streams a string value into the HTTP request
 * body.
 */
struct StringWriter
{
    std::string_view s;
    size_t offset;

    /**
     * Initializes a new StringWriter that will read the given string value and write it
     * into the request body when used as a functor.
     *
     * @param s The string data to write into the request body. The underlying storage
     *  for the string must remain stable throughout the lifetime of the StringWriter.
     */
    explicit StringWriter(std::string_view s)
        : s(s)
        , offset(0)
    {
    }

    size_t operator()(char* buffer, size_t num_bytes)
    {
        const size_t num_bytes_remaining = s.length() - offset;
        const size_t num_bytes_to_copy = std::min(num_bytes_remaining, num_bytes);
        if (num_bytes_to_copy)
        {
            std::memcpy(buffer, s.data() + offset, num_bytes_to_copy);
            offset += num_bytes_to_copy;
        }
        return num_bytes_to_copy;
    }
};
static_assert(std::is_convertible_v<StringWriter, HttpBodyWriter>,
    "StringWriter does not implement HttpBodyWriter"
);

}
