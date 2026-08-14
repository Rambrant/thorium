#pragma once

//
// No SETUP: nothing here powers the rig up yet -- the scripts below measure
// rails they assume are already live. TEARDOWN alone is deliberate rather than
// half-finished; powering down is worth sequencing whether or not this suite
// was the thing that powered up (see suite/scripts/rig_power_off.cpp), and an
// absent SETUP needs no placeholder.
//
TEARDOWN( rigPowerOff)

GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
    TEST( FuseRegister, fuseRegisterScript, "Verify fuse register and output voltage")
    TEST( SupplyRail,   supplyRailScript,   "Verify supply rail voltages via matrix")
END_GROUP
