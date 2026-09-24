#include "process_group.hpp"

#include <cerrno>
#include <csignal>
#include <ctime>

#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>

extern char ** environ;

namespace launcher
{
    namespace
    {
        constexpr int   kHighestInheritedFd = 1024;
        constexpr auto  kGraceSeconds = 2;

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

            // The watchdog signals its own group, which includes itself --
            // SIGTERM must not stop it before the SIGKILL that follows. SIGINT
            // and SIGHUP too: a Ctrl-C in the terminal the launcher was
            // started from goes to the terminal's foreground group, which
            // the launcher is in and this is not, but ignoring them costs
            // nothing and keeps the cleanup from depending on that.
            signal( SIGTERM, SIG_IGN);
            signal( SIGINT, SIG_IGN);
            signal( SIGHUP, SIG_IGN);

            // Drop every descriptor inherited from the launcher except the
            // lifeline -- above all the single-instance lock, which would
            // otherwise stay held for the grace period after the launcher
            // exited.
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

            kill( 0, SIGTERM);

            timespec  grace{ kGraceSeconds, 0 };
            while ( nanosleep( &grace, &grace) != 0 && errno == EINTR)
            {
            }

            // Takes the watchdog with it -- it is the last thing left to do.
            kill( 0, SIGKILL);
            _exit( 0);
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

        mGroup = pid;
        mLifeline = fds[ 1];
    }

    ProcessGroup::~ProcessGroup()
    {
        if ( mLifeline >= 0)
        {
            close( mLifeline);
        }
    }

    auto ProcessGroup::spawn( const std::vector<std::string> & argv, const std::string & output) const -> int
    {
        if ( !valid() || argv.empty())
        {
            return EINVAL;
        }

        std::vector<char *>  args;
        args.reserve( argv.size() + 1);
        for ( const auto & arg : argv)
        {
            args.push_back( const_cast<char *>( arg.c_str()));
        }
        args.push_back( nullptr);

        posix_spawnattr_t  attr;
        posix_spawnattr_init( &attr);
        posix_spawnattr_setpgroup( &attr, mGroup);

        // The launcher ignores SIGCHLD so the kernel reaps its children
        // without a waitpid loop (see main.cpp). That disposition would be
        // inherited across exec, and thorium_webui needs the default back:
        // it waitpid()s for run_scripts' exit code, which an ignored SIGCHLD
        // would make unavailable.
        sigset_t  defaults;
        sigemptyset( &defaults);
        sigaddset( &defaults, SIGCHLD);
        posix_spawnattr_setsigdefault( &attr, &defaults);

        posix_spawnattr_setflags( &attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF);

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
