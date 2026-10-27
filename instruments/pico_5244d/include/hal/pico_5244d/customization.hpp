#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/driver/describe.hpp"

#include "hal/driver/describe.hpp"

#include "hal/pico_5244d_oscilloscope.hpp"

//
// The ADL hooks the verbs and the journal find on this driver's config types.
// Part of hal/pico_5244d.hpp; the same set as the DSOX1202G's, so a script's
// Setup/Arm/Await/Fetch lines read identically.
//
namespace hal::pico_5244d
{
    inline auto setupDriver( const TriggerConfig & config)     -> void { config.Instrument.configureTrigger( config); }
    inline auto setupDriver( const TimebaseConfig & config)    -> void { config.Instrument.configureTimebase( config); }
    inline auto setupDriver( const AcquisitionConfig & config) -> void { config.Instrument.configureAcquisition( config); }
    inline auto setupDriver( const ChannelConfig & config)     -> void { config.Instrument.configureChannel( config); }

    inline auto armDriver( const SingleConfig & config) -> void
    {
        config.Instrument.armSingle( config);
    }

    [[nodiscard]]
    inline auto awaitDriver( const SingleConfig & config) -> bool
    {
        return config.Instrument.awaitAcquisition( config);
    }

    [[nodiscard]]
    inline auto fetchDriver( const WaveformConfig & config) -> core::Waveform
    {
        return config.Instrument.fetchWaveform( config);
    }

    //
    // Each channel files its trace under its own key (Osc1.Channel2) for the
    // reason the DSOX1202G's does: two channels hold two records.
    //
    [[nodiscard]]
    inline auto traceQualifier( const WaveformConfig & config) -> std::string
    {
        return "Channel" + std::to_string( config.Channel);
    }

    [[nodiscard]]
    inline auto describeFactor( const std::string_view name, const std::optional<double> & value) -> std::string
    {
        if( !value.has_value())
        {
            return {};
        }

        auto digits = std::to_string( value.value());

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

    inline auto describeConfig( const TriggerConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeCount(   "trigger.source", config.EdgeSource),
                describeChoice(  "trigger.slope",  config.Slope),
                describeSetting( "trigger.level",  config.Level),
                describeChoice(  "trigger.sweep",  config.Sweep)
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
                describeChoice(  "timebase.reference",   config.Reference),
                describeCount(   "timebase.points",      config.Points)
            })
        };
    }

    inline auto describeConfig( const AcquisitionConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( { describeChoice( "acquire.resolution", config.Bits) })
        };
    }

    inline auto describeConfig( const ChannelConfig & config) -> core::SourceDescription
    {
        const auto prefix = "ch" + std::to_string( config.Channel) + ".";

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeChoice(  prefix + "coupling",  config.InputCoupling),
                describeSetting( prefix + "range",     config.Range),
                describeSetting( prefix + "offset",    config.VerticalOffset),
                describeChoice(  prefix + "bandwidth", config.BandwidthLimit),
                describeFactor(  prefix + "probe",     config.ProbeAttenuation),
                describeChoice(  prefix + "display",   config.Display)
            })
        };
    }

    inline auto describeConfig( const WaveformConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            "ch" + std::to_string( config.Channel)
        };
    }

    inline auto describeConfig( const SingleConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                "single.timeout=" + core::describeValue( config.Timeout.value_or( Oscilloscope::kDefaultCaptureTimeout)),
                "single.armTimeout=" + core::describeValue( config.ArmTimeout.value_or( Oscilloscope::kDefaultArmTimeout))
            })
        };
    }
} // namespace hal::pico_5244d
