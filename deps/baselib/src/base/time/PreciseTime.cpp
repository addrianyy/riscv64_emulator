#include "PreciseTime.hpp"

#include <chrono>

using namespace base;

PreciseTime PreciseTime::now() {
  const auto now_timepoint = std::chrono::steady_clock::now();

  return PreciseTime::from_nanoseconds(
    std::chrono::duration_cast<std::chrono::nanoseconds>(now_timepoint.time_since_epoch()).count());
}