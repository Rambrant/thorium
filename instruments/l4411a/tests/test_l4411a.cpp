//
// hal::L4411A's own tests, moved here verbatim from framework/hal/tests/
// test_instrument.cpp when this driver moved out of framework/hal into its own
// directory. Test names are deliberately unchanged (still the HalInstrument
// suite, still L4411A-prefixed) so the move is provably behaviour-preserving:
// the same named tests run, in a different binary. Renaming the suite to
// L4411A and dropping the now-redundant prefix is a reasonable follow-up, but
// it is a separate change from the move, and doing both at once would have
// destroyed the before/after comparison that makes the move safe.
//
// What is left behind in framework/hal/tests/driver/test_instrument.cpp is the part that
// is genuinely about hal's generic mechanism rather than about any driver --
// hal::InstrumentId and to_string() over it.
//
#include "hal/l4411a.hpp"

#include "core/meta.hpp"

#include <gtest/gtest.h>

#include <concepts>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp
// uses for hal::SafeableInstrument, and hal/tests/driver/test_address.cpp for the
// hal::ReachableOver mechanism itself).
//
// An L4411A is an LXI box: LAN and USB, no GPIB connector at all, so a
// rig row addressing one over GPIB does not compile.
//
namespace
{
    static_assert(   std::constructible_from< hal::L4411A, hal::InstrumentId, hal::Lan> );
    static_assert(   std::constructible_from< hal::L4411A, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::L4411A, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::L4411A, hal::InstrumentId, hal::Gpib> );
    static_assert( ! std::constructible_from< hal::L4411A, hal::InstrumentId, hal::Serial> );

    //
    // And an address is not optional: an instrument the PC cannot reach is
    // not an instrument a rig has -- see rig/active_instruments.hpp on why
    // the INSTRUMENT() column is mandatory rather than defaulted.
    //
    static_assert( ! std::constructible_from< hal::L4411A, hal::InstrumentId> );
} // namespace

using namespace core::literals;
using namespace core::quantities;

TEST( HalInstrument, L4411AExposesBothVoltageAndCurrentPorts)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedVoltage( 5.02_V);
    dmm1.setSimulatedCurrent( 0.5_A);

    EXPECT_DOUBLE_EQ( dmm1.voltage().rawMeasure().value(), 5.02);
    EXPECT_DOUBLE_EQ( dmm1.current().rawMeasure().value(), 0.5);
}

//
// Two ids taken from whatever the linking deployment declares, rather than the
// literal Dmm1/Dmm2 this was first written with.
//
// The literals were a hidden dependency on one bench. Which enumerators
// hal::InstrumentId has comes from that deployment's instrument.inc, and a
// deployment with one meter on a desk has no Dmm2 -- so this file, in a
// directory whose README calls it independently packageable, failed to compile
// the first time it met a second rig (see dev/README.md). What the test is
// actually about is that the driver carries the id it was given through to its
// ports, which needs two distinct ids and does not care which two.
//
// Skipped rather than weakened on a deployment that declares only one: there is
// then nothing for an instrument to be distinguishable *from*, and a version of
// this that constructed two drivers on the same id would assert something true
// of a bug.
//
TEST( HalInstrument, TwoL4411AsAreDistinguishableByInstrumentId)
{
    constexpr auto ids = core::meta::values<hal::InstrumentId>;

    if constexpr( ids.size() < 2)
    {
        GTEST_SKIP() << "this deployment declares one instrument -- no second id to differ from";
    }
    else
    {
        hal::L4411A first{ ids[ 0], hal::Simulated{} };
        hal::L4411A second{ ids[ 1], hal::Simulated{} };

        EXPECT_EQ( first.voltage().instrumentId(), ids[ 0]);
        EXPECT_EQ( second.voltage().instrumentId(), ids[ 1]);
    }
}

TEST( HalInstrument, L4411AAcVoltagePortReadsTheAcSimulatedReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    EXPECT_DOUBLE_EQ( dmm1.acVoltage().rawMeasure().value(), 230.0);
}

TEST( HalInstrument, L4411AAcCurrentPortReadsTheAcSimulatedReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedCurrent( 0.5_A);
    dmm1.setSimulatedAcCurrent( 1.2_A);

    EXPECT_DOUBLE_EQ( dmm1.acCurrent().rawMeasure().value(), 1.2);
}

TEST( HalInstrument, L4411AVoltageAfterAcVoltageSwitchesBackToDcMode)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    (void)dmm1.acVoltage();

    EXPECT_DOUBLE_EQ( dmm1.voltage().rawMeasure().value(), 5.0);
}

TEST( HalInstrument, L4411AModeIsSharedAcrossPortHandlesHeldPastAModeSwitch)
{
    // Documents the known, accepted sharp edge from L4411A's own comment: a
    // port handle obtained before a mode switch still reads whichever mode
    // is current when rawMeasure() is eventually called, not the mode active
    // when the handle was created.
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    auto dcPort = dmm1.voltage();
    (void)dmm1.acVoltage();

    EXPECT_DOUBLE_EQ( dcPort.rawMeasure().value(), 230.0);
}

TEST( HalInstrument, L4411AResistancePortReadsTheTwoWireSimulatedReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedResistance( 100.0_Ohm);
    dmm1.setSimulatedFourWireResistance( 99.5_Ohm);

    EXPECT_DOUBLE_EQ( dmm1.resistance().rawMeasure().value(), 100.0);
}

TEST( HalInstrument, L4411AFourWireResistancePortReadsTheFourWireSimulatedReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedResistance( 100.0_Ohm);
    dmm1.setSimulatedFourWireResistance( 99.5_Ohm);

    EXPECT_DOUBLE_EQ( dmm1.fourWireResistance().rawMeasure().value(), 99.5);
}

TEST( HalInstrument, L4411AResistanceAfterFourWireResistanceSwitchesBackToTwoWireMode)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedResistance( 100.0_Ohm);
    dmm1.setSimulatedFourWireResistance( 99.5_Ohm);

    (void)dmm1.fourWireResistance();

    EXPECT_DOUBLE_EQ( dmm1.resistance().rawMeasure().value(), 100.0);
}

//
// The sense requirement is in the port's *type*, not in its runtime setup, so
// this is a static_assert rather than an EXPECT -- a two-wire reading and a
// four-wire one are now different types, and core::MeasureEngine branches on
// that with if constexpr.
//
TEST( HalInstrument, L4411AFourWireResistanceRequiresTheSensePathButTwoWireDoesNot)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };

    static_assert( decltype( dmm1.resistance())::SenseUse         == core::SensePath::NotUsed);
    static_assert( decltype( dmm1.fourWireResistance())::SenseUse == core::SensePath::Required);

    // The chained setup builders preserve it -- .nplc() on a four-wire port
    // must not quietly hand back a two-wire one.
    static_assert( decltype( dmm1.fourWireResistance().nplc( 10))::SenseUse == core::SensePath::Required);

    (void) dmm1;
}

//
// Generic core::Port machinery rather than anything L4411A-specific, but it
// needs a real driver to exercise it through and this is the one it was
// written against -- so it travels with the driver rather than staying behind
// in a framework/hal test that would then have had to keep including a driver
// header for this one case alone. core/tests/driver/test_port.cpp covers the builder
// chain itself without any instrument at all.
//
TEST( HalInstrument, PortRangeNplcAndFrequencyChainWithoutAffectingTheReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm1.setSimulatedVoltage( 5.02_V);

    auto port = dmm1.voltage().range( 20.0_V).nplc( 10).frequency( 50.0_Hz);

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 5.02);
    EXPECT_EQ( port.setup().Range,     std::optional{ 20.0_V});
    EXPECT_EQ( port.setup().Nplc,      std::optional{ 10});
    EXPECT_EQ( port.setup().Frequency, std::optional{ 50.0_Hz});
}

//
// The address survives construction and reads back as what the rig declared
// -- the whole of what this driver does with it today, exactly as
// hal::N6701A's mainframe slot was carried for a while before anything read
// it. When a real driver opens a session, this is the value it opens.
//
TEST( HalInstrument, L4411ACarriesTheAddressItWasDeclaredWith)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1, hal::Lan( "bench-dmm1") };

    EXPECT_EQ( to_string( dmm1.address()), "Lan bench-dmm1:5025");
}
