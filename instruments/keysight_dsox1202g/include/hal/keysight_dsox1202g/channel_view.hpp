#pragma once

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/keysight_dsox1202g/channel.hpp"
#include "hal/keysight_dsox1202g/dsox1202g.hpp"
#include "hal/keysight_dsox1202g/waveform.hpp"

//
// Channel<N>: the view a script names a channel through, for both of the
// things it does to one -- configure it (handing off to ChannelBuilder, see
// channel.hpp) and measure through it (the Ports).
//
// After the instrument rather than before it, because every member here
// needs DSOX1202G to be a complete type; vocabulary.hpp declares it early so
// that DSOX1202G::channel<N>() can name it.
//
// Part of hal/keysight_dsox1202g.hpp -- include that, not this.
//

namespace hal::keysight_dsox1202g
{
    //
    // One of the DSOX1202G's two physical input channels, narrowed to at
    // compile time via DSOX1202G::channel<N>().
    //
    // It is the one place a channel number is written in a script, and it
    // serves both things a script does to a channel -- configure it and
    // measure through it:
    //
    //     Setup(   Osc1.channel<2>().coupling( Coupling::Dc).voltsPerDivision( 100_mV));
    //     Measure( Osc1.channel<2>().vmin(), at( dut::Vout));
    //
    // Note there is deliberately no config() here, only on the builder each
    // setting method returns. So `Setup( Osc1.channel<2>())` -- a Setup that
    // names a channel and no setting, which can only be a mistake -- is "no
    // matching function" rather than a call that does nothing.
    //
    // Deliberately a thin, transient view over DSOX1202G&, never itself
    // retained by anything returned from it: every measurement method records
    // N onto the real DSOX1202G instance -- via setChannel(), the same way it
    // records which mode via setMode() -- and then hands back a
    // core::Port<Q, DSOX1202G> bound directly to that real, singular
    // instrument, not to this view.
    //
    // That is load-bearing rather than incidental, and the Infiniium driver's
    // own comment records why: an earlier version of that file had Port
    // referencing the channel view itself, so a Port obtained via
    // `osc1.channel<2>().vmax()` and held past the full expression that
    // created it referenced a temporary that no longer existed -- silent
    // dangling-reference UB, caught by a test rather than by the compiler.
    // Binding Port straight to the instrument removes the dangling risk at the
    // cost of the channel being instrument-level mutable state, which is the
    // same accepted sharp edge Mode already has: a Port handle read after a
    // later channel<M>() switch reads whichever channel is current at
    // rawMeasure() time. One documented sharp edge beats a second, worse,
    // undocumented one.
    //
    // The setting methods have no such edge, because they carry the channel
    // number into the config by value rather than leaving it on the
    // instrument -- see ChannelBuilder.
    //
    template<unsigned N>
        requires ValidChannel<N>
    class Channel
    {
        public:
            static constexpr unsigned Number = N;

            explicit Channel( DSOX1202G & instrument) : mInstrument( instrument) {}

            // --- Configuring this channel: hands off to the builder ---

            [[nodiscard]] auto coupling( Coupling value) const -> ChannelBuilder;
            [[nodiscard]] auto voltsPerDivision( core::quantities::Voltage value) const -> ChannelBuilder;
            [[nodiscard]] auto verticalOffset( core::quantities::Voltage value) const -> ChannelBuilder;
            [[nodiscard]] auto bandwidth( Bandwidth value) const -> ChannelBuilder;
            [[nodiscard]] auto probeAttenuation( double ratio) const -> ChannelBuilder;
            [[nodiscard]] auto display( ChannelDisplay value) const -> ChannelBuilder;

            //
            // The amplitude family -- the :MEASure:V... subset that answers
            // questions about levels. Each records both N (setChannel) and its
            // own mode (setMode) onto the real instrument before returning a
            // Port bound to that same instrument.
            //
            // vbase()/vtop() are not vmin()/vmax(), and the difference is the
            // one that matters for transient work. Min and max are the extreme
            // samples in the record, so a single spike moves them. Base and
            // top are the settled levels the waveform spends its time at, so
            // they are not moved by the spike at all -- which makes
            // vbase() - vmin() the size of a negative transient measured
            // against the rail it departed from.
            //
            // Eight, where the Infiniium driver has nine: there is no
            // :MEASure:VMIDdle on the 1000 X-Series, so vmiddle() is absent
            // rather than emulated as (vtop + vbase) / 2. A driver that
            // computed a reading the instrument did not take would be putting
            // a number in the run journal that no instrument ever answered,
            // and the journal's whole claim is that its numbers came off the
            // bench.
            //
            // vrms() and vaverage() carry one decision each that this
            // instrument makes explicit and the Infiniium does not: both
            // commands take an interval ({ CYCLe | DISPlay }) and :MEASure:VRMS
            // also takes a type ({ AC | DC }). This driver asks for neither, so
            // both take the instrument's documented defaults -- DISPlay, and
            // DC RMS. Whole-screen and DC-inclusive is what a rail measurement
            // wants; a script needing cycle-RMS of the ripple alone needs a
            // parameter here, and should get one rather than a comment saying
            // it was thought about.
            //
            [[nodiscard]] auto vpp()        -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vmax()       -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vmin()       -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vrms()       -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vaverage()   -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vbase()      -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vtop()       -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vamplitude() -> core::Port<core::quantities::Voltage, DSOX1202G>;

            //
            // The timing family. frequency()/period()/positiveWidth()/
            // negativeWidth() need nothing beyond "which channel";
            // riseTime()/fallTime() also carry the usual 10%/90% edge-timing
            // thresholds as a MeasureSetup, defaulted here so a bare
            // `.riseTime()` is still a complete, valid reading, and
            // overridable via the same chained-builder spelling as
            // core::Port's range()/nplc().
            //
            // Still not modeled: duty cycle, overshoot and preshoot, all of
            // which this instrument measures. All three are dimensionless
            // ratios, and this framework has no dimensionless quantity to
            // return them as -- core::quantities has PowerFactor and nothing
            // else without a unit, and borrowing it for a duty cycle would
            // make "0.45 of a power factor" the thing a criterion compares
            // against. That is a core question (a Ratio quantity, or a
            // criterion over a bare double), not a driver one, and inventing
            // an answer here would put it in the wrong file.
            //
            [[nodiscard]] auto frequency()      -> core::Port<core::quantities::Frequency, DSOX1202G>;
            [[nodiscard]] auto period()         -> core::Port<core::quantities::Time, DSOX1202G>;
            [[nodiscard]] auto riseTime()       -> core::Port<core::quantities::Time, DSOX1202G>;
            [[nodiscard]] auto fallTime()       -> core::Port<core::quantities::Time, DSOX1202G>;
            [[nodiscard]] auto positiveWidth()  -> core::Port<core::quantities::Time, DSOX1202G>;
            [[nodiscard]] auto negativeWidth()  -> core::Port<core::quantities::Time, DSOX1202G>;

            //
            // The whole captured record off this channel, for Fetch -- see
            // core/verbs/trace.hpp.
            //
            // Not a Port and not a Measure: everything above answers one number
            // about this channel and reaches the DUT point named at the Measure
            // call, with the route closed and reopened around it. A trace is
            // already inside the instrument, arrived over whatever route the
            // capture was taken on, and is not a quantity a criterion can be
            // pointed at (see core::Waveform on what a script does with one
            // instead).
            //
            // Unlike the measurement methods above, this does NOT switch the
            // instrument's mode or selected channel -- the channel travels in
            // the config by value, so the sharp edge those fourteen carry does
            // not exist here. Same as the setting builders.
            //
            [[nodiscard]] auto waveform() const -> WaveformBuilder;

        private:
            DSOX1202G & mInstrument;
    };

    //
    // ---------------------------------------------------------------------
    // Channel<N>, out of line
    // ---------------------------------------------------------------------
    //
    // Below DSOX1202G rather than inside the class template, because every one
    // of these needs the instrument to be a complete type.
    //

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::coupling( const Coupling value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.coupling( value);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::waveform() const -> WaveformBuilder
    {
        return WaveformBuilder{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::voltsPerDivision( const core::quantities::Voltage value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.voltsPerDivision( value);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::verticalOffset( const core::quantities::Voltage value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.verticalOffset( value);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::bandwidth( const Bandwidth value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.bandwidth( value);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::probeAttenuation( const double ratio) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.probeAttenuation( ratio);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::display( const ChannelDisplay value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.display( value);
    }

    //
    // The measurement family. Every one of these is the same three lines --
    // record the channel, record the mode, hand back a Port on the real
    // instrument, qualified by which measurement it is.
    //
    // That qualifier is what stops fourteen different answers about one DUT pin
    // from sharing one session slot: a routed reading keys as "Output5V.Vbase"
    // rather than as "Output5V" (see core::MeasureEngine, and
    // core::Port::qualifiedBy for the mechanism). A DMM measuring a rail needs
    // no such thing -- there is only one voltage at a pin -- which is why the
    // qualifier is opt-in and a scope is what opts in.
    //
    // The list is generated from a macro rather than written out fourteen
    // times: written out, each body was four lines of which three were
    // identical, and the qualifier would have been a fourth thing to remember
    // to spell correctly per measurement -- with a typo producing not a compile
    // error but a session key nothing injects against.
    //
    // The qualifiers are deliberately the same words the Infiniium driver uses
    // ("Vbase", "RiseTime", ...), which is what makes this instrument swap
    // invisible to a recording: a run recorded against the old scope replays
    // against this one, and suite/tests inject the same keys they always did.
    // Two of that driver's keys have no counterpart here -- "Vmiddle", and any
    // trace off "Channel3"/"Channel4" -- and a recording carrying those is a
    // recording of a run this bench can no longer make.
    //
#define THORIUM_DSOX1202G_PORT( method, quantity, mode)                                          \
    template<unsigned N>                                                                         \
        requires ValidChannel<N>                                                                 \
    auto Channel<N>::method() -> core::Port<core::quantities::quantity, DSOX1202G>               \
    {                                                                                            \
        mInstrument.setChannel( N);                                                              \
        mInstrument.setMode( DSOX1202G::Mode::mode);                                             \
        return core::Port<core::quantities::quantity, DSOX1202G>{ mInstrument }.qualifiedBy( #mode); \
    }

    THORIUM_DSOX1202G_PORT( vpp,           Voltage,   Vpp)
    THORIUM_DSOX1202G_PORT( vmax,          Voltage,   Vmax)
    THORIUM_DSOX1202G_PORT( vmin,          Voltage,   Vmin)
    THORIUM_DSOX1202G_PORT( vrms,          Voltage,   Vrms)
    THORIUM_DSOX1202G_PORT( vaverage,      Voltage,   Vaverage)
    THORIUM_DSOX1202G_PORT( vbase,         Voltage,   Vbase)
    THORIUM_DSOX1202G_PORT( vtop,          Voltage,   Vtop)
    THORIUM_DSOX1202G_PORT( vamplitude,    Voltage,   Vamplitude)
    THORIUM_DSOX1202G_PORT( frequency,     Frequency, Frequency)
    THORIUM_DSOX1202G_PORT( period,        Time,      Period)
    THORIUM_DSOX1202G_PORT( positiveWidth, Time,      PositiveWidth)
    THORIUM_DSOX1202G_PORT( negativeWidth, Time,      NegativeWidth)

#undef THORIUM_DSOX1202G_PORT

    //
    // riseTime()/fallTime() are written out rather than going through the macro
    // above, because they are the two that carry a MeasureSetup: they seed the
    // usual 10%/90% thresholds up front, so a bare `.riseTime()` is a complete
    // reading.
    //
    // 10%/90% is also what this instrument does by default -- :MEASure:DEFine
    // THResholds's STANdard setting -- so the two agree, and the values are
    // stated here rather than left implicit because a threshold is part of what
    // a rise time *means*: two scopes disagreeing about it produce two
    // different numbers for one edge.
    //
    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::riseTime() -> core::Port<core::quantities::Time, DSOX1202G>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSOX1202G::Mode::RiseTime);
        return core::Port<core::quantities::Time, DSOX1202G>{ mInstrument }
                   .qualifiedBy( "RiseTime").lowThreshold( 0.1).highThreshold( 0.9);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::fallTime() -> core::Port<core::quantities::Time, DSOX1202G>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSOX1202G::Mode::FallTime);
        return core::Port<core::quantities::Time, DSOX1202G>{ mInstrument }
                   .qualifiedBy( "FallTime").lowThreshold( 0.1).highThreshold( 0.9);
    }
} // namespace hal::keysight_dsox1202g
