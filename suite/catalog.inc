GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
    TEST( FuseRegister, fuseRegisterScript, "Verify fuse register and output voltage")
    TEST( SupplyRail,   supplyRailScript,   "Verify supply rail voltages via matrix")
END_GROUP
