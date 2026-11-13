//
// The Windows half of platform.hpp: Win32 and nothing else. The launcher is a
// GUI-subsystem program with no window of its own, so that Windows gives it no
// console -- every child it starts is created without one for the same reason
// (see createProcess).
//
#include "platform.hpp"

#include <cstdio>
#include <cwchar>
#include <string>
#include <string_view>

#include <windows.h>

#include <shellapi.h>
#include <tlhelp32.h>

namespace launcher::platform
{
    namespace
    {
        // "Local\\" rather than the default Global namespace: this guards one
        // console per interactive session, which is the unit "two operators"
        // means on a bench PC with one logged-in user.
        constexpr auto  kMutexName = L"Local\\ThoriumLauncherSingleInstance";

        // What the launcher's own executable is started with to be the
        // watchdog instead (see startWatchdog). Not one of the launcher's
        // flags: nobody types it.
        constexpr auto  kWatchdogFlag = L"--thorium-watchdog";

        // How long a run in flight is given to end by itself once the launcher
        // has gone -- the same bound, for the same reason, as the POSIX
        // watchdog's (src/posix/platform.cpp): run_scripts finishes its current
        // test, runs its teardown hooks and safes the rig, and killing it part
        // way through that leaves a rig energised with nobody watching.
        constexpr DWORD  kRunMilliseconds = 60'000;
        constexpr DWORD  kPollMilliseconds = 100;

        //
        // Exists from the moment a launcher has the single-instance mutex until
        // the last thing it started has gone -- which, with a run in flight
        // when the launcher died, is up to kRunMilliseconds after the launcher
        // itself. The single-instance mutex cannot say that: it is the
        // launcher's, and is gone the moment the launcher is. A console started
        // again in that minute would otherwise come up beside the old run's
        // last test and its safing, and could start a second run on the same
        // rig.
        //
        // A named event that is never set: all that matters about it is that
        // it is there for as long as somebody has it open. The launcher creates
        // it and the watchdog inherits it; nothing else does.
        //
        constexpr auto  kTeardownName = L"Local\\ThoriumLauncherTeardown";

        // A little over kRunMilliseconds, so the old console's kill comes first.
        constexpr DWORD  kTeardownWaitMilliseconds = kRunMilliseconds + 5'000;

        HANDLE  gTeardown = nullptr;

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

        // This is the console now. If the one before it died with a run in
        // flight, that run is still winding down and its watchdog still has
        // the teardown event open: wait for it here, before anything is
        // started.
        auto holdTeardown() -> void
        {
            for ( DWORD waited = 0; waited < kTeardownWaitMilliseconds; waited += kPollMilliseconds)
            {
                const auto  held = OpenEventW( SYNCHRONIZE, FALSE, kTeardownName);
                if ( held == nullptr)
                {
                    break;
                }
                CloseHandle( held);

                if ( waited == 0)
                {
                    static constexpr auto  kNotice =
                        "The previous console's run is still finishing; waiting for it before starting.\n";
                    std::fputs( kNotice, stderr);
                    OutputDebugStringA( kNotice);
                }
                Sleep( kPollMilliseconds);
            }

            // Past the bound it has been killed by now, whatever had it open;
            // and not having it is not worth refusing to start over -- the
            // lock that matters is held.
            gTeardown = CreateEventW( nullptr, TRUE, FALSE, kTeardownName);
        }

        //
        // Ends what the launcher itself started -- the server and Chrome --
        // and nothing they started. That is how a run is asked to stop here,
        // where there is no SIGTERM to send it: thorium_webui holds the other
        // end of run_scripts' stdin, the end of that pipe is a stop request
        // (framework/runner/src/stop_sources.hpp, "the console is gone"), and
        // the server's death is what ends it. Chrome's own children leave with
        // their browser process.
        //
        auto endLaunchersChildren( HANDLE job, DWORD launcherPid) -> void
        {
            const auto  snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0);
            if ( snapshot == INVALID_HANDLE_VALUE)
            {
                return;
            }

            PROCESSENTRY32W  entry{};
            entry.dwSize = sizeof( entry);

            for ( auto more = Process32FirstW( snapshot, &entry); more != 0; more = Process32NextW( snapshot, &entry))
            {
                if ( entry.th32ParentProcessID != launcherPid)
                {
                    continue;
                }

                const auto  process = OpenProcess( PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                                   entry.th32ProcessID);
                if ( process == nullptr)
                {
                    continue;
                }

                // In the job, so that it is the launcher's as spawn() started
                // it -- not this watchdog, and not whatever a recycled process
                // id now names.
                BOOL  inJob = FALSE;
                if ( IsProcessInJob( process, job, &inJob) != 0 && inJob != 0)
                {
                    TerminateProcess( process, 1);
                }
                CloseHandle( process);
            }

            CloseHandle( snapshot);
        }

        auto activeProcesses( HANDLE job) -> DWORD
        {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION  info{};
            if ( QueryInformationJobObject( job, JobObjectBasicAccountingInformation, &info, sizeof( info), nullptr) == 0)
            {
                return 0;
            }
            return info.ActiveProcesses;
        }

        //
        // The watchdog: a second process of this executable that outlives the
        // launcher by as long as a run needs to end. It waits for the launcher
        // to go -- a clean exit, a crash, a kill, all the same to a process
        // handle -- ends the server and Chrome, and then waits for the job to
        // empty or for kRunMilliseconds, whichever is first, before ending
        // whatever is left. With no run in flight the job is empty within
        // moments.
        //
        // It holds a handle to the job, which is why the launcher's death no
        // longer closes the last one. If the watchdog itself is killed, its
        // handle goes with it: once the launcher's is closed too, the kernel
        // ends the job at once, as it always did, so nothing outlives both.
        //
        [[noreturn]]
        auto runWatchdog( HANDLE launcher, HANDLE job) -> void
        {
            const auto  launcherPid = GetProcessId( launcher);

            WaitForSingleObject( launcher, INFINITE);

            endLaunchersChildren( job, launcherPid);

            for ( DWORD waited = 0; waited < kRunMilliseconds && activeProcesses( job) > 0; waited += kPollMilliseconds)
            {
                Sleep( kPollMilliseconds);
            }

            TerminateJobObject( job, 1);

            // And the teardown event with it, which is what a console started
            // in the meantime is waiting for.
            ExitProcess( 0);
        }

        auto toHandle( const wchar_t * text) -> HANDLE
        {
            return reinterpret_cast<HANDLE>( static_cast<UINT_PTR>( std::wcstoull( text, nullptr, 10)));
        }

        auto handleText( HANDLE handle) -> std::wstring
        {
            return std::to_wstring( reinterpret_cast<UINT_PTR>( handle));
        }

        //
        // Starts the watchdog for `job`: this executable again, outside the
        // job, inheriting exactly three handles and nothing else -- one to
        // this process, to see it go; the job; and the teardown event. An
        // explicit list rather than "everything inheritable", so that it cannot
        // also end up holding a pipe the launcher was started with and keep
        // whoever reads it waiting for a minute.
        //
        auto startWatchdog( HANDLE job) -> bool
        {
            wchar_t     path[ 32768]{};
            const auto  length = GetModuleFileNameW( nullptr, path, static_cast<DWORD>( std::size( path)));
            if ( length == 0 || length >= std::size( path))
            {
                return false;
            }

            HANDLE  self = nullptr;
            if ( DuplicateHandle( GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &self,
                                  SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0) == 0)
            {
                return false;
            }

            std::vector<HANDLE>  inherited{ self, job };
            if ( gTeardown != nullptr)
            {
                inherited.push_back( gTeardown);
            }
            for ( const auto handle : inherited)
            {
                SetHandleInformation( handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            }

            auto  line = quoteArgument( path) + L" " + kWatchdogFlag + L" " + handleText( self) + L" " + handleText( job);

            SIZE_T  size = 0;
            InitializeProcThreadAttributeList( nullptr, 1, 0, &size);
            std::vector<char>  storage( size);
            const auto  attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( storage.data());

            auto  started = false;
            if ( InitializeProcThreadAttributeList( attributes, 1, 0, &size) != 0)
            {
                STARTUPINFOEXW       startup{};
                PROCESS_INFORMATION  info{};
                startup.StartupInfo.cb = sizeof( startup);
                startup.lpAttributeList = attributes;

                started = UpdateProcThreadAttribute( attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                     inherited.data(), inherited.size() * sizeof( HANDLE),
                                                     nullptr, nullptr) != 0
                       && CreateProcessW( path, line.data(), nullptr, nullptr, TRUE,
                                          EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                                          nullptr, nullptr, &startup.StartupInfo, &info) != 0;
                if ( started)
                {
                    CloseHandle( info.hProcess);
                    CloseHandle( info.hThread);
                }
                DeleteProcThreadAttributeList( attributes);
            }

            // The watchdog has its own copies now, and no later child is to
            // get one: a server holding the job open is the leak again.
            for ( const auto handle : inherited)
            {
                SetHandleInformation( handle, HANDLE_FLAG_INHERIT, 0);
            }
            CloseHandle( self);
            return started;
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

        // Started by startWatchdog rather than by anybody's shortcut: this
        // process is the watchdog, and never becomes a launcher. Here because
        // this is the first thing main() asks the platform, before it has
        // looked for Chrome or taken any lock.
        if ( argc == 4 && std::wstring_view( argv[ 1]) == kWatchdogFlag)
        {
            runWatchdog( toHandle( argv[ 2]), toHandle( argv[ 3]));
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

        if ( mState == State::Acquired)
        {
            holdTeardown();
        }
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
        // The last handle is the watchdog's, not this one: it keeps the job
        // for as long as a run in flight needs to end (see runWatchdog), and
        // a console without one would be back to killing a run where it
        // stands -- so no watchdog is no process group.
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION  limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if ( SetInformationJobObject( job, JobObjectExtendedLimitInformation, &limits, sizeof( limits)) == 0
          || !startWatchdog( job))
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
