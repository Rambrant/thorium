#include "suite/scripts.hpp"

#include <gtest/gtest.h>

using core::quantities::Voltage;

namespace
{
    //
    // Injects values into the shared global Measure before calling the
    // script, then restores live routing afterward -- Measure is a single
    // rig-wide object (a catalog script takes no parameters at all, so there
    // is no per-call device to inject through instead; see hal/measure.hpp),
    // so every test must clean up after itself or leak into the next one.
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
    Measure.inject( "Output5V", Voltage{ 5.02 });
    Measure.inject( "Output3V3", Voltage{ 3.29 });

    EXPECT_TRUE(supplyRailScript());
}

TEST_F( SupplyRailFixture, FailsWhenARailIsOutOfTolerance)
{
    Measure.inject( "Output5V", Voltage{ 5.02 });
    Measure.inject( "Output3V3", Voltage{ 3.10 }); // outside +/-50mV

    EXPECT_FALSE(supplyRailScript());
}

TEST_F(SupplyRailFixture, ThrowsWhenAPointIsMissing)
{
    Measure.inject( "Output5V", Voltage{ 5.02 });
    // Output3V3 not provided at all -- nothing is queued for it, so Measure
    // throws rather than silently treating the missing point as a failed
    // check.

    EXPECT_THROW((void)supplyRailScript(), std::runtime_error);
}
