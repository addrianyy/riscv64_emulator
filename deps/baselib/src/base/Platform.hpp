#pragma once

#if defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS)
#define PLATFORM_WINDOWS
#elif defined(__linux__)
#define PLATFORM_LINUX
#elif defined(__APPLE__) && defined(__MACH__)

#include <TargetConditionals.h>

#define PLATFORM_APPLE

#if TARGET_OS_OSX
#define PLATFORM_MAC
#elif TARGET_OS_IOS
#define PLATFORM_IOS
#else
#error "Unknown Apple platform"
#endif

#else
#error "Unknown platform"
#endif

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX) || defined(PLATFORM_MAC)
#define PLATFORM_DESKTOP
#elif defined(PLATFORM_IOS)
#define PLATFORM_MOBILE
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define PLATFORM_X64
#elif defined(__aarch64__) || defined(_M_ARM64)
#define PLATFORM_AARCH64
#endif