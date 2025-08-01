#include "platform/clock.hpp"

#include <chrono>

uint64_t datadog::platform::Clock::read_utc_nanos()
{
    // Measure nanoseconds, stored as uint64
    using period = std::nano;
    using representation = uint64_t;
    using duration = std::chrono::duration<representation, period>;

    // Read from the system clock, i.e. OS wall-clock time
    using clock = std::chrono::system_clock;
    using time_point = std::chrono::time_point<clock, duration>;

    // Return the number of nanoseconds elapsed since the Unix epoch
    const time_point now = clock::now();
    return now.time_since_epoch().count();
}
