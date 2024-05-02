#include "Stopwatch.hpp"

using namespace base;

Stopwatch::Stopwatch() {
  reset();
}

void Stopwatch::reset() {
  start_time = PreciseTime::now();
  pause_start_time = {};
}

void Stopwatch::pause() {
  pause_start_time = PreciseTime::now();
}

void Stopwatch::resume() {
  const auto pause_time = PreciseTime::now() - pause_start_time;

  pause_start_time = {};
  start_time += pause_time;
}

PreciseTime Stopwatch::elapsed() const {
  return PreciseTime::now() - start_time;
}