//
// The Windows half of platform.hpp: Win32 and nothing else. The launcher is a
// GUI-subsystem program with no window of its own, so that Windows gives it no
// console -- every child it starts is created without one for the same reason
// (see createProcess).
//
#include "platform.hpp"

#include <cstdio>
#include <string>

#include <windows.h>

#include <shellapi.h>

namespace launcher::platform
{
    namespace
    {
        // "Local\\" rather than the default Global namespace: this guards one
        // console per interactive session, which is the unit "two operators"
        // means on a bench PC with one logged-in user.
        constexpr auto  kMutexName = L"Local\\ThoriumLauncherSingleInstance";

        constexpr auto  kAppPathsKey =
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe";

        auto toWide( const std::string & text) -> std::wstring
        {
            if ( text.empty())
            {
                return {};
            }
            const auto  size = MultiByteToWideChar( CP_UTF8, 0, text.data(), static_cast<int>( text.size()), nullptr, 0);
            std::wstring  wide( static_cast<std::size_t>( size), L'\0');
            MultiByteToWideChar( CP_UTF8, 0, text.data(), static_cast<int>( text.size()), wide.data(), size);
            return wide;
        }

        auto toUtf8( const std::wstring & wide) -> std::string
        {
            if ( wide.empty())
            {
                return {};
            }
            const auto  size = WideCharToMultiByte( CP_UTF8, 0, wide.data(), static_cast<int>( wide.size()),
                                                    nullptr, 0, nullptr, nullptr);
            std::string  text( static_cast<std::size_t>( size), '\0');
            WideCharToMultiByte( CP_UTF8, 0, wide.data(), static_cast<int>( wide.size()),
                                 text.data(), size, nullptr, nullptr);
            return text;
        }

        // "error 5: Access is denied." -- the system's own wording.
        auto describe( DWORD code) -> std::string
        {
            wchar_t *   buffer = nullptr;
            const auto  length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, code, 0, reinterpret_cast<wchar_t *>( &buffer), 0, nullptr);

            std::string  text = "error " + std::to_string( code);
            if ( length > 0 && buffer != nullptr)
            {
                std::wstring  message( buffer, length);
                while ( !message.empty() && ( message.back() == L'\n' || message.back() == L'\r' || message.back() == L' '))
                {
                    message.pop_back();
                }
                text += ": " + toUtf8( message);
            }
            LocalFree( buffer);
            return text;
        }

        // One argument as CommandLineToArgvW will read it back: quoted, with
        // backslashes doubled only where they precede a quote.
        auto quoteArgument( const std::wstring & arg) -> std::wstring
        {
            std::wstring  quoted = L"\"";
            std::size_t   backslashes = 0;

            for ( const auto c : arg)
            {
                if ( c == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if ( c == L'"')
                {
                    quoted.append( backslashes * 2 + 1, L'\\');
                }
                else
                {
                    quoted.append( backslashes, L'\\');
                }
                backslashes = 0;
                quoted += c;
            }

            quoted.append( backslashes * 2, L'\\');
            return quoted + L"\"";
        }

        auto commandLine( const std::vector<std::string> & argv) -> std::wstring
        {
            std::wstring  line;
            for ( const auto & arg : argv)
            {
                line += ( line.empty() ? L"" : L" ") + quoteArgument( toWide( arg));
            }
            return line;
        }

        //
        // Starts the process, inside `job` when there is one. Empty on
        // success, otherwise the system's reason.
        //
        // CREATE_SUSPENDED so that joining the job cannot lose a race against
        // the child starting grandchildren of its own before it is a member --
        // resumed once it is. Not CREATE_BREAKAWAY_FROM_JOB: a run_scripts left
        // behind by a killed server is exactly the leak the job exists to
        // prevent, so a child's own children stay in it too.
        //
        // CREATE_NO_WINDOW because the launcher has no console, so a console
        // child -- thorium_webui -- would otherwise get a fresh console
        // window, a black one beside the real one. The child still has a
        // (hidden) console, which run_scripts then inherits instead of opening
        // one per run. Windows ignores the flag for GUI children, which is
        // what Chrome is.
        //
        auto createProcess( const std::vector<std::string> & argv, HANDLE job) -> std::string
        {
            if ( argv.empty())
            {
                return "nothing to start";
            }

            auto  line = commandLine( argv);

            STARTUPINFOW         startup{};
            PROCESS_INFORMATION  info{};
            startup.cb = sizeof( startup);

            const auto  created = CreateProcessW(
                nullptr, line.data(), nullptr, nullptr, FALSE,
                CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &info);
            if ( created == 0)
            {
                return describe( GetLastError());
            }

            std::string  error;
            if ( job != nullptr && AssignProcessToJobObject( job, info.hProcess) == 0)
            {
                // Not started: a server outside the job is the leak, and a
                // window outside it is only untidy, but neither is worth the
                // exception.
                error = "could not join the process job: " + describe( GetLastError());
                TerminateProcess( info.hProcess, 1);
            }
            else
            {
                ResumeThread( info.hThread);
            }

            CloseHandle( info.hProcess);
            CloseHandle( info.hThread);
            return error;
        }

        auto readAppPath( HKEY root) -> std::optional<std::string>
        {
            wchar_t  buffer[ MAX_PATH]{};
            DWORD    size = sizeof( buffer);

            const auto  result = RegGetValueW( root, kAppPathsKey, nullptr, RRF_RT_REG_SZ, nullptr, buffer, &size);
            if ( result != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            return toUtf8( buffer);
        }
    }

    auto arguments( int, char **) -> std::vector<std::string>
    {
        int         argc = 0;
        const auto  argv = CommandLineToArgvW( GetCommandLineW(), &argc);
        if ( argv == nullptr)
        {
            return {};
        }

        std::vector<std::string>  args;
        for ( int i = 1; i < argc; ++i)
        {
            args.push_back( toUtf8( argv[ i]));
        }
        LocalFree( argv);
        return args;
    }

    auto supportDir() -> std::string
    {
        wchar_t     base[ MAX_PATH]{};
        const auto  length = GetEnvironmentVariableW( L"LOCALAPPDATA", base, MAX_PATH);

        std::wstring  dir = ( length > 0 && length < MAX_PATH) ? base : L".";
        dir += L"\\Thorium";
        CreateDirectoryW( dir.c_str(), nullptr);
        return toUtf8( dir);
    }

    SingleInstance::SingleInstance( const std::string &)
    {
        // The mutex is the lock; nothing is kept on disk for it.
        const auto  mutex = CreateMutexW( nullptr, TRUE, kMutexName);
        if ( mutex == nullptr)
        {
            mError = describe( GetLastError());
            return;
        }

        // A second launcher gets ERROR_ALREADY_EXISTS from CreateMutexW itself
        // rather than from a subsequent wait, so there is no race to lose.
        mHandle = reinterpret_cast<long long>( mutex);
        mState = GetLastError() == ERROR_ALREADY_EXISTS ? State::HeldByAnother : State::Acquired;
    }

    SingleInstance::~SingleInstance()
    {
        if ( mHandle >= 0)
        {
            const auto  mutex = reinterpret_cast<HANDLE>( mHandle);
            if ( mState == State::Acquired)
            {
                ReleaseMutex( mutex);
            }
            CloseHandle( mutex);
        }
    }

    ProcessGroup::ProcessGroup()
    {
        const auto  job = CreateJobObjectW( nullptr, nullptr);
        if ( job == nullptr)
        {
            return;
        }

        // The kernel tears the job down when the last handle to it closes,
        // which happens on a crash too, where a destructor would not run.
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION  limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if ( SetInformationJobObject( job, JobObjectExtendedLimitInformation, &limits, sizeof( limits)) == 0)
        {
            CloseHandle( job);
            return;
        }

        mHandle = reinterpret_cast<long long>( job);
        mValid = true;
    }

    ProcessGroup::~ProcessGroup()
    {
        if ( mValid)
        {
            CloseHandle( reinterpret_cast<HANDLE>( mHandle));
        }
    }

    auto ProcessGroup::spawn( const std::vector<std::string> & argv, const std::string &) const -> std::string
    {
        return mValid ? createProcess( argv, reinterpret_cast<HANDLE>( mHandle)) : "no process job";
    }

    auto findChrome() -> std::optional<std::string>
    {
        if ( auto path = readAppPath( HKEY_CURRENT_USER))
        {
            return path;
        }
        return readAppPath( HKEY_LOCAL_MACHINE);
    }

    auto prepareProfile( const std::string &) -> void
    {
        // Windows has no profile lock that outlives its Chrome.
    }

    auto spawnDetached( const std::vector<std::string> & argv) -> bool
    {
        return createProcess( argv, nullptr).empty();
    }

    auto reportError( const std::string & message) -> void
    {
        const auto  wide = toWide( message);

        std::fprintf( stderr, "%s\n", message.c_str());
        OutputDebugStringW( wide.c_str());

        // Topmost and foreground: with no owner window, the box would
        // otherwise open behind the console's Chrome window.
        MessageBoxW( nullptr, wide.c_str(), L"Thorium Bench Console",
                     MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    }
}
