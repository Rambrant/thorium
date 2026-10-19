#include <cstdio>
#include <string_view>

auto runProfileLockTests() -> int;

//
// One binary, one test per argument, so ctest can report each on its own --
// see CMakeLists.txt.
//
auto main( int argc, char ** argv) -> int
{
    const std::string_view  which = argc > 1 ? argv[ 1] : "";

    if ( which == "profile_lock")
    {
        return runProfileLockTests();
    }

    std::fprintf( stderr, "usage: launcher_tests profile_lock\n");
    return 2;
}
