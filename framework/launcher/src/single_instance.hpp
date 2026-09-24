#pragma once

#include <windows.h>

namespace launcher
{
    //
    // Guards against a second launcher starting while one already owns the
    // console. A named kernel mutex rather than a lock file: it is released
    // automatically if the owning process dies without cleaning up --
    // crashed, killed from Task Manager, whatever -- which a lock file is
    // not.
    //
    class SingleInstanceGuard
    {
        public:
            SingleInstanceGuard();
            ~SingleInstanceGuard();

            SingleInstanceGuard( const SingleInstanceGuard &) = delete;
            auto operator=( const SingleInstanceGuard &) -> SingleInstanceGuard & = delete;

            //
            // True if this process is the only one holding the mutex. A
            // second launcher gets ERROR_ALREADY_EXISTS from CreateMutexW
            // itself rather than from a subsequent wait, so there is no race
            // to lose.
            //
            [[nodiscard]]
            auto acquired() const -> bool { return mAcquired; }

        private:
            HANDLE  mMutex{ nullptr };
            bool    mAcquired{ false };
    };
}
