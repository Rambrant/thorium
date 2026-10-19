#pragma once

#include <string>

namespace launcher::platform
{
    //
    // Removes the profile lock a Chrome that is no longer running left behind
    // in userDataDir, and leaves a live one alone. True if it removed one.
    // See profile_lock.cpp on why it is needed at all; exposed here for
    // tests/test_profile_lock.cpp, which exercises it against a real directory.
    //
    auto clearStaleProfileLock( const std::string & userDataDir) -> bool;

    //
    // Whether the live process `pid` is Chrome -- the one thing the lock check
    // needs from the operating system, so the one thing each system_*.cpp
    // supplies: proc_pidpath on macOS, /proc/<pid>/exe on Linux.
    //
    [[nodiscard]]
    auto processIsChrome( long pid) -> bool;
}
