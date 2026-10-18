#pragma once

#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "core/control/stop_request.hpp"

//
// What asks a run to end early: a person at a terminal, or the console that
// supervises this process. The flag they set, and what reads it, is
// core/control/stop_request.hpp.
//
// There was no way to ask. Ctrl-C was the default SIGINT, which kills the
// process where it stands: no teardown hook, no hal::RigSafingGuard, no log.
// That is the right answer for a process that has stopped answering and the
// wrong one for an operator who simply has had enough of an unbounded
// --until-failure soak.
//
//   - SIGINT / SIGTERM, for a person at a terminal. The first sets the flag; a
//     second restores the default action and re-raises, so a run that does not
//     reach a boundary (a hung instrument read) can still be ended by pressing
//     it again -- and then needs a --safe, exactly as it always did.
//   - a line reading "stop" on stdin, under --stop-on-stdin, for the console.
//     A signal is not the transport because Windows has no way to send one to a
//     child that has no console; a pipe is the same on both. End of input is a
//     stop too: the console holds the other end for exactly as long as it is
//     there to watch, so an end means it is gone -- closed, crashed or killed --
//     and an unbounded run with nobody watching is not one to leave energising a
//     rig. It is recorded as a different reason, so the log says which happened.
//
namespace runner
{
    namespace detail
    {
        extern "C" inline void onStopSignal( int signal)
        {
            if( core::stopRequested())
            {
                //
                // Second time: the operator is not waiting for a boundary.
                //
                std::signal( signal, SIG_DFL);
                std::raise( signal);

                return;
            }

            core::requestStop();

#ifndef _WIN32
            static constexpr char kNotice[] =
                "\nStopping after the current test. Press Ctrl-C again to abort now "
                "(the rig is then left as it is: run --safe).\n";

            const auto ignored = ::write( STDERR_FILENO, kNotice, sizeof( kNotice) - 1);
            static_cast<void>( ignored);
#endif
        }
    } // namespace detail

    inline auto installStopSignals() -> void
    {
        std::signal( SIGINT,  detail::onStopSignal);
        std::signal( SIGTERM, detail::onStopSignal);
    }

    //
    // Reads stdin for the word "stop", and for its end, on a thread that is
    // never joined: it is blocked in a read for the life of the process, and
    // process exit is what ends it. Anything else on a line is ignored, so a
    // future command does not break an older runner.
    //
    inline auto watchStdinForStop() -> void
    {
        std::thread( []
        {
            std::string  line;

            while( std::getline( std::cin, line))
            {
                if( line == "stop" || line == "stop\r")
                    core::requestStop();
            }

            core::requestStop( core::StopReason::ConsoleGone);
        }).detach();
    }
} // namespace runner
