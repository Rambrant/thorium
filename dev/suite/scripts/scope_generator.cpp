#include "../prelude.hpp"

//
// The DSOX1202G's built-in waveform generator, Wfg2 -- the second face of box
// Scope1 -- measured by the scope it is built into: the ScopeGenerator group of
// dev/suite/test_catalog.inc.
//
// Same arrangement as WfgIntoScope (wfg_into_scope.cpp) and the same reasoning:
// a generator cannot say what it produces, only what it was told, so every
// script is a pair -- Wfg2 told to make something, Osc1 asked what arrived --
// and what passes is the two agreeing through a real cable. One cable, once:
//
//     scope rear/front "Gen Out" BNC  -->  BNC  -->  scope CH1
//
// The generator and the scope are one instrument and one session, which is the
// point of the group: both faces on one box, working together.
//
// Every waveform is told .into( Termination::HighImpedance) -- the scope's input
// is 1 MOhm, and the generator quotes amplitude against the load it is told to
// expect (see hal::keysight_dsox1202g::Termination). The criteria are
// DEV_Wfg_1's, the WfgIntoScope group's: what a generator told 2 Vpp must show
// does not depend on which generator it was.
//
// The generator reaches 5 Vpp into an open circuit and 20 MHz, against the
// 33522B's 20 Vpp and 30 MHz, so every setting here is chosen inside the
// smaller one: 2 Vpp at most, nothing above 1 kHz. A detached run never reaches
// the driver's range check (see WGEN::checkAgainstModel), so a unit test cannot
// catch a script that asks for too much; the first attached run would.
//
// Every script Removes what it Applied, and safing turns the output off after
// the run regardless.
//

namespace osc = hal::keysight_dsox1202g;

using core::quantities::Time;
using core::quantities::Voltage;

namespace
{
    constexpr auto kOpen = osc::Termination::HighImpedance;

    //
    // Scope channel 1 set to watch the generator: 1:1, DC coupled, Auto sweep
    // so a DC level or noise still draws.
    //
    auto watch( const Voltage perDiv, const Voltage centre, const Time timePerDiv) -> void
    {
        Setup( Osc1.channel<1>()
                   .coupling( osc::Coupling::Dc)
                   .probeAttenuation( 1.0)
                   .voltsPerDivision( perDiv)
                   .verticalOffset( centre)
                   .bandwidth( osc::Bandwidth::Full)
                   .display( osc::ChannelDisplay::On));

        Setup( Osc1.timebase()
                   .timePerDivision( timePerDiv)
                   .reference( osc::TimebaseReference::Center));

        Setup( Osc1.trigger()
                   .edgeSource<1>()
                   .slope( osc::TriggerSlope::Rising)
                   .level( 0_V)
                   .sweep( osc::TriggerSweep::Auto)
                   .coupling( osc::TriggerCoupling::Dc));

        Setup( Osc1.acquisition().type( osc::AcquisitionType::Normal));
    }
} // namespace

//
// A 1 kHz, 2 Vpp sine: level, frequency, centre and RMS -- the four things a
// sine is.
//
auto wgenSine() -> void
{
    Apply( Wfg2.sine().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));
    watch( 500_mV, 0_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V,    Measure( Osc1.channel<1>().vpp()));
    Verify( DEV_Wfg_1::DEV_Wfg_Freq1kHz, Measure( Osc1.channel<1>().frequency()));
    Verify( DEV_Wfg_1::DEV_Wfg_Zero,     Measure( Osc1.channel<1>().vaverage()));
    Verify( DEV_Wfg_1::DEV_Wfg_SineRms,  Measure( Osc1.channel<1>().vrms()));

    Remove( Wfg2.sine());
}

//
// A 25% square, read by its levels and both widths -- the one duty-cycle
// command this instrument has, and 25 is inside its 20-80% window.
//
auto wgenSquareDutyCycle() -> void
{
    Apply( Wfg2.square().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).dutyCycle( 25.0).into( kOpen));
    watch( 500_mV, 0_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_Top1V,       Measure( Osc1.channel<1>().vtop()));
    Verify( DEV_Wfg_1::DEV_Wfg_BaseMinus1V, Measure( Osc1.channel<1>().vbase()));
    Verify( DEV_Wfg_1::DEV_Wfg_Width250us,  Measure( Osc1.channel<1>().positiveWidth()));
    Verify( DEV_Wfg_1::DEV_Wfg_Width750us,  Measure( Osc1.channel<1>().negativeWidth()));

    Remove( Wfg2.square());
}

//
// A 100% ramp is a rising sawtooth: 0.8 ms from 10% to 90% on the way up, and a
// reset that is an edge on the way down.
//
auto wgenRampSymmetry() -> void
{
    Apply( Wfg2.ramp().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).symmetry( 100.0).into( kOpen));
    watch( 500_mV, 0_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_RampEdge, Measure( Osc1.channel<1>().riseTime()));
    Verify( DEV_Wfg_1::DEV_Wfg_FastEdge, Measure( Osc1.channel<1>().fallTime()));

    Remove( Wfg2.ramp());
}

//
// A pulse is set by its width, where the 33522B's is a duty cycle -- so this is
// the setting that reaches :WGEN:FUNCtion:PULSe:WIDTh, read as the high time.
//
auto wgenPulseWidth() -> void
{
    Apply( Wfg2.pulse().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).width( 100_us).into( kOpen));
    watch( 500_mV, 0_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_Width100us, Measure( Osc1.channel<1>().positiveWidth()));

    Remove( Wfg2.pulse());
}

//
// DC, which is the offset with nothing else: a level each side of zero.
//
auto wgenDcLevels() -> void
{
    watch( 1_V, 0_V, 200_us);

    Apply( Wfg2.dc().offset( 1.5_V).into( kOpen));
    Verify( DEV_Wfg_1::DEV_Wfg_Dc1V5, Measure( Osc1.channel<1>().vaverage()));

    Apply( Wfg2.dc().offset( -2_V).into( kOpen));
    Verify( DEV_Wfg_1::DEV_Wfg_DcMinus2V, Measure( Osc1.channel<1>().vaverage()));

    Remove( Wfg2.dc());
}

//
// The termination, checked by getting it deliberately "wrong": told 50 Ohm, the
// generator drives twice the programmed voltage into the scope's open circuit.
// The pair is what shows :WGEN:OUTPut:LOAD reached the instrument.
//
auto wgenTermination() -> void
{
    watch( 500_mV, 0_V, 200_us);

    Apply( Wfg2.sine().frequency( 1_kHz).amplitude( 1_V).offset( 0_V).into( osc::Termination::Ohms50));
    Verify( DEV_Wfg_1::DEV_Wfg_OpenCircuit, Measure( Osc1.channel<1>().vpp()));

    Apply( Wfg2.sine().frequency( 1_kHz).amplitude( 1_V).offset( 0_V).into( kOpen));
    Verify( DEV_Wfg_1::DEV_Wfg_Vpp1V, Measure( Osc1.channel<1>().vpp()));

    Remove( Wfg2.sine());
}

//
// Remove has to turn the output off: the reading after it is the scope's own
// noise floor. The check safing depends on.
//
auto wgenOutputOff() -> void
{
    Apply( Wfg2.sine().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));
    watch( 500_mV, 0_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V, Measure( Osc1.channel<1>().vpp()));

    Remove( Wfg2.sine());

    Verify( DEV_Wfg_1::DEV_Wfg_Off, Measure( Osc1.channel<1>().vpp()));
}
