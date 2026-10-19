//
// The launcher leaves a Chrome profile lock behind every time it quits: its
// watchdog ends the group with SIGTERM and then SIGKILL, and a Chrome killed
// that way never cleans up its SingletonLock. Chrome clears a dead lock by
// itself -- but only one recorded under this machine's current hostname, and
// a laptop's hostname follows the network it is on ("Mac.localdomain" on one,
// "MacBook-Air.local" on another). Across such a change Chrome takes the lock
// for another computer's and says so, which is the "profile in use on another
// computer" an operator saw.
//
// Safe to decide here because the launcher is the only thing that runs Chrome
// on this profile, and it holds its own single-instance lock while it does: by
// the time this runs, a lock naming a process that is gone -- or that is not
// Chrome -- is stale.
//
#include "platform.hpp"

#include "profile_lock.hpp"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <string_view>

#include <unistd.h>

namespace launcher::platform
{
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
        if ( pid > 0 && ( kill( static_cast<pid_t>( pid), 0) == 0 || errno == EPERM) && processIsChrome( pid))
        {
            return false;
        }

        // All three: Chrome writes them together, and a lock removed without
        // its socket and cookie is just a different stale state.
        for ( const auto * name : { "/SingletonLock", "/SingletonSocket", "/SingletonCookie" })
        {
            (void)unlink( ( userDataDir + name).c_str());
        }
        return true;
    }

    auto prepareProfile( const std::string & userDataDir) -> void
    {
        // Silently: it is there after every session, so saying so is noise.
        (void)clearStaleProfileLock( userDataDir);
    }
}
