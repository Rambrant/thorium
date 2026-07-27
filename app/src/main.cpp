#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

#include "core/active_test_catalog.hpp"
#include "hal/safing.hpp"

//
// Runner for the test-script catalog (core/active_test_catalog.hpp). Four
// modes, matching what tools/run-tests.sh expects:
//
//   run_scripts                    run every test in the catalog
//   run_scripts --list-tests       print "group|id|description", one per
//                                  test, then exit -- nothing is run
//   run_scripts --select=a,b,c     run only the named test ids (from any
//                                  group), in catalog order
//   run_scripts --safe             drop the rig to a known idle state and
//                                  exit -- no test is run, nothing is
//                                  measured, nothing is reported
//
// ids are only ever compared for a match against what's already in the
// catalog -- never parsed into anything -- so a typo in --select just
// means that test doesn't run, not a crash or an unintended one.
//
// --safe exists for a caller outside this process. The rig console
// supervises a run as a child process, and on an abnormal exit it cannot
// reach into the dead process's hal::fabric to open anything -- so it
// re-invokes this same binary with --safe instead, which is why the mode
// lives here rather than in a separate tool. Using the suite binary itself
// matters: it was built against this rig's exact hal (see
// hal/instrument.inc), so it needs no independent description of what
// instruments exist to safe, and cannot disagree with the run it is
// cleaning up after.
//
// Deliberately exclusive with the other three, and checked before them:
// --safe is what you pass when something has already gone wrong, so
// combining it with a run would mean choosing whether to safe before or
// after testing -- neither of which is what the flag means.
//
namespace
{
    auto splitCommaList( std::string_view csv) -> std::vector<std::string_view>
    {
        std::vector<std::string_view> parts;
        std::size_t                   start = 0;

        while ( start <= csv.size())
        {
            auto comma = csv.find( ',', start);
            auto end   = (comma == std::string_view::npos) ? csv.size() : comma;

            if ( end > start)
                parts.push_back( csv.substr( start, end - start));

            if ( comma == std::string_view::npos)
                break;

            start = comma + 1;
        }

        return parts;
    }

    auto isSelected( std::string_view id, const std::vector<std::string_view> & selection) -> bool
    {
        return selection.empty() || std::find( selection.begin(), selection.end(), id) != selection.end();
    }

    void listTests()
    {
        for ( const auto & group : core::catalog::Catalog)
            for ( const auto & test : group.tests)
                std::cout << group.name << '|' << test.id << '|' << test.description << '\n';
    }

    auto runTests( const std::vector<std::string_view> & selection) -> bool
    {
        bool allPassed = true;
        bool ranAny    = false;

        for ( const auto & group : core::catalog::Catalog)
        {
            for ( const auto & test : group.tests)
            {
                if ( !isSelected( test.id, selection))
                    continue;

                ranAny = true;
                std::cout << "--- " << group.name << "::" << test.id << " -- " << test.description << " ---\n";
                allPassed &= test.script( group.name, test.id);
            }
        }

        if ( !ranAny)
        {
            std::cerr << "No catalog test matched --select; nothing ran.\n";
            return false;
        }

        return allPassed;
    }
} // namespace

int main( int argc, char ** argv)
{
    std::vector<std::string_view> selection; // empty => run everything
    bool                          listOnly = false;
    bool                          safeOnly = false;

    for ( int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if ( arg == "--list-tests")
        {
            listOnly = true;
        }
        else if ( arg == "--safe")
        {
            safeOnly = true;
        }
        else if ( arg.starts_with( "--select="))
        {
            selection = splitCommaList( arg.substr( std::string_view{ "--select="}.size()));
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }

    //
    // Checked before --list-tests, and before any script runs -- see this
    // file's own comment on why --safe is exclusive with the other modes.
    // Always exits 0: safing is unconditional and has nothing to report a
    // failure about, and a console invoking this after a child crash has
    // no useful way to act on a non-zero exit here anyway.
    //
    if ( safeOnly)
    {
        hal::safeRig();
        std::cout << "Rig safed: all outputs off, all relays open.\n";
        return 0;
    }

    if ( listOnly)
    {
        listTests();
        return 0;
    }

    bool allPassed = runTests( selection);

    std::cout << "\n=== " << (allPassed ? "ALL SCRIPTS PASSED" : "SOME SCRIPTS FAILED") << " ===\n";

    return allPassed ? 0 : 1;
}
