#include "single_instance.hpp"

namespace launcher
{
    namespace
    {
        // "Local\\" rather than the default Global namespace: this guards one
        // console per interactive session, which is the unit "two operators"
        // means on a bench PC with one logged-in user.
        constexpr auto kMutexName = L"Local\\ThoriumLauncherSingleInstance";
    }

    SingleInstanceGuard::SingleInstanceGuard()
    {
        mMutex = CreateMutexW( nullptr, TRUE, kMutexName);
        mAcquired = ( mMutex != nullptr) && ( GetLastError() != ERROR_ALREADY_EXISTS);
    }

    SingleInstanceGuard::~SingleInstanceGuard()
    {
        if ( mMutex != nullptr)
        {
            if ( mAcquired)
            {
                ReleaseMutex( mMutex);
            }
            CloseHandle( mMutex);
        }
    }
}
