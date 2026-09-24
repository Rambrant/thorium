#include "browser_launch.hpp"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <string_view>

#include <libproc.h>
#include <unistd.h>

#include "appkit.hpp"

namespace launcher
{
    auto findChrome() -> std::optional<std::string>
    {
        const auto  app = appkit::kUrlForBundleId(
            appkit::kSharedWorkspace.onClass(), appkit::string( "com.google.Chrome"));
        if ( app == nullptr)
        {
            return std::nullopt;
        }

        const auto  bundle = appkit::kBundleWithUrl.onClass( app);
        const auto  executable = bundle != nullptr ? appkit::kExecutablePath( bundle) : nullptr;
        if ( executable == nullptr)
        {
            return std::nullopt;
        }
        return std::string( appkit::kFileSystemRepresentation( executable));
    }

    auto buildAppModeArgv(
        const std::string & chromePath,
        const std::string & url,
        const std::string & userDataDir) -> std::vector<std::string>
    {
        return {
            chromePath,
            "--app=" + url,
            "--user-data-dir=" + userDataDir,
            "--window-size=1024,768",
            "--no-first-run",
            "--no-default-browser-check",
        };
    }

    auto clearStaleProfileLock( const std::string & userDataDir) -> bool
    {
        const auto  lock = userDataDir + "/SingletonLock";

        // A symlink whose target is "<hostname>-<pid>", pointing nowhere.
        char        target[ PATH_MAX];
        const auto  length = readlink( lock.c_str(), target, sizeof( target) - 1);
        if ( length <= 0)
        {
            return false;   // no lock, nothing to do
        }

        const std::string_view  owner( target, static_cast<std::size_t>( length));
        const auto              dash = owner.rfind( '-');
        const auto              pid = dash == std::string_view::npos
                                          ? 0l
                                          : std::strtol( std::string( owner.substr( dash + 1)).c_str(), nullptr, 10);

        // Alive and Chrome: somebody's Chrome really has this profile open.
        // The hostname is deliberately not compared -- it is the part that
        // goes stale, and a pid is only meaningful on this machine anyway.
        if ( pid > 0 && ( kill( static_cast<pid_t>( pid), 0) == 0 || errno == EPERM))
        {
            char  path[ PROC_PIDPATHINFO_MAXSIZE];
            if ( proc_pidpath( static_cast<int>( pid), path, sizeof( path)) > 0 &&
                 std::string_view( path).find( "Google Chrome") != std::string_view::npos)
            {
                return false;
            }
        }

        // All three: Chrome writes them together, and a lock removed without
        // its socket and cookie is just a different stale state.
        for ( const auto * name : { "/SingletonLock", "/SingletonSocket", "/SingletonCookie" })
        {
            (void)unlink( ( userDataDir + name).c_str());
        }
        return true;
    }
}
