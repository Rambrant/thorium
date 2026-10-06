#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/driver/describe.hpp"

#include "hal/driver/describe.hpp"

#include "hal/keysight_dsox1202g/dsox1202g.hpp"

//
// The free functions the verbs find by ADL -- setupDriver, armDriver,
// awaitDriver, fetchDriver, traceQualifier -- and the describeConfig
// overloads the run journal renders each config with. One place, so that
// "what can a verb do with this scope" is answered by reading one file.
//
// Part of hal/keysight_dsox1202g.hpp -- include that, not this.
//

namespace hal::keysight_dsox1202g
{
    //
    // ---------------------------------------------------------------------
    // ADL customization points
    // ---------------------------------------------------------------------
    //
    // Four setupDriver overloads, one per config type -- see core/verbs/source.hpp
    // on why configuring is a verb of its own rather than a flavour of Apply.
    //
    // Note what this driver deliberately does NOT define: applyDriver and
    // removeDriver. A scope has no output to energise -- there is nothing an
    // Apply( Osc1.trigger()) could mean -- so Apply on this instrument is "no
    // matching function" at compile time, exactly the way it is on
    // hal::racal1260::Racal1260's serial port. The absence is the design.
    //
    // That is worth one extra line on this model specifically, because it is
    // the one claim here that a datasheet could be read as contradicting: the
    // G in DSOX1202G is a built-in 20 MHz waveform generator, which genuinely
    // is an output that energises things. It is not modelled (see this
    // driver's own comment on what is deferred), so this remains true of the
    // driver; the day somebody models it, this instrument grows applyDriver and
    // a place in rig/wiring.inc's safing order, and stops being passive.
    //
    // Nor connectDriver/disconnectDriver: this scope reaches its DUT point
    // through the matrix, and Measure closes and reopens that route around each
    // reading (see core::MeasureEngine). A script therefore never Connects it
    // -- it names the point at the Measure call instead.
    //
    inline auto setupDriver( const TriggerConfig & config) -> void
    {
        config.Instrument.configureTrigger( config);
    }

    inline auto setupDriver( const TimebaseConfig & config) -> void
    {
        config.Instrument.configureTimebase( config);
    }

    inline auto setupDriver( const AcquisitionConfig & config) -> void
    {
        config.Instrument.configureAcquisition( config);
    }

    inline auto setupDriver( const ChannelConfig & config) -> void
    {
        config.Instrument.configureChannel( config);
    }

    //
    // ADL targets for core::ArmEngine and core::AwaitEngine -- the
    // triggered-acquisition pair, see core/verbs/acquire.hpp for why they are
    // two verbs and why Arm's post-condition is "armed and ready" rather than
    // "told to arm".
    //
    inline auto armDriver( const SingleConfig & config) -> void
    {
        config.Instrument.armSingle( config);
    }

    [[nodiscard]]
    inline auto awaitDriver( const SingleConfig & config) -> bool
    {
        return config.Instrument.awaitAcquisition( config);
    }

    //
    // ADL target for core::FetchEngine -- the trace verb, see core/verbs/trace.hpp.
    //
    [[nodiscard]]
    inline auto fetchDriver( const WaveformConfig & config) -> core::Waveform
    {
        return config.Instrument.fetchWaveform( config);
    }

    //
    // Which session slot a trace off this instrument files under, appended to
    // the instrument id: "Osc1.Channel2".
    //
    // Present because this scope has to have it. Two channels hold two records
    // at once, and the default "Osc1.Trace" would give both one slot -- so a
    // test injecting a channel-1 trace would find a channel-2 Fetch taking it,
    // and a recording of a run that captured both would replay them into each
    // other. Two is fewer than the Infiniium's four and changes nothing about
    // the argument: one slot for two records is already one too few.
    //
    [[nodiscard]]
    inline auto traceQualifier( const WaveformConfig & config) -> std::string
    {
        return "Channel" + std::to_string( config.Channel);
    }

    //
    // A probe ratio as a log fragment: "10x", "0.1x".
    //
    // Written here rather than reached for in hal/driver/describe.hpp because
    // neither helper there fits, and the reason is the same one that made this
    // a double in the first place. describeSetting takes a
    // core::quantities::Quantity and a ratio has no unit; describeCount is
    // constrained to integrals and 0.1 is a legal attenuation. So this driver
    // renders its own, and renders it the way a probe is labelled -- 10x, not
    // 10.000000 -- since that is the number written on the switch the operator
    // slid.
    //
    [[nodiscard]]
    inline auto describeAttenuation( const std::string_view name, const std::optional<double> & value) -> std::string
    {
        if( !value.has_value())
        {
            return {};
        }

        auto digits = std::to_string( value.value());

        //
        // Trailing zeros go, but only from a fractional part, and the guard is
        // not defensive padding -- it is a bug this file already had. What
        // std::to_string does to a double is not fixed across standard
        // versions: it used to be printf's %f, so 10.0 arrived as "10.000000"
        // and needed trimming, and it is now the shortest round-trip form, so
        // 10.0 arrives as "10" and trimming it turns a 10:1 probe into a 1:1
        // one. Stripping only past a '.' is correct under both.
        //
        if( digits.contains( '.'))
        {
            while( !digits.empty() && digits.back() == '0')
            {
                digits.pop_back();
            }

            if( !digits.empty() && digits.back() == '.')
            {
                digits.pop_back();
            }
        }

        return std::string( name) + "=" + digits + "x";
    }

    //
    // ADL targets for the run journal -- see core/driver/describe.hpp's own
    // comment on the describeConfig customization point.
    //
    // Field by field, and only the fields that were set, for the reason
    // hal::describeSetting exists: a Setup that named only the trigger level is
    // a different instruction from one that named the whole trigger, and a
    // rendering that filled in the rest would be inventing settings the script
    // never chose.
    //
    inline auto describeConfig( const TriggerConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeCount(   "trigger.source",   config.EdgeSource),
                describeChoice(  "trigger.slope",    config.Slope),
                describeSetting( "trigger.level",    config.Level),
                describeChoice(  "trigger.sweep",    config.Sweep),
                describeChoice(  "trigger.coupling", config.Coupling),
                describeChoice(  "trigger.reject",   config.Reject),
                describeSetting( "trigger.holdoff",  config.Holdoff)
            })
        };
    }

    inline auto describeConfig( const TimebaseConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeSetting( "timebase.perDivision", config.TimePerDivision),
                describeSetting( "timebase.position",    config.Position),
                describeChoice(  "timebase.reference",   config.Reference)
            })
        };
    }

    //
    // Two fields, and the averaging count rendered only when averaging is what
    // was selected. A count beside "type=HighResolution" would be a number the
    // instrument is not using, which is worse than no number: a reader
    // diagnosing a noisy capture would spend time on it.
    //
    inline auto describeConfig( const AcquisitionConfig & config) -> core::SourceDescription
    {
        const auto averaging = config.Type == AcquisitionType::Averaged;

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeChoice( "acquire.type",     config.Type),
                averaging ? describeCount( "acquire.averages", config.AverageCount) : std::string{}
            })
        };
    }

    inline auto describeConfig( const ChannelConfig & config) -> core::SourceDescription
    {
        //
        // Every fragment carries the channel it belongs to, rather than the
        // channel being named once at the front. Two Setups on two channels
        // produce two log lines against the same InstrumentId, and a reader
        // scanning for "which channel was set to 100 mV/div" should not have to
        // carry a prefix in their head from the start of the line.
        //
        const auto prefix = "ch" + std::to_string( config.Channel) + ".";

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeChoice(      prefix + "coupling",  config.InputCoupling),
                describeSetting(     prefix + "perDiv",    config.VoltsPerDivision),
                describeSetting(     prefix + "offset",    config.VerticalOffset),
                describeChoice(      prefix + "bandwidth", config.BandwidthLimit),
                describeAttenuation( prefix + "probe",     config.ProbeAttenuation),
                describeChoice(      prefix + "display",   config.Display)
            })
        };
    }

    //
    // A trace's own line, which says only which channel it came off -- there is
    // nothing else in the config (see WaveformConfig on why). What the trace
    // *was* is the value column, and Fetch fills that with a summary rather
    // than the samples; see core::describeValue for a core::Waveform.
    //
    inline auto describeConfig( const WaveformConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            "ch" + std::to_string( config.Channel)
        };
    }

    inline auto describeConfig( const SingleConfig & config) -> core::SourceDescription
    {
        //
        // The defaults are rendered when the script did not name a timeout,
        // which is the one place in this file where an unset field is filled in
        // for the log. That is deliberate and specific to these two: a timeout
        // is the number that decides how a *failing* capture behaves, so a log
        // of a run that timed out has to say what it was waiting for, and "the
        // driver's default" is not an answer anyone reading a report at 2am can
        // act on.
        //
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                "single.timeout=" + core::describeValue( config.Timeout.value_or( DSOX1202G::kDefaultCaptureTimeout)),
                "single.armTimeout=" + core::describeValue( config.ArmTimeout.value_or( DSOX1202G::kDefaultArmTimeout))
            })
        };
    }
} // namespace hal::keysight_dsox1202g
