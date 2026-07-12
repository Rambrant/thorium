#include "hal/rig.hpp"

#include <gtest/gtest.h>

#include "core/rig.hpp"

using core::quantities::Voltage;

TEST( HalRig, ExposesInstrumentsThroughCoreInterface)
{
    hal::SimRig  rig;

    core::IRig & interface = rig;

    EXPECT_NE( interface.oscilloscope(), nullptr);
    EXPECT_NE( interface.voltmeter(), nullptr);
    EXPECT_NE( interface.powerSupply(), nullptr);
}

TEST( HalRig, ProgrammedScopeReadingIsVisibleThroughInterface)
{
    hal::SimRig rig;

    rig.simScope().setLevel(Voltage{ 5.0 });

    core::IRig & interface = rig;

    EXPECT_DOUBLE_EQ( interface.oscilloscope()->measureLevel().value(), 5.0);
}

TEST( HalRig, PowerSupplyRoundTripsThroughInterface)
{
    hal::SimRig  rig;
    core::IRig & interface = rig;
    auto *       supply    = interface.powerSupply();

    supply->setOutput( Voltage{ 3.3 });
    supply->enable();

    EXPECT_DOUBLE_EQ( rig.simSupply().output().value(), 3.3);
    EXPECT_TRUE( rig.simSupply().enabled());
}

TEST( HalRig, MatrixAccessibleThroughInterface)
{
    hal::SimRig  rig;
    core::IRig & interface = rig;

    interface.matrix().close({ 2, 3 });

    EXPECT_TRUE( interface.matrix().isClosed({2, 3}));
}

TEST( HalRig, PerCrosspointVoltmeterReadingFollowsMatrixRouting)
{
    hal::SimRig rig;

    rig.simVoltmeter().setReadingAt({ 3, 7 }, Voltage{ 5.0 });
    rig.simVoltmeter().setReadingAt({ 3, 6 }, Voltage{ 3.3 });

    core::IRig & interface = rig;

    interface.matrix().close({ 3, 7 });

    EXPECT_DOUBLE_EQ( interface.voltmeter()->measureVoltage().value(), 5.0);

    interface.matrix().openAll();
    interface.matrix().close({ 3, 6 });

    EXPECT_DOUBLE_EQ( interface.voltmeter()->measureVoltage().value(), 3.3);
}
