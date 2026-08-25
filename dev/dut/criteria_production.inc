// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's criteria -- what a reading has to be for a dev script to
// pass. Read exactly as dut/criteria_production.inc is; see dut/README.md for
// the CRITERIA/CRIT mechanism and what a variant is.
//
// One variant, where the bench deployment has three. Production, stress-chamber
// and aged-equipment tolerances are statements about a DUT's hardware over its
// life, and there is no DUT here -- so a second table would be a copy of this
// one with nothing to say. THORIUM_KNOWN_CRITERIA_VARIANTS is set to just
// `production` for this deployment (see CMakePresets.json), which is what keeps
// CRIT_FROM_PRODUCTION and the variant compile checks coherent with one table.
//
// What these check is the *instrument*, not a device: that the meter answers,
// and that what it answers is the value a known input should produce. That is
// the whole content of a driver-development check -- a shorted input reads
// zero, a reference cell reads what the cell is -- and it is exactly the
// question a rig cannot answer, because on a rig every reading is confounded
// with a route.
//
CRITERIA( DEV_Dmm_1, "The bench meter, against inputs whose value is known")
    CRIT( DEV_Dmm_Zero,  EQ( 0.0_V).epsilon( 0.01_V),  "A shorted input must read zero within 10mV")
    CRIT( DEV_Dmm_Ref,   EQ( 5.0_V).epsilon( 0.05_V),  "The 5V reference cell must read nominal within 50mV")
END_CRITERIA
