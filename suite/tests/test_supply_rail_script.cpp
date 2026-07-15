#include "suite/scripts.hpp"

#include <gtest/gtest.h>

using core::quantities::Voltage;

namespace
{
    //
    // Injects values into the shared global Measure before calling the
    // script, then restores live routing afterward -- Measure is a single
    // rig-wide object (the catalog's fixed (group, test) -> bool script
    // signature has no per-call device parameter to inject through instead;
    // see suite/measure.hpp), so every test must clean up after itself or
    // leak into the next one.
    //
    struct SupplyRailFixture : ::testing::Test
    {
        protected:

            void TearDown() override
            {
                Measure.useLive();
            }
    };
} // namespace

TEST_F( SupplyRailFixture, PassesWhenBothRailsInTolerance)
{
    Measure.inject( "5VOutput", Voltage{ 5.02 });
    Measure.inject( "3V3Output", Voltage{ 3.29 });

    EXPECT_TRUE(supplyRailScript("group", "test"));
}

TEST_F( SupplyRailFixture, FailsWhenARailIsOutOfTolerance)
{
    Measure.inject( "5VOutput", Voltage{ 5.02 });
    Measure.inject( "3V3Output", Voltage{ 3.10 }); // outside +/-50mV

    EXPECT_FALSE(supplyRailScript("group", "test"));
}

TEST_F(SupplyRailFixture, ThrowsWhenAPointIsMissing)
{
    Measure.inject( "5VOutput", Voltage{ 5.02 });
    // 3V3Output not provided at all -- nothing is queued for it, so Measure
    // throws rather than silently treating the missing point as a failed
    // check.

    EXPECT_THROW((void)supplyRailScript("group", "test"), std::runtime_error);
}
