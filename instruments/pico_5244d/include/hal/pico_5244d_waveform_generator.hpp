#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/driver/describe.hpp"
#include "core/quantities/format.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"

#include "hal/pico_5244d_oscilloscope.hpp"

//
// The scope's built-in signal generator -- the box's other face.
//
// Part of hal/pico_5244d.hpp -- include that, not this.
//

namespace hal::pico_5244d
{
    class WaveformGenerator;

    //
    // -- The shapes -------------------------------------------------------
    //
    // Tags, for the reason hal::keysight_dsox1202g's are: a builder offers only
    // the settings its shape has, so a setting this instrument cannot make is
    // "no matching function" and not something it accepts, forgets, and applies
    // to nothing. The built-in generator has no duty cycle, no symmetry and no
    // pulse width -- a square is 50 %, a triangle is a triangle, and the two
    // ramps are separate shapes -- so none of those setters exist here.
    //
    // Function is the SDK's PS5000A_WAVE_TYPE.
    //
    struct Sine
    {
        static constexpr std::int32_t     Function = 0;
        static constexpr std::string_view Name     = "sine";
        static constexpr double           MaxHertz = 20.0e6;
    };

    struct Square
    {
        static constexpr std::int32_t     Function = 1;
        static constexpr std::string_view Name     = "square";
        static constexpr double           MaxHertz = 20.0e6;
    };

    struct Triangle
    {
        static constexpr std::int32_t     Function = 2;
        static constexpr std::string_view Name     = "triangle";
        static constexpr double           MaxHertz = 20.0e6;
        static constexpr double           FrequencyFactor = 2.0;
    };

    //
    // A rising sawtooth: climbs for the whole period and resets in one step.
    // Where the DSOX1202G says ramp().symmetry( 100%), this is rampUp().
    //
    struct RampUp
    {
        static constexpr std::int32_t     Function = 3;
        static constexpr std::string_view Name     = "rampUp";
        static constexpr double           MaxHertz = 20.0e6;
        static constexpr double           FrequencyFactor = 2.0;
    };

    struct RampDown
    {
        static constexpr std::int32_t     Function = 4;
        static constexpr std::string_view Name     = "rampDown";
        static constexpr double           MaxHertz = 20.0e6;
        static constexpr double           FrequencyFactor = 2.0;
    };

    struct Noise
    {
        // Not wave type 9: on the 5000 series noise is an "extra operation"
        // (PS5000A_WHITENOISE) on top of an ordinary wave type, and the SDK
        // refuses WHITE_NOISE as a wave type with status 46.
        static constexpr std::int32_t     Function = 0;
        static constexpr std::int32_t     Operation = 1;
        static constexpr double           NominalHertz = 1000.0;
        static constexpr std::string_view Name     = "noise";
    };

    //
    // DC is the offset with nothing else, which is how the SDK has it too: a
    // DC_VOLTAGE wave whose offset is the level.
    //
    struct Dc
    {
        static constexpr std::int32_t     Function = 8;
        static constexpr std::string_view Name     = "dc";
    };

    template<typename Shape>
    concept Periodic = requires { { Shape::MaxHertz } -> std::convertible_to<double>; };

    template<typename Shape>
    concept Amplifiable = !std::is_same_v<Shape, Dc>;

    template<typename Shape>
    struct GeneratorConfig
    {
        WaveformGenerator &                         Instrument;
        std::optional<core::quantities::Frequency>  Frequency{};
        std::optional<core::quantities::Voltage>    Amplitude{};
        std::optional<core::quantities::Voltage>    Offset{};
    };

    template<typename Shape>
    class GeneratorBuilder : public ConfigBuilder<GeneratorBuilder<Shape>, GeneratorConfig<Shape>>
    {
        public:
            using Config = GeneratorConfig<Shape>;

            explicit GeneratorBuilder( WaveformGenerator & instrument) :
                GeneratorBuilder::ConfigBuilder( Config{ instrument })
            {}

            [[nodiscard]]
            auto frequency( const core::quantities::Frequency f) const requires Periodic<Shape>
            {
                return this->with( &Config::Frequency, f);
            }

            //
            // Peak to peak, always: the unit is on the value and never a mode
            // of the instrument.
            //
            [[nodiscard]]
            auto amplitude( const core::quantities::Voltage v) const requires Amplifiable<Shape>
            {
                return this->with( &Config::Amplitude, v);
            }

            //
            // For a DC shape this is the level.
            //
            [[nodiscard]]
            auto offset( const core::quantities::Voltage v) const
            {
                return this->with( &Config::Offset, v);
            }
    };

    namespace detail
    {
        //
        // One Apply, flattened: everything the SDK call needs and nothing about
        // the shape's type. The SDK takes microvolts.
        //
        struct GeneratorProgram
        {
            std::int32_t  WaveType{ 0 };
            std::int32_t  Operation{ 0 };
            double        Hertz{ 0.0 };
            std::int32_t  OffsetMicrovolts{ 0 };
            std::uint32_t PeakToPeakMicrovolts{ 0 };
        };

        auto program( Unit & unit, const GeneratorProgram & waveform) -> void;

        // Zero volts, zero amplitude: the generator's off.
        auto silence( Unit & unit) -> void;

        [[nodiscard]]
        inline auto microvolts( const double volts) -> std::int32_t
        {
            return static_cast<std::int32_t>( std::llround( volts * 1.0e6));
        }
    } // namespace detail

    //
    // The 5244D's built-in function generator: one output, the Gen BNC beside
    // the scope's inputs, the standard shapes at up to 20 MHz and +/-2 V.
    //
    // A second face of the scope's box, the same shape as
    // hal::keysight_dsox1202g::WaveformGenerator: one address, one open unit, and
    // a row of its own on the box --
    //
    //     INSTRUMENT( Pico1, pico_5244d::Oscilloscope,      Osc2, Usb( "..."))
    //     INSTRUMENT( Pico1, pico_5244d::WaveformGenerator, Wfg3, Usb( "..."))
    //
    // -- It is a source ------------------------------------------------------
    //
    // So it can be Applied and Removed and has a place in the rig's safing:
    //
    //     Apply(  Wfg3.sine().frequency( 1_kHz).amplitude( 1_V));
    //     Remove( Wfg3.sine());
    //
    // One output, so there is no channel<N>() -- the instance is the output.
    //
    // -- What the SDK cannot say ---------------------------------------------
    //
    // The generator has no readback. The DSOX1202G's outputIsOn() asks the
    // instrument, because an output can be switched off from its front panel;
    // here there is nothing to ask -- the SDK has no query for the generator, and
    // a PicoScope has no front panel to switch it from -- so outputIsOn() is what
    // this driver last sent. Likewise there is no load setting: the output is a
    // fixed 600 Ohm source, and amplitude and offset are quoted for a high-impedance
    // load, which is what a scope input is. Into 50 Ohm the voltage is a small
    // fraction of what a script asked for.
    //
    // -- What it does not do -------------------------------------------------
    //
    // The AWG, sweeps, shots, triggering of the generator and the other built-in
    // shapes (sinc, Gaussian, half-sine, PRBS), which are one enumerator each in
    // the SDK and are not scripted anywhere yet.
    //
    // -- Limits --------------------------------------------------------------
    //
    // Enforced here, from the programmer's guide and the data sheet: frequency
    // by shape, amplitude 0 to 4 Vpp, offset +/-2 V, and -- the one that
    // matters -- the two together, |offset| + amplitude / 2 <= 2 V, because the
    // SDK clips what leaves that range and says PICO_OK.
    //
    // UNCONFIRMED ON HARDWARE until the first run on the dev desk: the frequency
    // and voltage limits are the data sheet's, and the wave-type numbers are the
    // guide's enumeration order. A value the SDK itself refuses arrives as a
    // PicoFault naming the call.
    //
    class WaveformGenerator : public InstrumentTag
    {
        public:
            //
            // The scope's own back panel: the generator is inside it.
            //
            using Buses = Oscilloscope::Buses;

            template<typename AddressT>
                requires Buses::allows<AddressT>
            WaveformGenerator( const InstrumentId id, const AddressT address) : mId( id), mConnection( id, address) {}

            [[nodiscard]] auto id() const -> InstrumentId { return mId; }

            [[nodiscard]] auto address() const -> const Address & { return mConnection.address(); }

            [[nodiscard]] auto isSimulated() const -> bool { return mConnection.isSimulated(); }

            //
            // Talk through this library instead of the vendor's -- installed on
            // the box, so the scope face at this address uses it too.
            //
            auto useApi( std::shared_ptr<Ps5000a> api) -> void
            {
                detail::installApi( mConnection, std::move( api));
            }

            auto useAddress( const Address & address) -> void
            {
                mConnection.useAddress( address);
            }

            //
            // The box's unit, opened and checked to be a 5244D on first use by
            // either face.
            //
            [[nodiscard]]
            auto identity() -> std::string;

            // Stop and release the box's unit, for every face.
            auto closeSession() -> void
            {
                detail::closeUnit( mConnection);
            }

            // --- The shapes ---

            [[nodiscard]] auto sine()     -> GeneratorBuilder<Sine>     { return GeneratorBuilder<Sine>{ *this };     }
            [[nodiscard]] auto square()   -> GeneratorBuilder<Square>   { return GeneratorBuilder<Square>{ *this };   }
            [[nodiscard]] auto triangle() -> GeneratorBuilder<Triangle> { return GeneratorBuilder<Triangle>{ *this }; }
            [[nodiscard]] auto rampUp()   -> GeneratorBuilder<RampUp>   { return GeneratorBuilder<RampUp>{ *this };   }
            [[nodiscard]] auto rampDown() -> GeneratorBuilder<RampDown> { return GeneratorBuilder<RampDown>{ *this }; }
            [[nodiscard]] auto noise()    -> GeneratorBuilder<Noise>    { return GeneratorBuilder<Noise>{ *this };    }
            [[nodiscard]] auto dc()       -> GeneratorBuilder<Dc>       { return GeneratorBuilder<Dc>{ *this };       }

            //
            // What Apply ends in. Checks the config against the model, records
            // it, and -- attached -- programs the unit.
            //
            // The SDK sets everything at once -- there is no "change only the
            // frequency" -- so what a config does not name is what this driver
            // last sent, and a setting that was never named and has no
            // sensible zero (an amplitude, a frequency) is refused rather than
            // guessed. The offset's zero is zero.
            //
            template<typename Shape>
            auto applyGenerator( const GeneratorConfig<Shape> & config) -> void
            {
                const auto frequency = config.Frequency ? config.Frequency : mFrequency;
                const auto amplitude = config.Amplitude ? config.Amplitude : mAmplitude;
                const auto offset    = config.Offset.value_or( mOffset.value_or( core::quantities::Voltage{ 0.0 }));

                check<Shape>( config, frequency, amplitude, offset);

                mFunction = Shape::Name;
                mEnabled  = true;

                if( config.Frequency) { mFrequency = config.Frequency; }
                if( config.Amplitude) { mAmplitude = config.Amplitude; }

                mOffset = offset;

                if( isSimulated())
                {
                    return;
                }

                detail::GeneratorProgram flat;

                flat.WaveType         = Shape::Function;

                if constexpr( requires { Shape::Operation; })
                {
                    flat.Operation = Shape::Operation;
                    flat.Hertz     = Shape::NominalHertz;
                }
                flat.OffsetMicrovolts = detail::microvolts( offset.value());

                if constexpr( Periodic<Shape>)
                {
                    flat.Hertz = frequency->value();

                    // The 5000D plays a triangle or ramp at half the frequency
                    // asked for (two phase-accumulator engines), so ask twice.
                    if constexpr( requires { Shape::FrequencyFactor; })
                    {
                        flat.Hertz *= Shape::FrequencyFactor;
                    }
                }

                if constexpr( Amplifiable<Shape>)
                {
                    flat.PeakToPeakMicrovolts = static_cast<std::uint32_t>( detail::microvolts( amplitude->value()));
                }

                detail::program( detail::openedUnit( mConnection, std::nullopt), flat);
            }

            auto removeOutput() -> void
            {
                mEnabled = false;

                if( isSimulated())
                {
                    return;
                }

                detail::silence( detail::openedUnit( mConnection, std::nullopt));
            }

            //
            // What this driver last turned on or off -- and, see the class
            // comment, all anyone can know: the SDK cannot be asked.
            //
            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                return mEnabled;
            }

            [[nodiscard]]
            auto outputIsOn() const -> bool
            {
                return mEnabled;
            }

            //
            // The output off, down a unit some face has already opened and never
            // opening one, and never raising on a unit that has gone: safing runs
            // after failures, and an exception here would abandon every
            // instrument safed after this one.
            //
            auto safe() -> void
            {
                mEnabled   = false;
                mFrequency = std::nullopt;
                mAmplitude = std::nullopt;
                mOffset    = std::nullopt;

                auto * const open = detail::heldUnit( mConnection);

                if( !open)
                {
                    return;
                }

                try
                {
                    detail::silence( *open);
                }
                catch( ...)
                {
                }
            }

            // --- What this driver last sent -- settings, not readings ---

            [[nodiscard]] auto function() const  -> std::string_view                         { return mFunction; }
            [[nodiscard]] auto frequency() const -> std::optional<core::quantities::Frequency> { return mFrequency; }
            [[nodiscard]] auto amplitude() const -> std::optional<core::quantities::Voltage>   { return mAmplitude; }
            [[nodiscard]] auto offset() const    -> std::optional<core::quantities::Voltage>   { return mOffset; }

            // --- The model's limits ---

            // The output swings +/-2 V about zero, whatever the shape.
            [[nodiscard]] static constexpr auto maxPeak() -> core::quantities::Voltage      { return core::quantities::Voltage{ 2.0 }; }
            [[nodiscard]] static constexpr auto maxAmplitude() -> core::quantities::Voltage { return core::quantities::Voltage{ 4.0 }; }
            [[nodiscard]] static constexpr auto minFrequency() -> core::quantities::Frequency { return core::quantities::Frequency{ 0.03 }; }

        private:
            template<typename Shape>
            auto check( const GeneratorConfig<Shape> & config,
                        const std::optional<core::quantities::Frequency> & frequency,
                        const std::optional<core::quantities::Voltage> & amplitude,
                        const core::quantities::Voltage offset) const -> void
            {
                const std::string instrument{ to_string( mId) };

                if constexpr( Periodic<Shape>)
                {
                    if( !frequency)
                    {
                        throw SettingOutOfRange( instrument, "frequency",
                            "has not been set -- the generator has no default, name one in this Apply");
                    }

                    const auto hertz = frequency->value();

                    if( !( hertz >= minFrequency().value() && hertz <= Shape::MaxHertz))
                    {
                        throw SettingOutOfRange( instrument, "frequency",
                            "of " + core::describeValue( *frequency) + " is outside a "
                            + std::string( Shape::Name) + "'s range on a PicoScope 5244D ("
                            + core::describeValue( minFrequency()) + " to "
                            + core::describeValue( core::quantities::Frequency{ Shape::MaxHertz }) + ")");
                    }
                }

                double swing = 0.0;

                if constexpr( Amplifiable<Shape>)
                {
                    if( !amplitude)
                    {
                        throw SettingOutOfRange( instrument, "amplitude",
                            "has not been set -- the generator has no default, name one in this Apply");
                    }

                    const auto volts = amplitude->value();

                    if( !( volts >= 0.0 && volts <= maxAmplitude().value()))
                    {
                        throw SettingOutOfRange( instrument, "amplitude",
                            "of " + core::describeValue( *amplitude) + " peak-to-peak is outside 0 V to "
                            + core::describeValue( maxAmplitude()));
                    }

                    swing = volts / 2.0;
                }

                const auto level = offset.value();

                if( !( std::abs( level) + swing <= maxPeak().value() + 1e-9))
                {
                    throw SettingOutOfRange( instrument, "offset",
                        "of " + core::describeValue( offset) + " with a peak of "
                        + core::describeValue( core::quantities::Voltage{ swing }) + " leaves the +/-"
                        + core::describeValue( maxPeak()) + " the output can swing, and the SDK clips what leaves it "
                        "without a status");
                }

                static_cast<void>( config);
            }

            InstrumentId   mId;

            //
            // The scope's box and unit -- shared with the oscilloscope face on the
            // same address.
            //
            BoxConnection  mConnection;

            std::string_view                            mFunction{};
            std::optional<core::quantities::Frequency>  mFrequency{};
            std::optional<core::quantities::Voltage>    mAmplitude{};
            std::optional<core::quantities::Voltage>    mOffset{};
            bool                                        mEnabled{ false };
    };

    //
    // ADL targets for core::ApplyEngine and core::RemoveEngine -- one template
    // over every shape, the body not varying with it.
    //
    template<typename Shape>
    auto applyDriver( const GeneratorConfig<Shape> & config) -> void
    {
        config.Instrument.applyGenerator( config);
    }

    template<typename Shape>
    auto removeDriver( const GeneratorConfig<Shape> & config) -> void
    {
        config.Instrument.removeOutput();
    }

    //
    // ADL target for the electrical interlock -- whether the output is on at the
    // moment a contact in its path is about to move. This driver's record, for
    // the reason WaveformGenerator::outputIsOn() gives.
    //
    template<typename Shape>
    auto isEnergised( const GeneratorConfig<Shape> & config) -> bool
    {
        return config.Instrument.outputIsOn();
    }

    //
    // ADL target for the run journal: the shape first, unconditionally, then
    // only the settings the script named.
    //
    template<typename Shape>
    auto describeConfig( const GeneratorConfig<Shape> & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                std::string( Shape::Name),
                describeSetting( "frequency", config.Frequency),
                describeSetting( "amplitude", config.Amplitude),
                describeSetting( "offset",    config.Offset)
            })
        };
    }

    //
    // No connectDriver/disconnectDriver: the Gen BNC has no relay in front of it
    // on this rig, so there is nothing to move.
    //
} // namespace hal::pico_5244d
