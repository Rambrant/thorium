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

#include "hal/keysight_33522b/vocabulary.hpp"

//
// WaveformConfig + WaveformBuilder
//
// Part of hal/keysight_33522b.hpp -- include that, not this.
//

namespace hal::keysight_33522b
{
    //
    // What one Apply( Wfg1.channel<1>().sine()...) call boils down to: which
    // instrument, which output, and whichever settings were actually named.
    //
    // Channel is not optional, for the reason
    // hal::keysight_dsox1202g::ChannelConfig::Channel is not: a config that
    // does not say which output it programs is not an underspecified config,
    // it is not a config. It is carried by value rather than left on the
    // instrument, so a builder held across a later channel<M>() still programs
    // the output it named -- see Channel below on the dangling-view trap this
    // avoids.
    //
    // No Loc/AdapterPointTag: this instrument's outputs are hard-cabled on
    // this rig, so there is no point for at() to choose between. See
    // WaveformGenerator's own comment.
    //
    // DutyCycle and Symmetry sit in every shape's config even though only some
    // shapes' builders can set them. They stay nullopt for the rest -- the
    // builder is the gate, and a conditional member would buy a stricter
    // struct at the cost of a second config shape for detail::program to
    // reconcile.
    //
    template<typename Shape>
    struct WaveformConfig
    {
        WaveformGenerator &                                Instrument;
        unsigned                                   Channel{};
        std::optional<core::quantities::Frequency>  Frequency{};
        std::optional<core::quantities::Voltage>    Amplitude{};
        std::optional<core::quantities::Voltage>    Offset{};
        std::optional<double>                       DutyCycle{};
        std::optional<double>                       Symmetry{};
        std::optional<Termination>                  Load{};
    };

    //
    // The fluent chain a script builds before handing it to Apply/Remove. The
    // copy-modify-return shape is hal::ConfigBuilder's (hal/driver/builder.hpp),
    // so what is left here is one line per setting.
    //
    //     Apply( Wfg1.channel<1>().square().frequency( 10_kHz)
    //                                      .amplitude( 3.3_V)
    //                                      .dutyCycle( 25.0)
    //                                      .into( Termination::HighImpedance));
    //
    // Each setter that only some shapes have is constrained rather than
    // absent-by-specialisation, so the diagnostic names the setter and the
    // shape: .dutyCycle() on a sine is "no matching function", and
    // .frequency() on a DC level is too.
    //
    template<typename Shape>
    class WaveformBuilder : public ConfigBuilder<WaveformBuilder<Shape>, WaveformConfig<Shape>>
    {
        public:
            using Config = WaveformConfig<Shape>;

            WaveformBuilder( WaveformGenerator & instrument, const unsigned channel) :
                WaveformBuilder::ConfigBuilder( Config{ instrument, channel })
            {}

            //
            // SOURce[1|2]:FREQuency. Absent on noise and DC, which have none.
            //
            [[nodiscard]]
            auto frequency( const core::quantities::Frequency f) const requires Periodic<Shape>
            {
                return this->with( &Config::Frequency, f);
            }

            //
            // SOURce[1|2]:VOLTage -- peak to peak, in the units this driver
            // fixes at Vpp (see detail::program on why VOLT:UNIT is not a
            // setting here). Absent on DC.
            //
            [[nodiscard]]
            auto amplitude( const core::quantities::Voltage v) const requires Amplifiable<Shape>
            {
                return this->with( &Config::Amplitude, v);
            }

            //
            // SOURce[1|2]:VOLTage:OFFSet. On every shape, and on DC it is the
            // whole signal.
            //
            [[nodiscard]]
            auto offset( const core::quantities::Voltage v) const
            {
                return this->with( &Config::Offset, v);
            }

            //
            // A percentage on a 0-100 scale. The caller provides
            // core::quantities::Percentage (e.g. 25_pct) and this extracts
            // the raw value for the instrument command.
            //
            // Square and pulse only, and they reach two different commands --
            // FUNC:SQU:DCYC and FUNC:PULS:DCYC -- which is why the command is
            // on the shape tag rather than in the .cpp.
            //
            [[nodiscard]]
            auto dutyCycle( core::quantities::Percentage pct) const requires HasDutyCycle<Shape>
            {
                return this->with( &Config::DutyCycle, pct.value());
            }

            //
            // FUNC:RAMP:SYMM -- how much of each cycle the ramp spends rising.
            // 0% is a falling sawtooth, 100% a rising one, 50% a triangle.
            // Ramp only.
            //
            [[nodiscard]]
            auto symmetry( core::quantities::Percentage pct) const requires HasSymmetry<Shape>
            {
                return this->with( &Config::Symmetry, pct.value());
            }

            //
            // OUTPut[1|2]:LOAD -- what the amplitude and offset above are
            // quoted against. Named into() because that is how a bench
            // engineer says it ("3 volts into fifty ohms"), and because it
            // reads as a property of the whole chain rather than as one more
            // setpoint, which is what it is: see Termination.
            //
            [[nodiscard]]
            auto into( const Termination termination) const
            {
                return this->with( &Config::Load, termination);
            }
    };
} // namespace hal::keysight_33522b
