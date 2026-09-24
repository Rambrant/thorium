#include "single_instance.hpp"

#include <cerrno>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace launcher
{
    SingleInstanceGuard::SingleInstanceGuard( const std::string & lockPath)
    {
        // O_CLOEXEC: a server or browser spawned later must not inherit this
        // descriptor, or it would go on holding the lock after the launcher
        // itself had gone.
        mFd = open( lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if ( mFd < 0)
        {
            mError = errno;
            mState = State::Unavailable;
            return;
        }
        mState = flock( mFd, LOCK_EX | LOCK_NB) == 0 ? State::Acquired : State::HeldByAnother;
    }

    SingleInstanceGuard::~SingleInstanceGuard()
    {
        if ( mFd >= 0)
        {
            close( mFd);
        }
    }
}
