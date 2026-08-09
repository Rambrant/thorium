#include "suite/scripts.hpp"

#include <gtest/gtest.h>

using core::quantities::Voltage;

namespace
{
    //
    // The fuse register byte is a fixed stand-in (see
    // suite/scripts/fuse_register_script.cpp) -- no digital-register
    // instrument exists to feed it through, so these tests can only vary the
    // voltage half of the check, via Measure.inject().
    //
    struct FuseRegisterFixture : ::testing::Test
    {
        protected:

            void TearDown() override
            {
                Measure.useLive();
            }
    };
} // namespace

TEST_F(FuseRegisterFixture, PassesWhenVoltageIsWithinCriteria)
{
    Measure.inject( "Vout", Voltage{ 12.01 });

    EXPECT_TRUE(fuseRegisterScript());
}

TEST_F(FuseRegisterFixture, FailsWhenVoltageOutOfTolerance)
{
    Measure.inject( "Vout", Voltage{ 12.50 }); // outside 12.0 +/- 0.05

    EXPECT_FALSE(fuseRegisterScript());
}
