#pragma once

//
// The pair that brackets every run: rigPowerOn brings this rig's sources up in
// order and checks each came up (a false here stops the run before the first
// test), rigPowerOff takes them back down in the inverse order. Read the two
// files as one sequence and its reverse -- see suite/scripts/rig_power_on.cpp.
//
SETUP( rigPowerOn)
TEARDOWN( rigPowerOff)

GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
    TEST( FuseRegister, fuseRegisterScript, "Verify fuse register and output voltage")
    TEST( SupplyRail,   supplyRailScript,   "Verify supply rail voltages via matrix")
END_GROUP

GROUP( Console, "Tests validating the DUT's RS232 debug console")
    TEST( StatusRegister, consoleScript, "Verify the console acknowledges and reports a healthy status")
END_GROUP
