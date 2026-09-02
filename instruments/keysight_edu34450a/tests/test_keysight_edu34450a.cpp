//
// hal::keysight_edu34450a::EDU34450A's own tests.
//
// Linked against this driver alone -- hal and core and nothing else -- which is
// what keeps this directory independently packageable (see
// instruments/README.md). A test here that wanted the fabric, an Apply or a
// second instrument would fail to link where it sits, and belongs in
// rig/tests/.
//
// The suite is named for the driver rather than for the generic mechanism it
// exercises. instruments/keysight_l4411a/tests is still under the historical
// HalInstrument name, deliberately, so that its move out of framework/hal stayed
// provably behaviour-preserving; this file has no such history to preserve.
//
#include "hal/keysight_edu34450a.hpp"

#include "core/meta.hpp"
#include "core/verbs/interlock.hpp"

#include <gtest/gtest.h>

#include <concepts>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp uses
// for hal::SafeableInstrument, and framework/hal/tests/driver/test_address.cpp
// for the hal::ReachableOver mechanism itself).
//
// Gigabit LAN and a rear USBTMC port, and no GPIB connector at all: the
// rack-mount 34450A takes a GPIB option, and this is the bench box that does
// not. So a rig row addressing one over GPIB does not compile.
//
namespace
{
    static_assert(   std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Lan> );
    static_assert(   std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Gpib> );
    static_assert( ! std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Serial> );

    //
    // And an address is not optional: an instrument the PC cannot reach is not
    // an instrument a rig has -- see hal/topology/active_instruments.hpp on
    // why the INSTRUMENT() column is mandatory rather than defaulted.
    //
    static_assert( ! std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId> );

    //
    // The safing contract, asserted here rather than only in
    // rig/tests/test_safing.cpp: that file expands the rig's whole instrument
    // list and so can only run where a rig is linked, but whether *this* driver
    // satisfies the concept is a fact about this directory.
    //
    static_assert( hal::SafeableInstrument< hal::keysight_edu34450a::EDU34450A> );
    static_assert( std::derived_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentTag> );
} // namespace

using namespace core::literals;
using namespace core::quantities;

using hal::keysight_edu34450a::EDU34450A;

TEST( Edu34450A, ExposesBothVoltageAndCurrentPorts)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedVoltage( 5.02_V);
    dmm.setSimulatedCurrent( 0.5_A);

    EXPECT_DOUBLE_EQ( dmm.voltage().rawMeasure().value(), 5.02);
    EXPECT_DOUBLE_EQ( dmm.current().rawMeasure().value(), 0.5);
}

//
// Two ids taken from whatever the linking deployment declares, rather than the
// literal Dmm1/Dmm2 this would otherwise be written with -- see the same test
// in instruments/keysight_l4411a/tests for the bench dependency that shape
// removes. What is under test is that the driver carries the id it was given
// through to its ports, which needs two distinct ids and does not care which
// two.
//
// Skipped rather than weakened on a deployment that declares only one: there is
// then nothing for an instrument to be distinguishable *from*, and a version of
// this that constructed two drivers on the same id would assert something true
// of a bug.
//
TEST( Edu34450A, TwoMetersAreDistinguishableByInstrumentId)
{
    constexpr auto ids = core::meta::values<hal::InstrumentId>;

    if constexpr( ids.size() < 2)
    {
        GTEST_SKIP() << "this deployment declares one instrument -- no second id to differ from";
    }
    else
    {
        EDU34450A first{  ids[ 0], hal::Simulated{} };
        EDU34450A second{ ids[ 1], hal::Simulated{} };

        EXPECT_EQ( first.voltage().instrumentId(),  ids[ 0]);
        EXPECT_EQ( second.voltage().instrumentId(), ids[ 1]);
    }
}

TEST( Edu34450A, AcVoltagePortReadsTheAcSimulatedReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedVoltage( 5.0_V);
    dmm.setSimulatedAcVoltage( 230.0_V);

    EXPECT_DOUBLE_EQ( dmm.acVoltage().rawMeasure().value(), 230.0);
}

TEST( Edu34450A, AcCurrentPortReadsTheAcSimulatedReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedCurrent( 0.5_A);
    dmm.setSimulatedAcCurrent( 1.2_A);

    EXPECT_DOUBLE_EQ( dmm.acCurrent().rawMeasure().value(), 1.2);
}

TEST( Edu34450A, VoltageAfterAcVoltageSwitchesBackToDcMode)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedVoltage( 5.0_V);
    dmm.setSimulatedAcVoltage( 230.0_V);

    (void)dmm.acVoltage();

    EXPECT_DOUBLE_EQ( dmm.voltage().rawMeasure().value(), 5.0);
    EXPECT_EQ( dmm.mode(), EDU34450A::Mode::Dc);
}

TEST( Edu34450A, ModeIsSharedAcrossPortHandlesHeldPastAModeSwitch)
{
    // Documents the known, accepted sharp edge from EDU34450A's own comment: a
    // port handle obtained before a mode switch still reads whichever mode is
    // current when rawMeasure() is eventually called, not the mode active when
    // the handle was created. It mirrors the instrument, which has one
    // measurement front end and one SCPI FUNCtion setting.
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedVoltage( 5.0_V);
    dmm.setSimulatedAcVoltage( 230.0_V);

    auto dcPort = dmm.voltage();
    (void)dmm.acVoltage();

    EXPECT_DOUBLE_EQ( dcPort.rawMeasure().value(), 230.0);
}

TEST( Edu34450A, ResistancePortReadsTheTwoWireSimulatedReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedResistance( 100.0_Ohm);
    dmm.setSimulatedFourWireResistance( 99.5_Ohm);

    EXPECT_DOUBLE_EQ( dmm.resistance().rawMeasure().value(), 100.0);
}

TEST( Edu34450A, FourWireResistancePortReadsTheFourWireSimulatedReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedResistance( 100.0_Ohm);
    dmm.setSimulatedFourWireResistance( 99.5_Ohm);

    EXPECT_DOUBLE_EQ( dmm.fourWireResistance().rawMeasure().value(), 99.5);
}

TEST( Edu34450A, ResistanceAfterFourWireResistanceSwitchesBackToTwoWireMode)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedResistance( 100.0_Ohm);
    dmm.setSimulatedFourWireResistance( 99.5_Ohm);

    (void)dmm.fourWireResistance();

    EXPECT_DOUBLE_EQ( dmm.resistance().rawMeasure().value(), 100.0);
    EXPECT_EQ( dmm.resistanceMode(), EDU34450A::ResistanceMode::TwoWire);
}

//
// The sense requirement is in the port's *type*, not in its runtime setup, so
// this is a static_assert rather than an EXPECT -- a two-wire reading and a
// four-wire one are different types, and core::MeasureEngine branches on that
// with if constexpr.
//
TEST( Edu34450A, FourWireResistanceRequiresTheSensePathButTwoWireDoesNot)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };

    static_assert( decltype( dmm.resistance())::SenseUse         == core::SensePath::NotUsed);
    static_assert( decltype( dmm.fourWireResistance())::SenseUse == core::SensePath::Required);

    // The chained setup builders preserve it -- .range() on a four-wire port
    // must not quietly hand back a two-wire one.
    static_assert( decltype( dmm.fourWireResistance().range( 1000.0_Ohm))::SenseUse == core::SensePath::Required);

    (void) dmm;
}

//
// The FREQ function is its own function on this meter, not the AC half of the
// voltage one -- so what the DC/AC keys were last set to says nothing about
// what a frequency reading returns.
//
TEST( Edu34450A, FrequencyPortReadsRegardlessOfWhichModeTheMeterIsIn)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedFrequency( 50.0_Hz);

    EXPECT_DOUBLE_EQ( dmm.frequency().rawMeasure().value(), 50.0);

    (void)dmm.acVoltage();

    EXPECT_DOUBLE_EQ( dmm.frequency().rawMeasure().value(), 50.0);
}

//
// A frequency *reading* and the frequency *setting* on an AC port are two
// different things a dot apart -- see EDU34450A::frequency()'s own comment.
// This asserts they stay that way: setting the hint on an AC voltage port
// leaves that port a voltage port, and does not touch the counter.
//
TEST( Edu34450A, TheFrequencySettingOnAnAcPortIsNotAFrequencyReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedAcVoltage( 230.0_V);
    dmm.setSimulatedFrequency( 50.0_Hz);

    const auto port = dmm.acVoltage().frequency( 50.0_Hz);

    static_assert( std::same_as< decltype( port.rawMeasure()), Voltage> );

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 230.0);
    EXPECT_EQ( port.setup().Frequency, std::optional{ 50.0_Hz});
}

//
// The one function on this meter that neither the L4411A nor anything else on
// this bench has, and the reason core grew a farad. Its own function like FREQ,
// so likewise unaffected by which of the DC/AC keys was last pressed.
//
TEST( Edu34450A, CapacitancePortReadsRegardlessOfWhichModeTheMeterIsIn)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedCapacitance( 470.0_uF);

    EXPECT_DOUBLE_EQ( dmm.capacitance().rawMeasure().value(), 470.0e-6);

    (void)dmm.acVoltage();

    EXPECT_DOUBLE_EQ( dmm.capacitance().rawMeasure().value(), 470.0e-6);
}

//
// Two claims about the capacitance port's type, and the second is the one that
// matters on a rig.
//
// There is no 4-wire capacitance: the instrument has one capacitance function
// and it is 2-wire, so the port must not carry a sense requirement.
//
// And a capacitance reading is one core::requiresDeadNode names -- the meter
// charges the node from its own current source (see core/verbs/interlock.hpp)
// -- so core::MeasureEngine refuses to route one to a pin an energised supply
// is cabled onto. That check is an `if constexpr` on the port's quantity, so it
// is settled here, by the type this returns, and asserted for real against this
// rig's SOURCE_WIRING in rig/tests/test_interlock.cpp.
//
TEST( Edu34450A, CapacitanceIsATwoWireReadingThatRequiresADeadNode)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };

    static_assert( decltype( dmm.capacitance())::SenseUse == core::SensePath::NotUsed);

    static_assert(   core::requiresDeadNode( core::quantityKindOf<Capacitance>()));
    static_assert( ! core::requiresDeadNode( core::quantityKindOf<Voltage>()));

    (void) dmm;
}

//
// Slow is what the instrument resets to and what its accuracy specifications
// are quoted at, so it is what this driver starts in -- see Resolution's own
// comment on why this axis is instrument state and not a MeasureSetup field.
//
TEST( Edu34450A, ResolutionDefaultsToSlowAndIsInstrumentStateNotPortState)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };

    EXPECT_EQ( dmm.resolution(), EDU34450A::Resolution::Slow);

    dmm.setResolution( EDU34450A::Resolution::Fast);

    EXPECT_EQ( dmm.resolution(), EDU34450A::Resolution::Fast);

    // Survives a function change, exactly as the front-panel setting does.
    (void)dmm.acCurrent();

    EXPECT_EQ( dmm.resolution(), EDU34450A::Resolution::Fast);
}

//
// safe() is a no-op on a meter, and this is what "no-op" has to mean: the
// instrument's last function and resolution are still readable afterwards.
// Nothing about them can energise the DUT, and safing runs after a script has
// already died, so there is no reason to discard the one piece of state still
// worth reading. rig/tests/test_safing.cpp asserts the same thing through
// hal::safeRig() over the whole rig; this asserts it about the driver.
//
TEST( Edu34450A, SafeLeavesTheMetersOwnStateAlone)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    (void)dmm.acVoltage();
    dmm.setResolution( EDU34450A::Resolution::Medium);

    dmm.safe();

    EXPECT_EQ( dmm.mode(),       EDU34450A::Mode::Ac);
    EXPECT_EQ( dmm.resolution(), EDU34450A::Resolution::Medium);
}

//
// The address survives construction and reads back as what the rig declared --
// the whole of what this driver does with it today. When a real driver opens a
// session, this is the value it opens.
//
TEST( Edu34450A, CarriesTheAddressItWasDeclaredWith)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Lan( "bench-dmm1") };

    EXPECT_EQ( to_string( dmm.address()), "Lan bench-dmm1:5025");
}
