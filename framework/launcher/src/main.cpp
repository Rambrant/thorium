#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "config.hpp"
#include "console_client.hpp"
#include "platform.hpp"

namespace
{
    constexpr auto  kServerStartTimeoutMs = 10000u;

    // How long no console page may be open, with no run in flight, before the
    // launcher takes that as the operator having closed the console. Long
    // enough to ride out a reload -- the page reconnects in well under a
    // second -- and short enough that "I closed it" means closed.
    constexpr auto  kClosedGraceSeconds = 3;

    // How long nothing may be listening on the port before the server counts
    // as dead. Refused connections only: a server that accepts and does not
    // answer is busy, and a busy server in the middle of a run is left alone.
    constexpr auto  kServerGoneSeconds = 5;

    auto appModeArgv(
        const std::string &                chrome,
        const launcher::Config &           config,
        const std::string &                userDataDir) -> std::vector<std::string>
    {
        // --app=, not --kiosk and not an embedded webview: no tab strip and no
        // address bar, but a real, resizable, closable window. A profile of
        // its own, so the console never borrows the operator's browsing
        // session -- and so that a second window request goes to the Chrome
        // that already has this profile open instead of starting another.
        std::vector<std::string>  argv{
            chrome,
            "--app=http://127.0.0.1:" + std::to_string( config.Port),
            "--user-data-dir=" + userDataDir,
            "--window-size=1024,768",
            "--no-first-run",
            "--no-default-browser-check",
        };
        argv.insert( argv.end(), config.BrowserArgs.begin(), config.BrowserArgs.end());
        return argv;
    }

    //
    // Blocks until the console should end, and says why: 0 when the operator
    // closed its last window, 1 when the server went away under it.
    //
    // Watched through the server rather than through Chrome, because on macOS
    // a Chrome whose last window closes keeps running: there is no process
    // exit to see. Every open console page holds /api/presence open, and the
    // server counts them (see framework/webui/src/main.cpp).
    //
    // Two things keep this from quitting when it must not:
    //
    //   - a run in flight. Closing the window during a run leaves the run
    //     going -- a run must never die because a window closed. The console
    //     quits once the run ends, unless a window has been reopened by then
    //     (starting the launcher again does that).
    //   - never having seen a page at all. Until the first window connects,
    //     zero viewers means "not open yet", not "closed" -- a slow Chrome
    //     start must not be read as the operator giving up.
    //
    auto watchConsole( unsigned short port) -> int
    {
        bool  seenAWindow = false;
        int   idleSeconds = 0;
        int   goneSeconds = 0;

        for ( ;;)
        {
            std::this_thread::sleep_for( std::chrono::seconds( 1));

            const auto  status = launcher::queryStatus( port);

            goneSeconds = status.Server == launcher::ConsoleStatus::State::Gone ? goneSeconds + 1 : 0;
            if ( goneSeconds >= kServerGoneSeconds)
            {
                launcher::platform::reportError( "The console server stopped.");
                return 1;
            }

            const auto  up = status.Server == launcher::ConsoleStatus::State::Up;
            if ( !up || status.Viewers > 0 || status.Running || !seenAWindow)
            {
                seenAWindow = seenAWindow || ( up && status.Viewers > 0);
                idleSeconds = 0;
                continue;
            }

            if ( ++idleSeconds >= kClosedGraceSeconds)
            {
                return 0;
            }
        }
    }

    auto fail( const std::string & message) -> int
    {
        launcher::platform::reportError( message);
        return 1;
    }
}

auto main( int argc, char ** argv) -> int
{
    namespace platform = launcher::platform;

    std::string  configError;
    const auto   config = launcher::parseConfig( platform::arguments( argc, argv), configError);
    if ( !config)
    {
        return fail( configError);
    }

    const auto  dir = platform::supportDir();
    const auto  userDataDir = dir + "/chrome-profile";

    const auto  chrome = platform::findChrome();
    if ( !chrome)
    {
        return fail( "Chrome was not found. Install Chrome to run the bench console.");
    }

    const platform::SingleInstance  singleInstance( dir);
    switch ( singleInstance.state())
    {
        case platform::SingleInstance::State::Acquired:
            break;

        case platform::SingleInstance::State::HeldByAnother:
            // Another launcher already owns the console, and with no menu to
            // ask it from, starting the program again is how an operator who
            // closed the window mid-run gets it back. Chrome hands the window
            // to the one already running on this profile.
            if ( !platform::spawnDetached( appModeArgv( *chrome, *config, userDataDir)))
            {
                return fail( "The bench console is already running, and a window for it could not be opened.");
            }
            return 0;

        case platform::SingleInstance::State::Unavailable:
            return fail( "Could not take the console's lock: " + singleInstance.error());
    }

    // Before anything starts a thread: on macOS this forks.
    const platform::ProcessGroup  group;
    if ( !group.valid())
    {
        return fail( "Could not set up the console's process group.");
    }

    // Returning from main on any failure from here on tears the group down,
    // the server with it.
    std::vector<std::string>  serverArgv{ config->ServerCommand };
    serverArgv.insert( serverArgv.end(), config->ServerArgs.begin(), config->ServerArgs.end());

    if ( const auto error = group.spawn( serverArgv); !error.empty())
    {
        return fail( "Could not start the console server: " + config->ServerCommand + " (" + error + ")");
    }

    if ( !launcher::waitForServer( config->Port, kServerStartTimeoutMs))
    {
        return fail( "The console server did not start in time.");
    }

    // Chrome's own logging goes here rather than to the terminal the console
    // was started from, where it buried the server's lines. Truncated per
    // launch, so it holds this session's and cannot grow without bound.
    const auto  chromeLog = dir + "/chrome.log";
    std::ofstream( chromeLog, std::ios::trunc);

    platform::prepareProfile( userDataDir);

    if ( const auto error = group.spawn( appModeArgv( *chrome, *config, userDataDir), chromeLog); !error.empty())
    {
        return fail( "Could not open Chrome: " + error);
    }

    return watchConsole( config->Port);
}
