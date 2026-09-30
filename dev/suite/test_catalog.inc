#pragma once

//
// The dev suite's catalog -- three groups: the meter's sanity check, each of
// its functions, and the supply's outputs. Read exactly as
// suite/test_catalog.inc is; see suite/README.md for GROUP/TEST and the
// RUN_SETUP/RUN_TEARDOWN and per-group SETUP/TEARDOWN hooks.
//
// No RUN_SETUP or RUN_TEARDOWN, and that is the difference worth noticing. The
// bench suite brackets every run with rigPowerOn/rigPowerOff, because a DUT has
// to be brought up before anything can be measured and must not be left
// energised. This bench sources nothing: there is no rail to raise, so a hook
// pair here would be two scripts with empty bodies. Safing still happens on the
// way out -- that is hal::safeRig() in the runner, not a catalog hook.
//
GROUP( BenchSanity, "Checks that the bench meter itself is behaving")
    TEST( DmmSelfCheck, dmmSelfCheck, "The meter reads a known reference within tolerance")
END_GROUP

//
// One test per function, each needing its own reference across the meter's
// terminals -- so this group is run a test at a time, not as a whole: connect
// the reference, tick the one test. See dev/suite/scripts/dmm_functions.cpp,
// and dev/dut/criteria_production.inc for what each reference is.
//
GROUP( DmmFunctions, "Each of the meter's functions against its own reference -- run one at a time")
    TEST( DmmDcVoltage,          dmmDcVoltage,          "DC volts, autoranged and on a fixed range, off the 5 V cell")
    TEST( DmmResolution,         dmmResolution,         "DC volts at slow, medium and fast resolution, off the 5 V cell")
    TEST( DmmAcVoltage,          dmmAcVoltage,          "AC volts off a 1 V rms, 1 kHz sine")
    TEST( DmmDcCurrent,          dmmDcCurrent,          "DC current off a 100 mA reference")
    TEST( DmmAcCurrent,          dmmAcCurrent,          "AC current off a 100 mA rms reference")
    TEST( DmmResistance,         dmmResistance,         "2-wire ohms off the 1 kOhm resistor")
    TEST( DmmFourWireResistance, dmmFourWireResistance, "4-wire ohms off the same resistor, sense leads connected")
    TEST( DmmFrequency,          dmmFrequency,          "The frequency counter off a 1 kHz signal")
    TEST( DmmCapacitance,        dmmCapacitance,        "Capacitance off the 470 uF reference capacitor")
END_GROUP

//
// The supply with its terminals open, so unlike DmmFunctions this group runs
// whole: nothing has to be connected for any of it. Each output is set twice,
// read back, and removed; safing turns everything off afterwards regardless.
//
GROUP( SupplyOutputs, "Each output of the bench supply, set and read back with nothing connected")
    TEST( PsuOutput1, psuOutput1Check, "Output 1 (6 V / 5 A) follows two setpoints, draws nothing, and turns off")
    TEST( PsuOutput2, psuOutput2Check, "Output 2 (30 V / 1 A) follows two setpoints, draws nothing, and turns off")
    TEST( PsuOutput3, psuOutput3Check, "Output 3 (30 V / 1 A) follows two setpoints, draws nothing, and turns off")
END_GROUP

//
// The scope against its own probe-compensation output, both probes on the comp
// terminal. Runs whole: nothing is reconnected between tests. See
// dev/suite/scripts/scope_probe_comp.cpp.
//
GROUP( ScopeProbeComp, "The scope against its own 1 kHz probe-compensation square, both probes on the comp terminal")
    TEST( ScopeAmplitudeCh1,         scopeAmplitudeCh1,         "Channel 1: top, base, amplitude, Vpp, max, min, average and RMS")
    TEST( ScopeAmplitudeCh2,         scopeAmplitudeCh2,         "Channel 2: top, base, amplitude, Vpp, max, min, average and RMS")
    TEST( ScopeTimingCh1,            scopeTimingCh1,            "Channel 1: frequency, period, both widths, rise and fall time")
    TEST( ScopeTimingCh2,            scopeTimingCh2,            "Channel 2: frequency, period, both widths, rise and fall time")
    TEST( ScopeCapture,              scopeCapture,              "A single-shot capture, then both channels' traces fetched and checked")
    TEST( ScopeAcquisitionTypes,     scopeAcquisitionTypes,     "Normal, high-resolution, peak-detect and averaged acquisitions all read the square")
    TEST( ScopeCouplingAndBandwidth, scopeCouplingAndBandwidth, "AC coupling centres the square on zero; the bandwidth limit leaves it alone")
END_GROUP

//
// The waveform generator, measured by the scope: generator CH1 to scope CH1 and
// CH2 to CH2 on BNC cables, 1:1. Runs whole -- nothing is reconnected, and each
// test turns off what it turned on. See dev/suite/scripts/wfg_into_scope.cpp.
//
GROUP( WfgIntoScope, "The waveform generator, each setting checked by the scope on the other end of the cable")
    TEST( WfgSineCh1,             wfgSineCh1,             "Channel 1: a 1 kHz, 2 Vpp sine's level, frequency, centre and RMS")
    TEST( WfgSineCh2,             wfgSineCh2,             "Channel 2: a 1 kHz, 2 Vpp sine's level, frequency, centre and RMS")
    TEST( WfgFrequencySteps,      wfgFrequencySteps,      "A sine at 100 Hz, 1 kHz, 10 kHz and 1 MHz")
    TEST( WfgAmplitudeAndOffset,  wfgAmplitudeAndOffset,  "4 Vpp on a +1 V offset peaks at +3 V and troughs at -1 V")
    TEST( WfgSquareDutyCycle,     wfgSquareDutyCycle,     "A 25% square: top, base and both widths")
    TEST( WfgRampAndTriangle,     wfgRampAndTriangle,     "A rising sawtooth and a triangle, by their edge times")
    TEST( WfgPulse,               wfgPulse,               "A 10% pulse is 100 us wide at 1 kHz")
    TEST( WfgDcLevels,            wfgDcLevels,            "DC at +1.5 V and -2 V")
    TEST( WfgNoise,               wfgNoise,               "1 Vpp of noise: an RMS in band and a mean of zero")
    TEST( WfgTermination,         wfgTermination,         "Told 50 Ohm it doubles into the scope; told high-Z it does not")
    TEST( WfgOutputOff,           wfgOutputOff,           "Remove turns an output off -- the scope sees only its own noise")
    TEST( WfgChannelsIndependent, wfgChannelsIndependent, "A 1 kHz sine on channel 1 and a 5 kHz square on channel 2 at once")
END_GROUP

//
// The 34980A switch unit with nothing wired to it -- every check is a question
// the mainframe answers about itself. Runs whole. See
// dev/suite/scripts/swu_self_check.cpp.
//
GROUP( SwitchUnit, "The 34980A switch unit, tested in isolation: inventory, relays, refusal and relay life")
    TEST( SwuInventory,              swuInventory,              "Slots 1-4 hold 34921As, slot 5 is fitted; every slot's module noted")
    TEST( SwuRelaysSlot1,            swuRelaysSlot1,            "Slot 1: close, open, a list across both banks, exclusive, bus relays, open all")
    TEST( SwuRelaysSlot2,            swuRelaysSlot2,            "Slot 2: close, open, a list across both banks, exclusive, bus relays, open all")
    TEST( SwuRelaysSlot3,            swuRelaysSlot3,            "Slot 3: close, open, a list across both banks, exclusive, bus relays, open all")
    TEST( SwuRelaysSlot4,            swuRelaysSlot4,            "Slot 4: close, open, a list across both banks, exclusive, bus relays, open all")
    TEST( SwuRefusesAMissingChannel, swuRefusesAMissingChannel, "A channel a 34921A does not have is refused by the mainframe")
    TEST( SwuRelayCycles,            swuRelayCycles,            "A close and an open move the relay's life count")
END_GROUP
