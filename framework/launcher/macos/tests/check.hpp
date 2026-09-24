#pragma once

#include <cstdio>

//
// The whole test harness. The launcher is its own small CMake project with
// no third-party dependency, and GoogleTest would be the first -- for two
// test functions, a failure counter is enough.
//
namespace launcher::tests
{
    inline int  gFailures = 0;

    // Exit code ctest reads as "skipped" -- SKIP_RETURN_CODE in CMakeLists.txt.
    inline constexpr int  kSkipped = 77;

    inline auto result() -> int
    {
        return gFailures == 0 ? 0 : 1;
    }
}

#define LAUNCHER_CHECK( condition) \
    do \
    { \
        if ( !( condition)) \
        { \
            std::fprintf( stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            ++launcher::tests::gFailures; \
        } \
    } \
    while ( false)
