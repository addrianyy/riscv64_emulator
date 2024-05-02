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