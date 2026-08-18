// ReSharper disable once CppMissingIncludeGuard

//
// FS_Fuse_6 is unchanged from production: a fuse register readback is a
// digital exact-bits check, with no natural thermal tolerance to widen.
// Group/id are still stated explicitly (so a typo'd id still fails to
// compile), but the value and its description are borrowed from
// production's matching criterion via CRIT_FROM_PRODUCTION, so the actual
// numbers live in exactly one place.
//
CRITERIA( FS_Fuse_6, "Check of Fuses @ Register CB30")
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_01)
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_02)
END_CRITERIA

//
// PLACEHOLDER -- stress-chamber tolerances. Epsilon widened as a
// placeholder only; replace with real thermal derating numbers from the
// equipment spec before this is used for anything.
//
CRITERIA( FS_Supply_1, "Supply rail voltage checks (stress, PLACEHOLDER)")
    CRIT( FS_Supply_5V0, EQ( 5.0_V).epsilon( 0.10_V), "5Vdc port within +/-100mV (stress, PLACEHOLDER)")
    CRIT( FS_Supply_3V3, EQ( 3.3_V).epsilon( 0.10_V), "3.3Vdc port within +/-100mV (stress, PLACEHOLDER)")
END_CRITERIA

//
// FS_Console_1 is unchanged from production, for the same reason FS_Fuse_6 is:
// an acknowledgement is a byte sequence and a status flag is a bit, and neither
// has a thermal or an ageing tolerance to widen. A console that answers
// something other than ACK at temperature has not drifted -- it has failed.
//
CRITERIA( FS_Console_1, "Debug console dialogue over RS232")
    CRIT_FROM_PRODUCTION( FS_Console_1, FS_Console_Ack)
    CRIT_FROM_PRODUCTION( FS_Console_1, FS_Console_Ready)
    CRIT_FROM_PRODUCTION( FS_Console_1, FS_Console_Fault)
END_CRITERIA
