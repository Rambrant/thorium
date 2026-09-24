#pragma once

#include <string>

namespace launcher
{
    //
    // The macOS counterpart of the Windows launcher's named mutex: an
    // exclusive flock() on a file under ~/Library/Application Support/Thorium.
    //
    // A lock file is what the Windows side's comment warns against, but the
    // warning is about a file whose *existence* is the lock -- that one is
    // left behind by a crash. flock() is different in the way that matters:
    // the lock belongs to the open file, and the kernel drops it when the
    // last descriptor to that file closes, which happens on a crash or a
    // kill -9 just as it does on a clean exit. The file itself staying on
    // disk means nothing.
    //
    class SingleInstanceGuard
    {
        public:
            explicit SingleInstanceGuard( const std::string & lockPath);
            ~SingleInstanceGuard();

            SingleInstanceGuard( const SingleInstanceGuard &) = delete;
            auto operator=( const SingleInstanceGuard &) -> SingleInstanceGuard & = delete;

            enum class State
            {
                Acquired,
                HeldByAnother,
                // The lock file could not be opened at all -- reported as
                // itself, not as "already running", which would send the
                // operator looking for a console that does not exist.
                Unavailable,
            };

            //
            // LOCK_NB, so a second launcher learns it is second from the
            // flock() call itself rather than by blocking behind the first.
            //
            [[nodiscard]]
            auto state() const -> State { return mState; }

            // errno from the failed open(), for State::Unavailable.
            [[nodiscard]]
            auto error() const -> int { return mError; }

        private:
            int    mFd{ -1 };
            State  mState{ State::Unavailable };
            int    mError{ 0 };
    };
}
