#include "attribute/json.hpp"

#include <cassert>
#include <charconv>
#include <chrono>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#include "date/date.h"

#include "attribute/cow.hpp"

namespace datadog::impl {

static constexpr std::string_view LITERAL_NULL = "null";
static constexpr std::string_view LITERAL_TRUE = "true";
static constexpr std::string_view LITERAL_FALSE = "false";

// "YYYY-MM-DDTHH:MM:SS.sssZ": 24 chars + 2 for quotes
static const size_t QUOTED_ISO8601_LEN = 26;

// Writing to uint8_t* with std::to_chars etc. requires casts from uint8_t* to char*
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)

static size_t _literal_write(uint8_t* dst, size_t n, std::string_view value)
{
    assert(n >= value.size() && "insufficient buffer size on _literal_write");
    std::memcpy(dst, value.data(), value.size());
    return value.size();
}

/**
 * Returns the exact number of bytes required to represent an unsigned 64-bit integer as
 * a string in decimal format; i.e. the total number of digits in its base-10
 * representation.
 */
static size_t _uint64_decimal_len(uint64_t value)
{
    // LUT: powers_of_ten[n] => 10^n, for n=[0..19]
    static constexpr uint64_t powers_of_ten[20] = {
        1ull,
        10ull,
        100ull,
        1000ull,
        10000ull,
        100000ull,
        1000000ull,
        10000000ull,
        100000000ull,
        1000000000ull,
        10000000000ull,
        100000000000ull,
        1000000000000ull,
        10000000000000ull,
        100000000000000ull,
        1000000000000000ull,
        10000000000000000ull,
        100000000000000000ull,
        1000000000000000000ull,
        10000000000000000000ull
    };

    // Early-out for zero, as bit-counting intrinsics may be undefined for 0
    if (value == 0)
    {
        return 1;
    }

    // We want to compute `floor(log10(value))` in order to determine the number of
    // decimal digits required to represent the value in base-10: we can approximate
    // this computation in binary, with sufficiently small error that we can correct it
    // with a table lookup.

    // First, compute the number of significant bits in our value
#if defined(__GNUC__) || defined(__clang__)
    // The "count leading zeroes" (CLZ) intrinsic gives us the number of leading zeroes
    // in our int value's bit pattern, from which we can infer its bit length
    size_t num_bits = 64u - __builtin_clzll(value);
#elif defined(_MSC_VER) && defined(_M_X64)
    // The "bit scan reverse" (BSR) intrinsic gives us the 0-based index of the most
    // significant bit in our value
    unsigned long idx;
    _BitScanReverse64(&idx, value);
    size_t num_bits = static_cast<size_t>(idx + 1);
#else
    // Portable fallback: shift right until all remaining bits are zero, counting the
    // number of shifts
    size_t num_bits = 0;
    for (uint64_t x = value; x; x >>= 1)
    {
        ++num_bits;
    }
#endif
    // Bit count of a 64-bit value never exceeds 64
    assert(num_bits <= 64);

    // Now that we have the bit length, we can approximate `floor(log10(value))` as
    // `floor(num_bits * log10(2))`. Right-shifting by 12 is an integer division by
    // 4096, and the ratio 1233/4096 approximates log10(2):
    //
    // -  log10(2) ~= 0.30102999566
    // - 1233/4096 ~= 0.30102539062
    // ----------------------------
    //   max error  < 0.000005
    size_t estimated_num_digits = (num_bits * 1233u) >> 12;

    // estimated_num_digits is guaranteed to be in the range [0..19], as num_bits will
    // never exceed 64, and 64 * (1233/4096) ~= 19.265625
    static_assert((1u * 1233u) >> 12 == 0, "lower bound must be 0");
    static_assert((64u * 1233u) >> 12 == 19, "upper bound must be 19");
    assert(estimated_num_digits < 20);

    // Given max error < 0.000005, estimated_num_digits is either exactly
    // `floor(log10(value))` or it's off by one: if `value` is greater than or equal to
    // our estimated power of ten, then we've underestimated by one; otherwise, we're
    // right on the money
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    size_t correction = value >= powers_of_ten[estimated_num_digits] ? 1u : 0u;
    return estimated_num_digits + correction;
}

static size_t _uint64_decimal_write(uint8_t* dst, size_t n, uint64_t value)
{
    // Use std::to_chars, which for uint64 is guaranteed to use the minimal
    // representation, always succeeding so long as the buffer has sufficient space
    auto result = std::to_chars(
        reinterpret_cast<char*>(dst), reinterpret_cast<char*>(dst + n), value
    );
    assert(result.ec == std::errc{} && "insufficient buffer size on uint64 encode");
    return result.ptr - reinterpret_cast<char*>(dst);
}

/**
 * Returns the exact number of bytes required to represent a signed 64-bit integer as a
 * string in decimal format; i.e. the total number of base-10 digits plus an optional
 * sign byte if negative.
 */
static size_t _int64_decimal_len(int64_t value)
{
    // Take the two's-complement absolute value of our signed int: converting to
    // unsigned from a negative value is well-defined as a modulo 2^64. By contrast,
    // std::abs(value) would result in undefined behavior if value were INT64_MIN.
    const uint64_t magnitude =
        value < 0 ? (static_cast<uint64_t>(0) - static_cast<uint64_t>(value))
                  : static_cast<uint64_t>(value);

    // Compute the required number of decimal digits, plus 1 byte for the sign if
    // negative
    return _uint64_decimal_len(magnitude) + (value < 0 ? 1u : 0u);
}

static size_t _int64_decimal_write(uint8_t* dst, size_t n, int64_t value)
{
    // Use std::to_chars, which for int64 is guaranteed to use the minimal
    // representation, appending a sign only if negative, and always succeeding so long
    // as the buffer has sufficient space
    auto result = std::to_chars(
        reinterpret_cast<char*>(dst), reinterpret_cast<char*>(dst + n), value
    );
    assert(result.ec == std::errc{} && "insufficient buffer size on int64 encode");
    return result.ptr - reinterpret_cast<char*>(dst);
}

/**
 * Returns the worst-case number of bytes required to represent a double-precision
 * IEEE-754 value as a string, in 'general' (%g) format. Assumes that value is finite.
 */
static size_t _double_gfmt_len([[maybe_unused]] double value)
{
    // chars_format::general (%g) specifies 17 significant digits, plus 1 byte each for
    // sign, decimal point, exponent marker, exponent sign, and 3 bytes for exponent
    // value e.g. '-1.7976931348623157e+308`
    static constexpr size_t MAX_DECIMAL_DOUBLE_LEN = 24;

    // There's no way to precompute the exact size required to store a double as a
    // string without actually performing the (relatively costly) conversion: use the
    // worst-case length and accept a bit of waste in our preallocated sizes.
    return MAX_DECIMAL_DOUBLE_LEN;
}

static size_t _double_gfmt_write(uint8_t* dst, size_t n, double value)
{
    // Use std::to_chars with std::chars_format::general (%g), which is guaranteed not
    // to exceed our worst-case size of MAX_DECIMAL_DOUBLE_LEN (i.e. 24 bytes)
    auto result = std::to_chars(
        reinterpret_cast<char*>(dst),
        reinterpret_cast<char*>(dst + n),
        value,
        std::chars_format::general
    );
    assert(result.ec == std::errc{} && "insufficient buffer size on double encode");
    return result.ptr - reinterpret_cast<char*>(dst);
}

static void _write_timestamp_4d(uint8_t*& ptr, size_t n, uint64_t value)
{
    auto res = std::to_chars(
        reinterpret_cast<char*>(ptr), reinterpret_cast<char*>(ptr + n), value
    );
    assert(res.ec == std::errc{} && "insufficient buffer space on timestamp write");
    assert(
        reinterpret_cast<uint8_t*>(res.ptr) == ptr + 4 &&
        "unexpected write size on timestamp write"
    );
    ptr += 4;
}

static void _write_timestamp_02d(uint8_t*& ptr, size_t n, uint64_t value)
{
    assert(value <= 99 && "value out of range for 02d");
    assert(n >= 2 && "insufficient buffer space for 02d");

    static const char digits[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (value < 10)
    {
        *ptr++ = '0';
        *ptr++ = digits[value]; // NOLINT
    }
    else
    {
        auto res = std::to_chars(
            reinterpret_cast<char*>(ptr), reinterpret_cast<char*>(ptr + n), value
        );
        assert(res.ec == std::errc{} && "insufficient buffer space on timestamp write");
        assert(
            reinterpret_cast<uint8_t*>(res.ptr) == ptr + 2 &&
            "unexpected write size on timestamp write"
        );
        ptr += 2;
    }
}

static void _write_timestamp_03d(uint8_t*& ptr, size_t n, uint64_t value)
{
    assert(value <= 999 && "value out of range for 03d");
    assert(n >= 3 && "insufficient buffer space for 03d");

    if (value < 100)
    {
        *ptr++ = '0';
        n--;
    }

    if (value < 10)
    {
        *ptr++ = '0';
        n--;
    }

    auto res = std::to_chars(
        reinterpret_cast<char*>(ptr), reinterpret_cast<char*>(ptr + n), value
    );
    assert(res.ec == std::errc{} && "insufficient buffer space on timestamp write");

    const size_t num_written = reinterpret_cast<uint8_t*>(res.ptr) - ptr;
    assert(
        num_written == (value < 10 ? 1 : (value < 100 ? 2 : 3)) &&
        "unexpected write size on timestamp write"
    );
    ptr += num_written;
}

static size_t
_iso_timestamp_quoted_write(uint8_t* dst, size_t n, uint64_t nanoseconds_since_epoch)
{
    assert(n >= QUOTED_ISO8601_LEN && "insufficient buffer size for timestamp");

    // Attribute stores timestamps internally as uint64_t in nanoseconds: construct an
    // equivalent std::chrono::time_point for interoperability with HowardHinnant/date
    using duration = std::chrono::duration<uint64_t, std::nano>;
    using time_point = std::chrono::time_point<std::chrono::system_clock, duration>;
    time_point tp = time_point{} + duration{nanoseconds_since_epoch};

    // Use HowardHinnant/date to compute an accurate calendar date (YYYY-MM-DD) and
    // time (HH:MM:SS) from that time_point
    auto day_point = date::floor<date::days>(tp);
    date::year_month_day ymd = date::year_month_day{day_point};
    date::hh_mm_ss time = date::make_time(tp - day_point);

    // Sanity check: a uint64 Unix timestamp in nanoseconds can only represent years
    // within this range
    const int year_value = static_cast<int>(ymd.year());
    assert(year_value >= 1970 && "date::floor yielded year before 1970");
    assert(year_value <= 2555 && "date::floor yielded year after 2555");

    // Get unsigned values for our calendar date: [1970..2286], [1..12], [1..31]
    const uint64_t year = year_value;
    const uint64_t month = static_cast<unsigned>(ymd.month());
    const uint64_t day = static_cast<unsigned>(ymd.day());

    // Get unsigned values for our wall-clock time: [0..23], [0..59], [0..59]
    const uint64_t hours = time.hours().count();
    const uint64_t minutes = time.minutes().count();
    const uint64_t seconds = time.seconds().count();

    // Get subsecond milliseconds [0..999]
    std::chrono::milliseconds subsec =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.subseconds());
    const uint64_t millis = subsec.count();

    // date::format() returns a std::string - to avoid the overhead of allocating every
    // time we write a timestamp, we can instead handle the formatting ourselves with
    // std::to_chars
    uint8_t* ptr = dst;
    uint8_t* const dst_end = dst + n;

    // Write an opening quote to begin our JSON string literal
    *ptr++ = '"';

    // Write YYYY-MM-DD
    _write_timestamp_4d(ptr, dst_end - ptr, year);
    *ptr++ = '-';
    _write_timestamp_02d(ptr, dst_end - ptr, month);
    *ptr++ = '-';
    _write_timestamp_02d(ptr, dst_end - ptr, day);

    // Write T to delimit date from time
    *ptr++ = 'T';

    // Write HH:MM:SS.sss
    _write_timestamp_02d(ptr, dst_end - ptr, hours);
    *ptr++ = ':';
    _write_timestamp_02d(ptr, dst_end - ptr, minutes);
    *ptr++ = ':';
    _write_timestamp_02d(ptr, dst_end - ptr, seconds);
    *ptr++ = '.';
    _write_timestamp_03d(ptr, dst_end - ptr, millis);

    // Write trailing Z
    *ptr++ = 'Z';

    // Write closing quote
    *ptr++ = '"';

    // We should have ended up with a value that's exactly 26 bytes, representing a
    // quoted JSON literal string in ISO-8601 format
    const size_t num_bytes_written = ptr - dst;
    assert(
        num_bytes_written == QUOTED_ISO8601_LEN &&
        "unexpected result size for JSON-encoded timestamp"
    );
    return num_bytes_written;
}

/**
 * Returns the exact number of bytes required to represent a string value in JSON,
 * encompassing the surrounding double-quotes and accounting for any characters that
 * must be escaped.
 */
static size_t _string_quoted_escaped_len(std::string_view value)
{
    // 2 bytes for the surrounding double-quotes, plus N bytes for each character in the
    // string
    size_t len = 2 + value.size();

    // Increase len to account for any characters that will need to be escaped
    for (uint8_t c : value)
    {
        switch (c)
        {
            // Double-quotes, backslashes, and control codes with short escape sequences
            // will require an extra byte for the preceding slash that escapes them
            case '\"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                len += 1;
                break;

            // Other control bytes must be encoded '\u00XX'; all other bytes are emitted
            // unchanged
            default:
                if (c < 0x1F)
                {
                    len += 5;
                }
                break;
        }
    }
    return len;
}

static void _u00_escape_write(uint8_t*& ptr, uint8_t byte)
{
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
    static const char hex_digits[] = "0123456789abcdef";
    *ptr++ = '\\';
    *ptr++ = 'u';
    *ptr++ = '0';
    *ptr++ = '0';
    *ptr++ = hex_digits[(byte >> 4) & 0xF];
    *ptr++ = hex_digits[byte & 0xF];
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
}

static size_t
_string_quoted_escaped_write(uint8_t* dst, size_t n, std::string_view value)
{
    uint8_t* ptr = dst;

    // Double-quote to open string literal
    *ptr++ = '"';

    // Copy all bytes from the input string, escaping to ensure a valid JSON literal
    for (uint8_t c : value)
    {
        switch (c)
        {
            // Double-quotes, backslashes, and control codes get a preceding slash
            case '\"':
                *ptr++ = '\\';
                *ptr++ = '\"';
                break;
            case '\\':
                *ptr++ = '\\';
                *ptr++ = '\\';
                break;
            case '\b':
                *ptr++ = '\\';
                *ptr++ = 'b';
                break;
            case '\f':
                *ptr++ = '\\';
                *ptr++ = 'f';
                break;
            case '\n':
                *ptr++ = '\\';
                *ptr++ = 'n';
                break;
            case '\r':
                *ptr++ = '\\';
                *ptr++ = 'r';
                break;
            case '\t':
                *ptr++ = '\\';
                *ptr++ = 't';
                break;

            // Other control bytes must be encoded '\u00XX'; all other bytes are emitted
            // unchanged
            default:
                if (c < 0x1F)
                {
                    _u00_escape_write(ptr, c);
                }
                else
                {
                    *ptr++ = c;
                }
                break;
        }
    }

    // Double-quote to close string literal
    *ptr++ = '"';

    // Return total number of bytes written, which should have been less than or equal
    // to the available space in the buffer
    const size_t num_bytes_written = ptr - dst;
    assert(num_bytes_written <= n && "buffer overflow on string encode");
    return num_bytes_written;
}

/**
 * Returns the worst-case number of bytes required to represent an array of attributes
 * as a JSON array, accounting for brackets and delimiters, and recursively precomputing
 * the size of each value.
 *
 * Result is a worst-case estimate by virtue of the fact that any constituent double
 * values will use a size of 24 bytes; all other size computations are exact.
 */
static size_t _array_len(const impl::CowValue& value)
{
    // Early-out for empty array: '[]'
    const size_t num_items = value.Size();
    if (num_items == 0)
    {
        return 2;
    }

    // 2 bytes for '[' and ']', plus N-1 commas to delimit the items
    const size_t brackets_len = 2;
    const size_t num_commas = num_items - 1;
    size_t len = brackets_len + num_commas;

    // Accumulate required space for all items, recursively
    for (int i = 0, num = static_cast<int>(num_items); i < num; i++)
    {
        len += AttributeSerialization::ComputeValueLen(value.GetAt(i));
    }
    return len;
}

static size_t _array_write(uint8_t* dst, size_t n, const impl::CowValue& value)
{
    uint8_t* ptr = dst;
    uint8_t* const dst_end = dst + n;

    // Open array literal
    *ptr++ = '[';

    // Write each item, comma-delimited, serializing values recursively
    const size_t num_items = value.Size();
    for (int i = 0, num = static_cast<int>(num_items); i < num; i++)
    {
        if (i > 0)
        {
            *ptr++ = ',';
        }
        ptr += AttributeSerialization::WriteValue(value.GetAt(i), ptr, dst_end - ptr);
    }

    // Close array literal
    *ptr++ = ']';

    // Return total number of bytes written, which should have been less than or equal
    // to the available space in the buffer
    const size_t num_bytes_written = ptr - dst;
    assert(num_bytes_written <= n && "buffer overflow on array encode");
    return num_bytes_written;
}

/**
 * Returns the worst-case number of bytes required to represent an object attribute as a
 * JSON object, accounting for braces, delimiters, and literal property names, and
 * recursively precomputing the size of each property's value.
 *
 * Result is a worst-case estimate by virtue of the fact that any constituent double
 * values will use a size of 24 bytes; all other size computations are exact.
 */
static size_t _object_len(const impl::CowValue& value)
{
    // Early-out for empty object: '{}'
    const size_t num_properties = value.Size();
    if (num_properties == 0)
    {
        return 2;
    }

    // 2 bytes for '{' and '}', plus N-1 commas to delimit the properties, plus N bytes
    // for the colon delimiting each property's name from its value
    const size_t braces_len = 2;
    const size_t num_commas = num_properties - 1;
    const size_t num_colons = num_properties;
    size_t len = braces_len + num_commas + num_colons;

    // Accumulate required space for each property name as a quoted, escaped string
    for (int i = 0, num = static_cast<int>(num_properties); i < num; i++)
    {
        len += _string_quoted_escaped_len(value.GetPropertyNameCStr(i));
    }

    // Accumulate required space for all property values, recursively
    for (int i = 0, num = static_cast<int>(num_properties); i < num; i++)
    {
        len += AttributeSerialization::ComputeValueLen(value.GetAt(i));
    }
    return len;
}

static size_t _object_write(uint8_t* dst, size_t n, const impl::CowValue& value)
{
    uint8_t* ptr = dst;
    uint8_t* const dst_end = dst + n;

    // Open object literal
    *ptr++ = '{';

    // Write name:value pairs for each property, comma-delimited, serializing values
    // recursively
    const size_t num_properties = value.Size();
    for (int i = 0, num = static_cast<int>(num_properties); i < num; i++)
    {
        if (i > 0)
        {
            *ptr++ = ',';
        }
        ptr += _string_quoted_escaped_write(
            ptr, dst_end - ptr, value.GetPropertyNameCStr(i)
        );
        *ptr++ = ':';
        ptr += AttributeSerialization::WriteValue(value.GetAt(i), ptr, dst_end - ptr);
    }

    // Close object literal
    *ptr++ = '}';

    // Return total number of bytes written, which should have been less than or equal
    // to the available space in the buffer
    const size_t num_bytes_written = ptr - dst;
    assert(num_bytes_written <= n && "buffer overflow on object encode");
    return num_bytes_written;
}

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

size_t AttributeSerialization::ComputeValueLen(const Attribute& attribute)
{
    switch (attribute.type)
    {
        case ValueType::Null:
            return LITERAL_NULL.length();
        case ValueType::Bool:
            if (attribute.value.i64 != 0)
            {
                return LITERAL_TRUE.length();
            }
            return LITERAL_FALSE.length();
        case ValueType::Int:
            return _int64_decimal_len(attribute.value.i64);
        case ValueType::UInt:
            return _uint64_decimal_len(attribute.value.u64);
        case ValueType::Timestamp:
            return QUOTED_ISO8601_LEN;
        case ValueType::Double:
            // Non-finite values (NaN, -inf, +inf) can not be expressed as valid JSON
            // numbers; replace them with literal null
            if (!std::isfinite(attribute.value.f64))
            {
                return LITERAL_NULL.length();
            }
            return _double_gfmt_len(attribute.value.f64);
        case ValueType::String:
            return _string_quoted_escaped_len(attribute.value.ptr->CStr());
        case ValueType::Array:
            return _array_len(*attribute.value.ptr);
        case ValueType::Object:
            return _object_len(*attribute.value.ptr);
    }
}

size_t AttributeSerialization::WriteValue(
    const Attribute& attribute,
    uint8_t* buffer,
    size_t buffer_size
)
{
    switch (attribute.type)
    {
        case ValueType::Null:
            return _literal_write(buffer, buffer_size, LITERAL_NULL);
        case ValueType::Bool:
            if (attribute.value.i64 != 0)
            {
                return _literal_write(buffer, buffer_size, LITERAL_TRUE);
            }
            return _literal_write(buffer, buffer_size, LITERAL_FALSE);
        case ValueType::Int:
            return _int64_decimal_write(buffer, buffer_size, attribute.value.i64);
        case ValueType::UInt:
            return _uint64_decimal_write(buffer, buffer_size, attribute.value.u64);
        case ValueType::Timestamp:
            return _iso_timestamp_quoted_write(
                buffer, buffer_size, attribute.value.u64
            );
        case ValueType::Double:
            if (!std::isfinite(attribute.value.f64))
            {
                return _literal_write(buffer, buffer_size, LITERAL_NULL);
            }
            return _double_gfmt_write(buffer, buffer_size, attribute.value.f64);
        case ValueType::String:
            return _string_quoted_escaped_write(
                buffer, buffer_size, attribute.value.ptr->CStr()
            );
        case ValueType::Array:
            return _array_write(buffer, buffer_size, *attribute.value.ptr);
        case ValueType::Object:
            return _object_write(buffer, buffer_size, *attribute.value.ptr);
    }
}

void AttributeSerialization::ToJSON(
    const Attribute& attribute,
    std::vector<uint8_t>& out_buffer
)
{
    // Ensure that our buffer has space to fit this attribute when JSON-serialized
    const size_t precomputed_size = ComputeValueLen(attribute);
    out_buffer.resize(precomputed_size);

    // Serialize our value into the buffer
    const size_t num_bytes_written =
        WriteValue(attribute, out_buffer.data(), out_buffer.size());

    // Debug: verify that WriteValue never writes more data than ComputeValueLen
    // indicates we need
    assert(num_bytes_written <= precomputed_size);

    // Ensure that out_buffer is bounded to include only the data we've written, in case
    // we overestimated buffer size
    out_buffer.resize(num_bytes_written);
}

} // namespace datadog::impl
