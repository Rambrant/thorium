#pragma once

#include <optional>
#include <string>
#include <vector>

namespace launcher
{
    //
    // Asks Launch Services for Chrome by bundle identifier -- the macOS
    // counterpart of the Windows launcher reading the App Paths registry key,
    // chosen for the same reason: it is the lookup that stays correct wherever
    // Chrome was installed (/Applications, ~/Applications, a managed path),
    // rather than a guess at one of them. Returns the executable inside the
    // bundle, since that is what gets spawned: `open -a` would hand the
    // window to Launch Services and put it outside the launcher's process
    // group (process_group.hpp).
    //
    [[nodiscard]]
    auto findChrome() -> std::optional<std::string>;

    //
    // The same flags the Windows launcher passes -- see its
    // browser_launch.hpp for why --app= and a dedicated --user-data-dir.
    // On macOS the dedicated profile also decides what "Show console" does
    // a second time: a Chrome started against a profile that is already open
    // hands the new window to the running instance and exits, so each click
    // is one more window in one Chrome, not one more Chrome.
    //
    [[nodiscard]]
    auto buildAppModeArgv(
        const std::string & chromePath,
        const std::string & url,
        const std::string & userDataDir) -> std::vector<std::string>;

    //
    // Removes the profile lock a Chrome that is no longer running left behind
    // in userDataDir, and leaves a live one alone. True if it removed one.
    //
    // The launcher leaves one every time it quits: its watchdog ends the
    // group with SIGTERM and then SIGKILL (process_group.hpp), and a Chrome
    // killed that way never cleans up its SingletonLock. Chrome clears a
    // dead lock by itself -- but only one recorded under this machine's
    // current hostname, and a Mac's hostname follows the network it is on
    // ("Mac.localdomain" on one, "MacBook-Air.local" on another). Across such
    // a change Chrome takes the lock for another computer's and says so on
    // stderr, which is the error an operator sees.
    //
    // Safe to decide here because the launcher is the only thing that runs
    // Chrome on this profile, and it holds its own single-instance lock
    // (single_instance.hpp) while it does: by the time this runs, a lock
    // naming a process that is gone -- or that is not Chrome -- is stale.
    //
    auto clearStaleProfileLock( const std::string & userDataDir) -> bool;
}
