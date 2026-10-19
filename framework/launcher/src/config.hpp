#pragma once

#include <optional>
#include <string>
#include <vector>

namespace launcher
{
    //
    // The whole command-line surface, the same five flags on every platform:
    //
    //   --server=<path>      the console server to start (required)
    //   --server-arg=<arg>   one more argument for it; repeatable
    //   --browser-arg=<arg>  one more flag for every Chrome window; repeatable
    //   --port=<n>           the port the server listens on (default 8420)
    //
    // UTF-8 throughout; platform::arguments() does the conversion on Windows.
    //
    struct Config
    {
        std::string               ServerCommand;
        std::vector<std::string>  ServerArgs;
        std::vector<std::string>  BrowserArgs;
        unsigned short            Port{ 8420 };
    };

    //
    // Empty optional plus a plain sentence in `error` on anything it cannot
    // make sense of -- the only caller is main, which reports that sentence.
    //
    [[nodiscard]]
    auto parseConfig( const std::vector<std::string> & args, std::string & error) -> std::optional<Config>;
}
