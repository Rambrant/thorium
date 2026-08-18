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

//
// The DUT's debug console (see dut::Console and hal::Racal1260). Two different
// kinds of check, deliberately kept apart rather than folded into one
// "the console works" criterion:
//
//   - that the device acknowledged the command at all, which is a payload
//     compared byte for byte
//   - what the status register it returned actually said, which is a bit
//     pattern
//
// A single criterion covering both would fail identically for a DUT that never
// answered and one that answered with a fault flag set, and those are different
// findings with different next steps.
//
CRITERIA( FS_Console_1, "Debug console dialogue over RS232")
    CRIT( FS_Console_Ack,    EQ( bytes( "ACK")),        "Console must acknowledge the status command")
    CRIT( FS_Console_Ready,  BIT_SET<3>(),              "Status bit 3 (READY) must be set once the rails are up")
    CRIT( FS_Console_Fault,  BIT_CLEAR<7>(),            "Status bit 7 (FAULT) must be clear")
END_CRITERIA
