#include "../prelude.hpp"

//
// The 33522B waveform generator, measured by the DSOX1202G -- the WfgIntoScope
// group in dev/suite/test_catalog.inc.
//
// A generator is a source with nothing to read back: it cannot say what it is
// producing, only what it was told to. So every script here is a pair -- Wfg1
// told to make something, Osc1 asked what arrived -- and what passes is the two
// instruments agreeing, through a real cable, with nothing stubbed. Cabling,
// once, and the whole group runs whole:
//
//     generator CH1 output  -->  BNC  -->  scope CH1
//     generator CH2 output  -->  BNC  -->  scope CH2
//
// Every waveform is told .into( Termination::HighImpedance), and that is not a
// detail: the generator quotes amplitude against the load it is told to expect,
// and the scope's input is 1 MOhm. Told fifty ohms (its power-on default), it
// drives twice the programmed voltage into the open circuit the scope
// presents -- which is the one thing wfgTermination below checks on purpose.
//
// What is covered, one script per setting the driver models: each shape (sine,
// square, ramp, triangle, pulse, noise, DC), frequency across four decades,
// amplitude and offset together, duty cycle, ramp symmetry, the termination,
// Remove turning an output off, and the two channels staying independent.
//
// Every script Removes what it Applied before it returns, so the next one
// starts from two quiet outputs; safing turns everything off after the run
// regardless. Settings are well inside the 33522B's limits -- nothing near the
// 200 kHz ramp ceiling or the 20 Vpp high-impedance maximum -- because a
// detached run never reaches the driver's own range check (see
// Wfg33522B::checkAgainstModel), so a unit test cannot catch a script that
// asks for too much. The first attached run would.
//
// The scope readings key as in scope_probe_comp.cpp -- Osc1.<measurement>.
// <quantity>, no channel -- and are injected in read order by
// dev/suite/tests/test_wfg_into_scope.cpp.
//

namespace osc = hal::keysight_dsox1202g;
namespace wfg = hal::keysight_33522b;

using core::quantities::Frequency;
using core::quantities::Time;
using core::quantities::Voltage;

namespace
{
    constexpr auto kOpen = wfg::Termination::HighImpedance;

    //
    // Scope channel N set to watch a generator: 1:1 (a BNC cable, not a
    // probe), DC coupled, perDiv and centre chosen by the caller so the signal
    // fills most of the screen and none of it leaves -- an off-screen trace is
    // one the scope refuses to measure. timePerDiv likewise, usually two
    // periods across the ten divisions.
    //
    // Auto sweep, so a DC level or noise -- which have no edge to trigger on --
    // still draws, and a missing signal fails on its reading rather than on a
    // timeout.
    //
    template<unsigned N>
    auto watch( const Voltage perDiv, const Voltage centre, const Time timePerDiv, const Voltage triggerLevel) -> void
    {
        Setup( Osc1.channel<N>()
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
                   .edgeSource<N>()
                   .slope( osc::TriggerSlope::Rising)
                   .level( triggerLevel)
                   .sweep( osc::TriggerSweep::Auto)
                   .coupling( osc::TriggerCoupling::Dc));

        Setup( Osc1.acquisition().type( osc::AcquisitionType::Normal));
    }

    //
    // A 1 kHz, 2 Vpp sine on channel N, checked for level, frequency, centre
    // and RMS -- the four things a sine is.
    //
    template<unsigned N>
    auto sineOn() -> void
    {
        Apply( Wfg1.channel<N>().sine().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));
        watch<N>( 500_mV, 0_V, 200_us, 0_V);

        Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V,    Measure( Osc1.channel<N>().vpp()));
        Verify( DEV_Wfg_1::DEV_Wfg_Freq1kHz, Measure( Osc1.channel<N>().frequency()));
        Verify( DEV_Wfg_1::DEV_Wfg_Zero,     Measure( Osc1.channel<N>().vaverage()));
        Verify( DEV_Wfg_1::DEV_Wfg_SineRms,  Measure( Osc1.channel<N>().vrms()));

        Remove( Wfg1.channel<N>().sine());
    }
} // namespace

auto wfgSineCh1() -> void { sineOn<1>(); }
auto wfgSineCh2() -> void { sineOn<2>(); }

//
// Four decades of frequency on one sine, the timebase following so that two
// periods fill the screen each time: 100 Hz, 1 kHz, 10 kHz, 1 MHz. Each
// frequency is its own Apply of the whole chain, which is how a script changes
// one setting on this driver -- the config carries every setting it names.
//
auto wfgFrequencySteps() -> void
{
    const auto check = []( const Frequency nominal, const Time perDiv, const auto & criterion)
    {
        Apply( Wfg1.channel<1>().sine().frequency( nominal).amplitude( 2_V).offset( 0_V).into( kOpen));
        watch<1>( 500_mV, 0_V, perDiv, 0_V);
        Verify( criterion, Measure( Osc1.channel<1>().frequency()));
    };

    check( 100_Hz,  2_ms,   DEV_Wfg_1::DEV_Wfg_Freq100Hz);
    check( 1_kHz,   200_us, DEV_Wfg_1::DEV_Wfg_Freq1kHz);
    check( 10_kHz,  20_us,  DEV_Wfg_1::DEV_Wfg_Freq10kHz);
    check( 1_MHz,   200_ns, DEV_Wfg_1::DEV_Wfg_Freq1MHz);

    Remove( Wfg1.channel<1>().sine());
}

//
// Amplitude and offset at once, and on purpose: 4 Vpp on +1 V has to peak at
// +3 V and trough at -1 V, which a generator that applied either setting alone
// -- or applied the offset to the wrong end -- cannot fake.
//
auto wfgAmplitudeAndOffset() -> void
{
    Apply( Wfg1.channel<1>().sine().frequency( 1_kHz).amplitude( 4_V).offset( 1_V).into( kOpen));
    watch<1>( 1_V, 1_V, 200_us, 1_V);

    Verify( DEV_Wfg_1::DEV_Wfg_OffsetMax,  Measure( Osc1.channel<1>().vmax()));
    Verify( DEV_Wfg_1::DEV_Wfg_OffsetMin,  Measure( Osc1.channel<1>().vmin()));
    Verify( DEV_Wfg_1::DEV_Wfg_OffsetMean, Measure( Osc1.channel<1>().vaverage()));

    Remove( Wfg1.channel<1>().sine());
}

//
// A 25% square: its levels by top and base (settled, so the edges' overshoot
// does not count), and its duty cycle by the two widths, which have to add up
// to the period as well as split it 1:3.
//
auto wfgSquareDutyCycle() -> void
{
    Apply( Wfg1.channel<1>().square().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).dutyCycle( 25.0).into( kOpen));
    watch<1>( 500_mV, 0_V, 200_us, 0_V);

    Verify( DEV_Wfg_1::DEV_Wfg_Top1V,       Measure( Osc1.channel<1>().vtop()));
    Verify( DEV_Wfg_1::DEV_Wfg_BaseMinus1V, Measure( Osc1.channel<1>().vbase()));
    Verify( DEV_Wfg_1::DEV_Wfg_Width250us,  Measure( Osc1.channel<1>().positiveWidth()));
    Verify( DEV_Wfg_1::DEV_Wfg_Width750us,  Measure( Osc1.channel<1>().negativeWidth()));

    Remove( Wfg1.channel<1>().square());
}

//
// Symmetry, which only a ramp has, read as edge times: a 100% ramp is a rising
// sawtooth -- 0.8 ms from 10% to 90% on the way up, and a reset that is an edge
// on the way down. A triangle is its own function on this instrument, not a
// 50% ramp (see wfg::Triangle), and rises and falls in 0.4 ms each.
//
auto wfgRampAndTriangle() -> void
{
    Apply( Wfg1.channel<1>().ramp().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).symmetry( 100.0).into( kOpen));
    watch<1>( 500_mV, 0_V, 200_us, 0_V);

    Verify( DEV_Wfg_1::DEV_Wfg_RampEdge, Measure( Osc1.channel<1>().riseTime()));
    Verify( DEV_Wfg_1::DEV_Wfg_FastEdge, Measure( Osc1.channel<1>().fallTime()));

    Remove( Wfg1.channel<1>().ramp());

    Apply( Wfg1.channel<1>().triangle().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));

    Verify( DEV_Wfg_1::DEV_Wfg_TriangleEdge, Measure( Osc1.channel<1>().riseTime()));
    Verify( DEV_Wfg_1::DEV_Wfg_TriangleEdge, Measure( Osc1.channel<1>().fallTime()));

    Remove( Wfg1.channel<1>().triangle());
}

//
// A pulse is its own function with its own duty-cycle command (FUNC:PULS:DCYC,
// where a square's is FUNC:SQU:DCYC) -- so a 10% pulse is checked on its
// width, which is the setting that reaches the other command.
//
auto wfgPulse() -> void
{
    Apply( Wfg1.channel<1>().pulse().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).dutyCycle( 10.0).into( kOpen));
    watch<1>( 500_mV, 0_V, 200_us, 0_V);

    Verify( DEV_Wfg_1::DEV_Wfg_Width100us, Measure( Osc1.channel<1>().positiveWidth()));

    Remove( Wfg1.channel<1>().pulse());
}

//
// DC, which on this instrument is the offset with nothing else -- two levels,
// one each side of zero, read as the average of a flat line.
//
auto wfgDcLevels() -> void
{
    watch<1>( 1_V, 0_V, 200_us, 0_V);

    Apply( Wfg1.channel<1>().dc().offset( 1.5_V).into( kOpen));
    Verify( DEV_Wfg_1::DEV_Wfg_Dc1V5, Measure( Osc1.channel<1>().vaverage()));

    Apply( Wfg1.channel<1>().dc().offset( -2_V).into( kOpen));
    Verify( DEV_Wfg_1::DEV_Wfg_DcMinus2V, Measure( Osc1.channel<1>().vaverage()));

    Remove( Wfg1.channel<1>().dc());
}

//
// Noise, which has no frequency and so no frequency() -- only an amplitude,
// and a band rather than a value to check it against: Gaussian noise set to
// 1 Vpp is about a sixth of that RMS, and "about" is the honest word. Its mean
// has to be zero like anything else with no offset.
//
auto wfgNoise() -> void
{
    Apply( Wfg1.channel<1>().noise().amplitude( 1_V).offset( 0_V).into( kOpen));
    watch<1>( 200_mV, 0_V, 200_us, 0_V);

    Verify( DEV_Wfg_1::DEV_Wfg_NoiseRms, Measure( Osc1.channel<1>().vrms()));
    Verify( DEV_Wfg_1::DEV_Wfg_Zero,     Measure( Osc1.channel<1>().vaverage()));

    Remove( Wfg1.channel<1>().noise());
}

//
// The termination, checked by getting it deliberately "wrong". Told to expect
// fifty ohms, the generator puts out twice the programmed voltage so that half
// of it lands across the load it expects -- and into the scope's open circuit
// all of it arrives: 1 Vpp programmed, 2 Vpp measured. Told the truth, 1 Vpp is
// 1 Vpp. The pair is what shows OUTPut:LOAD reached the instrument; either
// half alone could be an amplitude that happened to be right.
//
auto wfgTermination() -> void
{
    watch<1>( 500_mV, 0_V, 200_us, 0_V);

    Apply( Wfg1.channel<1>().sine().frequency( 1_kHz).amplitude( 1_V).offset( 0_V).into( wfg::Termination::Ohms50));
    Verify( DEV_Wfg_1::DEV_Wfg_OpenCircuit, Measure( Osc1.channel<1>().vpp()));

    Apply( Wfg1.channel<1>().sine().frequency( 1_kHz).amplitude( 1_V).offset( 0_V).into( kOpen));
    Verify( DEV_Wfg_1::DEV_Wfg_Vpp1V, Measure( Osc1.channel<1>().vpp()));

    Remove( Wfg1.channel<1>().sine());
}

//
// Remove, which has to actually turn the output off -- the reading after it is
// the scope's own noise floor and nothing else. The check safing depends on.
//
auto wfgOutputOff() -> void
{
    Apply( Wfg1.channel<1>().sine().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));
    watch<1>( 500_mV, 0_V, 200_us, 0_V);

    Verify( DEV_Wfg_1::DEV_Wfg_Vpp2V, Measure( Osc1.channel<1>().vpp()));

    Remove( Wfg1.channel<1>().sine());

    Verify( DEV_Wfg_1::DEV_Wfg_Off, Measure( Osc1.channel<1>().vpp()));
}

//
// Both channels at once, and different: a 1 kHz sine on 1 and a 5 kHz square
// on 2, each read on its own scope channel. A driver that addressed the wrong
// SOURce -- or programmed both through one -- shows up as the wrong frequency
// on one side.
//
auto wfgChannelsIndependent() -> void
{
    Apply( Wfg1.channel<1>().sine().frequency( 1_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));
    Apply( Wfg1.channel<2>().square().frequency( 5_kHz).amplitude( 2_V).offset( 0_V).into( kOpen));

    watch<2>( 500_mV, 0_V, 200_us, 0_V);
    watch<1>( 500_mV, 0_V, 200_us, 0_V);

    Verify( DEV_Wfg_1::DEV_Wfg_Freq1kHz, Measure( Osc1.channel<1>().frequency()));
    Verify( DEV_Wfg_1::DEV_Wfg_Freq5kHz, Measure( Osc1.channel<2>().frequency()));

    Remove( Wfg1.channel<1>().sine());
    Remove( Wfg1.channel<2>().square());
}
