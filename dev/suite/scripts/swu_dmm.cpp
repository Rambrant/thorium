#include "../prelude.hpp"

#include "hal/fabric/switch_device.hpp"

#include <limits>

//
// The 34980A's internal DMM -- Dmm2, the second face of box Swu1 -- in the
// SwitchUnitDmm and SwitchUnitWired groups of dev/suite/test_catalog.inc.
//
// The meter measures the mainframe's Analog Buses, and a signal reaches a bus
// through the switching: a crosspoint onto a Matrix 2 row, and that row's bus
// relay -- 921 puts row 5 on ABus1, the meter's input. These scripts close
// those by hand (Swu1.close()), because the fabric does not drive this box yet;
// the day it does, the close below is a Connect.
//
// SwitchUnitDmm needs nothing wired and runs whole: the meter is there, an open
// bus reads as one, and closing a path onto nothing leaves it open.
//
// SwitchUnitWired is the whole chain and needs a cable, so it is run on its
// own: the supply's DcP7 output onto Matrix 2 column 1 of the 34932A in slot 1
// (its 34932T terminal block, HI and LO), and the meter must read the supply's
// 5 V through crosspoint 501 and bus relay 921. Three instruments -- the
// supply, the switching and the meter -- agreeing through real copper.
//
// Every script leaves slot 1 all open, and safing opens everything regardless.
//

using core::quantities::Resistance;
using hal::keysight_34980a::AnalogBus;
using hal::keysight_34980a::ChannelAddress;
using hal::keysight_34980a::analogBus;

namespace
{
    constexpr int kSlot = 1;

    //
    // Row 5, column 1: the first crosspoint of Matrix 2, which is the matrix
    // whose rows reach the Analog Buses.
    //
    constexpr ChannelAddress kRowFiveColumnOne{ kSlot, hal::detail::keysight34932ARowColumn( 5, 1) };

    //
    // Row 5's relay onto ABus1 -- the meter's input.
    //
    constexpr ChannelAddress kRowFiveOnTheBus = analogBus( kSlot, 2, AnalogBus::One);

    //
    // The open-bus resistance, as a value: the meter cannot measure an open
    // circuit and says so with an overload, and "beyond anything it can
    // measure" is what that means -- so it becomes an infinite resistance,
    // which the criterion's GT( 100 MOhm) then holds to. A reason that is not
    // an overload stays unmeasurable, and fails.
    //
    auto overloadIsOpen( const std::string_view reason) -> Resistance
    {
        return reason.contains( "overload")
                   ? Resistance{ std::numeric_limits<double>::infinity() }
                   : Resistance{ std::numeric_limits<double>::quiet_NaN() };
    }

    //
    // What an open bus reads: no DC voltage, and no resistance at all.
    //
    auto checkTheBusIsOpen() -> void
    {
        Verify( DEV_SwuDmm_1::DEV_SwuDmm_OpenVolts, Measure( Dmm2.voltage()));
        Verify( DEV_SwuDmm_1::DEV_SwuDmm_OpenOhms,  Measure( Dmm2.resistance().whenUnmeasurable( overloadIsOpen)));
    }
} // namespace

//
// The meter is in the box and on the buses -- asked of the switching face,
// which is the mainframe answering for its own assembly.
//
auto swuDmmFitted() -> void
{
    Verify( DEV_SwuDmm_1::DEV_SwuDmm_Fitted,  Swu1.internalDmmInstalled());
    Verify( DEV_SwuDmm_1::DEV_SwuDmm_Enabled, Swu1.internalDmmEnabled());
}

//
// With every relay of the slot open, nothing is on ABus1, and the meter has to
// read exactly that.
//
auto swuDmmOpenBus() -> void
{
    Swu1.openAll( kSlot);

    checkTheBusIsOpen();
}

//
// A path closed onto nothing is still open: row 5 on the bus, and a crosspoint
// onto an unwired column. What this catches is a short in the switching -- a
// relay welded, a row joined to another -- which would put something on the bus
// that nothing was told to put there.
//
auto swuDmmPathOntoNothing() -> void
{
    Swu1.openAll( kSlot);
    Swu1.close( { kRowFiveColumnOne, kRowFiveOnTheBus });

    checkTheBusIsOpen();

    Swu1.openAll( kSlot);
}

//
// The whole chain, wired: the supply onto column 1, through crosspoint 501 and
// bus relay 921, to the meter.
//
// In the order the framework holds every route to: the path closed while it is
// dead, then the source on, the reading, the source off, and only then the path
// opened -- so no relay here ever moves with 5 V across it. And the bus read
// open before and after, so a reading of 5 V is the supply arriving through the
// path rather than something left on the bus.
//
auto swuDmmThroughTheMatrix() -> void
{
    Swu1.openAll( kSlot);

    Verify( DEV_SwuDmm_1::DEV_SwuDmm_OpenVolts, Measure( Dmm2.voltage()));

    Swu1.close( { kRowFiveColumnOne, kRowFiveOnTheBus });

    Apply( DcP7.dc().voltage( 5.0_V).currentLimit( 10.0_mA).overVoltageProtection( 6.0_V));

    Verify( DEV_SwuDmm_1::DEV_SwuDmm_Through, Measure( Dmm2.voltage()));

    Remove( DcP7.dc());

    Swu1.openAll( kSlot);

    Verify( DEV_SwuDmm_1::DEV_SwuDmm_OpenVolts, Measure( Dmm2.voltage()));
}
