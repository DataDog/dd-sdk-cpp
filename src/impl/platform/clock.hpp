#pragma once

#include <cstdint>

namespace datadog::platform {

struct Clock
{
    static uint64_t read_utc_nanos();
};

}
