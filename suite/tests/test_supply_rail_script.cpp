#include "suite/scripts.hpp"
#include "verdict.hpp"

//
// Not suite/prelude.hpp: these tests are not scripts. They call one and
// inject its readings by point *name* -- Measure.inject( "Output5V", ...) --
// so they need the Measure verb and the quantity types, and none of the
// criteria tables or adapter points a script body is written against. That
// is also what lets scripts_tests build without the criteria compile
// definitions, which are PRIVATE to the scripts library (see
// app/CMakeLists.txt).
//
#include "core/quantity.hpp"
#include "hal/measure.hpp"

#include <gtest/gtest.h>

#include "core/criteria_variants.hpp"

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
    // The criteria variant is restored for the same reason and with the same
    // consequence if it isn't: it too is process-wide (see
    // core/criteria_variants.hpp), so a test that widened the tolerances and
    // left them widened would silently make every test after it a weaker check
    // than it reads as.
    //
    struct SupplyRailFixture : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                core::resetCriteriaVariantForTesting();
            }

            void TearDown() override
            {
                Measure.useLive();
                core::resetCriteriaVariantForTesting();
            }
    };
} // namespace

TEST_F( SupplyRailFixture, PassesWhenBothRailsInTolerance)
{
    Measure.inject( "Output5V", Voltage{ 5.02 });
    Measure.inject( "Output3V3", Voltage{ 3.29 });

    EXPECT_TRUE( verdictOf( supplyRailScript));
}

TEST_F( SupplyRailFixture, FailsWhenARailIsOutOfTolerance)
{
    Measure.inject( "Output5V", Voltage{ 5.02 });
    Measure.inject( "Output3V3", Voltage{ 3.10 }); // outside +/-50mV

    EXPECT_FALSE( verdictOf( supplyRailScript));
}

TEST_F(SupplyRailFixture, ThrowsWhenAPointIsMissing)
{
    Measure.inject( "Output5V", Voltage{ 5.02 });
    // Output3V3 not provided at all -- nothing is queued for it, so Measure
    // throws rather than silently treating the missing point as a failed
    // check.

    EXPECT_THROW(supplyRailScript(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Tolerance variants
// ---------------------------------------------------------------------------

//
// The whole point of the mechanism, on a real script: this one binary holds
// production, stress and aged at once, the script below was compiled exactly
// once, and which tolerances it is held to is decided here at runtime.
//
// 5.09V is the value that tells them apart -- outside production's +/-50mV,
// inside stress's +/-100mV and aged's +/-150mV (see dut/criteria_*.inc). The
// 3.3V rail is left comfortably in tolerance for all three, so the verdict
// turns on the 5V rail alone.
//
// This is the runtime half. The compile-time half is that
// suite/scripts/supply_rail_script.cpp still names FS_Supply_1::FS_Supply_5V0
// as an ordinary struct member and would still fail to compile on a typo --
// which is not something a test can assert, but is the reason this file can
// call the script at all.
//
TEST_F( SupplyRailFixture, TheSameScriptIsHeldToWhicheverVariantIsSelected)
{
    const auto runAgainst = []( const std::string_view variant)
    {
        core::resetCriteriaVariantForTesting();

        EXPECT_TRUE( core::selectCriteriaVariant( variant));

        Measure.inject( "Output5V",  Voltage{ 5.09 });
        Measure.inject( "Output3V3", Voltage{ 3.30 });

        return verdictOf( supplyRailScript);
    };

    EXPECT_FALSE( runAgainst( "production")) << "5.09V is outside production's +/-50mV";
    EXPECT_TRUE(  runAgainst( "stress"))     << "5.09V is inside stress's +/-100mV";
    EXPECT_TRUE(  runAgainst( "aged"))       << "5.09V is inside aged's +/-150mV";
}

//
// A variant that widens one criterion does not quietly widen the others: the
// fuse register check is CRIT_FROM_PRODUCTION in both stress and aged (a
// register readback has no tolerance to loosen -- see dut/criteria_stress.inc),
// so a rail far enough out fails under every variant.
//
TEST_F( SupplyRailFixture, NoVariantIsWideEnoughToHideARealFailure)
{
    for( const auto variant : core::criteriaVariantNames())
    {
        core::resetCriteriaVariantForTesting();

        ASSERT_TRUE( core::selectCriteriaVariant( variant));

        Measure.inject( "Output5V",  Voltage{ 4.00 });
        Measure.inject( "Output3V3", Voltage{ 3.30 });

        EXPECT_FALSE( verdictOf( supplyRailScript)) << "4.00V on the 5V rail must fail under '" << variant << "'";
    }
}
