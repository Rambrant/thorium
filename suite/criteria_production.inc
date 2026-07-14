// ReSharper disable once CppMissingIncludeGuard

//
// Production variant -- matches the tolerances shipped in
// fuse_register_script.cpp today. No scaffolding here on purpose: see
// scripts/include/scripts/fuse_register_criteria.hpp for that.
//
CRITERIA( FS_Fuse_6, "Check of Fuses @ Register CB30")
    CRIT( FS_Fuse_01, MASK( 0x0Fu, 0x05u), "Low nibble of fuse register must be 0x5")
    CRIT( FS_Fuse_02, EQ( 0xF5u), "Fuse register must read exactly 0xF5")
END_CRITERIA

CRITERIA( FS_Supply_1, "Supply rail voltage checks")
    CRIT( FS_Supply_5V0, EQ( 5.0_V).epsilon( 0.05_V), "5Vdc port within +/-50mV")
    CRIT( FS_Supply_3V3, EQ( 3.3_V).epsilon( 0.05_V), "3.3Vdc port within +/-50mV")
END_CRITERIA
