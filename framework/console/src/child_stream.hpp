#pragma once

#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace console
{
    //
    // Runs argv[0] with argv[1..] and hands complete lines of its stdout to
    // OnLine as they arrive, from a background thread -- not the thread that
    // called start(). OnExit fires exactly once, after the pipe has closed
    // and the process has been waited on, never before: a JSON-lines stream
    // (see core/journal/event_sink.hpp) can end mid-line if the child is
    // killed, and a caller must not be told "it's over" while a partial last
    // line is still arriving.
    //
    // argv is spawned directly -- CreateProcessW's argv on Windows, execvp's
    // on POSIX -- never through a shell. The values in argv can be operator
    // text from a POST body (a DUT serial, a hand-typed flag), and a shell
    // string built by concatenation would turn that text into a command
    // injection the moment it contained a quote or a semicolon. There is no
    // step here where argv becomes one string a shell re-parses.
    //
    class ChildStream
    {
        public:
            struct Handlers
            {
                std::function<void( const std::string &)>  OnLine{};

                //
                // A line of the child's stderr. Not folded into OnLine's own
                // stream: OnLine's contract is "this is exactly what
                // run_scripts wrote to stdout, one JSON object per line" (see
                // core/journal/event_sink.hpp), and a truncated read that
                // happened to interleave a stderr line into that stream would
                // hand a caller something that looks like a malformed event
                // rather than what it is. This is how a run that never
                // reaches journal().begin() -- a preflight that could not
                // reach an instrument, a contradictory flag -- is still
                // explainable: see framework/console/README.md's "never
                // started -- the reason is on stderr" row.
                //
                std::function<void( const std::string &)>  OnStderrLine{};

                std::function<void( int exitCode)>          OnExit{};
            };

            explicit ChildStream( Handlers handlers);
            ~ChildStream();

            ChildStream( const ChildStream &) = delete;
            auto operator=( const ChildStream &) -> ChildStream & = delete;

            //
            // False if the process could not be started at all. Handlers are
            // never called in that case -- there is nothing to report to
            // them yet.
            //
            [[nodiscard]]
            auto start( std::vector<std::string> argv) -> bool;

            //
            // Blocks until OnExit has been called (or start() never
            // succeeded). Every caller of start() must eventually call this,
            // or the destructor does -- see its comment.
            //
            auto join() -> void;

        private:
            auto readLoop() -> void;
            auto readStderrLoop() -> void;

            Handlers      mHandlers;
            std::thread   mReader;
            std::thread   mStderrReader;
            std::string   mPartialLine;
            std::string   mPartialStderrLine;

            struct Platform;
            Platform *  mPlatform{ nullptr };
    };

    //
    // Spawns argv, waits for it to exit, and returns everything it wrote to
    // stdout along with its exit code. For the three call sites that do not
    // need to watch a run live -- --describe-options, --list-tests, --safe --
    // where the wait is short and there is nobody to stream to yet.
    //
    struct BlockingResult
    {
        int          ExitCode{ -1 };
        std::string  Output;

        // False only when the process could not be started at all -- a
        // missing binary, most likely. Distinct from a nonzero ExitCode,
        // which is the process running and disagreeing, not failing to run.
        bool         Started{ false };
    };

    [[nodiscard]]
    auto runBlocking( std::vector<std::string> argv) -> BlockingResult;
}
