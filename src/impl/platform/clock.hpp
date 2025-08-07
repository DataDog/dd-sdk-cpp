#pragma once

#include <cstdint>

namespace datadog::platform {

struct Clock
{
    /**
     * Returns an unsigned 64-bit integer representing the elapsed wall-clock time since
     * the Unix epoch, i.e. since midnight January 1, 1970, UTC.
     *
     * TODO: Is this only indirection only here for the purposes of mocking? Do we
     * actually need to allow users to bring their own clock implementation, and/or is
     * there any platform on which std::chrono wouldn't be viable?
     *
     * We use times for different purposes internally (e.g. event timestamps vs. batch
     * file names), so it may not make sense for this to be a global/platform-wide
     * interface.
     */
    static uint64_t read_utc_nanos();
};

} // namespace datadog::platform
