//
// clearStaleProfileLock against a real directory, with no Chrome involved:
// the lock is only a symlink, so the states it can be left in are cheap to
// make by hand. Headless.
//
// The case that matters is the first one below -- a dead process under a
// hostname that is no longer this machine's, which is exactly what Chrome
// refuses to clear by itself and what put a "profile is in use on another
// computer" error in the console's terminal. The live-Chrome case (the lock
// must be left alone) needs a real Chrome on the profile, which is what
// teardown.sh's launch already exercises end to end.
//

#include <filesystem>
#include <string>

#include <unistd.h>

#include "browser_launch.hpp"
#include "check.hpp"

namespace
{
    namespace fs = std::filesystem;

    auto makeLock( const fs::path & dir, const std::string & owner) -> void
    {
        fs::create_symlink( owner, dir / "SingletonLock");
        fs::create_symlink( "/nonexistent/SingletonSocket", dir / "SingletonSocket");
        fs::create_symlink( "12345", dir / "SingletonCookie");
    }

    auto anyLeft( const fs::path & dir) -> bool
    {
        for ( const auto * name : { "SingletonLock", "SingletonSocket", "SingletonCookie" })
        {
            if ( fs::is_symlink( dir / name))
            {
                return true;
            }
        }
        return false;
    }
}

auto runProfileLockTests() -> int
{
    char        pattern[] = "/tmp/thorium-profile-lock.XXXXXX";
    const auto  made = mkdtemp( pattern);
    LAUNCHER_CHECK( made != nullptr);
    if ( made == nullptr)
    {
        return launcher::tests::result();
    }
    const fs::path  dir = made;

    // Nothing there: nothing removed, nothing reported.
    LAUNCHER_CHECK( !launcher::clearStaleProfileLock( dir.string()));

    // A dead Chrome, recorded under the hostname the Mac had on another
    // network. pid_t tops out far below this on macOS, so it cannot be alive.
    makeLock( dir, "Mac.localdomain-99999999");
    LAUNCHER_CHECK( launcher::clearStaleProfileLock( dir.string()));
    LAUNCHER_CHECK( !anyLeft( dir));

    // A live process that is not Chrome -- this test itself. A recycled pid
    // must not keep a dead Chrome's lock alive.
    makeLock( dir, "MacBook-Air.local-" + std::to_string( getpid()));
    LAUNCHER_CHECK( launcher::clearStaleProfileLock( dir.string()));
    LAUNCHER_CHECK( !anyLeft( dir));

    // A lock whose target is not "<host>-<pid>" at all is stale too.
    makeLock( dir, "garbage");
    LAUNCHER_CHECK( launcher::clearStaleProfileLock( dir.string()));
    LAUNCHER_CHECK( !anyLeft( dir));

    // Everything else in the profile is left alone.
    { std::FILE * keep = std::fopen( ( dir / "Preferences").c_str(), "w"); if ( keep) { std::fclose( keep); } }
    makeLock( dir, "Mac.localdomain-99999999");
    (void)launcher::clearStaleProfileLock( dir.string());
    LAUNCHER_CHECK( fs::exists( dir / "Preferences"));

    fs::remove_all( dir);
    return launcher::tests::result();
}
