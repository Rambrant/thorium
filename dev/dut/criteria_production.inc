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
// CRIT_FROM_MASTER and the variant compile checks coherent with one table.
//
// What these check is the *instrument*, not a device: that the meter answers,
// and that what it answers is the value a known input should produce. That is
// the whole content of a driver-development check -- a shorted input reads
// zero, a reference cell reads what the cell is -- and it is exactly the
// question a rig cannot answer, because on a rig every reading is confounded
// with a route.
//
//
// DEV_Dmm_Bulk's epsilon is 10% where the two voltage rows are well under 1%,
// and that is the capacitor's tolerance rather than the meter's. An aluminium
// electrolytic is sold at +/-20% and a decent one measures inside 10%; the
// EDU34450A's own capacitance accuracy is 1% of reading + 0.5% of range, an
// order of magnitude tighter. A criterion written to the instrument's
// specification here would fail on every part in the drawer and prove nothing
// about the driver, which is what this table is for.
//
CRITERIA( DEV_Dmm_1, "The bench meter, against inputs whose value is known")
    CRIT( DEV_Dmm_Zero,  EQ( 0.0_V).epsilon( 0.01_V),    "A shorted input must read zero within 10mV")
    CRIT( DEV_Dmm_Ref,   EQ( 5.0_V).epsilon( 0.05_V),    "The 5V reference cell must read nominal within 50mV")
    CRIT( DEV_Dmm_Bulk,  EQ( 470_uF).epsilon( 47_uF),    "The 470uF reference capacitor must read nominal within 10%")
END_CRITERIA

//
// The meter's other functions, one reference each -- read by the DmmFunctions
// group (dev/suite/scripts/dmm_functions.cpp), whose tests are run one at a
// time with that test's reference across the terminals. DC volts and
// capacitance are not repeated here: those tests read the same 5 V cell and
// 470 uF capacitor DmmSelfCheck does, and verify against DEV_Dmm_1's rows.
//
// TODO(desk): every nominal below is a placeholder for a reference this
// desk does not have yet -- a signal generator, a current source, a 1 kOhm
// precision resistor. Change a row to the part actually in the drawer, and
// size its epsilon to that part's tolerance rather than the meter's, for the
// reason DEV_Dmm_Bulk's comment above gives.
//
// The two resistance rows read the same resistor, and their epsilons differ by
// the one thing that separates the functions: a 2-wire reading carries the
// leads (the data sheet adds 0.2 Ohm without a NULL), a 4-wire one does not.
// A 4-wire reading that fails where the 2-wire one passes is a sense-lead
// problem, which is the question that test exists to answer.
//
CRITERIA( DEV_Dmm_Fn, "The meter's remaining functions, each against its own known input")
    CRIT( DEV_Dmm_AcRef,   EQ( 1.0_V).epsilon( 0.02_V),         "A 1 V rms, 1 kHz sine must read nominal within 20mV")
    CRIT( DEV_Dmm_DcIRef,  EQ( 100.0_mA).epsilon( 2.0_mA),      "A 100 mA DC reference current must read nominal within 2mA")
    CRIT( DEV_Dmm_AcIRef,  EQ( 100.0_mA).epsilon( 3.0_mA),      "A 100 mA rms, 1 kHz reference current must read nominal within 3mA")
    CRIT( DEV_Dmm_Res2W,   EQ( 1.0_kOhm).epsilon( 1.5_Ohm),     "The 1 kOhm 0.1% resistor must read nominal within 1.5 Ohm, leads included")
    CRIT( DEV_Dmm_Res4W,   EQ( 1.0_kOhm).epsilon( 1.2_Ohm),     "The 1 kOhm 0.1% resistor must read nominal within 1.2 Ohm, leads sensed out")
    CRIT( DEV_Dmm_Freq,    EQ( 1000.0_Hz).epsilon( 1.0_Hz),     "A 1 kHz signal must count 1 kHz within 1 Hz")
END_CRITERIA

//
// The bench supply, read back through its own interface with its terminals
// open -- read by the SupplyOutputs group (dev/suite/scripts/supply_outputs.cpp).
// Nothing is connected to it, so what these check is the supply alone: that an
// output goes where it is told, draws nothing into nothing, and is really off
// after a Remove.
//
// By setpoint rather than by output, because the tolerance is a property of
// the voltage asked for and not of which channel produced it -- an EDU36311A's
// programming and readback accuracies are a fraction of the value plus an
// offset, so 24 V gets a wider window than 1 V. Twice what the data sheet
// allows for setting plus reading back, so a healthy supply passes with room
// and a wrong range or a stuck DAC does not.
//
// DEV_Psu_Idle and DEV_Psu_Off are EQ( 0) with an epsilon rather than LT( ...),
// deliberately: an open output's readback sits a few counts either side of
// zero, and a small negative reading is as healthy as a small positive one.
//
CRITERIA( DEV_Psu_1, "The bench supply's outputs, set and read back with nothing connected")
    CRIT( DEV_Psu_1V,    EQ( 1.0_V).epsilon( 0.02_V),     "An output set to 1 V must read back within 20mV")
    CRIT( DEV_Psu_5V,    EQ( 5.0_V).epsilon( 0.03_V),     "An output set to 5 V must read back within 30mV")
    CRIT( DEV_Psu_24V,   EQ( 24.0_V).epsilon( 0.06_V),    "An output set to 24 V must read back within 60mV")
    CRIT( DEV_Psu_Idle,  EQ( 0.0_mA).epsilon( 5.0_mA),    "An open output must draw no current, within 5mA")
    CRIT( DEV_Psu_Off,   EQ( 0.0_V).epsilon( 0.05_V),     "A removed output must read back zero, within 50mV")
END_CRITERIA

//
// The scope, against its own front-panel probe-compensation output -- read by
// the ScopeProbeComp group (dev/suite/scripts/scope_probe_comp.cpp). A 1 kHz
// square wave from base to top, with both probes' tips on the comp terminal
// and their grounds on its ground lug.
//
// TODO(desk): the levels are placeholders. The comp output is meant for
// adjusting a probe's compensation, not for calibrating a scope, so its
// amplitude is loosely specified -- read it once off this scope's own front
// panel (Meas > Amplitude, Top, Base) and write those in, with the epsilons
// sized to how much it moves between runs rather than to the scope's own
// accuracy. What is *not* a placeholder is the shape of the table: a 50% duty
// square from 0 to Vtop has an average of Vtop/2 and a DC RMS of Vtop/sqrt(2),
// and the rows below are written in those terms so that changing Vtop means
// changing them together.
//
// Frequency, period and the two widths are tighter than the levels because
// they are: a comp output is a divided-down crystal, and its timing is the part
// of it worth trusting. The edge rows are an upper bound only -- a probe
// compensated well or badly changes a rise time by a lot, and "fast" is the
// claim a comp edge can support.
//
CRITERIA( DEV_Osc_1, "The scope, against its own 1 kHz probe-compensation square wave")
    CRIT( DEV_Osc_Vpp,        EQ( 2.5_V).epsilon( 0.3_V),      "Peak to peak, overshoot included")
    CRIT( DEV_Osc_Vamplitude, EQ( 2.5_V).epsilon( 0.2_V),      "Top minus base, the settled amplitude")
    CRIT( DEV_Osc_Vtop,       EQ( 2.5_V).epsilon( 0.2_V),      "The settled high level")
    CRIT( DEV_Osc_Vbase,      EQ( 0.0_V).epsilon( 0.1_V),      "The settled low level")
    CRIT( DEV_Osc_Vmax,       EQ( 2.5_V).epsilon( 0.4_V),      "The highest sample -- overshoot moves it, so wider than Vtop")
    CRIT( DEV_Osc_Vmin,       EQ( 0.0_V).epsilon( 0.3_V),      "The lowest sample -- undershoot moves it, so wider than Vbase")
    CRIT( DEV_Osc_Vaverage,   EQ( 1.25_V).epsilon( 0.15_V),    "Half of Vtop, for a 50% duty square")
    CRIT( DEV_Osc_Vrms,       EQ( 1.77_V).epsilon( 0.15_V),    "Vtop over root two, DC RMS of a 50% duty square")
    CRIT( DEV_Osc_AcAverage,  EQ( 0.0_V).epsilon( 0.1_V),      "AC coupled, the square sits around zero")
    CRIT( DEV_Osc_Frequency,  EQ( 1000.0_Hz).epsilon( 10.0_Hz), "The comp output runs at 1 kHz within 1%")
    CRIT( DEV_Osc_Period,     EQ( 1.0_ms).epsilon( 10.0_us),   "One period is 1 ms within 1%")
    CRIT( DEV_Osc_Width,      EQ( 0.5_ms).epsilon( 25.0_us),   "Each half of a 50% duty square, within 5%")
    CRIT( DEV_Osc_Edge,       LT( 5.0_us),                     "The comp edge is fast -- well under 5 us, 10% to 90%")
    CRIT( DEV_Osc_Captured,   EQ( true),                       "A single-shot capture completed")
    CRIT( DEV_Osc_HasSamples, EQ( true),                       "The fetched trace has samples in it")
END_CRITERIA
