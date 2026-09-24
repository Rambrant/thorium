#include "config.hpp"

#include <cstdlib>

#include <windows.h>

#include <shellapi.h>

namespace launcher
{
    namespace
    {
        auto startsWith( const std::wstring & value, const wchar_t * prefix) -> bool
        {
            const auto len = wcslen( prefix);
            return value.size() >= len && value.compare( 0, len, prefix) == 0;
        }
    }

    auto parseConfig( std::wstring & error) -> std::optional<Config>
    {
        int         argc = 0;
        const auto  argv = CommandLineToArgvW( GetCommandLineW(), &argc);

        if ( argv == nullptr)
        {
            error = L"CommandLineToArgvW failed";
            return std::nullopt;
        }

        Config  config;

        for ( int i = 1; i < argc; ++i)
        {
            const std::wstring arg = argv[ i];

            if ( startsWith( arg, L"--server="))
            {
                config.ServerCommand = arg.substr( 9);
            }
            else if ( startsWith( arg, L"--server-arg="))
            {
                config.ServerArgs.push_back( arg.substr( 13));
            }
            else if ( startsWith( arg, L"--port="))
            {
                config.Port = static_cast<unsigned short>(
                    std::wcstoul( arg.substr( 7).c_str(), nullptr, 10));
            }
            else if ( startsWith( arg, L"--title="))
            {
                config.Title = arg.substr( 8);
            }
            else
            {
                error = L"Unrecognised argument: " + arg;
                LocalFree( argv);
                return std::nullopt;
            }
        }

        LocalFree( argv);

        if ( config.ServerCommand.empty())
        {
            error = L"--server=<path to the console server executable> is required";
            return std::nullopt;
        }

        return config;
    }
}
