//
// FS_Fuse_6 is unchanged from production: a fuse register readback is a
// digital exact-bits check, with no natural thermal tolerance to widen.
// Group/id are still stated explicitly (so a typo'd id still fails to
// compile), but the value and its description are borrowed from
// production's matching criterion via CRIT_FROM_PRODUCTION, so the actual
// numbers live in exactly one place.
//
GROUP( FS_Fuse_6, "Check of Fuses @ Register CB30")
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_01)
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_02)
END_GROUP

//
// PLACEHOLDER -- stress-chamber tolerances. Epsilon widened as a
// placeholder only; replace with real thermal derating numbers from the
// equipment spec before this is used for anything.
//
GROUP( FS_Supply_1, "Supply rail voltage checks (stress, PLACEHOLDER)")
    CRIT( FS_Supply_5V0, core::EQ( 5.0).epsilon( 0.10), "5Vdc port within +/-100mV (stress, PLACEHOLDER)")
    CRIT( FS_Supply_3V3, core::EQ( 3.3).epsilon( 0.10), "3.3Vdc port within +/-100mV (stress, PLACEHOLDER)")
END_GROUP
