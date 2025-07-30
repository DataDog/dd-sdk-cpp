#include "platform/clock.hpp"

#include <chrono>

uint64_t datadog::platform::Clock::read_utc_nanos()
{
    // Measure nanoseconds, stored as uint64
    using period = std::nano;
    using representation = uint64_t;
    using duration = std::chrono::duration<representation, period>;

    // Return the number of nanoseconds elapsed since the Unix epoch
    using clock = std::chrono::system_clock;
    return clock::now().time_since_epoch().count();
}
