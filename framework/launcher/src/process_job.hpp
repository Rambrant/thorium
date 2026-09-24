#pragma once

#include <windows.h>

namespace launcher
{
    //
    // Every child this program starts -- the server, each browser window --
    // is assigned to one Job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
    // That is what makes "quit the launcher" mean "nothing of the console is
    // still running", including when the launcher itself dies unexpectedly:
    // the kernel tears the job down when the last handle to it closes, which
    // happens even on a crash, where a destructor-based cleanup would not
    // run.
    //
    class ProcessJob
    {
        public:
            ProcessJob();
            ~ProcessJob();

            ProcessJob( const ProcessJob &) = delete;
            auto operator=( const ProcessJob &) -> ProcessJob & = delete;

            // Not CREATE_BREAKAWAY_FROM_JOB anywhere this is used: a
            // run_scripts left behind by a killed server is exactly the leak
            // this class exists to prevent, so a child's own children stay in
            // the job too.
            auto assign( HANDLE process) const -> bool;

        private:
            HANDLE  mJob{ nullptr };
    };
}
