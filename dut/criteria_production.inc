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
//
// What the 5V output rail is allowed to do when the primary AC input is lost
// and the backup supplies take over (see suite/scripts/ac_dropout_script.cpp).
//
// Three criteria rather than one, because three separate things have to be
// true and they fail for different reasons with different next steps:
//
//   - the rail was where it should be before the test disturbed anything. A
//     dip measured from the wrong baseline is not a measurement of the dip.
//   - the scope actually captured the dropout. Without this, the depth below
//     is whatever was left in the acquisition buffer -- a number, very likely
//     in tolerance, and meaningless.
//   - the dip itself.
//
// The middle one is a criterion rather than a bare assertion in the script for
// the reason every criterion here exists: a check written into a script is
// untraceable to the specification it came from, and invisible in the report
// as anything other than prose.
//
CRITERIA( FS_Transient_1, "Output rail behaviour on loss of the primary AC input")
    CRIT( FS_Transient_5V0_Baseline, EQ( 5.0_V).epsilon( 0.05_V), "5Vdc rail must be at nominal before the input is dropped")
    CRIT( FS_Transient_Captured,     EQ( true),                   "The dropout must actually be captured by the scope")
    CRIT( FS_Transient_5V0_Dip,      LE( 200_mV),                 "5Vdc rail must not dip more than 200mV below its baseline")
END_CRITERIA

CRITERIA( FS_Console_1, "Debug console dialogue over RS232")
    CRIT( FS_Console_Ack,    EQ( bytes( "ACK")),        "Console must acknowledge the status command")
    CRIT( FS_Console_Ready,  BIT_SET<3>(),              "Status bit 3 (READY) must be set once the rails are up")
    CRIT( FS_Console_Fault,  BIT_CLEAR<7>(),            "Status bit 7 (FAULT) must be clear")
END_CRITERIA
