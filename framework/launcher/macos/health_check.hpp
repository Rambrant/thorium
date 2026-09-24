#pragma once

namespace launcher
{
    //
    // Same question and same answer as the Windows launcher's
    // health_check.hpp: a bare TCP connect() to 127.0.0.1:port succeeding is
    // exactly "has the server bound its socket yet", nothing more.
    //
    [[nodiscard]]
    auto waitForServer( unsigned short port, unsigned timeoutMs) -> bool;
}
