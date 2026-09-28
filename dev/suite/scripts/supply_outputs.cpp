#include "../prelude.hpp"

//
// The bench supply's three outputs, one script each -- the SupplyOutputs group
// in dev/suite/test_catalog.inc. An EDU36311A is three instruments in this
// framework (DcP5, DcP6, DcP7 in dev/rig/instrument.inc), one per output,
// because each output is its own rating and its own channel.
//
// With nothing connected, and that is what makes these runnable as a group
// where the meter's function tests are not: an open output needs no fixture.
// So each script checks what a supply can say about itself -- MEAS:VOLT? and
// MEAS:CURR? over its own interface -- against what it was told:
//
//   two setpoints, low then high, so that a readback which merely reports
//   "some voltage" or the previous setpoint fails the second check;
//   the current it draws into nothing, which has to be none;
//   and the readback after Remove, which has to be zero -- the check that
//   says "off" means off, and the one safing depends on.
//
// Every Apply carries a current limit and an over-voltage trip as well as the
// setpoint, so that all three settings the driver can send reach the wire on
// every run. Neither acts on an open output; what is exercised is that the
// supply accepts them. The limit is small on purpose: if a lead is left on
// something, 100 mA is what it gets.
//
// Point-free, with no Connect: the desk's rows are DirectOutput, so there is
// no isolation relay to close (see hal::keysight_edu36311a::DirectWiring).
//
namespace
{
    template<typename SupplyT, typename LowT, typename HighT>
    auto checkOutput( SupplyT & supply,
                      const core::quantities::Voltage low,  const LowT & lowCriterion,
                      const core::quantities::Voltage high, const HighT & highCriterion,
                      const core::quantities::Voltage trip) -> void
    {
        Apply( supply.dc().voltage( low).currentLimit( 100.0_mA).overVoltageProtection( trip));
        Verify( lowCriterion,          Measure( supply.measuredVoltage()));
        Verify( DEV_Psu_1::DEV_Psu_Idle, Measure( supply.measuredCurrent()));

        Apply( supply.dc().voltage( high).currentLimit( 100.0_mA).overVoltageProtection( trip));
        Verify( highCriterion,         Measure( supply.measuredVoltage()));

        Remove( supply.dc());
        Verify( DEV_Psu_1::DEV_Psu_Off,  Measure( supply.measuredVoltage()));
    }
} // namespace

//
// The 6 V / 5 A output: 1 V and 5 V, tripping at its 6 V rating.
//
auto psuOutput1Check() -> void
{
    checkOutput( DcP5, 1.0_V, DEV_Psu_1::DEV_Psu_1V, 5.0_V, DEV_Psu_1::DEV_Psu_5V, 6.0_V);
}

//
// The two 30 V / 1 A outputs: 5 V and 24 V, tripping at 26 V -- high enough
// to be well into the range a 6 V output cannot reach, low enough to leave
// the rating untouched.
//
auto psuOutput2Check() -> void
{
    checkOutput( DcP6, 5.0_V, DEV_Psu_1::DEV_Psu_5V, 24.0_V, DEV_Psu_1::DEV_Psu_24V, 26.0_V);
}

auto psuOutput3Check() -> void
{
    checkOutput( DcP7, 5.0_V, DEV_Psu_1::DEV_Psu_5V, 24.0_V, DEV_Psu_1::DEV_Psu_24V, 26.0_V);
}
