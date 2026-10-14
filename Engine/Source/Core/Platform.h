#pragma once

// Every macro here is always defined to 0 or 1. Test with #if, never #ifdef,
// so a misspelled name triggers -Wundef instead of evaluating to false.

#if defined(_WIN32)
    #define ARC_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_OSX
        #define ARC_PLATFORM_MACOS 1
    #else
        #error "Arclight supports macOS only on Apple platforms."
    #endif
#elif defined(__linux__)
    // Not a shipping target; used for compile checks only.
    #define ARC_PLATFORM_LINUX 1
#else
    #error "Unsupported platform."
#endif

#ifndef ARC_PLATFORM_WINDOWS
    #define ARC_PLATFORM_WINDOWS 0
#endif
#ifndef ARC_PLATFORM_MACOS
    #define ARC_PLATFORM_MACOS 0
#endif
#ifndef ARC_PLATFORM_LINUX
    #define ARC_PLATFORM_LINUX 0
#endif

// Clang is tested first because clang-cl also defines _MSC_VER.
#if defined(__clang__)
    #define ARC_COMPILER_CLANG 1
    #define ARC_COMPILER_MSVC  0
    #define ARC_COMPILER_GCC   0
#elif defined(_MSC_VER)
    #define ARC_COMPILER_CLANG 0
    #define ARC_COMPILER_MSVC  1
    #define ARC_COMPILER_GCC   0
#elif defined(__GNUC__)
    #define ARC_COMPILER_CLANG 0
    #define ARC_COMPILER_MSVC  0
    #define ARC_COMPILER_GCC   1
#else
    #error "Unsupported compiler."
#endif

// CMake defines NDEBUG for Release, RelWithDebInfo and MinSizeRel.
#if defined(NDEBUG)
    #define ARC_DEBUG 0
#else
    #define ARC_DEBUG 1
#endif

#if ARC_COMPILER_MSVC
    #define ARC_DEBUG_BREAK() __debugbreak()
#elif ARC_COMPILER_CLANG
    #define ARC_DEBUG_BREAK() __builtin_debugtrap()
#else
    #define ARC_DEBUG_BREAK() __builtin_trap()
#endif
