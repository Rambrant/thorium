//
// The menu bar item, built for real and driven from code: that its menu has
// the right items in the right order, that each one reaches its own handler
// through its own target and action (a click takes the same path), that the
// icon is set and is a template, and that postToMain() and stop() really
// wake and end the event loop from another thread.
//
// This is what the signature test cannot see -- a method that exists and is
// called with the right types, but on the wrong item or not at all.
//
// Needs a logged-in desktop session: an NSStatusItem is a real window-server
// object. Without one (a CI runner, an ssh session) it reports itself
// skipped rather than failed.
//

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "appkit.hpp"
#include "check.hpp"
#include "status_item.hpp"

// CoreGraphics and CoreFoundation, declared by hand for the same reason
// AppKit is reached through the runtime: their headers do not build with GCC.
// Both are plain C functions, so the linker checks these names.
extern "C" auto CGSessionCopyCurrentDictionary() -> const void *;
extern "C" auto CFRelease( const void *) -> void;

namespace
{
    constexpr auto  kLoopTimeout = std::chrono::seconds( 10);

    auto haveDesktopSession() -> bool
    {
        const auto  session = CGSessionCopyCurrentDictionary();
        if ( session == nullptr)
        {
            return false;
        }
        CFRelease( session);
        return true;
    }
}

auto runMenuTests() -> int
{
    if ( !haveDesktopSession())
    {
        std::printf( "skipped: no logged-in desktop session to put a menu bar item in\n");
        return launcher::tests::kSkipped;
    }

    (void)launcher::appkit::kInit( launcher::appkit::kAllocPool.onClass());

    int  shown = 0;
    int  safed = 0;
    int  quits = 0;

    launcher::StatusItem  item(
        "Thorium launcher test",
        launcher::StatusItem::Handlers{
            .OnShowConsole = [ & ] { ++shown; },
            .OnSafeTheRig = [ & ] { ++safed; },
            .OnQuit = [ & ] { ++quits; },
        });

    const std::vector<std::string>  expected{ "Show console", "Safe the rig", "-", "Quit" };
    LAUNCHER_CHECK( item.menuTitles() == expected);

    LAUNCHER_CHECK( item.hasImage());
    LAUNCHER_CHECK( item.imageIsTemplate());
    LAUNCHER_CHECK( item.toolTip() == "Thorium launcher test");

    // Each item reaches its own handler, and only its own.
    item.performItem( 0);
    LAUNCHER_CHECK( shown == 1 && safed == 0 && quits == 0);
    item.performItem( 1);
    LAUNCHER_CHECK( shown == 1 && safed == 1 && quits == 0);
    item.performItem( 3);
    LAUNCHER_CHECK( shown == 1 && safed == 1 && quits == 1);

    // postToMain from a std::thread -- the path "Safe the rig" reports a
    // failure through -- runs on the main thread, and stop() from there
    // makes run() return. A loop that never wakes would hang the test, so a
    // timer fails it instead.
    std::atomic<bool>  ranOnMain{ false };
    std::atomic<bool>  loopEnded{ false };
    const auto         mainThread = std::this_thread::get_id();

    std::thread  guard( [ & ]
    {
        const auto  deadline = std::chrono::steady_clock::now() + kLoopTimeout;
        while ( !loopEnded && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50));
        }
        if ( !loopEnded)
        {
            std::fprintf( stderr, "run() did not return within %lld s of postToMain + stop\n",
                          static_cast<long long>( kLoopTimeout.count()));
            _exit( 1);
        }
    });

    std::thread( [ & ]
    {
        launcher::StatusItem::postToMain( [ & ]
        {
            ranOnMain = std::this_thread::get_id() == mainThread;
            launcher::StatusItem::stop();
        });
    }).join();

    item.run();
    loopEnded = true;
    guard.join();

    LAUNCHER_CHECK( ranOnMain);

    return launcher::tests::result();
}
