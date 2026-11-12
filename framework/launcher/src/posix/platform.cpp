//
// The POSIX half of platform.hpp: what macOS and Linux agree on -- the lock, the
// process group with its watchdog, spawning, the argument vector. What they do
// not (where the support directory is, where Chrome is, how an error is shown)
// is in system_macos.cpp beside this file, and a Linux bench adds
// system_linux.cpp. Plain POSIX, nothing that needed more than the compiler the rest
// of Thorium uses.
//
#include "platform.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <unistd.h>

extern char ** environ;

namespace launcher::platform
{
    namespace
    {
        constexpr int   kHighestInheritedFd = 1024;

        // How long a run in flight is given to end by itself once the launcher
        // has gone: run_scripts takes SIGTERM as "stop after the current test",
        // then runs its teardown hooks and safes the rig, and killing it part
        // way through that leaves a rig energised with nobody watching. The
        // bound is for a run that never reaches a boundary -- then the promise
        // that nothing outlives the launcher wins.
        constexpr auto  kRunSeconds = 60;
        constexpr long  kPollNanoseconds = 100'000'000;

        // The grace when the group cannot be watched (see endGroup).
        constexpr auto  kGraceSeconds = 2;

        auto pause( timespec duration) -> void
        {
            while ( nanosleep( &duration, &duration) != 0 && errno == EINTR)
            {
            }
        }

        //
        // Ends everything in the watchdog's group and never returns: SIGTERM,
        // then SIGKILL once the group is empty or kRunSeconds have passed --
        // normally the former, well inside a second, with nothing left for the
        // SIGKILL to find.
        //
        // "Is the group empty" is kill( -group, 0), which POSIX has and which
        // needs no list of processes -- but the watchdog leads the group, and
        // would be counted. So the work is handed to a fork that leaves for a
        // session of its own (setsid, which a group leader may not call and its
        // child may), and the watchdog exits: what is left in the group is
        // exactly what the launcher spawned into it.
        //
        [[noreturn]]
        auto endGroup() -> void
        {
            const auto  group = getpgrp();
            const auto  reaper = fork();

            if ( reaper > 0)
            {
                _exit( 0);
            }

            if ( reaper == 0 && setsid() != -1)
            {
                kill( -group, SIGTERM);

                for ( long waited = 0; waited < kRunSeconds * 1'000'000'000L && kill( -group, 0) == 0;
                      waited += kPollNanoseconds)
                {
                    pause( timespec{ 0, kPollNanoseconds });
                }

                kill( -group, SIGKILL);
                _exit( 0);
            }

            // No fork, or no session: still in the group, so it cannot be seen
            // to empty. A fixed grace instead, as short as it has to be for the
            // SIGKILL to be certain.
            kill( 0, SIGTERM);
            pause( timespec{ kGraceSeconds, 0 });

            // Takes this process with it -- it is the last thing left to do.
            kill( 0, SIGKILL);
            _exit( 0);
        }

        //
        // Runs in the forked child and never returns. Only async-signal-safe
        // calls from here on: this is a fork of the launcher, and although
        // the launcher has no threads yet when it forks, nothing below needs
        // anything more than raw system calls anyway.
        //
        [[noreturn]]
        auto runWatchdog( int lifeline) -> void
        {
            setpgid( 0, 0);

            // SIGTERM must not stop it: endGroup's fallback signals its own
            // group, which includes itself, before the SIGKILL that follows. SIGINT
            // and SIGHUP too: a Ctrl-C in the terminal the launcher was
            // started from goes to the terminal's foreground group, which
            // the launcher is in and this is not, but ignoring them costs
            // nothing and keeps the cleanup from depending on that.
            signal( SIGTERM, SIG_IGN);
            signal( SIGINT, SIG_IGN);
            signal( SIGHUP, SIG_IGN);

            // Drop every descriptor inherited from the launcher except the
            // lifeline -- above all the single-instance lock, which would
            // otherwise stay held for as long as a run takes to end after the
            // launcher exited.
            for ( int fd = 3; fd < kHighestInheritedFd; ++fd)
            {
                if ( fd != lifeline)
                {
                    close( fd);
                }
            }

            char  byte;
            while ( true)
            {
                const auto  n = read( lifeline, &byte, 1);
                if ( n == 0 || ( n < 0 && errno != EINTR))
                {
                    break;
                }
            }

            endGroup();
        }

        auto toArgv( const std::vector<std::string> & argv) -> std::vector<char *>
        {
            std::vector<char *>  args;
            args.reserve( argv.size() + 1);
            for ( const auto & arg : argv)
            {
                args.push_back( const_cast<char *>( arg.c_str()));
            }
            args.push_back( nullptr);
            return args;
        }

        //
        // posix_spawn with the launcher's usual attributes. `group` of 0 means
        // "its own group" for POSIX_SPAWN_SETPGROUP, so a negative value is
        // what says "no group at all, stay in the launcher's".
        //
        auto spawnProcess( const std::vector<std::string> & argv, pid_t group, const std::string & output) -> int
        {
            if ( argv.empty())
            {
                return EINVAL;
            }

            auto  args = toArgv( argv);

            posix_spawnattr_t  attr;
            posix_spawnattr_init( &attr);

            // The launcher ignores SIGCHLD so the kernel reaps its children
            // without a waitpid loop (see ProcessGroup's constructor). That
            // disposition would be inherited across exec, and thorium_webui
            // needs the default back: it waitpid()s for run_scripts' exit
            // code, which an ignored SIGCHLD would make unavailable.
            sigset_t  defaults;
            sigemptyset( &defaults);
            sigaddset( &defaults, SIGCHLD);
            posix_spawnattr_setsigdefault( &attr, &defaults);

            short  flags = POSIX_SPAWN_SETSIGDEF;
            if ( group >= 0)
            {
                posix_spawnattr_setpgroup( &attr, group);
                flags |= POSIX_SPAWN_SETPGROUP;
            }
            posix_spawnattr_setflags( &attr, flags);

            // Opened in the child, by posix_spawn itself: nothing is opened here
            // that the launcher would then have to close again.
            posix_spawn_file_actions_t  actions;
            posix_spawn_file_actions_init( &actions);
            if ( !output.empty())
            {
                posix_spawn_file_actions_addopen( &actions, STDOUT_FILENO, output.c_str(),
                                                  O_WRONLY | O_CREAT | O_APPEND, 0644);
                posix_spawn_file_actions_adddup2( &actions, STDOUT_FILENO, STDERR_FILENO);
            }

            pid_t       pid = 0;
            const auto  result = posix_spawn( &pid, args.front(), &actions, &attr, args.data(), environ);

            posix_spawn_file_actions_destroy( &actions);
            posix_spawnattr_destroy( &attr);
            return result;
        }
    }

    auto arguments( int argc, char ** argv) -> std::vector<std::string>
    {
        // argv on macOS is already UTF-8.
        return std::vector<std::string>( argv + std::min( argc, 1), argv + argc);
    }

    SingleInstance::SingleInstance( const std::string & supportDir)
    {
        // O_CLOEXEC: a server or browser spawned later must not inherit this
        // descriptor, or it would go on holding the lock after the launcher
        // itself had gone.
        const auto  path = supportDir + "/launcher.lock";
        const auto  fd = open( path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if ( fd < 0)
        {
            mError = path + ": " + std::strerror( errno);
            return;
        }

        mHandle = fd;
        mState = flock( fd, LOCK_EX | LOCK_NB) == 0 ? State::Acquired : State::HeldByAnother;
    }

    SingleInstance::~SingleInstance()
    {
        if ( mHandle >= 0)
        {
            close( static_cast<int>( mHandle));
        }
    }

    ProcessGroup::ProcessGroup()
    {
        int  fds[ 2];
        if ( pipe( fds) != 0)
        {
            return;
        }

        // The write end must stay the launcher's alone. A server that
        // inherited a copy would keep the pipe open by itself, and the
        // watchdog would never see EOF while it lived -- exactly the case it
        // exists for.
        fcntl( fds[ 1], F_SETFD, FD_CLOEXEC);

        const auto  pid = fork();
        if ( pid < 0)
        {
            close( fds[ 0]);
            close( fds[ 1]);
            return;
        }

        if ( pid == 0)
        {
            close( fds[ 1]);
            runWatchdog( fds[ 0]);
        }

        close( fds[ 0]);

        // Also set from this side: whichever of the two setpgid calls runs
        // first wins, so the group exists before the first spawn() below no
        // matter how the scheduler orders them.
        setpgid( pid, pid);

        // Let the kernel reap exited children -- a superseded Chrome process,
        // or a server that died -- rather than keeping a waitpid loop for
        // processes whose exit codes nothing here reads. spawnProcess
        // restores the default for the children themselves.
        signal( SIGCHLD, SIG_IGN);

        mHandle = pid;
        mLifeline = fds[ 1];
        mValid = true;
    }

    ProcessGroup::~ProcessGroup()
    {
        if ( mLifeline >= 0)
        {
            close( static_cast<int>( mLifeline));
        }
    }

    auto ProcessGroup::spawn( const std::vector<std::string> & argv, const std::string & output) const
        -> std::string
    {
        if ( !mValid)
        {
            return "no process group";
        }

        const auto  error = spawnProcess( argv, static_cast<pid_t>( mHandle), output);
        return error == 0 ? std::string() : std::strerror( error);
    }

    auto spawnDetached( const std::vector<std::string> & argv) -> bool
    {
        return spawnProcess( argv, -1, {}) == 0;
    }
}
