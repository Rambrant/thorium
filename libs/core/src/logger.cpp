#include "core/logger.hpp"

#include "core/meta.hpp"

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

    //
    // core::meta::to_string_upper() (core/meta.hpp) reflects over LogLevel's
    // own enumerators and uppercases each one at compile time (Debug ->
    // "DEBUG") -- see that function's own comment for why to_string()
    // (verbatim spelling) isn't the right fit here.
    //
    auto to_string( LogLevel level) -> std::string_view
    {
        return meta::to_string_upper( level);
    }
} // namespace core
