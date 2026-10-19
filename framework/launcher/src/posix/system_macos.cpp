//
// The macOS-only half of platform.hpp: where things live on a Mac, where Chrome
// is, and how an error is shown. Everything else is POSIX and in
// platform.cpp beside it. No AppKit, no Objective-C runtime -- nothing here
// needed more than the compiler the rest of Thorium uses.
//
#include "platform.hpp"

#include "profile_lock.hpp"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>

#include <libproc.h>
#include <unistd.h>

namespace launcher::platform
{
    namespace
    {
        constexpr auto  kChromeExecutable = "/Contents/MacOS/Google Chrome";

        auto home() -> std::string
        {
            const auto  value = std::getenv( "HOME");
            return ( value != nullptr && *value != '\0') ? value : ".";
        }

        auto isExecutable( const std::string & path) -> bool
        {
            return access( path.c_str(), X_OK) == 0;
        }

        // AppleScript string literal: backslash and double quote are the only
        // characters that end or alter one.
        auto appleScriptQuoted( const std::string & text) -> std::string
        {
            std::string  quoted = "\"";
            for ( const auto c : text)
            {
                if ( c == '\\' || c == '"')
                {
                    quoted += '\\';
                }
                quoted += c;
            }
            return quoted + "\"";
        }
    }

    auto supportDir() -> std::string
    {
        // $HOME rather than asking Cocoa: a test's throwaway $HOME has to be
        // honoured.
        const auto  dir = home() + "/Library/Application Support/Thorium";

        // All the way down: an account (or a test's throwaway $HOME) need
        // not have Application Support yet. A failure here shows up as the
        // lock file failing to open, which main reports as itself.
        std::error_code  ignored;
        std::filesystem::create_directories( dir, ignored);
        return dir;
    }

    auto findChrome() -> std::optional<std::string>
    {
        // Where Chrome installs itself, for one account or for all of them.
        for ( const auto & folder : { std::string( "/Applications"), home() + "/Applications"})
        {
            const auto  executable = folder + "/Google Chrome.app" + kChromeExecutable;
            if ( isExecutable( executable))
            {
                return executable;
            }
        }

        // Anywhere else -- a managed path, a renamed folder -- Spotlight knows
        // by bundle identifier. Only asked once the usual places have failed:
        // it is a process and an index lookup.
        std::optional<std::string>  found;
        if ( auto * pipe = popen( "mdfind \"kMDItemCFBundleIdentifier == 'com.google.Chrome'\" 2>/dev/null", "r"))
        {
            char  line[ PATH_MAX];
            if ( std::fgets( line, sizeof( line), pipe) != nullptr)
            {
                std::string  bundle( line);
                while ( !bundle.empty() && ( bundle.back() == '\n' || bundle.back() == '\r'))
                {
                    bundle.pop_back();
                }
                if ( !bundle.empty() && isExecutable( bundle + kChromeExecutable))
                {
                    found = bundle + kChromeExecutable;
                }
            }
            pclose( pipe);
        }
        return found;
    }

    auto processIsChrome( long pid) -> bool
    {
        char  path[ PROC_PIDPATHINFO_MAXSIZE];
        return proc_pidpath( static_cast<int>( pid), path, sizeof( path)) > 0 &&
               std::string_view( path).find( "Google Chrome") != std::string_view::npos;
    }

    auto reportError( const std::string & message) -> void
    {
        std::fprintf( stderr, "%s\n", message.c_str());

        // At a terminal the line above is the report. Anywhere else -- a login
        // item, a double-click -- nobody reads stderr, so say it on screen.
        if ( isatty( STDERR_FILENO))
        {
            return;
        }

        const auto  script = "display alert \"Thorium Bench Console\" message "
                           + appleScriptQuoted( message) + " as critical";

        // Not waited for, and outside the group: the dialog is the last word
        // of a launcher that is about to exit, and has to outlive it.
        (void)spawnDetached( { "/usr/bin/osascript", "-e", script});
    }
}
