#include "child_stream.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

#if defined( _WIN32)
    // WIN32_LEAN_AND_MEAN / NOMINMAX are set via target_compile_definitions
    // in CMakeLists.txt, not #define'd here: <functional> above already pulls
    // in MinGW's own bits/os_defines.h, which defines NOMINMAX itself, and a
    // second #define after that is a redefinition under -Werror. A command-line
    // -D, applied before any header is parsed, has no such ordering to lose.
    #include <windows.h>
#else
    #include <csignal>
    #include <sys/wait.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

namespace webui
{
    namespace
    {
        //
        // Not std::promise<int>/std::future<int> -- a minimal, deliberately
        // boring stand-in for the one thing this file needs from them. On
        // this toolchain, <future> plus this project's mandatory
        // -static-libstdc++ (see the top-level CMakeLists.txt) fails to link
        // at all: std::logic_error's copy constructor -- pulled in by
        // std::future_error, which derives from it -- comes back out of
        // libstdc++.a as a second, non-foldable definition of a symbol a
        // translation unit including <future> also emits, and ld refuses the
        // "multiple definition" rather than picking one. Confirmed with a
        // two-line reproduction with no reflection, no contracts and none of
        // this file's own code in it -- a toolchain limitation, not a bug
        // here to fix by using <future> more carefully.
        //
        class ExitWaiter
        {
            public:
                auto set( int exitCode) -> void
                {
                    std::lock_guard  lock( mMutex);
                    mExitCode = exitCode;
                    mCv.notify_all();
                }

                [[nodiscard]]
                auto get() -> int
                {
                    std::unique_lock  lock( mMutex);
                    mCv.wait( lock, [ this] { return mExitCode.has_value(); });
                    return *mExitCode;
                }

            private:
                std::mutex               mMutex;
                std::condition_variable  mCv;
                std::optional<int>       mExitCode;
        };
    }

#if defined( _WIN32)

    namespace
    {
        //
        // The one thing CreateProcessW does not do for you: turning argv back
        // into the single string it will itself re-split. Every value here can
        // be arbitrary operator text (see child_stream.hpp's class comment), so
        // this is the actual Microsoft-documented algorithm -- doubling
        // backslashes only where a quote follows them, never elsewhere -- not
        // the "wrap it in quotes" shortcut that only survives values with no
        // quote or trailing backslash of their own.
        //
        auto quoteArg( const std::wstring & arg) -> std::wstring
        {
            if ( !arg.empty()
              && arg.find_first_of( L" \t\n\v\"") == std::wstring::npos)
            {
                return arg;
            }

            std::wstring  out = L"\"";

            for ( auto it = arg.begin(); ; ++it)
            {
                std::size_t  backslashes = 0;
                while ( it != arg.end() && *it == L'\\')
                {
                    ++it;
                    ++backslashes;
                }

                if ( it == arg.end())
                {
                    out.append( backslashes * 2, L'\\');
                    break;
                }
                if ( *it == L'"')
                {
                    out.append( backslashes * 2 + 1, L'\\');
                    out.push_back( L'"');
                }
                else
                {
                    out.append( backslashes, L'\\');
                    out.push_back( *it);
                }
            }

            out.push_back( L'"');
            return out;
        }

        auto toWide( const std::string & utf8) -> std::wstring
        {
            if ( utf8.empty())
            {
                return {};
            }

            const auto  needed = MultiByteToWideChar(
                CP_UTF8, 0, utf8.data(), static_cast<int>( utf8.size()), nullptr, 0);

            std::wstring  wide( static_cast<std::size_t>( needed), L'\0');
            MultiByteToWideChar(
                CP_UTF8, 0, utf8.data(), static_cast<int>( utf8.size()), wide.data(), needed);
            return wide;
        }

        auto buildCommandLine( const std::vector<std::string> & argv) -> std::wstring
        {
            std::wstring  line;
            for ( const auto & arg : argv)
            {
                if ( !line.empty())
                {
                    line += L' ';
                }
                line += quoteArg( toWide( arg));
            }
            return line;
        }
    }

    struct ChildStream::Platform
    {
        HANDLE  Process{ nullptr };
        HANDLE  Thread{ nullptr };
        HANDLE  ReadEnd{ nullptr };
        HANDLE  StderrReadEnd{ nullptr };

        // Guards StdinWrite and Process, which requestStop() and kill() use
        // from a request thread while the reader thread closes them.
        std::mutex  Mutex;
        HANDLE      StdinWrite{ nullptr };
    };

    ChildStream::ChildStream( Handlers handlers)
        : mHandlers( std::move( handlers))
    {
    }

    ChildStream::~ChildStream()
    {
        join();
        delete mPlatform;
    }

    auto ChildStream::start( std::vector<std::string> argv) -> bool
    {
        auto  platform = std::make_unique<Platform>();

        SECURITY_ATTRIBUTES  pipeAttrs{};
        pipeAttrs.nLength = sizeof( pipeAttrs);
        pipeAttrs.bInheritHandle = TRUE;

        HANDLE  writeEnd = nullptr;
        if ( CreatePipe( &platform->ReadEnd, &writeEnd, &pipeAttrs, 0) == 0)
        {
            return false;
        }
        SetHandleInformation( platform->ReadEnd, HANDLE_FLAG_INHERIT, 0);

        HANDLE  stderrWriteEnd = nullptr;
        if ( CreatePipe( &platform->StderrReadEnd, &stderrWriteEnd, &pipeAttrs, 0) == 0)
        {
            CloseHandle( platform->ReadEnd);
            CloseHandle( writeEnd);
            return false;
        }
        SetHandleInformation( platform->StderrReadEnd, HANDLE_FLAG_INHERIT, 0);

        // stdin is a pipe too, the one way this program can tell a run to stop
        // (see requestStop). Nothing else is ever written to it, and a child
        // that reads it without being asked to (no --stop-on-stdin) simply
        // blocks, which is what it did on NUL's cousin anyway.
        // stdout and stderr each get their own pipe: see child_stream.hpp's
        // Handlers comment on why a preflight failure's explanation must not
        // be silently discarded, and must not be folded into OnLine's stream
        // either.
        HANDLE  stdinRead = nullptr;
        if ( CreatePipe( &stdinRead, &platform->StdinWrite, &pipeAttrs, 0) == 0)
        {
            CloseHandle( platform->ReadEnd);
            CloseHandle( writeEnd);
            CloseHandle( platform->StderrReadEnd);
            CloseHandle( stderrWriteEnd);
            return false;
        }
        SetHandleInformation( platform->StdinWrite, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW  startup{};
        startup.cb = sizeof( startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdinRead;
        startup.hStdOutput = writeEnd;
        startup.hStdError = stderrWriteEnd;

        auto  commandLine = buildCommandLine( argv);
        PROCESS_INFORMATION  info{};

        // No CREATE_SUSPENDED / job assignment here, unlike
        // framework/launcher/src/windows/platform.cpp: this process inherits
        // whatever job the console server itself is a member of (it does not
        // set CREATE_BREAKAWAY_FROM_JOB), so when the launcher's job goes
        // down, this child goes down with it -- see framework/launcher's
        // README on how the group is torn down.
        const auto  created = CreateProcessW(
            nullptr, commandLine.data(),
            nullptr, nullptr, TRUE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr, nullptr,
            &startup, &info);

        CloseHandle( writeEnd);
        CloseHandle( stderrWriteEnd);
        CloseHandle( stdinRead);

        if ( created == 0)
        {
            CloseHandle( platform->ReadEnd);
            CloseHandle( platform->StderrReadEnd);
            CloseHandle( platform->StdinWrite);
            return false;
        }

        platform->Process = info.hProcess;
        platform->Thread = info.hThread;
        mPlatform = platform.release();

        mStderrReader = std::thread( &ChildStream::readStderrLoop, this);
        mReader = std::thread( &ChildStream::readLoop, this);
        return true;
    }

    auto ChildStream::readLoop() -> void
    {
        char  buffer[ 4096];
        DWORD read = 0;

        while ( ReadFile( mPlatform->ReadEnd, buffer, sizeof( buffer), &read, nullptr) != 0
             && read > 0)
        {
            mPartialLine.append( buffer, read);

            std::size_t  pos;
            while ( ( pos = mPartialLine.find( '\n')) != std::string::npos)
            {
                auto  line = mPartialLine.substr( 0, pos);
                if ( !line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if ( mHandlers.OnLine)
                {
                    mHandlers.OnLine( line);
                }
                mPartialLine.erase( 0, pos + 1);
            }
        }

        CloseHandle( mPlatform->ReadEnd);

        {
            std::lock_guard  lock( mPlatform->Mutex);
            CloseHandle( mPlatform->StdinWrite);
            mPlatform->StdinWrite = nullptr;
        }

        // Drain stderr before reporting the exit, not after: a caller told
        // "it's over" must already have every stderr line a preflight
        // failure wrote, not receive it as a straggler after OnExit.
        if ( mStderrReader.joinable())
        {
            mStderrReader.join();
        }

        WaitForSingleObject( mPlatform->Process, INFINITE);

        DWORD  exitCode = 0;
        GetExitCodeProcess( mPlatform->Process, &exitCode);

        {
            std::lock_guard  lock( mPlatform->Mutex);
            CloseHandle( mPlatform->Process);
            mPlatform->Process = nullptr;
        }
        CloseHandle( mPlatform->Thread);

        if ( mHandlers.OnExit)
        {
            mHandlers.OnExit( static_cast<int>( exitCode));
        }
    }

    auto ChildStream::readStderrLoop() -> void
    {
        char  buffer[ 4096];
        DWORD read = 0;

        while ( ReadFile( mPlatform->StderrReadEnd, buffer, sizeof( buffer), &read, nullptr) != 0
             && read > 0)
        {
            mPartialStderrLine.append( buffer, read);

            std::size_t  pos;
            while ( ( pos = mPartialStderrLine.find( '\n')) != std::string::npos)
            {
                auto  line = mPartialStderrLine.substr( 0, pos);
                if ( !line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if ( mHandlers.OnStderrLine)
                {
                    mHandlers.OnStderrLine( line);
                }
                mPartialStderrLine.erase( 0, pos + 1);
            }
        }

        CloseHandle( mPlatform->StderrReadEnd);
    }

    auto ChildStream::join() -> void
    {
        if ( mReader.joinable())
        {
            mReader.join();
        }
    }

    auto ChildStream::requestStop() -> bool
    {
        if ( mPlatform == nullptr)
        {
            return false;
        }

        std::lock_guard  lock( mPlatform->Mutex);

        if ( mPlatform->StdinWrite == nullptr)
        {
            return false;
        }

        static constexpr char  kStop[] = "stop\n";
        DWORD  written = 0;

        return WriteFile( mPlatform->StdinWrite, kStop, sizeof( kStop) - 1, &written, nullptr) != 0
            && written == sizeof( kStop) - 1;
    }

    auto ChildStream::kill() -> bool
    {
        if ( mPlatform == nullptr)
        {
            return false;
        }

        std::lock_guard  lock( mPlatform->Mutex);

        return mPlatform->Process != nullptr && TerminateProcess( mPlatform->Process, 1) != 0;
    }

    auto runBlocking( std::vector<std::string> argv) -> BlockingResult
    {
        BlockingResult  result;
        ExitWaiter      exitWaiter;

        ChildStream  child( ChildStream::Handlers{
            .OnLine = [ &result]( const std::string & line)
            {
                result.Output += line;
                result.Output += '\n';
            },
            .OnExit = [ &exitWaiter]( int exitCode)
            {
                exitWaiter.set( exitCode);
            },
        });

        result.Started = child.start( std::move( argv));
        if ( !result.Started)
        {
            return result;
        }

        child.join();
        result.ExitCode = exitWaiter.get();
        return result;
    }

#else // POSIX

    namespace
    {
        auto buildArgv( const std::vector<std::string> & argv, std::vector<char *> & storage) -> char * *
        {
            storage.clear();
            storage.reserve( argv.size() + 1);
            for ( const auto & arg : argv)
            {
                // execvp does not modify argv, but its signature is
                // char *const[] rather than const char *const[] for
                // historical reasons -- the const_cast is the standard,
                // safe workaround every POSIX exec wrapper needs.
                storage.push_back( const_cast<char *>( arg.c_str()));
            }
            storage.push_back( nullptr);
            return storage.data();
        }
    }

    struct ChildStream::Platform
    {
        pid_t  Pid{ -1 };
        int    ReadFd{ -1 };
        int    StderrReadFd{ -1 };

        // Guards StdinFd and Reaped, which requestStop() and kill() use from a
        // request thread while the reader thread closes and reaps.
        std::mutex  Mutex;
        int         StdinFd{ -1 };
        bool        Reaped{ false };
    };

    ChildStream::ChildStream( Handlers handlers)
        : mHandlers( std::move( handlers))
    {
    }

    ChildStream::~ChildStream()
    {
        join();
        delete mPlatform;
    }

    auto ChildStream::start( std::vector<std::string> argv) -> bool
    {
        if ( argv.empty())
        {
            return false;
        }

        int  pipeFds[ 2];
        if ( pipe( pipeFds) != 0)
        {
            return false;
        }

        int  stderrPipeFds[ 2];
        if ( pipe( stderrPipeFds) != 0)
        {
            close( pipeFds[ 0]);
            close( pipeFds[ 1]);
            return false;
        }

        int  stdinPipeFds[ 2];
        if ( pipe( stdinPipeFds) != 0)
        {
            close( pipeFds[ 0]);
            close( pipeFds[ 1]);
            close( stderrPipeFds[ 0]);
            close( stderrPipeFds[ 1]);
            return false;
        }

        // Writing "stop" to a child that has already exited is an ordinary
        // outcome of a click that raced the end of a run, and must be an error
        // return rather than a SIGPIPE that ends this server.
        std::signal( SIGPIPE, SIG_IGN);

        const auto  pid = fork();
        if ( pid < 0)
        {
            close( pipeFds[ 0]);
            close( pipeFds[ 1]);
            close( stderrPipeFds[ 0]);
            close( stderrPipeFds[ 1]);
            close( stdinPipeFds[ 0]);
            close( stdinPipeFds[ 1]);
            return false;
        }

        if ( pid == 0)
        {
            // Child: stdout and stderr each go to their own pipe -- see
            // child_stream.hpp's Handlers comment on why stderr is captured
            // rather than discarded, and kept separate from stdout's own
            // stream -- and stdin to a third, the one way this program can
            // tell a run to stop (see requestStop).
            dup2( pipeFds[ 1], STDOUT_FILENO);
            dup2( stderrPipeFds[ 1], STDERR_FILENO);
            dup2( stdinPipeFds[ 0], STDIN_FILENO);

            close( pipeFds[ 0]);
            close( pipeFds[ 1]);
            close( stderrPipeFds[ 0]);
            close( stderrPipeFds[ 1]);
            close( stdinPipeFds[ 0]);
            close( stdinPipeFds[ 1]);

            std::vector<char *>  storage;
            execvp( argv.front().c_str(), buildArgv( argv, storage));

            // Only reached if execvp failed. _exit, not exit: this is a
            // forked copy of the server and must not run its atexit
            // handlers or flush its parent's buffered stdio.
            _exit( 127);
        }

        close( pipeFds[ 1]);
        close( stderrPipeFds[ 1]);
        close( stdinPipeFds[ 0]);

        // Not inherited by any later child (--safe, the next run's): a second
        // holder of this end is a pipe that never closes.
        fcntl( stdinPipeFds[ 1], F_SETFD, FD_CLOEXEC);

        auto  platform = std::make_unique<Platform>();
        platform->StdinFd = stdinPipeFds[ 1];
        platform->Pid = pid;
        platform->ReadFd = pipeFds[ 0];
        platform->StderrReadFd = stderrPipeFds[ 0];
        mPlatform = platform.release();

        mStderrReader = std::thread( &ChildStream::readStderrLoop, this);
        mReader = std::thread( &ChildStream::readLoop, this);
        return true;
    }

    auto ChildStream::readLoop() -> void
    {
        char  buffer[ 4096];
        ssize_t  bytesRead;

        while ( ( bytesRead = read( mPlatform->ReadFd, buffer, sizeof( buffer))) > 0)
        {
            mPartialLine.append( buffer, static_cast<std::size_t>( bytesRead));

            std::size_t  pos;
            while ( ( pos = mPartialLine.find( '\n')) != std::string::npos)
            {
                if ( mHandlers.OnLine)
                {
                    mHandlers.OnLine( mPartialLine.substr( 0, pos));
                }
                mPartialLine.erase( 0, pos + 1);
            }
        }

        close( mPlatform->ReadFd);

        {
            std::lock_guard  lock( mPlatform->Mutex);
            close( mPlatform->StdinFd);
            mPlatform->StdinFd = -1;
        }

        // Drain stderr before reporting the exit, not after -- same ordering
        // as the Windows readLoop, and the same reason.
        if ( mStderrReader.joinable())
        {
            mStderrReader.join();
        }

        //
        // Polled rather than blocked in, and under the lock: kill() must never
        // signal a pid that has been reaped and may have been handed to
        // another process, so reaping and "has been reaped" change together.
        // The child has closed its stdout by now, so this is a few
        // milliseconds, not a wait on the run.
        //
        int  status = 0;
        for ( ;;)
        {
            {
                std::lock_guard  lock( mPlatform->Mutex);

                if ( waitpid( mPlatform->Pid, &status, WNOHANG) != 0)
                {
                    mPlatform->Reaped = true;
                    break;
                }
            }

            usleep( 5000);
        }

        const auto  exitCode = WIFEXITED( status) ? WEXITSTATUS( status) : -1;

        if ( mHandlers.OnExit)
        {
            mHandlers.OnExit( exitCode);
        }
    }

    auto ChildStream::readStderrLoop() -> void
    {
        char     buffer[ 4096];
        ssize_t  bytesRead;

        while ( ( bytesRead = read( mPlatform->StderrReadFd, buffer, sizeof( buffer))) > 0)
        {
            mPartialStderrLine.append( buffer, static_cast<std::size_t>( bytesRead));

            std::size_t  pos;
            while ( ( pos = mPartialStderrLine.find( '\n')) != std::string::npos)
            {
                if ( mHandlers.OnStderrLine)
                {
                    mHandlers.OnStderrLine( mPartialStderrLine.substr( 0, pos));
                }
                mPartialStderrLine.erase( 0, pos + 1);
            }
        }

        close( mPlatform->StderrReadFd);
    }

    auto ChildStream::join() -> void
    {
        if ( mReader.joinable())
        {
            mReader.join();
        }
    }

    auto ChildStream::requestStop() -> bool
    {
        if ( mPlatform == nullptr)
        {
            return false;
        }

        std::lock_guard  lock( mPlatform->Mutex);

        if ( mPlatform->StdinFd < 0)
        {
            return false;
        }

        static constexpr char  kStop[] = "stop\n";

        return write( mPlatform->StdinFd, kStop, sizeof( kStop) - 1)
            == static_cast<ssize_t>( sizeof( kStop) - 1);
    }

    auto ChildStream::kill() -> bool
    {
        if ( mPlatform == nullptr)
        {
            return false;
        }

        std::lock_guard  lock( mPlatform->Mutex);

        return !mPlatform->Reaped && ::kill( mPlatform->Pid, SIGKILL) == 0;
    }

    auto runBlocking( std::vector<std::string> argv) -> BlockingResult
    {
        BlockingResult  result;
        ExitWaiter      exitWaiter;

        ChildStream  child( ChildStream::Handlers{
            .OnLine = [ &result]( const std::string & line)
            {
                result.Output += line;
                result.Output += '\n';
            },
            .OnExit = [ &exitWaiter]( int exitCode)
            {
                exitWaiter.set( exitCode);
            },
        });

        result.Started = child.start( std::move( argv));
        if ( !result.Started)
        {
            return result;
        }

        child.join();
        result.ExitCode = exitWaiter.get();
        return result;
    }

#endif
}
