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

//
// The waveform generator, as the scope sees it -- read by the WfgIntoScope
// group (dev/suite/scripts/wfg_into_scope.cpp). Generator channel N on a BNC
// cable into scope channel N, 1:1, the generator told it is driving a high
// impedance so its amplitudes are what the scope's 1 MOhm input actually sees.
//
// Every nominal here is a setting the script sends, not a placeholder: the
// generator is the reference now, where the comp output was a guess. What the
// epsilons are sized to is the *scope*, which is the weaker of the two -- a
// 33522B's amplitude is good to about 1%, the DSOX1202G's vertical to about 3%
// of full scale -- so the amplitude windows are roughly 5% and the timing
// windows, where both instruments are crystal-referenced, a fraction of that.
//
// Where a nominal is derived rather than sent, the derivation is in the
// description: a sine's DC RMS, the 10-90% part of a ramp, the doubling an
// open circuit gives a source that was told to expect fifty ohms.
//
CRITERIA( DEV_Wfg_1, "The waveform generator, measured by the scope on the other end of a BNC cable")
    CRIT( DEV_Wfg_Vpp2V,        EQ( 2.0_V).epsilon( 0.10_V),        "A 2 Vpp setting reads 2 Vpp, within 5%")
    CRIT( DEV_Wfg_Vpp1V,        EQ( 1.0_V).epsilon( 0.08_V),        "A 1 Vpp setting reads 1 Vpp, within 8%")
    CRIT( DEV_Wfg_Zero,         EQ( 0.0_V).epsilon( 0.05_V),        "A waveform with no offset averages to zero")
    CRIT( DEV_Wfg_SineRms,      EQ( 0.707_V).epsilon( 0.035_V),     "A 2 Vpp sine is 0.707 V RMS -- 1 V peak over root two")
    CRIT( DEV_Wfg_Freq100Hz,    EQ( 100.0_Hz).epsilon( 0.5_Hz),     "100 Hz within 0.5%")
    CRIT( DEV_Wfg_Freq1kHz,     EQ( 1000.0_Hz).epsilon( 5.0_Hz),    "1 kHz within 0.5%")
    CRIT( DEV_Wfg_Freq5kHz,     EQ( 5.0_kHz).epsilon( 25.0_Hz),     "5 kHz within 0.5%")
    CRIT( DEV_Wfg_Freq10kHz,    EQ( 10.0_kHz).epsilon( 50.0_Hz),    "10 kHz within 0.5%")
    CRIT( DEV_Wfg_Freq1MHz,     EQ( 1.0_MHz).epsilon( 5.0_kHz),     "1 MHz within 0.5%")
    CRIT( DEV_Wfg_OffsetMax,    EQ( 3.0_V).epsilon( 0.15_V),        "4 Vpp on a +1 V offset peaks at +3 V")
    CRIT( DEV_Wfg_OffsetMin,    EQ( -1.0_V).epsilon( 0.15_V),       "4 Vpp on a +1 V offset troughs at -1 V")
    CRIT( DEV_Wfg_OffsetMean,   EQ( 1.0_V).epsilon( 0.08_V),        "4 Vpp on a +1 V offset averages +1 V")
    CRIT( DEV_Wfg_Top1V,        EQ( 1.0_V).epsilon( 0.06_V),        "A 2 Vpp square's top is +1 V")
    CRIT( DEV_Wfg_BaseMinus1V,  EQ( -1.0_V).epsilon( 0.06_V),       "A 2 Vpp square's base is -1 V")
    CRIT( DEV_Wfg_Width250us,   EQ( 250.0_us).epsilon( 5.0_us),     "25% of a 1 kHz period is 250 us")
    CRIT( DEV_Wfg_Width750us,   EQ( 750.0_us).epsilon( 5.0_us),     "75% of a 1 kHz period is 750 us")
    CRIT( DEV_Wfg_Width100us,   EQ( 100.0_us).epsilon( 3.0_us),     "A 10% duty pulse at 1 kHz is 100 us wide")
    CRIT( DEV_Wfg_RampEdge,     EQ( 0.8_ms).epsilon( 0.05_ms),      "A 1 kHz rising sawtooth spends 0.8 ms from 10% to 90%")
    CRIT( DEV_Wfg_TriangleEdge, EQ( 0.4_ms).epsilon( 0.02_ms),      "A 1 kHz triangle spends 0.4 ms from 10% to 90% each way")
    CRIT( DEV_Wfg_FastEdge,     LT( 20.0_us),                       "The sawtooth's reset is an edge, not a ramp")
    CRIT( DEV_Wfg_Dc1V5,        EQ( 1.5_V).epsilon( 0.06_V),        "A +1.5 V DC level reads +1.5 V")
    CRIT( DEV_Wfg_DcMinus2V,    EQ( -2.0_V).epsilon( 0.08_V),       "A -2 V DC level reads -2 V")
    CRIT( DEV_Wfg_NoiseRms,     IN( 0.08_V, 0.30_V),                "1 Vpp of Gaussian noise is about a sixth of that RMS")
    CRIT( DEV_Wfg_OpenCircuit,  EQ( 2.0_V).epsilon( 0.10_V),        "1 Vpp told to expect 50 Ohm doubles into the scope's open circuit")
    CRIT( DEV_Wfg_Off,          LT( 0.10_V),                        "A removed output is gone -- nothing but the scope's own noise")
END_CRITERIA

//
// The switch unit, with nothing wired to it -- read by the SwitchUnit group
// (dev/suite/scripts/swu_self_check.cpp). Every row is a yes-or-no the
// mainframe answers about itself, so every row is EQ( true): the value is in
// the description, which says what the yes means.
//
// The two module rows name what this desk has -- four 34932A matrices (with
// the 34932T terminal block, which SYST:CTYP? does not report: it names the
// module, and the T is the block on its front) and a 34941A RF multiplexer --
// and are the rows to change when the rack does.
//
CRITERIA( DEV_Swu_1, "The switch unit's relays and inventory, with nothing wired to it")
    CRIT( DEV_Swu_Module34932A, EQ( true), "Slots 1-4 each hold a 34932A dual 4 x 16 matrix")
    CRIT( DEV_Swu_Module34941A, EQ( true), "Slot 5 holds a 34941A quad 1 x 4 RF multiplexer")
    CRIT( DEV_Swu_Closed,       EQ( true), "A relay told to close reports closed")
    CRIT( DEV_Swu_Open,         EQ( true), "A relay told to open, or never closed, reports open")
    CRIT( DEV_Swu_Refused,      EQ( true), "A channel the module does not have is refused, not ignored")
    CRIT( DEV_Swu_RfSelects,    EQ( true), "Closing an RF channel opens the one its bank had closed")
    CRIT( DEV_Swu_RfNoOpen,     EQ( true), "An RF channel cannot be opened, only switched away from")
    CRIT( DEV_Swu_CycleCounted, EQ( true), "Closing and opening a relay moves its life count by one or two")
END_CRITERIA
