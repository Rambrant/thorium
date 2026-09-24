#pragma once

namespace launcher
{
    //
    // Polls 127.0.0.1:port with a raw TCP connect until something answers or
    // timeoutMs elapses. A full HTTP request is not needed to answer "has the
    // server bound its socket yet" -- a bare connect() succeeding is exactly
    // that question, nothing more.
    //
    [[nodiscard]]
    auto waitForServer( unsigned short port, unsigned timeoutMs) -> bool;
}
