#pragma once

#include <cstdio>

#ifdef CORE_PLATFORM_WINDOWS
    #define CORE_DEBUGBREAK() __debugbreak()
#else
    #define CORE_DEBUGBREAK() ((void)0)
#endif

#define CORE_INTERNAL_FAIL(condition, message)                                                     \
    do                                                                                             \
    {                                                                                              \
        std::fprintf(stderr, "Assertion failed: %s\n  %s\n  %s:%d\n", (condition), (message),      \
                     __FILE__, __LINE__);                                                          \
        CORE_DEBUGBREAK();                                                                         \
    } while (false)

#ifdef CORE_DEBUG
    #define CORE_ASSERT(cond, message)                                                             \
        do                                                                                         \
        {                                                                                          \
            if (!(cond))                                                                           \
                CORE_INTERNAL_FAIL(#cond, message);                                                \
        } while (false)

    #define CORE_VERIFY(cond, message) CORE_ASSERT(cond, message)
#else
    #define CORE_ASSERT(cond, message) ((void)0)

    #define CORE_VERIFY(cond, message)                                                             \
        do                                                                                         \
        {                                                                                          \
            (void)(cond);                                                                          \
        } while (false)
#endif
