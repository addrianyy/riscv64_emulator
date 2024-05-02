#include "CoreCount.hpp"

#include <base/Error.hpp>
#include <base/Platform.hpp>

#include <thread>

uint32_t base::core_count() {
  return std::thread::hardware_concurrency();
}

#ifdef PLATFORM_MAC

#include <sys/sysctl.h>

uint32_t base::performance_core_count() {
  int cpus = 0;
  size_t cpus_size = sizeof(cpus);

  verify(sysctlbyname("hw.perflevel0.physicalcpu", &cpus, &cpus_size, nullptr, 0) == 0,
         "sysctlbyname failed");

  return uint32_t(cpus);
}

#else

uint32_t base::performance_core_count() {
  return core_count();
}

#endif