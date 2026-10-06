#include "../prelude.hpp"


//
// The DSOX1202G against its own front-panel probe-compensation output -- the
// ScopeProbeComp group in dev/suite/test_catalog.inc.
//
// The one signal on this desk that needs no other instrument: a 1 kHz square
// wave the scope makes itself, off the comp terminal beside its inputs. Both
// probes' tips on that terminal, both grounds on its ground lug, and the whole
// group runs as a group -- unlike the meter's function tests, nothing is
// reconnected between scripts.
//
// What it covers is what the driver models, one script per family:
//
//   amplitude  all eight :MEASure:V... readings, on each channel
//   timing     frequency, period, both widths, both edges, on each channel
//   capture    a single-shot Arm/Await, then both channels' traces fetched
//   acquisition  the four acquisition types, each still reading the square
//   coupling   AC coupling and the bandwidth limit
//
// What it does not: the built-in waveform generator, which is its own face of
// the box (Wfg2, see scope_generator.cpp),
// and the setting builders' every value -- a volts/div or a holdoff is
// exercised by being sent, and a wrong one shows up as a reading off screen.
//
// Point-free, like everything on this desk: a scope reading needs no route,
// only a probe. The keys are "Osc1.<measurement>.<quantity>" -- Osc1.Vpp.Voltage
// -- and carry no channel, so channel 1's and channel 2's Vpp share one; each
// script reads one channel, so within a script they never meet. A fetch keys
// by channel (Osc1.Channel1), and a capture as Osc1.Acquisition. See
// dev/suite/tests/test_scope_probe_comp.cpp.
//
// Each Measure takes its port inline, never from a variable: a scope Port reads
// whichever channel and measurement the instrument was last set to, not the one
// it was created for (see the driver README, "The two sharp edges").
//

namespace osc = hal::keysight_dsox1202g;

using core::quantities::Voltage;

namespace
{
    //
    // The picture every script starts from, on channel N: 500 mV/div with the
    // centre of the screen at 1.25 V, so the square fills the middle six of the
    // eight divisions -- on screen with room for its overshoot, which an
    // off-screen trace would make unmeasurable. 200 us/div is two whole
    // periods across the screen, and whole periods matter: vrms and vaverage
    // integrate over the display, and a screen ending mid-cycle biases both.
    //
    // A 10:1 probe, which is what ships with this scope. A 1:1 lead instead is
    // this one number changed, and nothing else.
    //
    // Auto sweep, so a scope that loses the edge still draws something and a
    // reading fails on its value rather than timing out. The capture script
    // below changes that on purpose.
    //
    template<unsigned N>
    auto frameTheCompSignal() -> void
    {
        Setup( Osc1.channel<N>()
                   .coupling( osc::Coupling::Dc)
                   .probeAttenuation( 10.0)
                   .voltsPerDivision( 500_mV)
                   .verticalOffset( 1.25_V)
                   .bandwidth( osc::Bandwidth::Full)
                   .display( osc::ChannelDisplay::On));

        Setup( Osc1.timebase()
                   .timePerDivision( 200_us)
                   .reference( osc::TimebaseReference::Center));

        Setup( Osc1.trigger()
                   .edgeSource<N>()
                   .slope( osc::TriggerSlope::Rising)
                   .level( 1.25_V)
                   .sweep( osc::TriggerSweep::Auto)
                   .coupling( osc::TriggerCoupling::Dc));

        Setup( Osc1.acquisition()
                   .type( osc::AcquisitionType::Normal));
    }

    //
    // All eight levels. Top and base before max and min, and both kept, because
    // they are different questions: top and base are where the square settles,
    // max and min are its extreme samples, and the difference is the overshoot
    // a badly compensated probe adds -- which is what this signal is for.
    //
    template<unsigned N>
    auto amplitude() -> void
    {
        frameTheCompSignal<N>();

        Verify( DEV_Osc_1::DEV_Osc_Vtop,       Measure( Osc1.channel<N>().vtop()));
        Verify( DEV_Osc_1::DEV_Osc_Vbase,      Measure( Osc1.channel<N>().vbase()));
        Verify( DEV_Osc_1::DEV_Osc_Vamplitude, Measure( Osc1.channel<N>().vamplitude()));
        Verify( DEV_Osc_1::DEV_Osc_Vpp,        Measure( Osc1.channel<N>().vpp()));
        Verify( DEV_Osc_1::DEV_Osc_Vmax,       Measure( Osc1.channel<N>().vmax()));
        Verify( DEV_Osc_1::DEV_Osc_Vmin,       Measure( Osc1.channel<N>().vmin()));
        Verify( DEV_Osc_1::DEV_Osc_Vaverage,   Measure( Osc1.channel<N>().vaverage()));
        Verify( DEV_Osc_1::DEV_Osc_Vrms,       Measure( Osc1.channel<N>().vrms()));
    }

    //
    // The timing family, at the framing above -- then the edges, which need a
    // different one. At 200 us/div a sample is hundreds of nanoseconds apart
    // and a microsecond edge is two samples, so each edge is measured on its
    // own 1 us/div screen with the trigger on that edge, at the centre.
    //
    template<unsigned N>
    auto timing() -> void
    {
        frameTheCompSignal<N>();

        Verify( DEV_Osc_1::DEV_Osc_Frequency, Measure( Osc1.channel<N>().frequency()));
        Verify( DEV_Osc_1::DEV_Osc_Period,    Measure( Osc1.channel<N>().period()));
        Verify( DEV_Osc_1::DEV_Osc_Width,     Measure( Osc1.channel<N>().positiveWidth()));
        Verify( DEV_Osc_1::DEV_Osc_Width,     Measure( Osc1.channel<N>().negativeWidth()));

        Setup( Osc1.timebase().timePerDivision( 1_us));

        Verify( DEV_Osc_1::DEV_Osc_Edge, Measure( Osc1.channel<N>().riseTime()));

        Setup( Osc1.trigger()
                   .edgeSource<N>()
                   .slope( osc::TriggerSlope::Falling)
                   .level( 1.25_V)
                   .sweep( osc::TriggerSweep::Auto)
                   .coupling( osc::TriggerCoupling::Dc));

        Verify( DEV_Osc_1::DEV_Osc_Edge, Measure( Osc1.channel<N>().fallTime()));
    }

    //
    // What one channel's fetched trace has to say about the square: that there
    // is one, and that its extreme samples are the square's. The trace's own
    // reductions against the same rows vmax()/vmin() are held to, so the
    // instrument's measurement and the transferred record have to agree --
    // which is the check that the transfer's scaling is right, and the one a
    // text-format trace (see the driver README) is most likely to get wrong.
    //
    auto checkTrace( const core::Waveform & trace) -> void
    {
        Verify( DEV_Osc_1::DEV_Osc_HasSamples, !trace.empty());

        if( trace.empty())
        {
            return;
        }

        Verify( DEV_Osc_1::DEV_Osc_Vmax, trace.maximum<Voltage>());
        Verify( DEV_Osc_1::DEV_Osc_Vmin, trace.minimum<Voltage>());
    }
} // namespace

auto scopeAmplitudeCh1() -> void { amplitude<1>(); }
auto scopeAmplitudeCh2() -> void { amplitude<2>(); }
auto scopeTimingCh1()    -> void { timing<1>(); }
auto scopeTimingCh2()    -> void { timing<2>(); }

//
// A single-shot capture of both channels, then both traces.
//
// Normal sweep, not the Auto the framing uses: a single capture on Auto
// completes whether or not anything triggered it, and "captured" would then say
// nothing about the trigger. On Normal it waits for a real rising edge through
// 1.25 V on channel 1 -- which a comp output provides every millisecond, so a
// capture that does not complete is a scope or a probe problem.
//
// Checked before anything is fetched, and the fetch skipped if it failed, for
// the reason the bench's ac_dropout_script.cpp gives: a trace fetched after a
// capture that never completed is whatever the buffer held before.
//
auto scopeCapture() -> void
{
    frameTheCompSignal<2>();
    frameTheCompSignal<1>();

    Setup( Osc1.trigger()
               .edgeSource<1>()
               .slope( osc::TriggerSlope::Rising)
               .level( 1.25_V)
               .sweep( osc::TriggerSweep::Normal)
               .coupling( osc::TriggerCoupling::Dc));

    Arm( Osc1.single().timeout( 2_s));

    const auto captured = Await( Osc1.single());

    Verify( DEV_Osc_1::DEV_Osc_Captured, captured);

    if( !captured)
    {
        return;
    }

    checkTrace( Fetch( Osc1.channel<1>().waveform()));
    checkTrace( Fetch( Osc1.channel<2>().waveform()));
}

//
// The four acquisition types, each reading the same square. Peak-to-peak is
// the reading that tells them apart if anything does -- peak detect keeps the
// extremes a normal acquisition decimates away, averaging removes the noise on
// top -- and on a clean comp edge all four have to land in the same window.
// Left on Normal, which is where the framing puts it anyway.
//
auto scopeAcquisitionTypes() -> void
{
    frameTheCompSignal<1>();

    for( const auto type : { osc::AcquisitionType::Normal,
                             osc::AcquisitionType::HighResolution,
                             osc::AcquisitionType::PeakDetect })
    {
        Setup( Osc1.acquisition().type( type));
        Verify( DEV_Osc_1::DEV_Osc_Vpp, Measure( Osc1.channel<1>().vpp()));
    }

    Setup( Osc1.acquisition().averagedOver( 16));
    Verify( DEV_Osc_1::DEV_Osc_Vpp, Measure( Osc1.channel<1>().vpp()));

    Setup( Osc1.acquisition().type( osc::AcquisitionType::Normal));
}

//
// AC coupling, then the bandwidth limit.
//
// AC coupled, the square's DC is gone and it sits around zero -- so the offset
// and the trigger level come down to zero with it, or the trace is off the
// bottom of the screen and the trigger never crosses. The average is what
// shows the coupling took effect: it was Vtop/2 a moment ago.
//
// Then DC again with the bandwidth limit on. A 1 kHz square is nowhere near the
// limit, so its amplitude must not move -- a limit that changes the settled
// levels of a slow signal is a limit set on the wrong channel, or a gain
// error in the path it switches.
//
auto scopeCouplingAndBandwidth() -> void
{
    frameTheCompSignal<1>();

    Setup( Osc1.channel<1>()
               .coupling( osc::Coupling::Ac)
               .verticalOffset( 0_V));

    Setup( Osc1.trigger()
               .edgeSource<1>()
               .slope( osc::TriggerSlope::Rising)
               .level( 0_V)
               .sweep( osc::TriggerSweep::Auto)
               .coupling( osc::TriggerCoupling::Dc));

    Verify( DEV_Osc_1::DEV_Osc_AcAverage, Measure( Osc1.channel<1>().vaverage()));

    frameTheCompSignal<1>();

    Setup( Osc1.channel<1>().bandwidth( osc::Bandwidth::Limited));

    Verify( DEV_Osc_1::DEV_Osc_Vamplitude, Measure( Osc1.channel<1>().vamplitude()));

    Setup( Osc1.channel<1>().bandwidth( osc::Bandwidth::Full));
}
