// ReSharper disable once CppMissingIncludeGuard

//
// FS_Fuse_6 is unchanged from production: a fuse register readback is a
// digital exact-bits check, with no natural thermal tolerance to widen.
// Group/id are still stated explicitly (so a typo'd id still fails to
// compile), but the value and its description are borrowed from the master
// table's matching criterion -- production, for this deployment -- via
// CRIT_FROM_MASTER, so the actual numbers live in exactly one place.
//
CRITERIA( FS_Fuse_6, "Check of Fuses @ Register CB30")
    CRIT_FROM_MASTER( FS_Fuse_6, FS_Fuse_01)
    CRIT_FROM_MASTER( FS_Fuse_6, FS_Fuse_02)
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
// The transient group, at temperature. The baseline widens with the rail
// tolerance it mirrors (a rail that is allowed to sit at 5.10 V must be
// allowed to *start* there), and the permitted dip widens too: a supply's
// hold-up capacitors have less margin hot, and derating the transient the DUT
// is allowed to show is the whole reason a stress table exists.
//
// FS_Transient_Captured does not widen and cannot: either the scope captured
// the dropout or it did not, and there is no temperature at which half a
// capture becomes acceptable. Borrowed from production so the fact lives in
// one place -- the same treatment FS_Fuse_6 and FS_Console_1 get above.
//
// PLACEHOLDER -- widened by the same ratio as FS_Supply_1 above rather than
// read off a specification. Replace with real thermal derating numbers.
//
CRITERIA( FS_Transient_1, "Output rail behaviour on loss of the primary AC input (stress, PLACEHOLDER)")
    CRIT( FS_Transient_5V0_Baseline, EQ( 5.0_V).epsilon( 0.10_V), "5Vdc rail must be at nominal before the input is dropped (stress, PLACEHOLDER)")
    CRIT_FROM_MASTER( FS_Transient_1, FS_Transient_Captured)
    CRIT( FS_Transient_5V0_Dip,      LE( 350_mV),                 "5Vdc rail must not dip more than 350mV below its baseline (stress, PLACEHOLDER)")
END_CRITERIA

//
// FS_Console_1 is unchanged from production, for the same reason FS_Fuse_6 is:
// an acknowledgement is a byte sequence and a status flag is a bit, and neither
// has a thermal or an ageing tolerance to widen. A console that answers
// something other than ACK at temperature has not drifted -- it has failed.
//
CRITERIA( FS_Console_1, "Debug console dialogue over RS232")
    CRIT_FROM_MASTER( FS_Console_1, FS_Console_Ack)
    CRIT_FROM_MASTER( FS_Console_1, FS_Console_Ready)
    CRIT_FROM_MASTER( FS_Console_1, FS_Console_Fault)
END_CRITERIA

//
// The bulk capacitance group, at temperature -- and the one criterion in this
// table whose widening is a datasheet curve rather than a placeholder.
//
// An aluminium electrolytic's capacitance falls with temperature, and falls
// hard: the electrolyte's conductivity drops, and a part that measures 1000 uF
// at 20 degC can be down by a third at -40 degC and is a little *above* nominal
// at the top of its range. So the floor moves a long way and the ceiling barely
// moves, which is why this is the one row here that widens asymmetrically.
//
// FS_BulkCap_Rail_Dead does not widen and cannot: a node is discharged or it is
// not, and there is no temperature at which a charged one becomes measurable.
// Borrowed from production so the fact lives in one place -- the same treatment
// FS_Transient_Captured gets above.
//
// PLACEHOLDER -- the -33%/+5% band below is the shape of the curve, taken from
// what an aluminium electrolytic does rather than from Device X's own part.
// Replace with the capacitor's own temperature characteristic.
//
CRITERIA( FS_BulkCap_1, "DUT input bulk capacitance on the 24Vdc battery rail (stress, PLACEHOLDER)")
    CRIT_FROM_MASTER( FS_BulkCap_1, FS_BulkCap_Rail_Dead)
    CRIT( FS_BulkCap_Battery,   IN( 670_uF, 1260_uF),    "24Vdc input bulk capacitance, 1000uF -33%/+26% over the chamber range (stress, PLACEHOLDER)")
END_CRITERIA
