#include <chrono>

#include "platform/clock.hpp"

namespace datadog::platform {

/**
 * Implements IClock by sampling std::chrono::system_clock.
 */
class StdClock final : public IClock {
 public:
  Timestamp Now() const final { return std::chrono::system_clock::now(); }
};

std::unique_ptr<IClock> Clock::Init() { return std::make_unique<StdClock>(); }

}  // namespace datadog::platform
