#pragma once

//
// The pair that brackets every run: rigPowerOn brings this rig's sources up in
// order and checks each came up (a false here stops the run before the first
// test), rigPowerOff takes them back down in the inverse order. Read the two
// files as one sequence and its reverse -- see suite/scripts/rig_power_on.cpp.
//
RUN_SETUP(    rigPowerOn,  "Bring the AC input and the three DC supplies up, checking each rail as it comes")
RUN_TEARDOWN( rigPowerOff, "Take the supplies back down in the inverse order and open the fabric")

//
// Two groups below bracket themselves: Transient, whose test takes the primary
// AC input away, and Capacitance, whose test takes the battery rail down. That
// is the case a group-level pair exists for -- a rig state one group disturbs
// and the others must be handed intact -- and each runs only for a selection
// that actually reaches its group. The power cycle above is not that case.
// Every test here wants it, which is why it is stated once, at the top, for the
// whole run.
//

GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
    TEST( FuseRegister, fuseRegisterScript, "Verify fuse register and output voltage")
    TEST( SupplyRail,   supplyRailScript,   "Verify supply rail voltages via matrix")
END_GROUP

//
// The single-shot group. Its own group rather than a third entry under
// OutputVoltage, because what it exercises is different in kind: the tests
// above read a rig that is standing still, and this one measures what the DUT
// does while the bench takes something away from it. A --select naming this
// group selects the tests that disturb the rig, which is a distinction worth
// being able to make from the command line.
//
GROUP( Transient, "Tests validating DUT behaviour while a supply is disturbed")
    SETUP(    transientSetup,    "Record how the primary AC source is programmed, before its test drops it")
    TEST( AcDropout, acDropoutScript, "Verify the 5V rail rides a loss of the primary AC input")
    TEARDOWN( transientTeardown, "Put the primary AC source back the way the setup found it")
END_GROUP

//
// The second group that brackets itself, and the second kind of rig state a
// group can disturb. Transient above takes the AC input away as its test's
// *stimulus* -- the event it measures the effect of. This one takes the battery
// rail away as its test's *precondition*: a capacitance reading sources into the
// node it measures, so the rail has to be down before the reading is possible at
// all (see core::requiresDeadNode, and the script's own comment).
//
// Which is why the drop is in the script rather than in the setup, in both
// groups and for opposite reasons -- there it is the thing being tested, here it
// is the thing the test needs -- and why the restore is in the teardown in both:
// a rail this suite's later groups expect to find up must be handed back whether
// or not the test that took it away ever ran.
//
GROUP( Capacitance, "Tests validating DUT input capacitance, measured cold")
    SETUP(    capacitanceSetup,    "Record how the battery supply is programmed, before its test drops it")
    TEST( BulkCapacitance, bulkCapacitanceScript, "Verify the 24Vdc input bulk capacitance with the rail down")
    TEARDOWN( capacitanceTeardown, "Put the battery supply back the way the setup found it")
END_GROUP

GROUP( Console, "Tests validating the DUT's RS232 debug console")
    TEST( StatusRegister, consoleScript, "Verify the console acknowledges and reports a healthy status")
END_GROUP
