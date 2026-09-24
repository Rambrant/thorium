#pragma once

#include <optional>
#include <string>
#include <vector>

namespace launcher
{
    //
    // The same four flags as the Windows launcher's config.hpp, spelled the
    // same way, so a deployment script can hand either launcher the same
    // arguments. Narrow strings rather than wide: argv on macOS is already
    // UTF-8, and every API this program hands a path to takes UTF-8.
    //
    struct Config
    {
        std::string               ServerCommand;
        std::vector<std::string>  ServerArgs;
        unsigned short            Port{ 8420 };
        std::string               Title{ "Thorium Bench Console" };
    };

    //
    // Empty optional plus a plain sentence in `error` on anything it cannot
    // make sense of -- the only caller is main, which puts that sentence in
    // an alert, same as the Windows launcher's message box.
    //
    [[nodiscard]]
    auto parseConfig( int argc, char ** argv, std::string & error) -> std::optional<Config>;
}
