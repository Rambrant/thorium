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

        // The child gets no stdin of its own -- it points at NUL, so a
        // program that unexpectedly reads it does not block waiting for us.
        // stdout and stderr each get their own pipe: see child_stream.hpp's
        // Handlers comment on why a preflight failure's explanation must not
        // be silently discarded, and must not be folded into OnLine's stream
        // either.
        const auto  nul = CreateFileW(
            L"NUL", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &pipeAttrs, OPEN_EXISTING, 0, nullptr);

        STARTUPINFOW  startup{};
        startup.cb = sizeof( startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = nul;
        startup.hStdOutput = writeEnd;
        startup.hStdError = stderrWriteEnd;

        auto  commandLine = buildCommandLine( argv);
        PROCESS_INFORMATION  info{};

        // No CREATE_SUSPENDED / job assignment here, unlike
        // framework/launcher/src/child_process.cpp: this process inherits
        // whatever job the console server itself is a member of (it does not
        // set CREATE_BREAKAWAY_FROM_JOB), so when the launcher's job goes
        // down, this child goes down with it -- see framework/launcher's
        // README on JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
        const auto  created = CreateProcessW(
            nullptr, commandLine.data(),
            nullptr, nullptr, TRUE,
            CREATE_UNICODE_ENVIRONMENT,
            nullptr, nullptr,
            &startup, &info);

        CloseHandle( writeEnd);
        CloseHandle( stderrWriteEnd);
        if ( nul != nullptr && nul != INVALID_HANDLE_VALUE)
        {
            CloseHandle( nul);
        }

        if ( created == 0)
        {
            CloseHandle( platform->ReadEnd);
            CloseHandle( platform->StderrReadEnd);
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

        CloseHandle( mPlatform->Process);
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

        const auto  pid = fork();
        if ( pid < 0)
        {
            close( pipeFds[ 0]);
            close( pipeFds[ 1]);
            close( stderrPipeFds[ 0]);
            close( stderrPipeFds[ 1]);
            return false;
        }

        if ( pid == 0)
        {
            // Child: stdout and stderr each go to their own pipe, stdin to
            // /dev/null -- see child_stream.hpp's Handlers comment on why
            // stderr is captured rather than discarded, and kept separate
            // from stdout's own stream.
            dup2( pipeFds[ 1], STDOUT_FILENO);
            dup2( stderrPipeFds[ 1], STDERR_FILENO);

            const auto  nullFd = open( "/dev/null", O_RDONLY);
            if ( nullFd >= 0)
            {
                dup2( nullFd, STDIN_FILENO);
                close( nullFd);
            }

            close( pipeFds[ 0]);
            close( pipeFds[ 1]);
            close( stderrPipeFds[ 0]);
            close( stderrPipeFds[ 1]);

            std::vector<char *>  storage;
            execvp( argv.front().c_str(), buildArgv( argv, storage));

            // Only reached if execvp failed. _exit, not exit: this is a
            // forked copy of the server and must not run its atexit
            // handlers or flush its parent's buffered stdio.
            _exit( 127);
        }

        close( pipeFds[ 1]);
        close( stderrPipeFds[ 1]);

        auto  platform = std::make_unique<Platform>();
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

        // Drain stderr before reporting the exit, not after -- same ordering
        // as the Windows readLoop, and the same reason.
        if ( mStderrReader.joinable())
        {
            mStderrReader.join();
        }

        int  status = 0;
        waitpid( mPlatform->Pid, &status, 0);

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
