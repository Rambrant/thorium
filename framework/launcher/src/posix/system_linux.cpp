//
// The Linux-only half of platform.hpp: where things live on a Linux desktop,
// where Chrome is, and how an error is shown. Everything else is POSIX and in
// platform.cpp beside it.
//
// Written without a Linux machine at hand, and meant to be tried under WSL2 or
// on a real bench -- see README.md's "Testing".
//
#include "platform.hpp"

#include "profile_lock.hpp"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string_view>

#include <unistd.h>

namespace launcher::platform
{
    namespace
    {
        auto environment( const char * name) -> std::string
        {
            const auto  value = std::getenv( name);
            return value != nullptr ? value : "";
        }

        auto isExecutable( const std::string & path) -> bool
        {
            return access( path.c_str(), X_OK) == 0;
        }

        // First of `names` that is an executable file in a PATH directory.
        auto searchPath( std::initializer_list<const char *> names) -> std::optional<std::string>
        {
            std::istringstream  path( environment( "PATH"));
            std::string         directory;

            while ( std::getline( path, directory, ':'))
            {
                for ( const auto * name : names)
                {
                    const auto  candidate = directory + "/" + name;
                    if ( !directory.empty() && isExecutable( candidate))
                    {
                        return candidate;
                    }
                }
            }
            return std::nullopt;
        }
    }

    auto supportDir() -> std::string
    {
        // The XDG data directory, which is where an application's own state
        // belongs -- $XDG_DATA_HOME, or ~/.local/share when it is unset.
        auto  base = environment( "XDG_DATA_HOME");
        if ( base.empty())
        {
            const auto  home = environment( "HOME");
            base = ( home.empty() ? "." : home) + "/.local/share";
        }

        const auto  dir = base + "/thorium";

        std::error_code  ignored;
        std::filesystem::create_directories( dir, ignored);
        return dir;
    }

    auto findChrome() -> std::optional<std::string>
    {
        // The names Google's package and the distributions' Chromium packages
        // install under; --app= and --user-data-dir= are the same in all.
        if ( const auto onPath = searchPath( { "google-chrome", "google-chrome-stable", "chromium", "chromium-browser"}))
        {
            return onPath;
        }

        // Google's package puts the real binary here and a symlink on PATH;
        // a PATH without /usr/bin (a login item's, say) still finds it.
        if ( isExecutable( "/opt/google/chrome/chrome"))
        {
            return "/opt/google/chrome/chrome";
        }
        return std::nullopt;
    }

    auto processIsChrome( long pid) -> bool
    {
        char        path[ PATH_MAX];
        const auto  length = readlink( ( "/proc/" + std::to_string( pid) + "/exe").c_str(), path, sizeof( path) - 1);
        if ( length <= 0)
        {
            return false;
        }

        // Chrome, Chromium and the helper processes all carry "chrom" in the
        // executable's name.
        return std::string_view( path, static_cast<std::size_t>( length)).find( "chrom") != std::string_view::npos;
    }

    auto reportError( const std::string & message) -> void
    {
        std::fprintf( stderr, "%s\n", message.c_str());

        // At a terminal the line above is the report. Anywhere else -- a
        // launcher entry, a login item -- nobody reads stderr, so say it on
        // screen, with whichever of the two common tools this desktop has.
        if ( isatty( STDERR_FILENO))
        {
            return;
        }

        // Not waited for, and outside the group: the dialog is the last word
        // of a launcher that is about to exit, and has to outlive it.
        if ( const auto zenity = searchPath( { "zenity"}))
        {
            (void)spawnDetached( { *zenity, "--error", "--title=Thorium Bench Console", "--text=" + message});
        }
        else if ( const auto notify = searchPath( { "notify-send"}))
        {
            (void)spawnDetached( { *notify, "--urgency=critical", "Thorium Bench Console", message});
        }
    }
}
