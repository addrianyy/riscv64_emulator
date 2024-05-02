#include <base/Error.hpp>
#include <base/Log.hpp>

#include <atomic>
#include <cstdlib>
#include <thread>

static std::atomic_bool g_is_panicking = false;

[[noreturn]] void base::detail::error::do_fatal_error(const char* file,
                                                      int line,
                                                      const std::string& message) {
  if (g_is_panicking.exchange(true)) {
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(10));
    }
  }

  log_error("{}:{} => {}", file, line, message);

  // For debugger breakpoint:
  {
    int _unused = 0;
    (void)_unused;
  }

  std::abort();
}

[[noreturn]] void base::detail::error::do_verify_fail(const char* file,
                                                      int line,
                                                      const std::string& message) {
  if (message.empty()) {
    do_fatal_error(file, line, "assertion failed");
  } else {
    do_fatal_error(file, line, base::format("assertion failed: {}", message));
  }
}