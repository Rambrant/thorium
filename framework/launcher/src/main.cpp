#include <chrono>
#include <stop_token>
#include <string>
#include <thread>

#include <windows.h>

#include "browser_launch.hpp"
#include "child_process.hpp"
#include "config.hpp"
#include "health_check.hpp"
#include "process_job.hpp"
#include "rig_client.hpp"
#include "single_instance.hpp"
#include "tray.hpp"

namespace
{
    constexpr auto  kServerStartTimeoutMs = 10000u;

    // How long the console may have no page open before the launcher quits
    // -- long enough for a page reload, which drops and reopens its
    // /api/presence connection, not to be taken for a close. The same three
    // seconds as macos/main.cpp.
    constexpr auto  kClosedGraceSeconds = 3;

    //
    // Quits once the last console window has closed, as macos/main.cpp's
    // watchForClosedConsole does -- by asking the server, not by watching
    // Chrome: every open console page holds /api/presence open, and
    // /api/status reports how many do and whether a run is active (see
    // framework/webui/README.md). Never while a run is in flight: a window
    // closed mid-run leaves the run, the server and "Safe the rig" going,
    // and the launcher quits once the run ends, unless "Show console" has
    // brought a window back by then.
    //
    // Nothing counts until a page has been seen at all, so a slow first
    // Chrome start is not mistaken for a closed console.
    //
    // A jthread, joined when main returns, rather than detached as on macOS:
    // it calls into the tray icon, which main destroys on the way out.
    //
    auto watchForClosedConsole( unsigned short port, const launcher::TrayIcon & tray) -> std::jthread
    {
        return std::jthread( [ port, &tray]( std::stop_token stop)
        {
            bool  seenAWindow = false;
            int   idleSeconds = 0;

            while ( !stop.stop_requested())
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
                    tray.postQuit();
                    return;
                }
            }
        });
    }

    auto quoteArg( const std::wstring & value) -> std::wstring
    {
        return L"\"" + value + L"\"";
    }

    auto buildServerCommandLine( const launcher::Config & config) -> std::wstring
    {
        std::wstring  line = quoteArg( config.ServerCommand);
        for ( const auto & arg : config.ServerArgs)
        {
            line += L" " + quoteArg( arg);
        }
        return line;
    }

    // %LOCALAPPDATA%\Thorium\chrome-profile, kept separate from whatever
    // Chrome profile the operator otherwise uses on that PC -- see
    // browser_launch.hpp on why the console gets its own.
    auto chromeUserDataDir() -> std::wstring
    {
        wchar_t     base[ MAX_PATH]{};
        const auto  len = GetEnvironmentVariableW( L"LOCALAPPDATA", base, MAX_PATH);

        std::wstring  dir = ( len > 0 && len < MAX_PATH) ? base : L".";
        dir += L"\\Thorium";
        CreateDirectoryW( dir.c_str(), nullptr);
        dir += L"\\chrome-profile";
        return dir;
    }

    auto fail( const std::wstring & message) -> void
    {
        OutputDebugStringW( message.c_str());
        // Topmost and foreground: with no owner window, the box would
        // otherwise open behind the console's Chrome window.
        MessageBoxW( nullptr, message.c_str(), L"Thorium Bench Console",
                     MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    }
}

auto WINAPI wWinMain( HINSTANCE, HINSTANCE, PWSTR, int) -> int
{
    std::wstring  configError;
    const auto    config = launcher::parseConfig( configError);
    if ( !config)
    {
        fail( configError);
        return 1;
    }

    const launcher::SingleInstanceGuard  singleInstance;
    if ( !singleInstance.acquired())
    {
        fail( L"The bench console is already running.");
        return 1;
    }

    const auto  chrome = launcher::findChrome();
    if ( !chrome)
    {
        fail( L"Chrome was not found. Install Chrome to run the bench console.");
        return 1;
    }

    const launcher::ProcessJob  job;

    launcher::ChildProcess  server;
    if ( !server.start( buildServerCommandLine( *config), job))
    {
        fail( L"Could not start the console server: " + config->ServerCommand
            + L" (error " + std::to_wstring( GetLastError()) + L")");
        return 1;
    }

    if ( !launcher::waitForServer( config->Port, kServerStartTimeoutMs))
    {
        fail( L"The console server did not start in time.");
        server.terminate();
        return 1;
    }

    const auto  url = L"http://127.0.0.1:" + std::to_wstring( config->Port);
    const auto  userDataDir = chromeUserDataDir();

    launcher::ChildProcess  browserWindow;

    // Always opens a fresh window rather than refocusing an existing one --
    // acceptable for a first cut, since the operator closing the console and
    // pressing "Show console" again is the uncommon path. Refocusing a
    // specific Chrome app-mode window from outside it needs an EnumWindows
    // search by title/class, which is worth adding once this is in real use.
    auto  showConsole = [ & ]
    {
        browserWindow.start(
            launcher::buildAppModeCommandLine( *chrome, url, userDataDir), job);
    };

    showConsole();

    launcher::TrayIcon  tray(
        config->Title,
        launcher::TrayIcon::Handlers{
            .OnShowConsole = showConsole,
            .OnSafeTheRig = [ port = config->Port]
            {
                // The failure is reported, as on macOS: a safe that silently
                // did not happen is the worst way this action can fail.
                // MessageBoxW runs its own modal loop on this background
                // thread, so the tray stays responsive while it is up.
                std::thread( [ port]
                {
                    if ( !launcher::postSafeTheRig( port))
                    {
                        fail( L"Safe the rig failed: the console server did not confirm it. "
                              L"Check the rig by hand.");
                    }
                }).detach();
            },
            .OnQuit = [] { PostQuitMessage( 0); },
        });

    const auto  closedConsoleWatcher = watchForClosedConsole( config->Port, tray);

    const auto  exitCode = tray.run();

    server.terminate();
    return exitCode;
}
