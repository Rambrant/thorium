#include "suite/scripts.hpp"

#include <gtest/gtest.h>

using core::quantities::Voltage;

namespace
{
    //
    // The fuse register byte is a fixed stand-in (see
    // suite/scripts/fuse_register_script.cpp) -- no digital-register
    // instrument exists to feed it through, so these tests can only vary the
    // voltage half of the check via rig::testSession().
    //
    struct FuseRegisterFixture : ::testing::Test
    {
        core::ScriptedSession session;

        void TearDown() override
        {
            rig::testSession().useDefault();
        }
    };
} // namespace

TEST_F(FuseRegisterFixture, PassesWhenVoltageIsWithinCriteria)
{
    session.program( "Vout", Voltage{ 12.01 });
    rig::testSession().use( session);

    EXPECT_TRUE(fuseRegisterScript("group", "test"));
}

TEST_F(FuseRegisterFixture, FailsWhenVoltageOutOfTolerance)
{
    session.program( "Vout", Voltage{ 12.50 }); // outside 12.0 +/- 0.05
    rig::testSession().use( session);

    EXPECT_FALSE(fuseRegisterScript("group", "test"));
}
