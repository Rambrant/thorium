#include "config.hpp"

#include <cstdlib>
#include <string_view>

namespace launcher
{
    namespace
    {
        // The text after `prefix`, if `arg` starts with it.
        auto valueOf( const std::string & arg, std::string_view prefix) -> std::optional<std::string>
        {
            if ( !arg.starts_with( prefix))
            {
                return std::nullopt;
            }
            return arg.substr( prefix.size());
        }
    }

    auto parseConfig( const std::vector<std::string> & args, std::string & error) -> std::optional<Config>
    {
        Config  config;

        for ( const auto & arg : args)
        {
            if ( const auto value = valueOf( arg, "--server="))
            {
                config.ServerCommand = *value;
            }
            else if ( const auto value = valueOf( arg, "--server-arg="))
            {
                config.ServerArgs.push_back( *value);
            }
            else if ( const auto value = valueOf( arg, "--browser-arg="))
            {
                config.BrowserArgs.push_back( *value);
            }
            else if ( const auto value = valueOf( arg, "--port="))
            {
                char *      end = nullptr;
                const auto  port = std::strtoul( value->c_str(), &end, 10);
                if ( value->empty() || *end != '\0' || port == 0 || port > 65535)
                {
                    error = "Not a port number: " + arg;
                    return std::nullopt;
                }
                config.Port = static_cast<unsigned short>( port);
            }
            else
            {
                error = "Unrecognised argument: " + arg;
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
