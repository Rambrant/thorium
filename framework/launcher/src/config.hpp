#pragma once

#include <optional>
#include <string>
#include <vector>

namespace launcher
{
    //
    // Everything the launcher needs to bring the console up. There is no
    // config file: the server this drives does not exist yet, so a fixed set
    // of command-line flags is the whole story until there is a real
    // deployment to make configurable, at which point this struct is the one
    // place that changes.
    //
    struct Config
    {
        std::wstring               ServerCommand;
        std::vector<std::wstring>  ServerArgs;
        unsigned short             Port{ 8420 };
        std::wstring               Title{ L"Thorium Bench Console" };
    };

    //
    // Reads this process's own command line. Empty optional plus a
    // human-readable sentence in `error` on anything it cannot make sense of
    // -- the only caller is wWinMain, and a message box is the right way to
    // report it, which is why the message is a plain sentence rather than an
    // exception type.
    //
    [[nodiscard]]
    auto parseConfig( std::wstring & error) -> std::optional<Config>;
}
