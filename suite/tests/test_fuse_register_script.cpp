#include "suite/scripts.hpp"

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
