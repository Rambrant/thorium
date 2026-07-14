#include "suite/scripts.hpp"

#include <gtest/gtest.h>

using core::quantities::Voltage;

namespace
{
    //
    // Feeds values through rig::testSession() before calling the script,
    // then restores live routing afterward -- rig::bench() is a shared
    // global (the catalog's fixed (group, test) -> bool script signature has
    // no per-call device parameter to inject through instead; see
    // suite/rig.hpp), so every test must clean up after itself or leak into
    // the next one.
    //
    struct SupplyRailFixture : ::testing::Test
    {
        core::ScriptedSession session;

        void TearDown() override
        {
            rig::testSession().useDefault();
        }
    };
} // namespace

TEST_F(SupplyRailFixture, PassesWhenBothRailsInTolerance)
{
    session.program( "5VOutput", Voltage{ 5.02 });
    session.program( "3V3Output", Voltage{ 3.29 });
    rig::testSession().use( session);

    EXPECT_TRUE(supplyRailScript("group", "test"));
}

TEST_F(SupplyRailFixture, FailsWhenARailIsOutOfTolerance)
{
    session.program( "5VOutput", Voltage{ 5.02 });
    session.program( "3V3Output", Voltage{ 3.10 }); // outside +/-50mV
    rig::testSession().use( session);

    EXPECT_FALSE(supplyRailScript("group", "test"));
}

TEST_F(SupplyRailFixture, ThrowsWhenAPointIsMissing)
{
    session.program( "5VOutput", Voltage{ 5.02 });
    // 3V3Output not provided at all -- the ScriptedSession has nothing
    // queued for it, so dut::Measure throws rather than silently treating
    // the missing point as a failed check.
    rig::testSession().use( session);

    EXPECT_THROW((void)supplyRailScript("group", "test"), std::runtime_error);
}
