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
// No group below brackets itself. A GROUP may, with SETUP/TEARDOWN rows of its
// own (see core/test_catalog.hpp), and those run only for a selection that
// actually reaches the group -- which is the case for reaching for them: a rig
// state one group's tests need and the others must not be given. The power
// cycle above is not that case. Every test here wants it, which is why it is
// stated once, at the top, for the whole run.
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
    TEST( AcDropout, acDropoutScript, "Verify the 5V rail rides a loss of the primary AC input")
END_GROUP

GROUP( Console, "Tests validating the DUT's RS232 debug console")
    TEST( StatusRegister, consoleScript, "Verify the console acknowledges and reports a healthy status")
END_GROUP
