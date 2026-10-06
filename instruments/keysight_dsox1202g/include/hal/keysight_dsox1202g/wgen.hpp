#pragma once

#include <concepts>
#include <limits>
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
#include "hal/io/scpi.hpp"

#include "hal/keysight_dsox1202g/dsox1202g.hpp"

//
// The scope's built-in waveform generator -- the box's other face.
//
// Part of hal/keysight_dsox1202g.hpp -- include that, not this.
//

namespace hal::keysight_dsox1202g
{
    class WGEN;

    namespace detail
    {
        //
        // *IDN?, and a refusal unless the box is a DSOX1202G or 1202A -- the
        // check both of this package's faces make on the session they share.
        // With requireGenerator, only the G: the A is the same scope with no
        // generator in it, and a row naming WGEN on one is a row that can never
        // work.
        //
        auto verifyScope( io::ScpiSession & session, bool requireGenerator = false) -> std::string;
    } // namespace detail

    //
    // How the generator's Gen Out BNC is loaded -- which is the frame of
    // reference for every voltage this face quotes. The BNC's own impedance is
    // fixed at 50 Ohm; what :WGEN:OUTPut:LOAD tells the instrument is the load
    // it should *assume*, so that it displays and scales the amplitude and
    // offset for that. Told the wrong one, the voltage at the connector is
    // twice or half what the script asked for, and nothing complains.
    //
    // Named as hal::keysight_33522b::Termination names them, so a script moved
    // between the two generators keeps its into( ...) argument.
    //
    enum class Termination
    {
        Ohms50,
        HighImpedance
    };

    //
    // -- The shapes -------------------------------------------------------
    //
    // Tags, for the reason hal::keysight_33522b's are: a builder offers only
    // the settings its shape has, so .dutyCycle() on a sine is "no matching
    // function" instead of a setting this instrument would accept, remember
    // for the next square, and apply to nothing.
    //
    // Six, the instrument's own list. There is no triangle: it is a ramp with
    // 50 % symmetry, which a script says as ramp().symmetry( 50.0).
    //
    struct Sine
    {
        static constexpr std::string_view Function = "SINusoid";
        static constexpr std::string_view Name     = "sine";
        static constexpr double           MaxHertz = 20.0e6;
    };

    struct Square
    {
        static constexpr std::string_view Function         = "SQUare";
        static constexpr std::string_view Name             = "square";
        static constexpr double           MaxHertz         = 10.0e6;
        static constexpr std::string_view DutyCycleCommand = ":WGEN:FUNCtion:SQUare:DCYCle";
    };

    struct Ramp
    {
        static constexpr std::string_view Function        = "RAMP";
        static constexpr std::string_view Name            = "ramp";
        static constexpr double           MaxHertz        = 100.0e3;
        static constexpr std::string_view SymmetryCommand = ":WGEN:FUNCtion:RAMP:SYMMetry";
    };

    //
    // A pulse is set by its width, not by a duty cycle -- this instrument has
    // no duty-cycle command for it.
    //
    struct Pulse
    {
        static constexpr std::string_view Function = "PULSe";
        static constexpr std::string_view Name     = "pulse";
        static constexpr double           MaxHertz = 10.0e6;
        static constexpr bool             HasWidth = true;
    };

    struct Noise
    {
        static constexpr std::string_view Function = "NOISe";
        static constexpr std::string_view Name     = "noise";
    };

    struct Dc
    {
        static constexpr std::string_view Function = "DC";
        static constexpr std::string_view Name     = "dc";
    };

    template<typename Shape>
    concept Periodic = requires { { Shape::MaxHertz } -> std::convertible_to<double>; };

    template<typename Shape>
    concept HasDutyCycle = requires { { Shape::DutyCycleCommand } -> std::convertible_to<std::string_view>; };

    template<typename Shape>
    concept HasSymmetry = requires { { Shape::SymmetryCommand } -> std::convertible_to<std::string_view>; };

    template<typename Shape>
    concept HasWidth = requires { { Shape::HasWidth } -> std::convertible_to<bool>; };

    template<typename Shape>
    concept Amplifiable = !std::is_same_v<Shape, Dc>;

    //
    // A setting the instrument cannot produce, refused before anything is sent.
    // The same reason hal::keysight_33522b::SettingOutOfRange exists: a
    // simulated generator refuses nothing, and a real one answers an
    // out-of-range value with an error *and a clamped value*, so a script that
    // passes in CI would otherwise misbehave on the bench.
    //
    class SettingOutOfRange : public std::out_of_range
    {
        public:
            SettingOutOfRange( const std::string & instrument, const std::string_view setting,
                               const std::string & detail) :
                std::out_of_range(
                    instrument + ": " + std::string( setting) + " " + detail
                    + " -- see instruments/keysight_dsox1202g/README.md")
            {}
    };

    template<typename Shape>
    struct GeneratorConfig
    {
        WGEN &                                      Instrument;
        std::optional<core::quantities::Frequency>  Frequency{};
        std::optional<core::quantities::Voltage>    Amplitude{};
        std::optional<core::quantities::Voltage>    Offset{};
        std::optional<double>                       DutyCycle{};
        std::optional<double>                       Symmetry{};
        std::optional<core::quantities::Time>       Width{};
        std::optional<Termination>                  Load{};
    };

    template<typename Shape>
    class GeneratorBuilder : public ConfigBuilder<GeneratorBuilder<Shape>, GeneratorConfig<Shape>>
    {
        public:
            using Config = GeneratorConfig<Shape>;

            explicit GeneratorBuilder( WGEN & instrument) :
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

            [[nodiscard]]
            auto dutyCycle( const double percent) const requires HasDutyCycle<Shape>
            {
                return this->with( &Config::DutyCycle, percent);
            }

            [[nodiscard]]
            auto symmetry( const double percent) const requires HasSymmetry<Shape>
            {
                return this->with( &Config::Symmetry, percent);
            }

            [[nodiscard]]
            auto width( const core::quantities::Time t) const requires HasWidth<Shape>
            {
                return this->with( &Config::Width, t);
            }

            [[nodiscard]]
            auto into( const Termination termination) const
            {
                return this->with( &Config::Load, termination);
            }
    };

    namespace detail
    {
        //
        // One Apply, flattened: everything the wire needs and nothing about the
        // shape's type.
        //
        struct GeneratorProgram
        {
            std::string_view      Function;      // "SINusoid", ...
            std::string_view      Load;          // "FIFTy" / "ONEMeg", or empty to leave it
            std::optional<double> Hertz;
            std::optional<double> Volts;
            std::optional<double> OffsetVolts;
            std::string_view      ShapeCommand;
            std::optional<double> ShapeValue;
        };

        auto program( io::ScpiSession & session, const GeneratorProgram & program) -> void;
        auto disableOutput( io::ScpiSession & session) -> void;

        [[nodiscard]]
        auto outputIsOn( io::ScpiSession & session) -> bool;

        auto sendSafe( io::ScpiSession & session) -> void;
    } // namespace detail

    //
    // The DSOX1202G's built-in waveform generator: one output, the Gen Out BNC
    // beside the scope's inputs, 100 mHz to 20 MHz.
    //
    // A second face of the scope's box, the same shape as
    // hal::keysight_34980a::InternalDmm: one address, one session, and a row of
    // its own on the box --
    //
    //     INSTRUMENT( Scope1, keysight_dsox1202g::DSOX1202G, Osc1, Usb( "CN64504143"))
    //     INSTRUMENT( Scope1, keysight_dsox1202g::WGEN,      Wfg2, Usb( "CN64504143"))
    //
    // -- It is a source ------------------------------------------------------
    //
    // Which is what the scope face's own comment said modelling it would mean:
    // applyDriver and removeDriver, an output to be switched off, and a place in
    // the rig's safing. So unlike the scope it can be Applied and Removed:
    //
    //     Apply(  Wfg2.sine().frequency( 1_kHz).amplitude( 1_V).into( Termination::HighImpedance));
    //     Remove( Wfg2.sine());
    //
    // One output, so there is no channel<N>() -- the instance is the output,
    // where hal::keysight_33522b::Wfg33522B has two and names one at the call
    // site.
    //
    // -- What it does not do -------------------------------------------------
    //
    // Modulation (AM, FM, FSK), which is a subsystem of its own;
    // :WGEN:OUTPut:POLarity, inversion; and the high/low-level voltage pair
    // (:WGEN:VOLTage:HIGH/LOW), because amplitude and offset say the same thing
    // and a script should say it one way.
    //
    // -- Limits --------------------------------------------------------------
    //
    // Enforced here, as the 33522B's are, from the programmer's guide's own
    // numbers: frequency by shape, amplitude 10 mVpp to 2.5 Vpp into 50 Ohm (20
    // mVpp to 5 Vpp into an open circuit), square duty cycle 20-80 %, ramp
    // symmetry 0-100 %, pulse width 20 ns up to the period less 20 ns.
    //
    // The guide gives no limit for the offset. This driver assumes the same
    // thing every generator's peak limit is -- half the largest amplitude, which
    // is the most the output stage can swing either way -- and applies the
    // coupled form |offset| <= peak - amplitude / 2 only when one config names
    // both. UNCONFIRMED against the instrument: an offset it refuses arrives as
    // an hal::io::ScpiFault naming the command, not as a clamped value.
    //
    // -- On the wire ---------------------------------------------------------
    //
    // The order is the 33522B's, for the same reasons (see its README): the load,
    // because it is the frame of reference for the voltages; the function,
    // because the frequency limits depend on it; the frequency; the shape
    // parameter; the amplitude; the offset, because setting the amplitude can
    // move it; and the output on, last.
    //
    // Never :WGEN:RST on the way: it resets the load, amplitude and offset a
    // script did not name, and an Apply is meant to change only what it says.
    // Safing is where it is used, because there that is what is wanted.
    //
    // UNCONFIRMED ON HARDWARE until the first run on the dev desk. The commands
    // are the 1000 X-Series programmer's guide's :WGEN chapter, which applies to
    // G-suffix models only.
    //
    class WGEN : public InstrumentTag
    {
        public:
            //
            // The scope's own back panel: the generator is inside it.
            //
            using Buses = DSOX1202G::Buses;

            template<typename AddressT>
                requires Buses::allows<AddressT>
            WGEN( const InstrumentId id, const AddressT address) : mId( id), mConnection( id, address) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mConnection.address();
            }

            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return mConnection.isSimulated();
            }

            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void
            {
                mConnection.useTransport( std::move( transport));
            }

            auto useAddress( const Address & address) -> void
            {
                mConnection.useAddress( address);
            }

            //
            // The box's session, prepared for this face: the error queue
            // drained and the model checked to be a DSOX1202G -- the G, since
            // an A has no generator.
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &;

            [[nodiscard]]
            auto identity() -> std::string;

            auto closeSession() -> void
            {
                mConnection.close();
            }

            // --- The shapes ---

            [[nodiscard]] auto sine()   -> GeneratorBuilder<Sine>   { return GeneratorBuilder<Sine>{ *this };   }
            [[nodiscard]] auto square() -> GeneratorBuilder<Square> { return GeneratorBuilder<Square>{ *this }; }
            [[nodiscard]] auto ramp()   -> GeneratorBuilder<Ramp>   { return GeneratorBuilder<Ramp>{ *this };   }
            [[nodiscard]] auto pulse()  -> GeneratorBuilder<Pulse>  { return GeneratorBuilder<Pulse>{ *this };  }
            [[nodiscard]] auto noise()  -> GeneratorBuilder<Noise>  { return GeneratorBuilder<Noise>{ *this };  }
            [[nodiscard]] auto dc()     -> GeneratorBuilder<Dc>     { return GeneratorBuilder<Dc>{ *this };     }

            //
            // What Apply ends in. Checks the config against the model, records
            // it, and -- attached -- programs the instrument and turns the
            // output on.
            //
            template<typename Shape>
            auto applyGenerator( const GeneratorConfig<Shape> & config) -> void
            {
                const auto termination = config.Load.value_or( mLoad);

                checkAgainstModel<Shape>( config, termination);

                mFunction = Shape::Name;
                mLoad     = termination;
                mEnabled  = true;

                if( config.Frequency) { mFrequency = config.Frequency; }
                if( config.Amplitude) { mAmplitude = config.Amplitude; }
                if( config.Offset)    { mOffset    = config.Offset;    }

                if( isSimulated())
                {
                    return;
                }

                detail::program( session(), flatten<Shape>( config, termination));
            }

            auto removeOutput() -> void
            {
                mEnabled = false;

                if( isSimulated())
                {
                    return;
                }

                detail::disableOutput( session());
            }

            //
            // What this driver last turned on or off.
            //
            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                return mEnabled;
            }

            //
            // What the instrument says, which is not the same question: an
            // output can be switched off from the front panel. Asked of the
            // instrument when attached -- the electrical interlock's answer.
            //
            [[nodiscard]]
            auto outputIsOn() -> bool
            {
                if( isSimulated())
                {
                    return mEnabled;
                }

                return detail::outputIsOn( session());
            }

            //
            // The output off, and the generator back to its factory state --
            // see the .cpp. Only down a session some face has already opened,
            // and never raising on a transport that has gone: safing runs after
            // failures, and an exception here would abandon every instrument
            // safed after this one.
            //
            auto safe() -> void
            {
                mEnabled   = false;
                mAmplitude = std::nullopt;
                mOffset    = std::nullopt;

                auto * const open = mConnection.openSession();

                if( !open)
                {
                    return;
                }

                detail::sendSafe( *open);
            }

            // --- What this driver last sent -- settings, not readings ---

            [[nodiscard]]
            auto function() const -> std::string_view
            {
                return mFunction;
            }

            [[nodiscard]]
            auto frequency() const -> std::optional<core::quantities::Frequency>
            {
                return mFrequency;
            }

            [[nodiscard]]
            auto amplitude() const -> std::optional<core::quantities::Voltage>
            {
                return mAmplitude;
            }

            [[nodiscard]]
            auto offset() const -> std::optional<core::quantities::Voltage>
            {
                return mOffset;
            }

            [[nodiscard]]
            auto termination() const -> Termination
            {
                return mLoad;
            }

            // --- The model's limits ---

            [[nodiscard]]
            static constexpr auto minAmplitude( const Termination termination) -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ termination == Termination::HighImpedance ? 0.020 : 0.010 };
            }

            [[nodiscard]]
            static constexpr auto maxAmplitude( const Termination termination) -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ termination == Termination::HighImpedance ? 5.0 : 2.5 };
            }

            //
            // The most the output can swing either way: half the largest
            // amplitude. An assumption -- see this class's comment.
            //
            [[nodiscard]]
            static constexpr auto maxPeak( const Termination termination) -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ maxAmplitude( termination).value() / 2.0 };
            }

            [[nodiscard]]
            static constexpr auto minFrequency() -> core::quantities::Frequency
            {
                return core::quantities::Frequency{ 0.1 };
            }

            [[nodiscard]]
            static constexpr auto minPulseWidth() -> core::quantities::Time
            {
                return core::quantities::Time{ 20.0e-9 };
            }

        private:
            template<typename Shape>
            auto checkAgainstModel( const GeneratorConfig<Shape> & config, const Termination termination) const -> void
            {
                const std::string instrument{ to_string( mId) };

                if constexpr( Periodic<Shape>)
                {
                    if( config.Frequency)
                    {
                        const auto hertz = config.Frequency->value();

                        if( hertz < minFrequency().value() || hertz > Shape::MaxHertz)
                        {
                            throw SettingOutOfRange( instrument, "frequency",
                                "of " + core::describeValue( *config.Frequency) + " is outside a "
                                + std::string( Shape::Name) + "'s range on a DSOX1202G ("
                                + core::describeValue( minFrequency()) + " to "
                                + core::describeValue( core::quantities::Frequency{ Shape::MaxHertz }) + ")");
                        }
                    }
                }

                if constexpr( Amplifiable<Shape>)
                {
                    if( config.Amplitude)
                    {
                        const auto volts = config.Amplitude->value();

                        if( volts < minAmplitude( termination).value() || volts > maxAmplitude( termination).value())
                        {
                            throw SettingOutOfRange( instrument, "amplitude",
                                "of " + core::describeValue( *config.Amplitude) + " peak-to-peak is outside "
                                + core::describeValue( minAmplitude( termination)) + " to "
                                + core::describeValue( maxAmplitude( termination)) + " "
                                + std::string( describeTermination( termination)));
                        }
                    }
                }

                if( config.Offset)
                {
                    const auto offset = config.Offset->value();
                    const auto peak   = maxPeak( termination).value();

                    if( offset < -peak || offset > peak)
                    {
                        throw SettingOutOfRange( instrument, "offset",
                            "of " + core::describeValue( *config.Offset) + " is outside +-"
                            + core::describeValue( maxPeak( termination)) + " "
                            + std::string( describeTermination( termination)));
                    }

                    if constexpr( Amplifiable<Shape>)
                    {
                        if( config.Amplitude)
                        {
                            const auto headroom = peak - config.Amplitude->value() / 2.0;

                            if( offset < -headroom || offset > headroom)
                            {
                                throw SettingOutOfRange( instrument, "offset",
                                    "of " + core::describeValue( *config.Offset) + " leaves no headroom for a "
                                    + core::describeValue( *config.Amplitude) + " peak-to-peak amplitude "
                                    + std::string( describeTermination( termination)) + " -- at most "
                                    + core::describeValue( core::quantities::Voltage{ headroom }));
                            }
                        }
                    }
                }

                if constexpr( HasDutyCycle<Shape>)
                {
                    checkPercent( instrument, "duty cycle", config.DutyCycle, 20.0, 80.0);
                }

                if constexpr( HasSymmetry<Shape>)
                {
                    checkPercent( instrument, "symmetry", config.Symmetry, 0.0, 100.0);
                }

                if constexpr( HasWidth<Shape>)
                {
                    if( config.Width)
                    {
                        const auto width = config.Width->value();

                        //
                        // The upper bound is the period, and only a config
                        // that names both frequency and width has one to
                        // check against -- otherwise the frequency is
                        // whatever the instrument holds.
                        //
                        const auto longest = config.Frequency
                            ? 1.0 / config.Frequency->value() - minPulseWidth().value()
                            : std::numeric_limits<double>::infinity();

                        if( width < minPulseWidth().value() || width > longest)
                        {
                            throw SettingOutOfRange( instrument, "pulse width",
                                "of " + core::describeValue( *config.Width) + " is outside "
                                + core::describeValue( minPulseWidth()) + " to the period less "
                                + core::describeValue( minPulseWidth()));
                        }
                    }
                }
            }

            static auto checkPercent( const std::string & instrument, const std::string_view setting,
                                      const std::optional<double> & value, const double low, const double high) -> void
            {
                if( !value || ( *value >= low && *value <= high))
                {
                    return;
                }

                throw SettingOutOfRange( instrument, setting,
                    "of " + io::ScpiSession::number( *value) + "% is outside "
                    + io::ScpiSession::number( low) + "% to " + io::ScpiSession::number( high) + "%");
            }

            template<typename Shape>
            [[nodiscard]]
            static auto flatten( const GeneratorConfig<Shape> & config, const Termination termination) -> detail::GeneratorProgram
            {
                detail::GeneratorProgram flat;

                flat.Function = Shape::Function;

                if( config.Load)
                {
                    flat.Load = termination == Termination::HighImpedance ? "ONEMeg" : "FIFTy";
                }

                if( config.Frequency) { flat.Hertz       = config.Frequency->value(); }
                if( config.Amplitude) { flat.Volts       = config.Amplitude->value(); }
                if( config.Offset)    { flat.OffsetVolts = config.Offset->value();    }

                if constexpr( HasDutyCycle<Shape>)
                {
                    if( config.DutyCycle)
                    {
                        flat.ShapeCommand = Shape::DutyCycleCommand;
                        flat.ShapeValue   = config.DutyCycle;
                    }
                }

                if constexpr( HasSymmetry<Shape>)
                {
                    if( config.Symmetry)
                    {
                        flat.ShapeCommand = Shape::SymmetryCommand;
                        flat.ShapeValue   = config.Symmetry;
                    }
                }

                if constexpr( HasWidth<Shape>)
                {
                    if( config.Width)
                    {
                        flat.ShapeCommand = ":WGEN:FUNCtion:PULSe:WIDTh";
                        flat.ShapeValue   = config.Width->value();
                    }
                }

                return flat;
            }

            [[nodiscard]]
            static constexpr auto describeTermination( const Termination termination) -> std::string_view
            {
                return termination == Termination::HighImpedance ? "into a high-impedance load" : "into 50 Ohm";
            }

            InstrumentId   mId;

            //
            // The scope's box and session -- shared with DSOX1202G on the same
            // address, as a second family that prepares for itself.
            //
            BoxConnection  mConnection;

            //
            // The factory load is 1 MOhm (:WGEN:RST), which is also this
            // driver's idea of "not yet told".
            //
            std::string_view                            mFunction{};
            std::optional<core::quantities::Frequency>  mFrequency{};
            std::optional<core::quantities::Voltage>    mAmplitude{};
            std::optional<core::quantities::Voltage>    mOffset{};
            Termination                                 mLoad{ Termination::HighImpedance };
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
    // ADL target for the electrical interlock -- whether the output is on at
    // the moment a contact in its path is about to move. Asked of the
    // instrument, for the reason WGEN::outputIsOn() gives.
    //
    template<typename Shape>
    auto isEnergised( const GeneratorConfig<Shape> & config) -> bool
    {
        return config.Instrument.outputIsOn();
    }

    namespace detail
    {
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
                describeSetting( "offset",    config.Offset),
                detail::describePercent( "dutyCycle", config.DutyCycle),
                detail::describePercent( "symmetry",  config.Symmetry),
                describeSetting( "width", config.Width),
                describeChoice( "load", config.Load)
            })
        };
    }

    //
    // No connectDriver/disconnectDriver: Gen Out is a BNC with no relay in front
    // of it on this rig, so there is nothing to move.
    //
} // namespace hal::keysight_dsox1202g
