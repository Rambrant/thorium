#include "../prelude.hpp"

#include "hal/fabric/switch_device.hpp"

#include <limits>

//
// The 34980A's internal DMM -- Dmm2, the second face of box Swu1 -- in the
// SwitchUnitDmm and SwitchUnitWired groups of dev/suite/test_catalog.inc.
//
// The meter measures the mainframe's Analog Buses, and a signal reaches a bus
// through the switching: a crosspoint onto a Matrix 2 row, and that row's bus
// relay -- 921 puts row 5 on ABus1, the meter's input. The SwitchUnitDmm
// scripts close those by hand (Swu1.close()), because what they check is the
// meter and the bus with no route at all; SwitchUnitWired reaches the terminal
// the framework's way, a routed Measure the fabric switches for.
//
// SwitchUnitDmm runs whole: the meter is there, an open bus reads as one, and
// closing a path onto nothing leaves it open -- column 2, which nothing is
// cabled to.
//
// SwitchUnitWired is the whole chain, through the desk's one cable: the
// supply's DcP7 output onto Matrix 2 column 1 of the 34932A in slot 1 (its
// 34932T terminal block, HI and LO -- dut::DeskTerminal, and a WIRE_SOURCE row
// in dev/rig/wiring.inc), and the meter must read the supply's 5 V through
// crosspoint 501 and bus relay 921. Three instruments -- the
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
    // Row 5, column 2: a crosspoint of Matrix 2, the matrix whose rows reach
    // the Analog Buses, onto a column nothing is cabled to. Not column 1:
    // DcP7 is on that one (dut::DeskTerminal), so a path closed there is a
    // path onto the supply's output, cold or not.
    //
    constexpr ChannelAddress kRowFiveColumnTwo{ kSlot, hal::detail::keysight34932ARowColumn( 5, 2) };

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
    Swu1.close( { kRowFiveColumnTwo, kRowFiveOnTheBus });

    checkTheBusIsOpen();

    Swu1.openAll( kSlot);
}

//
// The whole chain, wired: the supply onto dut::DeskTerminal -- Matrix 2 column 1
// of slot 1 -- and the meter reading it the framework's way, a routed Measure
// at the point. The fabric composes the path from dev/rig/wiring.inc (Dmm2's bus
// relay 921, the terminal's crosspoint 501), closes it through the Chassis,
// reads, and opens it again -- the relay moves the scripts above make by hand,
// made by the fabric, and journalled.
//
// Supply on, reading, supply off, reading: the second reading is the terminal
// with the supply removed, which has to be zero for the first to mean the
// supply arrived rather than something else on the column. The routed readings
// close their path onto a live 5 V, which is how every routed rail reading on
// the bench is taken -- the meter is a high-impedance load, and the rule about
// relays moving on a dead path is the sources' (see core/verbs/source.hpp). And
// the bus is read open, point-free, before and after: the path is released
// after each routed reading, so nothing is left on it.
//
auto swuDmmThroughTheMatrix() -> void
{
    Swu1.openAll( kSlot);

    Verify( DEV_SwuDmm_1::DEV_SwuDmm_OpenVolts, Measure( Dmm2.voltage()));

    Apply( DcP7.dc().voltage( 5.0_V).currentLimit( 10.0_mA).overVoltageProtection( 6.0_V));

    Verify( DEV_SwuDmm_1::DEV_SwuDmm_Through, Measure( Dmm2.voltage(), at( dut::DeskTerminal)));

    Remove( DcP7.dc());

    Verify( DEV_SwuDmm_1::DEV_SwuDmm_OpenVolts, Measure( Dmm2.voltage(), at( dut::DeskTerminal)));

    Verify( DEV_SwuDmm_1::DEV_SwuDmm_OpenVolts, Measure( Dmm2.voltage()));
}
