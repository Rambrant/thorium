#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "appkit.hpp"
#include "browser_launch.hpp"
#include "config.hpp"
#include "health_check.hpp"
#include "process_group.hpp"
#include "rig_client.hpp"
#include "single_instance.hpp"
#include "status_item.hpp"

namespace
{
    constexpr auto  kServerStartTimeoutMs = 10000u;

    // How long no console page may be open, with no run in flight, before
    // the launcher takes that as the operator having closed the console.
    // Long enough to ride out a reload -- the page reconnects in well under
    // a second -- and short enough that "I closed it" means closed.
    constexpr auto  kClosedGraceSeconds = 3;

    //
    // Quits the console once its last window has closed -- the launcher's
    // counterpart of the old wxWidgets console ending when its frame did.
    //
    // Watched through the server rather than through Chrome, because on
    // macOS a Chrome whose last window closes keeps running: there is no
    // process exit to see. Every open console page holds /api/presence
    // open, and the server counts them (see framework/webui/src/main.cpp).
    //
    // Two things keep this from quitting when it must not:
    //
    //   - a run in flight. Closing the window during a run leaves the run
    //     going, and the menu bar's Safe the rig with it -- a run must never
    //     die because a window closed. The console quits once the run ends,
    //     unless a window has been reopened by then.
    //   - never having seen a page at all. Until the first window connects,
    //     zero viewers means "not open yet", not "closed" -- a slow Chrome
    //     start must not be read as the operator giving up.
    //
    // Only the decision is made here; quitting is StatusItem::stop on the
    // main thread, the same path the menu's Quit takes, so the process
    // group's watchdog tears everything down exactly as it does then.
    //
    auto watchForClosedConsole( unsigned short port) -> void
    {
        std::thread( [ port]
        {
            bool  seenAWindow = false;
            int   idleSeconds = 0;

            for ( ;;)
            {
                std::this_thread::sleep_for( std::chrono::seconds( 1));

                const auto  status = launcher::getConsoleStatus( port);
                if ( !status || status->Viewers > 0 || status->Running || !seenAWindow)
                {
                    seenAWindow = seenAWindow || ( status && status->Viewers > 0);
                    idleSeconds = 0;
                    continue;
                }

                if ( ++idleSeconds >= kClosedGraceSeconds)
                {
                    launcher::StatusItem::postToMain( [] { launcher::StatusItem::stop(); });
                    return;
                }
            }
        }).detach();
    }

    // ~/Library/Application Support/Thorium -- where the Windows launcher
    // uses %LOCALAPPDATA%\Thorium. $HOME rather than NSHomeDirectory(): this
    // runs before the process group forks its watchdog, and nothing before
    // that fork may touch Cocoa (see process_group.hpp).
    auto supportDir() -> std::string
    {
        const auto  home = std::getenv( "HOME");

        std::string  dir = ( home != nullptr && *home != '\0') ? home : ".";
        dir += "/Library/Application Support/Thorium";

        // All the way down: an account (or a test's throwaway $HOME) need
        // not have Application Support yet. A failure here shows up as the
        // lock file failing to open, which main reports as itself.
        std::error_code  ignored;
        std::filesystem::create_directories( dir, ignored);
        return dir;
    }

    // An autorelease pool for the main thread that is never drained: what
    // main creates before the event loop starts lives until exit anyway, and
    // the loop drains its own pool per event after that. See
    // status_item.hpp on why nothing here is released.
    auto startMainPool() -> void
    {
        (void)launcher::appkit::kInit( launcher::appkit::kAllocPool.onClass());
    }

    // Main thread only -- see StatusItem::postToMain for getting here from
    // another one.
    auto alert( const std::string & message) -> void
    {
        using namespace launcher;

        std::fprintf( stderr, "%s\n", message.c_str());

        const auto  app = appkit::kSharedApplication.onClass();
        appkit::kSetActivationPolicy( app, appkit::kActivationPolicyAccessory);
        appkit::kActivate( app);

        const auto  box = appkit::kInit( appkit::kAllocAlert.onClass());
        appkit::kSetAlertStyle( box, appkit::kAlertStyleCritical);
        appkit::kSetMessageText( box, appkit::string( "Thorium Bench Console"));
        appkit::kSetInformativeText( box, appkit::string( message.c_str()));
        (void)appkit::kRunModal( box);
    }

    auto fail( const std::string & message) -> void
    {
        startMainPool();
        alert( message);
    }
}

auto main( int argc, char ** argv) -> int
{
    std::string  configError;
    const auto   config = launcher::parseConfig( argc, argv, configError);
    if ( !config)
    {
        fail( configError);
        return 1;
    }

    const auto  dir = supportDir();

    const auto                           lockPath = dir + "/launcher.lock";
    const launcher::SingleInstanceGuard  singleInstance( lockPath);
    switch ( singleInstance.state())
    {
        case launcher::SingleInstanceGuard::State::Acquired:
            break;

        case launcher::SingleInstanceGuard::State::HeldByAnother:
            fail( "The bench console is already running.");
            return 1;

        case launcher::SingleInstanceGuard::State::Unavailable:
            fail( "Could not open " + lockPath + ": " + std::strerror( singleInstance.error()));
            return 1;
    }

    // Before any Cocoa call: this forks. See process_group.hpp.
    const launcher::ProcessGroup  group;
    if ( !group.valid())
    {
        fail( std::string( "Could not set up the console's process group: ") + std::strerror( errno));
        return 1;
    }

    startMainPool();

    // Let the kernel reap exited children -- a superseded Chrome process,
    // or a server that died -- rather than keeping a waitpid loop for
    // processes whose exit codes nothing here reads. ProcessGroup::spawn
    // restores the default for the children themselves.
    signal( SIGCHLD, SIG_IGN);

    const auto  chrome = launcher::findChrome();
    if ( !chrome)
    {
        alert( "Chrome was not found. Install Chrome to run the bench console.");
        return 1;
    }

    std::vector<std::string>  serverArgv{ config->ServerCommand };
    serverArgv.insert( serverArgv.end(), config->ServerArgs.begin(), config->ServerArgs.end());

    if ( const auto error = group.spawn( serverArgv); error != 0)
    {
        alert( "Could not start the console server: " + config->ServerCommand
             + " (" + std::strerror( error) + ")");
        return 1;
    }

    // Returning from main on any failure from here on closes the process
    // group's lifeline, and its watchdog takes the server down -- the
    // counterpart of the Windows launcher's explicit server.terminate().
    if ( !launcher::waitForServer( config->Port, kServerStartTimeoutMs))
    {
        alert( "The console server did not start in time.");
        return 1;
    }

    const auto  url = "http://127.0.0.1:" + std::to_string( config->Port);
    const auto  userDataDir = dir + "/chrome-profile";

    // Opens another window every time, as on Windows -- but here into the
    // one Chrome already running against userDataDir, which is what makes
    // the second click cheap. See browser_launch.hpp.
    auto  showConsole = [ & ]
    {
        (void)group.spawn( launcher::buildAppModeArgv( *chrome, url, userDataDir));
    };

    // Accessory: a menu bar item and no Dock icon, the way the Windows
    // launcher is a tray icon and no taskbar button. The console window
    // itself is Chrome's, and Chrome has its own Dock icon.
    launcher::appkit::kSetActivationPolicy(
        launcher::appkit::kSharedApplication.onClass(), launcher::appkit::kActivationPolicyAccessory);

    showConsole();

    launcher::StatusItem  statusItem(
        config->Title,
        launcher::StatusItem::Handlers{
            .OnShowConsole = showConsole,
            .OnSafeTheRig = [ port = config->Port]
            {
                std::thread( [ port]
                {
                    if ( !launcher::postSafeTheRig( port))
                    {
                        launcher::StatusItem::postToMain( []
                        {
                            alert( "Safe the rig failed: the console server did not confirm it. "
                                   "Check the rig by hand.");
                        });
                    }
                }).detach();
            },
            .OnQuit = [] { launcher::StatusItem::stop(); },
        });

    watchForClosedConsole( config->Port);

    return statusItem.run();
}
