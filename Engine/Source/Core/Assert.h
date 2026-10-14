#pragma once

#include "Core/Platform.h"

#include <format>
#include <source_location>
#include <string_view>

// Asserts check for programming errors. Runtime failures that can legitimately
// happen (missing file, no compatible GPU) are reported through return values.
//
//   ARC_ASSERT(cond, ...)  Debug only. cond is not evaluated in release.
//   ARC_VERIFY(cond, ...)  cond is always evaluated; failure is reported in debug only.
//   ARC_UNREACHABLE()      Reports and breaks in every build type.
//
// The optional trailing arguments are a std::format string and its arguments.

#ifndef ARC_ENABLE_ASSERTS
    #define ARC_ENABLE_ASSERTS ARC_DEBUG
#endif

namespace arc::detail
{
    void reportAssertFailure(std::string_view condition, const std::source_location& where,
                             std::string_view message = {});
}

#if ARC_ENABLE_ASSERTS

    #define ARC_ASSERT(cond, ...)                                                                 \
        do                                                                                        \
        {                                                                                         \
            if (!(cond)) [[unlikely]]                                                             \
            {                                                                                     \
                ::arc::detail::reportAssertFailure(#cond, std::source_location::current()        \
                                                   __VA_OPT__(, std::format(__VA_ARGS__)));       \
                ARC_DEBUG_BREAK();                                                                \
            }                                                                                     \
        } while (0)

    #define ARC_VERIFY(cond, ...) ARC_ASSERT(cond __VA_OPT__(, ) __VA_ARGS__)

#else

    // sizeof keeps cond type-checked without evaluating it.
    #define ARC_ASSERT(cond, ...) do { (void)sizeof(cond); } while (0)
    #define ARC_VERIFY(cond, ...) do { (void)(cond); } while (0)

#endif

#define ARC_UNREACHABLE()                                                                         \
    do                                                                                            \
    {                                                                                             \
        ::arc::detail::reportAssertFailure("unreachable code reached",                            \
                                           std::source_location::current());                      \
        ARC_DEBUG_BREAK();                                                                        \
    } while (0)
