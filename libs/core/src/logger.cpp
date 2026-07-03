#include "core/logger.hpp"

#include <iostream>

namespace core
{
    Logger::Logger( std::string_view name) :
        mName( name)
    {}

    auto Logger::log( LogLevel level, std::string_view message ) const -> void
    {
        std::cout << "[" << to_string(level) << "] " << mName << ": " << message << "\n";
    }

    auto to_string( LogLevel level) -> std::string_view
    {
        switch( level)
        {
            case LogLevel::Debug : return "DEBUG";
            case LogLevel::Info  : return "INFO";
            case LogLevel::Warn  : return "WARN";
            case LogLevel::Error : return "ERROR";
        }
        return "UNKNOWN";
    }
} // namespace core
