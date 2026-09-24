#include <cstdio>
#include <string_view>

auto runSignatureTests() -> int;
auto runMenuTests() -> int;

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

    std::fprintf( stderr, "usage: launcher_macos_tests signatures|menu\n");
    return 2;
}
