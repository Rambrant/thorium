#pragma once

#include <span>
#include <string_view>

namespace webui
{
    //
    // The browser side of this program -- static/index.html, console.css and
    // the ES modules under static/js/, entered at main.js -- compiled in
    // rather than read from disk. The files are edited as the ordinary HTML,
    // CSS and JavaScript they are;
    // cmake/EmbedFiles.cmake turns them into the definition of staticFiles()
    // at build time, so editing one rebuilds the server with it.
    //
    // Embedded so the binary has no path to get wrong: wherever
    // thorium_webui.exe ends up -- built in place, copied to a bench,
    // launched by framework/launcher from a shortcut -- this comes with it.
    // set_mount_point() on a real static/ directory would serve the same
    // files, but would make "which page is this binary showing" depend on
    // what happens to sit beside it.
    //
    struct StaticFile
    {
        std::string_view  Path;          // as requested, e.g. "/console.css"
        std::string_view  ContentType;
        std::string_view  Body;
    };

    auto staticFiles() -> std::span<const StaticFile>;
}
