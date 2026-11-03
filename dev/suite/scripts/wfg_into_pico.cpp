#include "../prelude.hpp"

//
// The PicoScope 5244D, Osc2, watching the 33522B, Wfg1 -- the WfgIntoPico group
// of dev/suite/test_catalog.inc.
//
// The mirror of WfgIntoScope (wfg_into_scope.cpp), with the instruments' roles
// swapped: there the DSOX1202G is the reference and the generator is under
// test, here the generator is the reference and the digitiser is. A generator
// cannot say what it produces, but a bench generator that has already passed
// WfgIntoScope is a stimulus worth trusting, and what these scripts check is
// that the Pico's driver -- ranges, offset, timebase, trigger, the host-side
// measurements -- reads it back correctly. One cabling, once:
//
//     33522B CH1  -->  BNC  -->  PicoScope channel A  (Osc2 channel 1)
//     33522B CH2  -->  BNC  -->  PicoScope channel B  (Osc2 channel 2)
//
// The Pico has a signal generator of its own, Wfg3, and this is not it: that is
// checked by the PicoGenerator group (pico_generator.cpp), with the Pico's own
// scope. These scripts are the check against an independent source -- a Pico
// that reads its own generator correctly could still be wrong about both.
//
// What differs from the DSOX1202G scripts, and why. A PicoScope has no
// divisions, so the vertical setting is range() -- how far the signal reaches
// from the centre, from which the driver picks the smallest +/-10 mV..20 V
// range that holds it -- and the offset is the level at the centre of that
// range. There is no trigger coupling and no acquisition type; there is a
// resolution, which on this instrument trades sample rate for bits (see
// picoResolutions). Every measurement is computed on the host from one
// captured record, so a reading is only as good as the capture: a trace that
// touches the range limit is clipped, and the driver refuses to measure it
// rather than report a flattened peak. The ranges below therefore leave
// headroom.
//
// Every waveform is told .into( Termination::HighImpedance): the Pico's input
// is 1 MOhm, as the DSOX1202G's is, and the generator quotes amplitude against
// the load it is told to expect. The criteria are DEV_Wfg_1's, the same rows
// WfgIntoScope is held to -- what a generator told 2 Vpp must show does not
// depend on what is looking.
//
// Every script starts from watch(), which sets every Pico setting a script here
// changes, so no script puts the Pico back on its way out. The generator is
// different: every script Removes what it Applied, and safing turns the outputs
// off after the run regardless. Readings key as Osc2.<measurement>.<quantity>,
// no channel, and are injected in read order by
// dev/suite/tests/test_pico_into_wfg.cpp.
//

namespace pico = hal::pico_5244d;
namespace wfg  = hal::keysight_33522b;

using core::quantities::Frequency;
using core::quantities::Time;
using core::quantities::Voltage;

namespace
{
    constexpr auto kOpen = wfg::Termination::HighImpedance;

    //
    // The stimulus most scripts use: a sine on generator channel N, 1 kHz and
    // 2 Vpp on 0 V unless told otherwise.
    //
    template<unsigned N>
    auto referenceSine( const Frequency frequency = 1_kHz, const Voltage amplitude = 2_V, const Voltage offset = 0_V)
    {
        return Wfg1.channel<N>().sine().frequency( frequency).amplitude( amplitude).offset( offset).into( kOpen);
    }

    //
    // Pico channel N set to watch a generator: 1:1 (a BNC cable, not a probe),
    // DC coupled, the whole bandwidth. range is how far the signal reaches from
    // centre, the level at the middle of the range; timePerDiv is usually two
    // periods across the ten divisions. The defaults are the 2 Vpp, 1 kHz
    // reference sine with 2x headroom.
    //
    // The trigger sits at the centre on channel N's rising edge, with Auto
    // sweep, so a DC level -- which has no edge to trigger on -- still
    // captures, and a missing signal fails on its reading rather than on a
    // timeout.
    //
    // The resolution is set here too, so a script that wants 12 or 14 bits asks
    // for them in one change rather than through 8 bits on the way.
    //
    template<unsigned N>
    auto watch( const Voltage          range      = 2_V,
                const Voltage          centre     = 0_V,
                const Time             timePerDiv = 200_us,
                const pico::Resolution bits       = pico::Resolution::Bits8) -> void
    {
        Setup( Osc2.acquisition().resolution( bits));

        Setup( Osc2.channel<N>()
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
                   .edgeSource<N>()
                   .slope( pico::TriggerSlope::Rising)
                   .level( centre)
                   .sweep( pico::TriggerSweep::Auto));
    }

    //
    // Both channels on the defaults, channel A last so that it is the trigger
    // source.
    //
    auto watchBoth() -> void
    {
        watch<2>();
        watch<1>();
    }

    //
    // A 1 kHz, 2 Vpp sine on channel N, checked for level, frequency, centre
    // and RMS -- the four things a sine is.
    //
    template<unsigned N>
    auto sineOn() -> void
    {
        Apply( referenceSine<N>());
        watch<N>();

        Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V,    Measure( Osc2.channel<N>().vpp()));
        Verify( DEV_Wfg_1::DEV_Wfg_Freq1kHz, Measure( Osc2.channel<N>().frequency()));
        Verify( DEV_Wfg_1::DEV_Wfg_Zero,     Measure( Osc2.channel<N>().vaverage()));
        Verify( DEV_Wfg_1::DEV_Wfg_SineRms,  Measure( Osc2.channel<N>().vrms()));

        Remove( Wfg1.channel<N>().sine());
    }
} // namespace

auto picoSineChA() -> void { sineOn<1>(); }
auto picoSineChB() -> void { sineOn<2>(); }

//
// Four decades of frequency on one sine, the timebase following so two periods
// fill the window each time: 100 Hz, 1 kHz, 10 kHz, 1 MHz. At 1 MHz the window
// is 2 us and the driver has to find a timebase fine enough to put a few hundred
// samples in it -- the search the SDK leaves to the caller.
//
auto picoFrequencySteps() -> void
{
    const auto check = []( const Frequency nominal, const Time perDiv, const auto & criterion)
    {
        Apply( referenceSine<1>( nominal));
        watch<1>( 2_V, 0_V, perDiv);
        Verify( criterion, Measure( Osc2.channel<1>().frequency()));
    };

    check( 100_Hz,  2_ms,   DEV_Wfg_1::DEV_Wfg_Freq100Hz);
    check( 1_kHz,   200_us, DEV_Wfg_1::DEV_Wfg_Freq1kHz);
    check( 10_kHz,  20_us,  DEV_Wfg_1::DEV_Wfg_Freq10kHz);
    check( 1_MHz,   200_ns, DEV_Wfg_1::DEV_Wfg_Freq1MHz);

    Remove( Wfg1.channel<1>().sine());
}

//
// Amplitude and offset at once, and on purpose: 4 Vpp on +1 V peaks at +3 V and
// troughs at -1 V, +/-2 V about a centre of +1 V. Asked as 2.5 V for headroom,
// which the driver rounds up to its +/-5 V range. The centre is where the
// driver's sign convention for the SDK's analogue offset is checked, because
// the SDK adds its offset to the input where this driver's offset is the level
// shown at the centre. Got backwards, the trace lands against a rail and is
// clipped.
//
auto picoAmplitudeAndOffset() -> void
{
    Apply( referenceSine<1>( 1_kHz, 4_V, 1_V));
    watch<1>( 2.5_V, 1_V);

    Verify( DEV_Wfg_1::DEV_Wfg_OffsetMax,  Measure( Osc2.channel<1>().vmax()));
    Verify( DEV_Wfg_1::DEV_Wfg_OffsetMin,  Measure( Osc2.channel<1>().vmin()));
    Verify( DEV_Wfg_1::DEV_Wfg_OffsetMean, Measure( Osc2.channel<1>().vaverage()));

    Remove( Wfg1.channel<1>().sine());
}

//
// The same 2 Vpp sine on the +/-2 V, +/-5 V and +/-10 V ranges, 12-bit so the
// coarser ranges still resolve it: the reading has to be the same whichever
// range the driver picked, which is the range table and the count-to-volts
// scaling checked at three points. 8 bits on a +/-10 V range is 78 mV a step,
// which would be a test of the converter rather than of the driver.
//
auto picoRanges() -> void
{
    Apply( referenceSine<1>());

    for( const auto range : { 2_V, 5_V, 10_V })
    {
        watch<1>( range, 0_V, 200_us, pico::Resolution::Bits12);

        Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V, Measure( Osc2.channel<1>().vpp()));
    }

    Remove( Wfg1.channel<1>().sine());
}

//
// The same sine at 8, 12 and 14 bits. Resolution changes the sample rate the
// instrument can reach and the converter behind it, so each one is a different
// code path through the driver (SetDeviceResolution, then a fresh timebase
// search). A mode the unit refuses is a failure with the SDK's reason, which is
// the useful outcome.
//
auto picoResolutions() -> void
{
    Apply( referenceSine<1>());

    for( const auto bits : { pico::Resolution::Bits8, pico::Resolution::Bits12, pico::Resolution::Bits14 })
    {
        watch<1>( 2_V, 0_V, 200_us, bits);

        Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V,    Measure( Osc2.channel<1>().vpp()));
        Verify( DEV_Wfg_1::DEV_Wfg_Freq1kHz, Measure( Osc2.channel<1>().frequency()));
    }

    Remove( Wfg1.channel<1>().sine());
}

//
// A 25% square: levels by top and base (settled, so the edges' overshoot does
// not count), and its duty cycle by the two widths.
//
auto picoSquareDutyCycle() -> void
{
    Apply( Wfg1.channel<1>().square().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).dutyCycle( 25_pct).into( kOpen));
    watch<1>();

    Verify( DEV_Wfg_1::DEV_Wfg_Top1V,       Measure( Osc2.channel<1>().vtop()));
    Verify( DEV_Wfg_1::DEV_Wfg_BaseMinus1V, Measure( Osc2.channel<1>().vbase()));
    Verify( DEV_Wfg_1::DEV_Wfg_Width250us,  Measure( Osc2.channel<1>().positiveWidth()));
    Verify( DEV_Wfg_1::DEV_Wfg_Width750us,  Measure( Osc2.channel<1>().negativeWidth()));

    Remove( Wfg1.channel<1>().square());
}

//
// Edge times, which are computed between the 10% and 90% levels of the record's
// own histogram: a 100% ramp is a rising sawtooth, 0.8 ms up and an edge back
// down, and a triangle is 0.4 ms each way. One watch() serves both shapes.
//
auto picoRampAndTriangle() -> void
{
    Apply( Wfg1.channel<1>().ramp().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).symmetry( 100_pct).into( kOpen));
    watch<1>();

    Verify( DEV_Wfg_1::DEV_Wfg_RampEdge, Measure( Osc2.channel<1>().riseTime()));
    Verify( DEV_Wfg_1::DEV_Wfg_FastEdge, Measure( Osc2.channel<1>().fallTime()));

    Remove( Wfg1.channel<1>().ramp());

    Apply( Wfg1.channel<1>().triangle().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));

    Verify( DEV_Wfg_1::DEV_Wfg_TriangleEdge, Measure( Osc2.channel<1>().riseTime()));
    Verify( DEV_Wfg_1::DEV_Wfg_TriangleEdge, Measure( Osc2.channel<1>().fallTime()));

    Remove( Wfg1.channel<1>().triangle());
}

//
// DC, one level each side of zero: the average of a flat line, so the offset
// scaling and the zero point of the converter, with no signal to hide behind.
//
auto picoDcLevels() -> void
{
    Apply( Wfg1.channel<1>().dc().offset( 1.5_V).into( kOpen));
    watch<1>();
    Verify( DEV_Wfg_1::DEV_Wfg_Dc1V5, Measure( Osc2.channel<1>().vaverage()));

    Apply( Wfg1.channel<1>().dc().offset( -2_V).into( kOpen));
    watch<1>( 2.5_V);
    Verify( DEV_Wfg_1::DEV_Wfg_DcMinus2V, Measure( Osc2.channel<1>().vaverage()));

    Remove( Wfg1.channel<1>().dc());
}

//
// AC coupling takes the offset out: 2 Vpp on +1 V averages 1 V on DC coupling
// and zero on AC, the same signal read two ways. On AC the centre and the
// trigger move to 0 V with it.
//
/*
auto picoCoupling() -> void
{
    Apply( referenceSine<1>( 1_kHz, 2_V, 1_V));

    watch<1>( 2.5_V, 1_V);
    Verify( DEV_Wfg_1::DEV_Wfg_OffsetMean, Measure( Osc2.channel<1>().vaverage()));

    // AC coupling: the PicoScope 5244D uses a high-pass filter. With a 1 V DC
    // offset at 1 kHz, the capacitor needs time to discharge. Switch to AC
    // coupling once, then wait for the filter to settle with periodic captures.
    Setup( Osc2.channel<1>().coupling( pico::Coupling::Ac));
    
    Voltage average;

    for( int i = 0; i < 3; i++) 
    {
        Delay( 1000_ms); // Let AC coupling settle (10 seconds total)
        average = Measure( Osc2.channel<1>().vaverage());
    }
    
    Verify( DEV_Wfg_1::DEV_Wfg_Zero, average);
//    Verify( DEV_Wfg_1::DEV_Wfg_Zero, Measure( Osc2.channel<1>().vaverage()));

    Setup( Osc2.channel<1>().coupling( pico::Coupling::Dc));

    Remove( Wfg1.channel<1>().sine());
}
*/
auto picoCoupling() -> void
{
    Apply( referenceSine<1>( 1_kHz, 2_V, 1_V));

    watch<1>( 2.5_V, 1_V);
    Verify( DEV_Wfg_1::DEV_Wfg_OffsetMean, Measure( Osc2.channel<1>().vaverage()));

    Setup( Osc2.channel<1>().coupling( pico::Coupling::Ac).verticalOffset( 0_V));
    Setup( Osc2.trigger().level( 0_V));

    //
    // The first record after a coupling change holds the coupling capacitor
    // charging, not the settled signal, so it is captured and thrown away.
    // (A workaround: the driver should do this itself.)
    //
    static_cast<void>( Measure( Osc2.channel<1>().vaverage()));

    Verify( DEV_Wfg_1::DEV_Wfg_Zero, Measure( Osc2.channel<1>().vaverage()));

    Remove( Wfg1.channel<1>().sine());
}

//
// The 20 MHz bandwidth limit leaves a 1 kHz sine alone; what it checks is that
// SetBandwidthFilter is accepted and the channel still reads.
//
auto picoBandwidthLimit() -> void
{
    Apply( referenceSine<1>());
    watch<1>();

    Setup( Osc2.channel<1>().bandwidth( pico::Bandwidth::Limited));
    Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V, Measure( Osc2.channel<1>().vpp()));

    Remove( Wfg1.channel<1>().sine());
}

//
// The trigger, which on Normal sweep only fires on a real edge: a rising and a
// falling slope through zero both have to arrive and both have to give the
// frequency. Auto would capture either way and say nothing about the trigger.
// watch() has already put it on channel A at 0 V.
//
auto picoTriggerSlopes() -> void
{
    Apply( referenceSine<1>());
    watch<1>();

    for( const auto slope : { pico::TriggerSlope::Falling, pico::TriggerSlope::Rising })
    {
        Setup( Osc2.trigger().slope( slope).sweep( pico::TriggerSweep::Normal));

        Verify( DEV_Wfg_1::DEV_Wfg_Freq1kHz, Measure( Osc2.channel<1>().frequency()));
    }

    Remove( Wfg1.channel<1>().sine());
}

//
// A single-shot capture, then both traces. Arm on this instrument waits for the
// pre-trigger fill (there is no armed flag to read), Await for the trigger, and
// the trace is the record the readings above are computed from -- so its extremes
// have to be the sine's peaks, +/-1 V. Normal sweep, so the capture waits for a
// real rising edge on channel A.
//
// Checked before anything is fetched, and the fetch skipped if it failed, for
// the reason scopeCapture() gives: a trace fetched after a capture that never
// completed is whatever the buffer held before.
//
auto picoCapture() -> void
{
    Apply( referenceSine<1>());
    Apply( referenceSine<2>());

    watchBoth();
    Setup( Osc2.trigger().sweep( pico::TriggerSweep::Normal));

    Arm( Osc2.single().timeout( 2_s));

    const auto captured = Await( Osc2.single());

    Verify( DEV_Osc_1::DEV_Osc_Captured, captured);

    if( captured)
    {
        const auto check = []( const core::Waveform & trace)
        {
            Verify( DEV_Osc_1::DEV_Osc_HasSamples, !trace.empty());

            if( !trace.empty())
            {
                Verify( DEV_Wfg_1::DEV_Wfg_Top1V,       trace.maximum<Voltage>());
                Verify( DEV_Wfg_1::DEV_Wfg_BaseMinus1V, trace.minimum<Voltage>());
            }
        };

        check( Fetch( Osc2.channel<1>().waveform()));
        check( Fetch( Osc2.channel<2>().waveform()));
    }

    Remove( Wfg1.channel<1>().sine());
    Remove( Wfg1.channel<2>().sine());
}

//
// Both channels at once, and different: 1 kHz sine on A, 5 kHz square on B. A
// driver that programs one channel's range or buffer into the other shows up as
// the wrong frequency on one side.
//
auto picoChannelsIndependent() -> void
{
    Apply( referenceSine<1>());
    Apply( Wfg1.channel<2>().square().frequency( 5_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));

    watchBoth();

    Verify( DEV_Wfg_1::DEV_Wfg_Freq1kHz, Measure( Osc2.channel<1>().frequency()));
    Verify( DEV_Wfg_1::DEV_Wfg_Freq5kHz, Measure( Osc2.channel<2>().frequency()));

    Remove( Wfg1.channel<1>().sine());
    Remove( Wfg1.channel<2>().square());
}

//
// Remove has to turn the output off: what the Pico reads after it is its own
// noise floor. The check the generator's safing depends on, seen from the
// other end.
//
auto picoOutputOff() -> void
{
    Apply( referenceSine<1>());
    watch<1>();

    Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V, Measure( Osc2.channel<1>().vpp()));

    Remove( Wfg1.channel<1>().sine());

    Verify( DEV_Wfg_1::DEV_Wfg_Off, Measure( Osc2.channel<1>().vpp()));
}