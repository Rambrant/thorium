#pragma once

#include <array>
#include <type_traits>

#include "core/port.hpp"
#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"

#include "hal/instrument.hpp"

namespace hal
{
    class DSO8064;

    //
    // Bounds a channel number to this instrument's real hardware -- a real
    // DSO8064 has four physical input channels, so DSO8064Channel<5> (or
    // <0>) simply has no valid instantiation. Kept as its own concept
    // (rather than inlined into DSO8064Channel's own template) so the
    // bound lives in exactly one place and reads at the call site as "this
    // channel number, checked against this instrument" rather than a bare
    // arithmetic condition.
    //
    template<unsigned N>
    concept ValidDso8064Channel = ( N >= 1 && N <= 4);

    //
    // One of the DSO8064's four physical input channels, narrowed to at
    // compile time via DSO8064::channel<N>() -- resolves the "Channel
    // selection" item this file's older comment deferred, the same way
    // DcP1..DcP4 resolve N6701A's four module slots: a real hardware axis
    // modeled as its own compile-time fact rather than an implicit,
    // whichever-was-last-selected mode.
    //
    // Deliberately a thin, transient view over DSO8064&, never itself
    // retained by anything returned from it: every measurement method
    // (vpp(), riseTime(), ...) records N onto the real DSO8064 instance --
    // via setChannel(), the same way it already records which mode via
    // setMode() -- and then hands back a core::Port<Q, DSO8064> bound
    // directly to that real, singular instrument, not to this view. That
    // is deliberate and load-bearing: an earlier version of this file had
    // Port referencing DSO8064Channel<N> itself, which meant a Port
    // obtained via `osc1.channel<3>().vmax()` and then stored past the
    // full expression that created it held a reference to a temporary
    // that no longer existed -- silent dangling-reference UB, caught by a
    // test, not by the compiler. Binding Port straight to DSO8064& removes
    // the dangling risk entirely, at the cost of channel now being
    // instrument-level mutable state -- but that is exactly the same
    // accepted sharp edge Mode already has (see DSO8064's own comment): a
    // Port handle read after a later channel<M>() switch reads whichever
    // channel is current at rawMeasure() time, not the channel selected
    // when the handle was obtained. One documented sharp edge is a better
    // trade than a second, worse, undocumented one.
    //
    // None of this affects the intended call shapes: `osc1.channel<3>()`
    // kept alive as a named local so setCoupling()/setVerticalScale()/etc
    // (once those exist) and vmax() all agree on which channel, exactly
    // the way N6701ABuilder already works; or `Measure( osc1.channel<3>()
    // .vmax(), at(...))` chained straight through in one expression. Both
    // still read channel-first: `osc1.channel<3>().vmax()`, not
    // `osc1.vmax().channel<3>()`.
    //
    template<unsigned N>
        requires ValidDso8064Channel<N>
    class DSO8064Channel
    {
        public:
            static constexpr unsigned Number = N;

            explicit DSO8064Channel( DSO8064 & instrument) : mInstrument( instrument) {}

            //
            // The amplitude family -- Vpp, Vmax, Vmin, Vrms, Vaverage --
            // exactly hal::DSO8064's old top-level methods, just now
            // channel-scoped. Each records both N (setChannel) and its own
            // mode (setMode) onto the real instrument before returning a
            // Port bound to that same instrument -- see this class's own
            // comment for why Port never references DSO8064Channel itself.
            //
            [[nodiscard]] auto vpp()      -> core::Port<core::quantities::Voltage, DSO8064>;
            [[nodiscard]] auto vmax()     -> core::Port<core::quantities::Voltage, DSO8064>;
            [[nodiscard]] auto vmin()     -> core::Port<core::quantities::Voltage, DSO8064>;
            [[nodiscard]] auto vrms()     -> core::Port<core::quantities::Voltage, DSO8064>;
            [[nodiscard]] auto vaverage() -> core::Port<core::quantities::Voltage, DSO8064>;

            //
            // The timing family -- the other item this file's older
            // comment deferred. frequency()/period() need nothing beyond
            // "which channel"; riseTime()/fallTime() also carry the usual
            // 10%/90% edge-timing thresholds as a MeasureSetup, defaulted
            // here so a bare `.riseTime()` is still a complete, valid
            // reading, and overridable via the same chained-builder
            // spelling as core::Port's range()/nplc() --
            // `.riseTime().lowThreshold( 0.2).highThreshold( 0.8)`.
            //
            // Still not modeled: pulse width and duty cycle -- same shape
            // as period()/frequency() (a Time/dimensionless result, no
            // extra setup), left for whoever adds the next one to follow
            // rather than speculatively built ahead of a real need.
            //
            [[nodiscard]] auto frequency() -> core::Port<core::quantities::Frequency, DSO8064>;
            [[nodiscard]] auto period()    -> core::Port<core::quantities::Time, DSO8064>;
            [[nodiscard]] auto riseTime()  -> core::Port<core::quantities::Time, DSO8064>;
            [[nodiscard]] auto fallTime()  -> core::Port<core::quantities::Time, DSO8064>;

        private:
            DSO8064 & mInstrument;
    };

    //
    // Agilent/Keysight Infiniium DSO8064: this rig's actual scope, replacing
    // the old generic hal::Oscilloscope placeholder (see that class's own
    // comment, still in hal/instrument.hpp, for why it existed) now that the
    // real model is known -- the same retirement hal::L4411A gave the old
    // generic hal::Dmm.
    //
    // Models the amplitude-family subset of the DSO8064's :MEASure:V...
    // automatic-measurement subsystem -- Vpp, Vmax, Vmin, Vrms, Vaverage --
    // plus the timing family -- Frequency, Period, RiseTime, FallTime --
    // both channel-scoped via channel<N>()/DSO8064Channel above. Calling
    // one of the Mode-tagged methods there still switches the instrument's
    // current measurement mode *and* channel, the same way a real
    // Infiniium's front-panel Measure menu switches which readout (and
    // which channel) is active before a reading is taken, mirroring
    // hal::L4411A's own Mode-switch pattern. Same accepted sharp edge as
    // hal::L4411A's AC/DC mode: a port handle read after a later mode or
    // channel switch reads whichever mode/channel is current at
    // rawMeasure() time, not whichever was active when the handle was
    // obtained -- harmless for Measure(port, at(...))'s
    // read-immediately-and-discard usage.
    //
    // Still deliberately deferred, pending the compile-time instrument-
    // alias/named-port design mentioned in the project's "on the horizon"
    // notes:
    //   - Trigger/timebase/acquire-mode setup (:TRIGGER:..., :ACQUIRE:...,
    //     Waveform.HorizontalScale in the old Idefix vocabulary) --
    //     instrument-wide, not per-channel, so it belongs directly on
    //     DSO8064, not on DSO8064Channel.
    //   - Pulse width and duty cycle -- see DSO8064Channel's own comment.
    //
    class DSO8064
    {
        public:
            static constexpr unsigned channel_count = 4;

            enum class Mode { Vpp, Vmax, Vmin, Vrms, Vaverage, Frequency, Period, RiseTime, FallTime };

            explicit DSO8064( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            //
            // Narrows to one of the instrument's four physical channels --
            // channel<5>() or channel<0>() simply has no valid
            // instantiation (ValidDso8064Channel), a hard compile error,
            // not a runtime range check. See DSO8064Channel's own comment
            // for why this returns a transient view rather than
            // transferring ownership, and for why Port never ends up
            // referencing that view.
            //
            template<unsigned N>
                requires ValidDso8064Channel<N>
            [[nodiscard]]
            auto channel() -> DSO8064Channel<N>
            {
                return DSO8064Channel<N>{ *this };
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
            }

            [[nodiscard]]
            auto channelNumber() const -> unsigned
            {
                return mChannel;
            }

            //
            // Nothing to do -- a scope is passive, exactly as hal::L4411A
            // is; see that class's safe() for why this is written out as an
            // explicit empty body rather than simply left absent.
            //
            // Note this deliberately does not reset mMode/mChannel. They
            // are instrument state a script set (see this class's own
            // comment on the mode-switch-on-the-instrument pattern), not
            // anything that can energise the DUT -- and safing runs when a
            // script has already died, so there is nobody left to surprise
            // with a mode change. Resetting them would only discard the
            // last thing the scope was told to look at, which is the one
            // piece of state worth still being able to read afterwards.
            //
            auto safe() -> void
            {
            }

            // Switches the instrument's current measurement mode/channel --
            // called by DSO8064Channel's builder methods (vpp()/vmax()/.../
            // riseTime()/...), never by a script directly.
            auto setMode( const Mode mode) -> void
            {
                mMode = mode;
            }

            auto setChannel( const unsigned channel) -> void
            {
                mChannel = channel;
            }

            // Test/simulation hooks -- real hardware has no such setters.
            // Channel is a plain runtime unsigned here (1-4): this is test
            // scaffolding setting up canned data, not the compile-time-
            // checked script-facing surface channel<N>() provides.
            auto setSimulatedVpp( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vpp = v;
            }

            auto setSimulatedVmax( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vmax = v;
            }

            auto setSimulatedVmin( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vmin = v;
            }

            auto setSimulatedVrms( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vrms = v;
            }

            auto setSimulatedVaverage( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vaverage = v;
            }

            auto setSimulatedFrequency( const unsigned channel, const core::quantities::Frequency f) -> void
            {
                atChannel( channel).Freq = f;
            }

            auto setSimulatedPeriod( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).Period = t;
            }

            auto setSimulatedRiseTime( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).RiseTime = t;
            }

            auto setSimulatedFallTime( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).FallTime = t;
            }

            //
            // The one rawMeasure() Port<Q, DSO8064> actually calls --
            // mChannel/mMode are already instrument state by the time this
            // runs (set by whichever DSO8064Channel<N> builder method
            // produced the Port), so this needs no channel argument of its
            // own, exactly the same shape hal::L4411A's rawMeasure() has.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & ) -> QuantityT
            {
                const auto & data = atChannel( mChannel);

                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    switch( mMode)
                    {
                        case Mode::Vpp:      return data.Vpp;
                        case Mode::Vmax:     return data.Vmax;
                        case Mode::Vmin:     return data.Vmin;
                        case Mode::Vrms:     return data.Vrms;
                        case Mode::Vaverage: return data.Vaverage;
                        default:             return data.Vpp;
                    }
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    return data.Freq;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Time>)
                {
                    switch( mMode)
                    {
                        case Mode::Period:   return data.Period;
                        case Mode::RiseTime: return data.RiseTime;
                        case Mode::FallTime: return data.FallTime;
                        default:             return data.Period;
                    }
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "DSO8064 has no port for this quantity");
                }
            }

        private:
            struct ChannelData
            {
                core::quantities::Voltage    Vpp{};
                core::quantities::Voltage    Vmax{};
                core::quantities::Voltage    Vmin{};
                core::quantities::Voltage    Vrms{};
                core::quantities::Voltage    Vaverage{};
                core::quantities::Frequency  Freq{};
                core::quantities::Time       Period{};
                core::quantities::Time       RiseTime{};
                core::quantities::Time       FallTime{};
            };

            [[nodiscard]]
            auto atChannel( const unsigned channel) -> ChannelData &
            {
                return mChannels.at( channel - 1);
            }

            InstrumentId               mId;
            Mode                       mMode{ Mode::Vpp};
            unsigned                   mChannel{ 1};
            std::array<ChannelData, 4> mChannels;
    };

    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::vpp() -> core::Port<core::quantities::Voltage, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::Vpp);
        return core::Port<core::quantities::Voltage, DSO8064>{ mInstrument };
    }

    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::vmax() -> core::Port<core::quantities::Voltage, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::Vmax);
        return core::Port<core::quantities::Voltage, DSO8064>{ mInstrument };
    }

    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::vmin() -> core::Port<core::quantities::Voltage, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::Vmin);
        return core::Port<core::quantities::Voltage, DSO8064>{ mInstrument };
    }

    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::vrms() -> core::Port<core::quantities::Voltage, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::Vrms);
        return core::Port<core::quantities::Voltage, DSO8064>{ mInstrument };
    }

    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::vaverage() -> core::Port<core::quantities::Voltage, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::Vaverage);
        return core::Port<core::quantities::Voltage, DSO8064>{ mInstrument };
    }

    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::frequency() -> core::Port<core::quantities::Frequency, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::Frequency);
        return core::Port<core::quantities::Frequency, DSO8064>{ mInstrument };
    }

    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::period() -> core::Port<core::quantities::Time, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::Period);
        return core::Port<core::quantities::Time, DSO8064>{ mInstrument };
    }

    //
    // riseTime()/fallTime() seed the usual 10%/90% thresholds up front, so
    // a bare `.riseTime()` is a complete reading -- same "still valid, if
    // underspecified" shape as N6701ABuilder's voltage()/currentLimit(),
    // just pre-filled here rather than left at nullopt, since 10%/90% is
    // the de-facto default on real scopes rather than genuinely optional.
    //
    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::riseTime() -> core::Port<core::quantities::Time, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::RiseTime);
        return core::Port<core::quantities::Time, DSO8064>{ mInstrument }.lowThreshold( 0.1).highThreshold( 0.9);
    }

    template<unsigned N>
        requires ValidDso8064Channel<N>
    auto DSO8064Channel<N>::fallTime() -> core::Port<core::quantities::Time, DSO8064>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064::Mode::FallTime);
        return core::Port<core::quantities::Time, DSO8064>{ mInstrument }.lowThreshold( 0.1).highThreshold( 0.9);
    }
} // namespace hal
