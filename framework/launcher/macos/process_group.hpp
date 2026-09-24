#pragma once

#include <string>
#include <vector>

#include <sys/types.h>

namespace launcher
{
    //
    // The macOS stand-in for the Windows launcher's Job object with
    // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. macOS has no such kernel object, so
    // the same promise -- "the launcher is gone" means "nothing of the
    // console is still running", even when the launcher crashes -- is built
    // from two things that do exist:
    //
    // - One process group. Every child this program spawns (the server, each
    //   Chrome window) is put into it by posix_spawn, and their own children
    //   stay in it too: thorium_webui starts run_scripts with a plain fork(),
    //   which keeps the group.
    //
    // - A watchdog, forked once in the constructor and leading that group,
    //   holding the read end of a pipe whose only write end is this object's.
    //   When the write end closes -- the destructor on a clean exit, or the
    //   kernel closing every descriptor of a launcher that crashed or was
    //   killed -- the watchdog's read() returns 0 and it signals the whole
    //   group: SIGTERM, a short grace period, then SIGKILL. That EOF is the
    //   same guarantee the Job object's last-handle-closed rule gives: it is
    //   the kernel's doing, not a destructor's, so it happens on a crash too.
    //
    // The constructor forks, so it must run before anything starts a thread
    // -- in practice, before the first Cocoa call. See main.cpp.
    //
    // What it cannot cover: a process that deliberately leaves the group
    // (setsid, setpgid). Nothing in the console does; the Windows side has
    // the matching gap for CREATE_BREAKAWAY_FROM_JOB.
    //
    class ProcessGroup
    {
        public:
            ProcessGroup();
            ~ProcessGroup();

            ProcessGroup( const ProcessGroup &) = delete;
            auto operator=( const ProcessGroup &) -> ProcessGroup & = delete;

            [[nodiscard]]
            auto valid() const -> bool { return mGroup > 0; }

            //
            // Starts argv[0] (a path, not a name to search PATH for) inside
            // the group. Returns 0 or the errno posix_spawn reported. The
            // process is not tracked afterwards -- the group is what gets
            // torn down, not individual children, the same way the Windows
            // launcher leaves a superseded browser window to the job.
            //
            [[nodiscard]]
            auto spawn( const std::vector<std::string> & argv) const -> int;

        private:
            pid_t  mGroup{ -1 };
            int    mLifeline{ -1 };
    };
}
