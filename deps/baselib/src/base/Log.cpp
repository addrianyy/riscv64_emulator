#include <base/Log.hpp>
#include <base/Platform.hpp>
#include <base/Print.hpp>

#include <string_view>

#ifdef PLATFORM_MOBILE
#define ESCAPE(x)
#else
#define ESCAPE(x) x
#endif

void base::detail::log::log(const char* file,
                            int line,
                            LogLevel level,
                            const std::string& message) {
  (void)file;
  (void)line;

  std::string_view header;

  switch (level) {
    case LogLevel::Debug:
      header = ESCAPE("\x1b[32;1m") "[debug] ";
      break;
    case LogLevel::Info:
      header = ESCAPE("\x1b[36;1m") "[info ] ";
      break;
    case LogLevel::Warn:
      header = ESCAPE("\x1b[33;1m") "[warn ] ";
      break;
    case LogLevel::Error:
      header = ESCAPE("\x1b[31;1m") "[error] ";
      break;
    default:
      header = "??? ";
  }

  base::println("{}{}" ESCAPE("\x1b[0m"), header, message);
}