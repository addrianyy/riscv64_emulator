#include <base/Initialization.hpp>
#include <base/Platform.hpp>

#ifdef PLATFORM_WINDOWS

#include <Windows.h>

void base::initialize() {
  const auto stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  const auto stderr_handle = GetStdHandle(STD_ERROR_HANDLE);

  DWORD console_mode;

  {
    GetConsoleMode(stdout_handle, &console_mode);
    console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(stdout_handle, console_mode);
  }

  {
    GetConsoleMode(stderr_handle, &console_mode);
    console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(stderr_handle, console_mode);
  }
}

#else

void base::initialize() {}

#endif