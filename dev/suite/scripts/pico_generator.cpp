#include "../prelude.hpp"

//
// The PicoScope's built-in function generator, Wfg3 -- the second face of box
// Pico1 -- measured by the scope it is built into: the PicoGenerator group of
// dev/suite/test_catalog.inc.
//
// The same arrangement as ScopeGenerator (scope_generator.cpp), which is the
// DSOX1202G's generator checked by the DSOX1202G, and the same reasoning: a
// generator cannot say what it produces, only what it was told, so every script
// is a pair -- Wfg3 told to make something, Osc2 asked what arrived -- and what
// passes is the two agreeing through a real cable. One cable, once:
//
//     PicoScope "Gen" BNC  -->  BNC  -->  PicoScope channel A  (Osc2 channel 1)
//
// The generator and the scope are one instrument and one open unit, which is the
// point of the group: both faces on one box, working together. If the unit were
// opened twice -- once per face -- the SDK would refuse the second, so this
// group passing is also the check that the faces share it.
//
// What differs from the DSOX1202G's, because the instrument does: there is no
// termination to tell it (the Pico's output is a fixed 600 Ohm source, and its
// amplitude is quoted for the scope's 1 MOhm input, which is what this is), no
// duty cycle (a square is 50 %), no pulse, and two ramps in place of one with a
// symmetry. The generator reaches 4 Vpp but only +/-2 V in all, so the settings
// below keep the peak, offset included, inside that: the driver refuses one that
// would not (the SDK would clip it and say nothing), but a detached run never
// reaches that check, so a unit test cannot catch a script that asks for too
// much. The first attached run would.
//
// The criteria are DEV_Wfg_1's, the WfgIntoScope group's: what a generator told
// 2 Vpp must show does not depend on which generator it was.
//
// Every script Removes what it Applied, and safing silences the output after the
// run regardless.
//

namespace pico = hal::pico_5244d;

using core::quantities::Frequency;
using core::quantities::Time;
using core::quantities::Voltage;

namespace
{
    //
    // Scope channel 1 set to watch the generator: 1:1, DC coupled, 8-bit, Auto
    // sweep so a DC level or noise still draws. range is the largest voltage
    // the signal reaches, centre the level at the middle of it.
    //
    auto watch( const Voltage range, const Voltage centre, const Time timePerDiv) -> void
    {
        Setup( Osc2.acquisition().resolution( pico::Resolution::Bits8));

        Setup( Osc2.channel<1>()
                   .coupling( pico::Coupling::Dc)
                   .probeAttenuation( 1.0)
                   .range( range)
                   .verticalOffset( centre)
                   .bandwidth( pico::Bandwidth::Full)
                   .display( pico::ChannelDisplay::On));

        Setup( Osc2.timebase()
                   .timePerDivision( timePerDiv)
                   .reference( pico::TimebaseReference::Center));

        Setup( Osc2.trigger()
                   .edgeSource<1>()
                   .slope( pico::TriggerSlope::Rising)
                   .level( centre)
                   .sweep( pico::TriggerSweep::Auto));
    }
} // namespace

//
// A 1 kHz, 2 Vpp sine: level, frequency, centre and RMS -- the four things a
// sine is.
//
auto pgenSine() -> void
{
    Apply( Wfg3.sine().frequency( 1_kHz).amplitude( 2_V).offset( 0_V));
    watch( 2_V, 0_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V,    Measure( Osc2.channel<1>().vpp()));
    Verify( DEV_Wfg_1::DEV_Wfg_Freq1kHz, Measure( Osc2.channel<1>().frequency()));
    Verify( DEV_Wfg_1::DEV_Wfg_Zero,     Measure( Osc2.channel<1>().vaverage()));
    Verify( DEV_Wfg_1::DEV_Wfg_SineRms,  Measure( Osc2.channel<1>().vrms()));

    Remove( Wfg3.sine());
}

//
// Four decades of frequency on one sine, the scope's timebase following so two
// periods fill the window each time: 100 Hz, 1 kHz, 10 kHz, 1 MHz. Each is its
// own Apply -- the frequency alone changes, the amplitude and offset carried
// over from the first, which is how the driver's one-call-sets-everything SDK
// is hidden from a script.
//
auto pgenFrequencySteps() -> void
{
    Apply( Wfg3.sine().frequency( 100_Hz).amplitude( 2_V).offset( 0_V));

    const auto check = []( const Frequency nominal, const Time perDiv, const auto & criterion)
    {
        Apply( Wfg3.sine().frequency( nominal));
        watch( 2_V, 0_V, perDiv);
        Verify( criterion, Measure( Osc2.channel<1>().frequency()));
    };

    check( 100_Hz,  2_ms,   DEV_Wfg_1::DEV_Wfg_Freq100Hz);
    check( 1_kHz,   200_us, DEV_Wfg_1::DEV_Wfg_Freq1kHz);
    check( 10_kHz,  20_us,  DEV_Wfg_1::DEV_Wfg_Freq10kHz);
    check( 1_MHz,   200_ns, DEV_Wfg_1::DEV_Wfg_Freq1MHz);

    Remove( Wfg3.sine());
}

//
// A square is 50 %, always: its levels by top and base (settled, so the edges'
// overshoot does not count) and its two widths, which have to be equal.
//
auto pgenSquare() -> void
{
    Apply( Wfg3.square().frequency( 1_kHz).amplitude( 2_V).offset( 0_V));
    watch( 2_V, 0_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_Top1V,       Measure( Osc2.channel<1>().vtop()));
    Verify( DEV_Wfg_1::DEV_Wfg_BaseMinus1V, Measure( Osc2.channel<1>().vbase()));
    Verify( DEV_Wfg_1::DEV_Wfg_Width500us,  Measure( Osc2.channel<1>().positiveWidth()));
    Verify( DEV_Wfg_1::DEV_Wfg_Width500us,  Measure( Osc2.channel<1>().negativeWidth()));

    Remove( Wfg3.square());
}

//
// The three shapes with a slope, told apart by their edge times: a rising
// sawtooth is 0.8 ms from 10% to 90% on the way up and an edge on the way down,
// a falling sawtooth the other way round, and a triangle 0.4 ms each way.
//
auto pgenRamps() -> void
{
    watch( 2_V, 0_V, 200_us);

    Apply( Wfg3.rampUp().frequency( 1_kHz).amplitude( 2_V).offset( 0_V));
    Verify( DEV_Wfg_1::DEV_Wfg_RampEdge, Measure( Osc2.channel<1>().riseTime()));
    Verify( DEV_Wfg_1::DEV_Wfg_FastEdge, Measure( Osc2.channel<1>().fallTime()));

    Apply( Wfg3.rampDown().frequency( 1_kHz).amplitude( 2_V).offset( 0_V));
    Verify( DEV_Wfg_1::DEV_Wfg_FastEdge, Measure( Osc2.channel<1>().riseTime()));
    Verify( DEV_Wfg_1::DEV_Wfg_RampEdge, Measure( Osc2.channel<1>().fallTime()));

    Apply( Wfg3.triangle().frequency( 1_kHz).amplitude( 2_V).offset( 0_V));
    Verify( DEV_Wfg_1::DEV_Wfg_TriangleEdge, Measure( Osc2.channel<1>().riseTime()));
    Verify( DEV_Wfg_1::DEV_Wfg_TriangleEdge, Measure( Osc2.channel<1>().fallTime()));

    Remove( Wfg3.triangle());
}

//
// Amplitude and offset at once, and on purpose: 2 Vpp on +0.5 V peaks at
// +1.5 V and troughs at -0.5 V, which a generator that applied either setting
// alone -- or applied the offset to the wrong end -- cannot fake. 2 Vpp on
// +1 V would be the larger test and sits exactly at the +2 V the output reaches,
// where the instrument's own accuracy decides the verdict; this one does not.
//
auto pgenAmplitudeAndOffset() -> void
{
    Apply( Wfg3.sine().frequency( 1_kHz).amplitude( 2_V).offset( 0.5_V));
    watch( 2_V, 0.5_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_Pk1V5,      Measure( Osc2.channel<1>().vmax()));
    Verify( DEV_Wfg_1::DEV_Wfg_PkMinus0V5, Measure( Osc2.channel<1>().vmin()));
    Verify( DEV_Wfg_1::DEV_Wfg_Mean0V5,    Measure( Osc2.channel<1>().vaverage()));

    Remove( Wfg3.sine());
}

//
// DC, which is the offset with nothing else: a level each side of zero.
//
auto pgenDcLevels() -> void
{
    watch( 2_V, 0_V, 200_us);

    Apply( Wfg3.dc().offset( 1.5_V));
    Verify( DEV_Wfg_1::DEV_Wfg_Dc1V5, Measure( Osc2.channel<1>().vaverage()));

    Apply( Wfg3.dc().offset( -1.5_V));
    Verify( DEV_Wfg_1::DEV_Wfg_DcMinus1V5, Measure( Osc2.channel<1>().vaverage()));

    Remove( Wfg3.dc());
}

//
// Noise has no frequency and so no frequency() -- only an amplitude, and a band
// rather than a value to check it against. Its mean has to be zero like anything
// else with no offset.
//
// auto pgenNoise() -> void -- noise waveform not supported by SDK
// {
//     Apply( Wfg3.noise().amplitude( 1_V).offset( 0_V));
//     watch( 1_V, 0_V, 200_us);
//
//     Verify( DEV_Wfg_1::DEV_Wfg_NoiseRms, Measure( Osc2.channel<1>().vrms()));
//     Verify( DEV_Wfg_1::DEV_Wfg_Zero,     Measure( Osc2.channel<1>().vaverage()));
//
//     Remove( Wfg3.noise());
// }
//
// Remove has to silence the output: the reading after it is the scope's own
// noise floor. The check safing depends on -- and the one where this generator
// differs most from the others, because it has no output-off, only a DC level of
// zero volts.
//
auto pgenOutputOff() -> void
{
    Apply( Wfg3.sine().frequency( 1_kHz).amplitude( 2_V).offset( 0_V));
    watch( 2_V, 0_V, 200_us);

    Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V, Measure( Osc2.channel<1>().vpp()));

    Remove( Wfg3.sine());

    Verify( DEV_Wfg_1::DEV_Wfg_Off, Measure( Osc2.channel<1>().vpp()));
}
