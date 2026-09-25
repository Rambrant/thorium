#include "child_process.hpp"

namespace launcher
{
    ChildProcess::~ChildProcess()
    {
        reset();
    }

    ChildProcess::ChildProcess( ChildProcess && other) noexcept
        : mInfo( other.mInfo), mStarted( other.mStarted)
    {
        other.mInfo = PROCESS_INFORMATION{};
        other.mStarted = false;
    }

    auto ChildProcess::operator=( ChildProcess && other) noexcept -> ChildProcess &
    {
        if ( this != &other)
        {
            reset();
            mInfo = other.mInfo;
            mStarted = other.mStarted;
            other.mInfo = PROCESS_INFORMATION{};
            other.mStarted = false;
        }
        return *this;
    }

    auto ChildProcess::reset() -> void
    {
        if ( mStarted)
        {
            CloseHandle( mInfo.hProcess);
            CloseHandle( mInfo.hThread);
            mInfo = PROCESS_INFORMATION{};
            mStarted = false;
        }
    }

    auto ChildProcess::start( std::wstring commandLine, const ProcessJob & job) -> bool
    {
        reset();

        STARTUPINFOW  startup{};
        startup.cb = sizeof( startup);

        // CREATE_SUSPENDED so the job assignment below cannot lose a race
        // against the child spawning grandchildren of its own before it is a
        // job member -- resumed via ResumeThread once assign() has run.
        //
        // CREATE_NO_WINDOW because the launcher is a GUI program with no
        // console of its own, so a console child -- thorium_webui -- would
        // otherwise get a fresh console window, an extra black window beside
        // the real one. The child still has a (hidden) console, which
        // run_scripts then inherits instead of opening one per run. Windows
        // ignores the flag for GUI children, which is what Chrome is.
        const auto created = CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr, nullptr,
            FALSE,
            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
            nullptr, nullptr,
            &startup, &mInfo);

        if ( created == 0)
        {
            return false;
        }

        mStarted = true;
        job.assign( mInfo.hProcess);
        ResumeThread( mInfo.hThread);
        return true;
    }

    auto ChildProcess::running() const -> bool
    {
        if ( !mStarted)
        {
            return false;
        }

        DWORD  exitCode = 0;
        if ( GetExitCodeProcess( mInfo.hProcess, &exitCode) == 0)
        {
            return false;
        }
        return exitCode == STILL_ACTIVE;
    }

    auto ChildProcess::terminate() const -> void
    {
        if ( mStarted)
        {
            TerminateProcess( mInfo.hProcess, 1);
        }
    }
}
