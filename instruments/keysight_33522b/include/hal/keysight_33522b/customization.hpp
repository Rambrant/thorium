#pragma once

#include <array>
#include <concepts>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "core/driver/describe.hpp"
#include "core/quantities/format.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

#include "hal/keysight_33522b_waveform_generator.hpp"

//
// the ADL hooks the verbs and journal find
//
// Part of hal/keysight_33522b.hpp -- include that, not this.
//

namespace hal::keysight_33522b
{
    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see
    // core/verbs/source.hpp's own comment on the applyDriver/removeDriver
    // customization points. Found via ADL because WaveformConfig lives in this
    // namespace, which is also what keeps these out of the pile every driver
    // used to share.
    //
    // One template over every shape rather than an overload apiece: the body
    // does not vary with the shape at all, which is the whole reason the shape
    // is spent inside applyWaveform().
    //
    template<typename Shape>
    auto applyDriver( const WaveformConfig<Shape> & config) -> void
    {
        config.Instrument.applyWaveform( config);
    }

    template<typename Shape>
    auto removeDriver( const WaveformConfig<Shape> & config) -> void
    {
        config.Instrument.removeOutput( config.Channel);
    }

    //
    // ADL target for the electrical interlock -- see core/verbs/interlock.hpp
    // on the isEnergised customization point. Answers whether the output this
    // config names is on at the moment a contact in its path is about to move.
    //
    // Per channel, not per instrument, and that is the difference between this
    // and the no-argument isEnabled() above: this config names one output, and
    // whether the *other* one is live says nothing about the path this
    // Connect is about to close. Through outputIsOn(), which asks the
    // instrument -- see that member's shared comment for the whole argument.
    //
    // Required of this config rather than optional, and the requirement is
    // checked: core::detail::energisedNow static_asserts it for any config
    // that has an applyDriver.
    //
    template<typename Shape>
    auto isEnergised( const WaveformConfig<Shape> & config) -> bool
    {
        return config.Instrument.outputIsOn( config.Channel);
    }

    namespace detail
    {
        //
        // "dutyCycle=25%" -- the counterpart of hal::describeSetting for a
        // setting that has no unit at all. Local to this header rather than
        // alongside the shared helpers because a percentage is not a
        // core::quantities::Quantity (there is no dimensionless one) and not a
        // count either, so neither describeSetting nor describeCount fits.
        //
        [[nodiscard]]
        inline auto describePercent( const std::string_view name, const std::optional<double> & value) -> std::string
        {
            if( !value.has_value())
            {
                return {};
            }

            return std::string( name) + "=" + io::ScpiSession::number( *value) + "%";
        }
    } // namespace detail

    //
    // ADL target for the run journal -- see core/driver/describe.hpp's own
    // comment on the describeConfig customization point, and
    // hal/driver/describe.hpp for the optional-field helpers.
    //
    // The channel and the shape are stated first and unconditionally, because
    // they are the two things this config always has and the two a reader of
    // the log needs before any of the numbers mean anything: "amplitude=3.3V"
    // says nothing about which of two connectors it came out of, or whether it
    // was a sine or a square.
    //
    template<typename Shape>
    auto describeConfig( const WaveformConfig<Shape> & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                "channel " + std::to_string( config.Channel),
                std::string( Shape::Name),
                describeSetting( "frequency", config.Frequency),
                describeSetting( "amplitude", config.Amplitude),
                describeSetting( "offset",    config.Offset),
                detail::describePercent( "dutyCycle", config.DutyCycle),
                detail::describePercent( "symmetry",  config.Symmetry),
                describeChoice( "load", config.Load)
            })
        };
    }

    //
    // No connectDriver/disconnectDriver, deliberately -- this generator's
    // outputs are hard-cabled on this rig and there is nothing to move. See
    // WaveformGenerator's own comment on why that is stated by their absence rather
    // than by a DirectWiring tag.
    //
} // namespace hal::keysight_33522b
