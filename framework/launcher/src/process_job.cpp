#include "process_job.hpp"

namespace launcher
{
    ProcessJob::ProcessJob()
    {
        mJob = CreateJobObjectW( nullptr, nullptr);
        if ( mJob == nullptr)
        {
            return;
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION  limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

        SetInformationJobObject(
            mJob, JobObjectExtendedLimitInformation, &limits, sizeof( limits));
    }

    ProcessJob::~ProcessJob()
    {
        if ( mJob != nullptr)
        {
            CloseHandle( mJob);
        }
    }

    auto ProcessJob::assign( HANDLE process) const -> bool
    {
        return ( mJob != nullptr) && AssignProcessToJobObject( mJob, process) != 0;
    }
}
