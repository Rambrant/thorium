#pragma once

#include <optional>
#include <string>
#include <vector>

namespace launcher
{
    //
    // Everything the launcher needs from the operating system that has no
    // portable spelling -- and nothing else. The rest of the program is
    // ordinary C++ and one HTTP client; this is the whole seam, with one
    // implementation per family: src/windows/platform.cpp, and for POSIX
    // src/posix/platform.cpp (what macOS and Linux agree on) plus a small
    // system_macos.cpp beside it (what they do not: supportDir, findChrome,
    // prepareProfile, reportError). A Linux bench adds system_linux.cpp.
    //
    // All strings are UTF-8, so nothing above this line has to know that
    // Windows spells paths in UTF-16.
    //
    namespace platform
    {
        //
        // The command-line arguments after the program's own name, as UTF-8.
        // Windows hands a program its arguments in the ANSI code page unless
        // asked for the wide command line.
        //
        [[nodiscard]]
        auto arguments( int argc, char ** argv) -> std::vector<std::string>;

        //
        // Where the launcher keeps what it owns -- the lock file, Chrome's
        // profile and log -- created if need be: %LOCALAPPDATA%\Thorium on
        // Windows, ~/Library/Application Support/Thorium on macOS.
        //
        [[nodiscard]]
        auto supportDir() -> std::string;

        //
        // At most one launcher per interactive session, released by the
        // operating system however the holder dies -- crash and kill -9
        // included, which a lock file that merely exists would not be.
        // A named mutex on Windows, an flock() on macOS.
        //
        class SingleInstance
        {
            public:
                enum class State { Acquired, HeldByAnother, Unavailable };

                explicit SingleInstance( const std::string & supportDir);
                ~SingleInstance();

                SingleInstance( const SingleInstance &) = delete;
                auto operator=( const SingleInstance &) -> SingleInstance & = delete;

                [[nodiscard]] auto state() const -> State { return mState; }

                // For Unavailable: what the system said, as a sentence.
                [[nodiscard]] auto error() const -> const std::string & { return mError; }

            private:
                State        mState{ State::Unavailable };
                std::string  mError;
                long long    mHandle{ -1 };
        };

        //
        // Everything started through spawn() goes down with the launcher --
        // on a clean exit, and also when the launcher crashes or is killed,
        // which is the case that matters: a run_scripts left behind is a rig
        // nobody is watching. Each platform builds that promise from what the
        // kernel offers. Windows has it as one object, a Job with
        // KILL_ON_JOB_CLOSE. macOS has no such thing, so a forked watchdog
        // leading a process group holds the read end of a pipe whose only
        // write end is the launcher's: when that closes, the watchdog signals
        // the group (SIGTERM, a grace period, SIGKILL). A child's own
        // children stay inside either, run_scripts included.
        //
        // The macOS watchdog is forked by the constructor, so construct this
        // before starting any thread.
        //
        class ProcessGroup
        {
            public:
                ProcessGroup();
                ~ProcessGroup();

                ProcessGroup( const ProcessGroup &) = delete;
                auto operator=( const ProcessGroup &) -> ProcessGroup & = delete;

                [[nodiscard]] auto valid() const -> bool { return mValid; }

                //
                // Starts argv[0] (a path, not a name to search for) inside the
                // group and does not wait for it. Empty on success, otherwise
                // what went wrong as a sentence.
                //
                // With `output` set, the child's stdout and stderr are
                // appended to that file rather than shared with the launcher
                // -- for Chrome, whose updater and crash-reporter chatter
                // would otherwise bury the server's lines in the terminal the
                // console was started from. Windows has no terminal to
                // protect and ignores it.
                //
                [[nodiscard]]
                auto spawn( const std::vector<std::string> & argv, const std::string & output = {}) const
                    -> std::string;

            private:
                bool       mValid{ false };
                long long  mHandle{ -1 };
                long long  mLifeline{ -1 };
        };

        //
        // The Chrome executable, wherever it was installed: the App Paths
        // registry key on Windows, the standard application folders and then
        // Spotlight on macOS.
        //
        [[nodiscard]]
        auto findChrome() -> std::optional<std::string>;

        //
        // Before Chrome opens a profile the launcher has not had open this
        // session: removes what a killed Chrome left behind in it. Nothing to
        // do on Windows; on macOS see src/posix/system_macos.cpp.
        //
        auto prepareProfile( const std::string & userDataDir) -> void;

        //
        // Starts a process outside the group, for the one case where being
        // torn down with the launcher is wrong: a second launcher asking the
        // first one's Chrome for another window. That Chrome hands the window
        // over and the new process exits, so nothing is left to supervise.
        //
        [[nodiscard]]
        auto spawnDetached( const std::vector<std::string> & argv) -> bool;

        //
        // Tells the operator something went wrong. Always to stderr; and, when
        // nobody is looking at a terminal -- started from a shortcut or a login
        // item -- as a dialog too, since otherwise a launcher that could not
        // start is just a console that never appears.
        //
        auto reportError( const std::string & message) -> void;
    }
}
