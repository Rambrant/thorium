#pragma once

#include <string>

#include <windows.h>

#include "process_job.hpp"

namespace launcher
{
    //
    // One spawned process, tracked just well enough to answer "is it still
    // running" and to be killed on demand. There is no stdout to read here --
    // unlike ui::ChildProcess (framework/ui/src/app/process.hpp), neither the
    // server nor the browser talks to the launcher over a pipe. The server
    // talks HTTP (rig_client.hpp) and the browser talks nothing at all.
    //
    class ChildProcess
    {
        public:
            ChildProcess() = default;
            ~ChildProcess();

            ChildProcess( const ChildProcess &) = delete;
            auto operator=( const ChildProcess &) -> ChildProcess & = delete;

            ChildProcess( ChildProcess && other) noexcept;
            auto operator=( ChildProcess && other) noexcept -> ChildProcess &;

            //
            // commandLine is mutated by CreateProcessW (it writes into the
            // buffer), so it is taken by value rather than by const reference
            // -- a caller passing a string literal would otherwise crash.
            //
            // Starting again on an object that already holds a running
            // process does not stop the old one; it only stops tracking it.
            // The old process stays alive under the job and is cleaned up
            // when the job closes, same as any other member. See main.cpp's
            // showConsole for why that is the wanted behaviour here.
            //
            auto start( std::wstring commandLine, const ProcessJob & job) -> bool;

            [[nodiscard]]
            auto running() const -> bool;

            auto terminate() const -> void;

        private:
            auto reset() -> void;

            PROCESS_INFORMATION  mInfo{};
            bool                 mStarted{ false };
    };
}
