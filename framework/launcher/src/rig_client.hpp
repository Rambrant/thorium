#pragma once

#include <optional>

namespace launcher
{
    //
    // The one server call the launcher makes on its own initiative, because
    // it is also the one call that must always work: "Safe the rig" is never
    // greyed out, including when the browser window has been closed by
    // mistake (see framework/webui/README.md's "What a run means").
    // Routing it through the tray icon as well as an in-page button is what
    // keeps that promise once the console is a browser window that can
    // simply be closed.
    //
    // Synchronous, and expected to be called from a background thread by the
    // tray icon's click handler -- see main.cpp -- so a slow or hung server
    // cannot freeze the message loop that owns every other tray interaction.
    //
    // True only for a 2xx answer: thorium_webui answers 500 when
    // run_scripts --safe could not even be started.
    //
    [[nodiscard]]
    auto postSafeTheRig( unsigned short port) -> bool;

    //
    // GET /api/status: how many console pages are open, and whether a run is
    // in flight -- the same call, and the same answer, as
    // macos/rig_client.hpp's. main.cpp polls it to quit once the last console
    // window has closed, but never during a run. Nothing when the server does
    // not answer, which the caller treats as "don't know" rather than "idle".
    //
    struct ConsoleStatus
    {
        int   Viewers{ 0 };
        bool  Running{ false };
    };

    [[nodiscard]]
    auto getConsoleStatus( unsigned short port) -> std::optional<ConsoleStatus>;
}
