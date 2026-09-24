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
        MessageBoxW( nullptr, message.c_str(), L"Thorium Bench Console", MB_OK | MB_ICONERROR);
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
                std::thread( [ port] { (void)launcher::postSafeTheRig( port); }).detach();
            },
            .OnQuit = [] { PostQuitMessage( 0); },
        });

    const auto  exitCode = tray.run();

    server.terminate();
    return exitCode;
}
