#include "../prelude.hpp"

//
// How much bulk capacitance the DUT presents on its 24 V battery input.
//
// A dried-out or missing input electrolytic is one of the few faults that a
// powered test cannot see: the rails all come up, every voltage is in
// tolerance, and the board fails the first time its input is disturbed. This
// measures the part directly.
//
// The point of this script, beyond the check itself, is the shape of a reading
// that cannot be taken on a live bench -- the only one in this suite. Every
// other Measure here happens with the rig in the state rigPowerOn left it in.
// This one has to take a rail away first, and the framework will not let it
// skip that:
//
//     take the rail down, prove it is down, then measure
//
// -- Why the rail has to come down ------------------------------------------
//
// A DMM measures capacitance by charging the node from its own current source
// and timing the ramp -- 100 nA to 1 mA depending on range (see
// hal/keysight_edu34450a.hpp). A rail driving that same node does not merely
// spoil the reading; it holds the node at its own voltage and the meter ends up
// measuring the supply's regulation loop.
//
// So core::requiresDeadNode names Capacitance alongside Current and Resistance
// (see core/verbs/interlock.hpp), and dut::BatterySupply is a SOURCE_POINT with
// DcP7 cabled onto it (rig/wiring.inc). Written without the Remove below, the
// Measure throws core::InterlockViolation before a single relay closes, naming
// the pin, the rail and the way out. That is not a check this script performs;
// it is the check that made this script have to be written this way.
//
// -- And why "down" is then verified rather than assumed --------------------
//
// Removing a rail is not the same as the node being at zero: the very capacitor
// under test is what holds the charge, and a 1 mF part behind a light load
// takes its time. The interlock is satisfied the instant the supply's output is
// off -- it asks the *instrument* whether it is energised, which is the only
// question it can answer -- so nothing downstream of that would notice a node
// still sitting at 20 V.
//
// A capacitance reading taken there is not an error, which is exactly the
// problem: it is a plausible number, low, and wrong. So the residual is
// measured and checked like anything else, and the capacitance reading sits
// behind it -- the same shape acDropoutScript uses around FS_Transient_Captured,
// and for the same reason. A reading whose precondition failed must not appear
// in the report as a reading.
//
// -- Putting the rail back is not this script's job -------------------------
//
// DcP7 stays off and disconnected when this returns. The group's TEARDOWN puts
// it back -- see suite/scripts/capacitance_bracket.cpp, and suite/test_catalog.inc
// where the Capacitance group declares the pair. A restore written at the end
// of a script only runs on the path where the script reaches its end.
//
auto bulkCapacitanceScript() -> void
{
    //
    // Remove before Disconnect, so the relay opens on a dead path rather than
    // under load -- see core/verbs/source.hpp, which states the rule and says
    // why it is recorded rather than refused. Swapping these two lines is not a
    // build error and not a failing script; what catches it is
    // AcceptanceMachineLog.NoShippedScriptOrHookMovesARelayUnderLoad, which
    // reads this run's own machine log.
    //
    // The Disconnect is not merely tidiness here, and it is the one thing this
    // script does that acDropoutScript does not do for the same reason: it
    // takes DcP7's output impedance off the node. What is being measured is
    // what the *DUT* presents at that pin, and a supply left cabled on -- even
    // with its output off -- is part of the node the meter charges. DcP7 can do
    // this because it is a hal::keysight_edu36311a::RelayOutput3; the same two lines on
    // DcP5 would not compile (see hal::keysight_edu36311a::SwitchableIsolation)
    // -- which is exactly why the battery rail is on DcP7 and not on the one
    // hard-wired output this supply has.
    //
    Remove(     DcP7.dc());
    Disconnect( DcP7.dc());

    //
    // A plain voltage tap, which stays permitted at a live rail precisely
    // because it is high-impedance -- so this reading is legal whether or not
    // the Remove above actually took effect, and is therefore able to report
    // that it did not.
    //
    // Measured at the pin rather than as DcP7.measuredVoltage(), and the
    // difference is the whole check: the supply's own readback says what the
    // supply is delivering, which with its output off and its relay open is
    // zero no matter what charge is sitting on the DUT's capacitor. What holds
    // that charge is the part under test. Only a meter at the pin can see it.
    //
    // .qualifiedBy( ...) on both readings, because this script takes two of
    // them at one point and a routed reading keys by its point name. Two
    // *quantities* at one pin is the collision core::MeasureEngine's own
    // comment names as the one it does not fix -- folding QuantityKind into
    // the key would rename every key in every existing recording -- and a
    // qualifier is the remedy it points at.
    //
    // This is the one place in this suite where a *script* sets one rather
    // than a driver (see core::Port::qualifiedBy, which says as much). A scope
    // qualifies its own ports because fifteen answers about one pin is a fact
    // about the instrument; a DMM's voltage() is simply its voltage, and
    // nothing about the meter knows this script is going to ask it two
    // questions about one pin. That is a fact about this test, so this test is
    // where it is written -- and "BatterySupply.Residual"/".Bulk" name what the
    // readings are for, which "...Voltage"/"...Capacitance" would not.
    //
    const auto residual = Measure( Dmm1.voltage().qualifiedBy( "Residual"), at( dut::BatterySupply));

    //
    // Deliberately not a bare `if` on a threshold written here. What counts as
    // discharged is a fact about this DUT and belongs in the criteria tables
    // with everything else that is -- and a precondition checked in a script is
    // invisible in the report as anything but prose (see dut/criteria_production.inc's
    // own comment on FS_Transient_Captured, which is the same argument).
    //
    if( Verify( FS_BulkCap_1::FS_BulkCap_Rail_Dead, residual))
    {
        //
        // No .range( ...): the meter autoranges, and a range named here would
        // be this script asserting which decade the part is in -- which is the
        // criterion's business, and would have to be kept in step with it by
        // hand. Autoranging costs a second or so on this function and this test
        // takes one reading.
        //
        const auto bulk = Measure( Dmm1.capacitance().qualifiedBy( "Bulk"), at( dut::BatterySupply));

        Verify( FS_BulkCap_1::FS_BulkCap_Battery, bulk);
    }
    else
    {
        //
        // The prose form of Fail rather than FS_BulkCap_Battery. The criterion
        // means "the input bulk capacitance is this much", and nothing here
        // measured the input bulk capacitance -- naming it, even as unchecked,
        // would attach this run's silence to a requirement it has nothing to
        // say about. What did happen is that the check could not be made, and
        // that is what this says.
        //
        Fail( "24Vdc input bulk capacitance -- not measured, the rail had not discharged");
    }
}
