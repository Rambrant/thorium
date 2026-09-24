#include <cstdio>
#include <string_view>

auto runSignatureTests() -> int;
auto runMenuTests() -> int;
auto runProfileLockTests() -> int;

//
// One binary, one test per argument, so ctest can report (and skip) each on
// its own -- see CMakeLists.txt.
//
auto main( int argc, char ** argv) -> int
{
    const std::string_view  which = argc > 1 ? argv[ 1] : "";

    if ( which == "signatures")
    {
        return runSignatureTests();
    }
    if ( which == "menu")
    {
        return runMenuTests();
    }
    if ( which == "profile_lock")
    {
        return runProfileLockTests();
    }

    std::fprintf( stderr, "usage: launcher_macos_tests signatures|menu|profile_lock\n");
    return 2;
}
