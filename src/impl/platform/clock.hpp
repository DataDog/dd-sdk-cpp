#pragma once

#include <cstdint>

namespace datadog::platform {

struct Clock
{
    /**
     * Returns an unsigned 64-bit integer representing the elapsed wall-clock time since
     * the Unix epoch, i.e. since midnight January 1, 1970, UTC.
     */
    static uint64_t read_utc_nanos();
};

}
