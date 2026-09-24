#pragma once

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
}
