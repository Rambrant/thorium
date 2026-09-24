#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "child_stream.hpp"

namespace console
{
    //
    // The one run this console is allowed to have in flight at a time, and
    // every viewer's window onto it.
    //
    // A rig has one fabric. Two run_scripts processes opening it at once is
    // not a race this class arbitrates so much as a thing it exists to make
    // impossible -- start() refuses a second run outright, which is the
    // server-side half of the "nothing arbitrates two operators" gap README.md's
    // "What it does not do yet" describes. (The other half -- one *operator*, as opposed to
    // one run -- is a session/auth question for whenever this server binds
    // anything but 127.0.0.1, and is not this class's job.)
    //
    // Every line the run has produced is kept, not just the newest one, so a
    // browser tab opened after the run started -- or reopened after being
    // closed, which framework/launcher's "Show console" does on every click
    // -- sees the run from its own beginning rather than from whenever it
    // happened to connect.
    //
    class RunSession
    {
        public:
            //
            // False if a run is already active; true does not mean the
            // process launched successfully, only that this call is the one
            // that gets to try -- see waitForUpdate's first Update for that.
            //
            [[nodiscard]]
            auto start( std::vector<std::string> argv) -> bool;

            [[nodiscard]]
            auto active() const -> bool;

            struct Update
            {
                std::vector<std::string>  Lines;   // new since `from`; may be empty
                std::size_t                Next{ 0 };  // pass this back as the next `from`
                bool                       StillRunning{ false };  // false: stop asking
            };

            //
            // Blocks until there is something new past `from`, or the run has
            // ended and everything has already been delivered. One caller per
            // SSE connection, each with its own `from` -- this is how several
            // browser tabs watch the same run without stepping on each other.
            //
            [[nodiscard]]
            auto waitForUpdate( std::size_t from) -> Update;

        private:
            mutable std::mutex          mMutex;
            std::condition_variable     mCv;
            std::vector<std::string>    mLines;
            bool                        mActive{ false };
            std::unique_ptr<ChildStream>  mChild;
    };
}
