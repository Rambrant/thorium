//
// RunSession's two ways of ending a run early, against stand-in children.
//
// Not run_scripts: what is under test is that "stop" reaches a child's stdin
// and that a kill ends one that ignores it, and neither needs a rig or a
// catalog. cat echoes what it is sent, which makes a delivered "stop" visible
// in the run's own stream; sleep reads nothing and answers only to a kill.
// The real binary's reaction to "stop" is tested in test_protocol.cpp.
//
// POSIX only: both children are POSIX tools.
//
#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "run_session.hpp"

#ifndef _WIN32
namespace
{
    // Waits, bounded, until the run is over.
    auto waitUntilOver( webui::RunSession & session) -> bool
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10);

        while ( session.active() && std::chrono::steady_clock::now() < deadline)
        {
            static_cast<void>( session.waitForUpdate( session.lines().size()));
        }

        return !session.active();
    }
}

TEST( RunSession, AStopIsWrittenToTheChildsStdin)
{
    webui::RunSession  session;

    ASSERT_TRUE( session.start( { "/bin/cat" }));
    ASSERT_TRUE( session.stop());

    //
    // cat writes the line back, so it arrives in the stream rather than being
    // assumed delivered from a true return.
    //
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10);

    while ( session.lines().empty() && std::chrono::steady_clock::now() < deadline)
    {
        static_cast<void>( session.waitForUpdate( 0));
    }

    ASSERT_FALSE( session.lines().empty());
    EXPECT_EQ( session.lines().front(), "stop");

    ASSERT_TRUE( session.kill());
    EXPECT_TRUE( waitUntilOver( session));
}

TEST( RunSession, AKillEndsAChildThatIgnoresStop)
{
    webui::RunSession  session;

    ASSERT_TRUE( session.start( { "/bin/sleep", "60" }));
    EXPECT_TRUE( session.active());

    EXPECT_TRUE( session.stop()) << "delivered, whether or not anything is listening";
    EXPECT_TRUE( session.active()) << "sleep is not listening";

    EXPECT_TRUE( session.kill());
    EXPECT_FALSE( session.active());
}

TEST( RunSession, WithNoRunThereIsNothingToStopOrKill)
{
    webui::RunSession  session;

    EXPECT_FALSE( session.stop());
    EXPECT_FALSE( session.kill());

    ASSERT_TRUE( session.start( { "/bin/sleep", "60" }));
    ASSERT_TRUE( session.kill());

    EXPECT_FALSE( session.stop()) << "the run is over";
    EXPECT_FALSE( session.kill()) << "and a second kill has nothing to signal";
}
#endif
