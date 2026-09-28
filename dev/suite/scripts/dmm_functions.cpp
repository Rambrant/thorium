#include "../prelude.hpp"

//
// Every measurement function the EDU34450A has, one script each -- the
// DmmFunctions group in dev/suite/test_catalog.inc.
//
// One script per function rather than one script reading them all, because
// the desk has no switching: the meter has one pair of terminals (two, for
// 4-wire and for current), and a DC reference, a sine, a resistor and a
// capacitor cannot all be across them at once. So each test is run on its
// own, with its own reference connected -- tick that one test in the console
// -- and a run of the whole group is not expected to pass. DmmSelfCheck is
// the exception that proves it: its two readings share one fixture.
//
// Several scripts in one file, where the rest of the tree has one per file:
// these nine are one thing -- the meter's function list, in the order its
// header declares them -- and reading them side by side is the point. Each is
// still its own catalog row and its own declaration in dev/suite/scripts.hpp.
//
// All point-free, like DmmSelfCheck and for the same reason: no fabric, no
// pin, just a reference across the meter's own terminals. That also means the
// readings key as "Dmm1.<quantity>" -- and so dmmDcVoltage and dmmAcVoltage
// both inject as "Dmm1.Voltage", which a unit test needs to know and a run
// never notices. See dev/suite/tests/test_dmm_functions.cpp.
//

//
// Twice: once autoranged, once on a range fixed by the script, so that the
// range argument the driver sends (CONF:VOLT:DC 10) is exercised as well as
// autorange. 10 V is the range a 5 V reading lands on by itself, so both
// readings are of the same thing and meet the same criterion.
//
auto dmmDcVoltage() -> void
{
    const auto autoranged = Measure( Dmm1.voltage());
    const auto ranged     = Measure( Dmm1.voltage().range( 10.0_V));

    Verify( DEV_Dmm_1::DEV_Dmm_Ref, autoranged);
    Verify( DEV_Dmm_1::DEV_Dmm_Ref, ranged);
}

//
// The resolution setting, which is instrument state rather than a per-reading
// port setting (see EDU34450A::setResolution) -- so this is the one script
// that changes the meter and has to put it back. The 5 V cell, read at each of
// the three resolutions; each must still meet the same criterion, which is the
// claim worth checking: a faster reading is a noisier one, not a different one.
//
auto dmmResolution() -> void
{
    using Resolution = hal::keysight_edu34450a::EDU34450A::Resolution;

    const auto previous = Dmm1.resolution();

    for( const auto resolution : { Resolution::Slow, Resolution::Fast })
    {
        Dmm1.setResolution( resolution);
        Verify( DEV_Dmm_1::DEV_Dmm_Ref, Measure( Dmm1.voltage()));
    }

    Dmm1.setResolution( previous);
}

auto dmmAcVoltage() -> void
{
    Verify( DEV_Dmm_Fn::DEV_Dmm_AcRef, Measure( Dmm1.acVoltage()));
}

auto dmmDcCurrent() -> void
{
    Verify( DEV_Dmm_Fn::DEV_Dmm_DcIRef, Measure( Dmm1.current()));
}

auto dmmAcCurrent() -> void
{
    Verify( DEV_Dmm_Fn::DEV_Dmm_AcIRef, Measure( Dmm1.acCurrent()));
}

auto dmmResistance() -> void
{
    Verify( DEV_Dmm_Fn::DEV_Dmm_Res2W, Measure( Dmm1.resistance()));
}

//
// Point-free, and legal: the sense path fourWireResistance() requires is a
// routing requirement, and a reading with no route has nothing to route --
// the sense leads go from the meter's own terminals to the resistor by hand.
//
auto dmmFourWireResistance() -> void
{
    Verify( DEV_Dmm_Fn::DEV_Dmm_Res4W, Measure( Dmm1.fourWireResistance()));
}

//
// The FREQ function, a reading -- not core::Port::frequency(), which tells a
// meter what frequency to expect. See EDU34450A::frequency() on the two.
//
auto dmmFrequency() -> void
{
    Verify( DEV_Dmm_Fn::DEV_Dmm_Freq, Measure( Dmm1.frequency()));
}

auto dmmCapacitance() -> void
{
    Verify( DEV_Dmm_1::DEV_Dmm_Bulk, Measure( Dmm1.capacitance()));
}
