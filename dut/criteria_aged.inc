// ReSharper disable once CppMissingIncludeGuard

//
// FS_Fuse_6 is unchanged from production: a fuse register readback has no
// natural tolerance to loosen for aged equipment either -- the fuse is
// blown or it isn't. Group/id are still stated explicitly (so a typo'd id
// still fails to compile), but the value and its description are borrowed
// from production's matching criterion via CRIT_FROM_PRODUCTION, so the
// actual numbers live in exactly one place.
//
CRITERIA( FS_Fuse_6, "Check of Fuses @ Register CB30")
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_01)
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_02)
END_CRITERIA

//
// PLACEHOLDER -- equipment that's been in service for years (looser
// tolerances to account for component drift/ageing). Epsilon widened as a
// placeholder only; replace with real numbers before this is used for
// anything.
//
CRITERIA( FS_Supply_1, "Supply rail voltage checks (aged equipment)")
    CRIT( FS_Supply_5V0, EQ( 5.0_V).epsilon( 0.15_V), "5Vdc port within +/-150mV (aged, PLACEHOLDER)")
    CRIT( FS_Supply_3V3, EQ( Voltage{3.3}).epsilon( 0.15_V), "3.3Vdc port within +/-150mV (aged, PLACEHOLDER)")
END_CRITERIA

//
// The transient group, on equipment that has been in service for years. Same
// shape as the stress table's: the baseline tracks FS_Supply_1's own widened
// tolerance, the permitted dip widens with the hold-up capacitance an ageing
// supply has lost, and whether the capture happened at all is not a thing that
// ages.
//
// PLACEHOLDER -- widened by the same ratio as FS_Supply_1 above rather than
// read off a specification. Replace with real numbers.
//
CRITERIA( FS_Transient_1, "Output rail behaviour on loss of the primary AC input (aged equipment)")
    CRIT( FS_Transient_5V0_Baseline, EQ( 5.0_V).epsilon( 0.15_V), "5Vdc rail must be at nominal before the input is dropped (aged, PLACEHOLDER)")
    CRIT_FROM_PRODUCTION( FS_Transient_1, FS_Transient_Captured)
    CRIT( FS_Transient_5V0_Dip,      LE( 500_mV),                 "5Vdc rail must not dip more than 500mV below its baseline (aged, PLACEHOLDER)")
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
