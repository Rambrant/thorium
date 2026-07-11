//
// FS_Fuse_6 is unchanged from production: a fuse register readback has no
// natural tolerance to loosen for aged equipment either -- the fuse is
// blown or it isn't. Group/id are still stated explicitly (so a typo'd id
// still fails to compile), but the value and its description are borrowed
// from production's matching criterion via CRIT_FROM_PRODUCTION, so the
// actual numbers live in exactly one place.
//
GROUP( FS_Fuse_6, "Check of Fuses @ Register CB30")
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_01)
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_02)
END_GROUP

//
// PLACEHOLDER -- equipment that's been in service for years (looser
// tolerances to account for component drift/ageing). Epsilon widened as a
// placeholder only; replace with real numbers before this is used for
// anything.
//
GROUP( FS_Supply_1, "Supply rail voltage checks (aged equipment)")
    CRIT( FS_Supply_5V0, core::EQ( 5.0).epsilon( 0.15), "5Vdc port within +/-150mV (aged, PLACEHOLDER)")
    CRIT( FS_Supply_3V3, core::EQ( 3.3).epsilon( 0.15), "3.3Vdc port within +/-150mV (aged, PLACEHOLDER)")
END_GROUP
