#pragma once

#include <optional>

namespace launcher
{
    //
    // POST /safe, for the same reason the Windows launcher's rig_client.hpp
    // gives: it is the one action that must work with the console window
    // closed, so it goes from the menu bar straight to the server.
    //
    // Synchronous, and called from a background thread by main.cpp, the same
    // shape as the Windows side -- a hung server cannot freeze the menu.
    // Plain POSIX sockets rather than NSURLSession, whose completion handler
    // is a block, which GCC cannot write.
    //
    // True only for a 2xx answer. thorium_webui answers 500 when
    // run_scripts --safe could not even be started, and the operator has to
    // hear about that -- which is why the macOS launcher reports this result
    // where the Windows one discards it.
    //
    [[nodiscard]]
    auto postSafeTheRig( unsigned short port) -> bool;

    //
    // GET /api/status: how many console pages are open, and whether a run is
    // in flight. main.cpp polls it to quit the console once its last window
    // has closed -- but never during a run. Nothing when the server does not
    // answer, which the caller treats as "don't know" rather than as "idle".
    //
    struct ConsoleStatus
    {
        int   Viewers{ 0 };
        bool  Running{ false };
    };

    [[nodiscard]]
    auto getConsoleStatus( unsigned short port) -> std::optional<ConsoleStatus>;
}
