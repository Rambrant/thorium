#pragma once

namespace launcher
{
    //
    // The one server call the launcher makes on its own initiative, because
    // it is also the one call that must always work: "Safe the rig" is never
    // greyed out, including when the browser window has been closed by
    // mistake (see framework/console/README.md's "What a run means").
    // Routing it through the tray icon as well as an in-page button is what
    // keeps that promise once the console is a browser window that can
    // simply be closed.
    //
    // Synchronous, and expected to be called from a background thread by the
    // tray icon's click handler -- see main.cpp -- so a slow or hung server
    // cannot freeze the message loop that owns every other tray interaction.
    //
    [[nodiscard]]
    auto postSafeTheRig( unsigned short port) -> bool;
}
