#include "config.hpp"

#include <cstdlib>
#include <string_view>

namespace launcher
{
    auto parseConfig( int argc, char ** argv, std::string & error) -> std::optional<Config>
    {
        Config  config;

        for ( int i = 1; i < argc; ++i)
        {
            const std::string_view  arg = argv[ i];

            if ( arg.starts_with( "--server="))
            {
                config.ServerCommand = arg.substr( 9);
            }
            else if ( arg.starts_with( "--server-arg="))
            {
                config.ServerArgs.emplace_back( arg.substr( 13));
            }
            else if ( arg.starts_with( "--port="))
            {
                config.Port = static_cast<unsigned short>(
                    std::strtoul( std::string( arg.substr( 7)).c_str(), nullptr, 10));
            }
            else if ( arg.starts_with( "--title="))
            {
                config.Title = arg.substr( 8);
            }
            else
            {
                error = "Unrecognised argument: " + std::string( arg);
                return std::nullopt;
            }
        }

        if ( config.ServerCommand.empty())
        {
            error = "--server=<path to the console server executable> is required";
            return std::nullopt;
        }

        return config;
    }
}
