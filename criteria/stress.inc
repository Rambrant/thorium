//
// Stress variant
//
GROUP( FS_Fuse_6, "Check of Fuses @ Register CB30 (stress)")
    CRIT( FS_Fuse_01, core::MASK( 0x0Fu, 0x05u), "Low nibble of fuse register must be 0x5")
    CRIT( FS_Fuse_02, core::EQ( 0xF5u), "Fuse register must read exactly 0xF5")
END_GROUP

GROUP( FS_Supply_1, "Supply rail voltage checks (stress)")
    CRIT( FS_Supply_5V0, core::EQ( 5.0).epsilon( 0.10), "5Vdc port within +/-100mV (cold, PLACEHOLDER)")
    CRIT( FS_Supply_3V3, core::EQ( 3.3).epsilon( 0.10), "3.3Vdc port within +/-100mV (cold, PLACEHOLDER)")
END_GROUP
