#pragma once

namespace launcher
{
    //
    // What the launcher asks of the console server, over HTTP on loopback.
    // One client for every platform -- cpp-httplib, the library the server
    // itself is built on.
    //
    struct ConsoleStatus
    {
        // Up: answered. Hung: accepted the connection and did not answer in
        // time -- busy, not gone. Gone: nothing is listening at all.
        enum class State { Up, Hung, Gone };

        State  Server{ State::Gone };

        // Valid when Server is Up: how many console pages are open, and whether
        // a run is in flight (GET /api/status).
        int    Viewers{ 0 };
        bool   Running{ false };
    };

    //
    // One GET /api/status. The distinction between Hung and Gone is the
    // caller's whole use for it: a server that is merely slow while a run is
    // in flight must never be mistaken for one that has died.
    //
    [[nodiscard]]
    auto queryStatus( unsigned short port) -> ConsoleStatus;

    //
    // True once the server answers, false if it has not within `timeoutMs`.
    // An answer, not just an open port: the server binds before it can serve.
    //
    [[nodiscard]]
    auto waitForServer( unsigned short port, unsigned timeoutMs) -> bool;
}
