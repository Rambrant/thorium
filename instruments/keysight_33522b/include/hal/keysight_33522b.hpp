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
#include "hal/driver/api_version.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when
// it moves, and instruments/README.md for why a driver package has to say this
// at all (it travels separately from the hal it compiles against).
//
// Three, because this driver opens a real session and so needs hal/io/ -- the
// same number the meter, the scope and the supply ask for.
//
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it.
//
// -- Why the directory says 33522b and not 33500b -------------------------
//
// "33500B" is a *series*, not a model, and the eight members of it differ in
// exactly the three things this driver has to get right:
//
//     33509B   20 MHz   1 channel   no arb
//     33510B   20 MHz   2 channels  no arb
//     33511B   20 MHz   1 channel   arb
//     33512B   20 MHz   2 channels  arb
//     33519B   30 MHz   1 channel   no arb
//     33520B   30 MHz   2 channels  no arb
//     33521B   30 MHz   1 channel   arb
//     33522B   30 MHz   2 channels  arb
//
// (Keysight Trueform Series Operating and Service Guide, "Models and
// Options".) They share one command set and one guide, which is exactly what
// makes a series-wide name tempting and wrong: a driver called keysight_33500b
// would have to either believe 30 MHz on a 20 MHz box or believe 20 MHz on a
// 30 MHz one, and would let a rig write down channel<2>() on a single-channel
// model. So the package names the model on the bench -- the same call
// keysight_edu36311a makes about its E36311A sibling, from the same
// programming guide, for the same reason.
//
// If a second member of the series turns up, the move is a second package
// (they are three constants apart, and a package here is meant to be cheap),
// not a template parameter added to this one.
//
// -- Why the class carries a Wfg prefix -----------------------------------
//
// "33522B" is not a C++ identifier. hal::keysight_ac6834b::Ac6834B has the
// same problem and solved it the same way: a short prefix saying what kind of
// box it is, and then the model number exactly as the badge spells it.
//
// The manufacturer token is keysight even though this unit very likely says
// Agilent on the front -- the series launched as an Agilent product and the
// company is Keysight now. That is instruments/README.md's rule (the 34980A
// and the Ac6834B are the same case), and it is also why verifyIdentity()
// below checks the *model* field of *IDN? and not the manufacturer one: both
// badges answer with the same model number, and either is this instrument.
//
namespace hal::keysight_33522b
{
    class Wfg33522B;

    //
    // -- Which of the two outputs -------------------------------------------
    //
    // A 33522B has two independent front-panel channel connectors. The bound
    // is compile-time, so channel<3>() -- or channel<2>() written for this
    // model and then re-pointed at a 33521B -- stops compiling rather than
    // failing on the wire. Same mechanism, and the same reasoning, as
    // hal::keysight_dsox1202g::ValidChannel.
    //
    template<unsigned N>
    concept ValidChannel = ( N >= 1 && N <= 2);

    //
    // -- What termination the amplitude and offset are quoted against -------
    //
    // OUTPut[1|2]:LOAD. Not a property of the signal, and not something this
    // instrument measures: it is the load impedance the *user* tells the
    // instrument is attached, and every amplitude and offset the instrument
    // accepts or reports is expressed in terms of it. The guide is explicit
    // that changing it re-scales the numbers by two without changing a volt at
    // the connector: "If the amplitude is 10 Vpp and you change the output
    // termination setting from 50 Ohm to high impedance, the displayed
    // amplitude doubles to 20 Vpp."
    //
    // So it belongs in the config beside the amplitude rather than being a
    // rig-level constant: a script asking for 2 Vpp is asking for a different
    // signal depending on this, and the two settings have to travel together
    // or the second one means something the script did not say.
    //
    // Two values, where the instrument accepts 1 Ohm to 10 kOhm as well.
    // Deliberate, and the reason is the range checks below rather than
    // convenience: the amplitude and offset limits this driver enforces are
    // stated in the guide for exactly these two cases (10 Vpp / +-5 V into
    // 50 Ohm, 20 Vpp / +-10 V into an open circuit) and interpolating them for
    // 75 Ohm would be this driver inventing a limit the guide does not give.
    // A rig that genuinely terminates in something else wants those numbers
    // looked up, not guessed -- see this header's list of what is not
    // modelled.
    //
    enum class Termination
    {
        Ohms50,
        HighImpedance
    };

    //
    // -- The output shapes, one tag each ------------------------------------
    //
    // Each tag carries what the instrument calls this shape (the FUNCtion
    // keyword), what a log should call it, and -- for the shapes that have one
    // -- the top of its frequency range and the command for its own shape
    // parameter.
    //
    // Tags rather than an enum because the *set of settings* differs by shape,
    // and that difference is the point: only a square or a pulse has a duty
    // cycle, only a ramp has a symmetry, noise and DC have no frequency at
    // all, and DC has no amplitude. Carried in the type, those are six
    // "no matching function" diagnostics at the call site instead of six
    // settings that are silently ignored on the wire -- which is what an enum
    // plus one wide builder would give, since the instrument accepts
    // FUNC:SQU:DCYC while a sine is selected and simply remembers it for next
    // time.
    //
    // The maxima are this model's. All eight members of the series share this
    // table's *shape* and not its numbers -- a 20 MHz model's sine and square
    // stop at 20 MHz -- which is the whole argument for naming one model, see
    // this namespace's own comment.
    //
    //     Sine, Square, Pulse, Noise   30 MHz
    //     Ramp, Triangle              200 kHz
    //
    // (Guide, "Models and Options" -> 33500 Series, Higher Frequency Models.)
    // The 200 kHz ramp ceiling is the one that catches people: it is two
    // decades below the sine ceiling on the same box, and a script that moves
    // a frequency from a sine to a ramp without re-reading it gets a
    // "Settings conflict" and a silently clamped output on an attached
    // instrument -- and nothing at all on a simulated one. Hence the check in
    // checkAgainstModel() below.
    //
    // Triangle is its own FUNCtion keyword rather than RAMP at 100% symmetry,
    // because the guide lists it as its own function. It is deliberately given
    // no SymmetryCommand: a triangle whose symmetry is programmable is a ramp,
    // and this driver leaves the two spelled the way the instrument spells
    // them.
    //
    struct Sine
    {
        static constexpr std::string_view Function = "SIN";
        static constexpr std::string_view Name     = "sine";
        static constexpr double           MaxHertz = 30.0e6;
    };

    struct Square
    {
        static constexpr std::string_view Function         = "SQU";
        static constexpr std::string_view Name             = "square";
        static constexpr double           MaxHertz         = 30.0e6;
        static constexpr std::string_view DutyCycleCommand = "FUNC:SQU:DCYC";
    };

    struct Ramp
    {
        static constexpr std::string_view Function        = "RAMP";
        static constexpr std::string_view Name            = "ramp";
        static constexpr double           MaxHertz        = 200.0e3;
        static constexpr std::string_view SymmetryCommand = "FUNC:RAMP:SYMM";
    };

    struct Triangle
    {
        static constexpr std::string_view Function = "TRI";
        static constexpr std::string_view Name     = "triangle";
        static constexpr double           MaxHertz = 200.0e3;
    };

    struct Pulse
    {
        static constexpr std::string_view Function         = "PULS";
        static constexpr std::string_view Name             = "pulse";
        static constexpr double           MaxHertz         = 30.0e6;
        static constexpr std::string_view DutyCycleCommand = "FUNC:PULS:DCYC";
    };

    //
    // Gaussian noise. No frequency -- the instrument has a noise *bandwidth*
    // (FUNCtion:NOISe:BANDwidth), which is a different setting with a
    // different meaning, and is not modelled here (see the list at the bottom
    // of this header). So Noise carries no MaxHertz and therefore has no
    // frequency() setter at all, rather than one that would have to be
    // ignored.
    //
    struct Noise
    {
        static constexpr std::string_view Function = "NOIS";
        static constexpr std::string_view Name     = "noise";
    };

    //
    // A DC level, which on this instrument is the offset with the function set
    // to DC: "While the instrument is in DC mode, setting amplitude has no
    // effect." So this is the one shape with neither a frequency nor an
    // amplitude, and .offset() is the whole of its vocabulary.
    //
    struct Dc
    {
        static constexpr std::string_view Function = "DC";
        static constexpr std::string_view Name     = "dc";
    };

    //
    // What a shape has, asked of the tag rather than of a list kept somewhere
    // else. A shape that gains a setting gains it by declaring the constant
    // the setting needs, which is also the constant the .cpp sends.
    //
    template<typename Shape>
    concept Periodic = requires
    {
        { Shape::MaxHertz } -> std::convertible_to<double>;
    };

    template<typename Shape>
    concept HasDutyCycle = requires
    {
        { Shape::DutyCycleCommand } -> std::convertible_to<std::string_view>;
    };

    template<typename Shape>
    concept HasSymmetry = requires
    {
        { Shape::SymmetryCommand } -> std::convertible_to<std::string_view>;
    };

    //
    // The one negative in this file, and it earns its place: every function
    // this instrument generates has a peak-to-peak amplitude except DC, whose
    // level is its offset. Written as the exclusion rather than as a marker
    // constant on the other six tags, because that is the honest shape of the
    // fact -- there is one exception, not six affirmations.
    //
    template<typename Shape>
    concept Amplifiable = !std::is_same_v<Shape, Dc>;

    //
    // -- Asking this instrument for something it cannot produce -------------
    //
    // Thrown by Apply, before anything reaches the wire.
    //
    // Checked in the driver rather than left to the instrument, and the reason
    // is the simulated/attached asymmetry that hal::keysight_edu36311a::
    // RatingExceeded exists for -- read that one's comment, the argument is
    // the same and is not repeated here. What differs is which way the
    // instrument fails when the check is absent, and here it is the worse of
    // the two directions: a 33522B told FREQ 10e6 while a ramp is selected
    // does not refuse and stop. It generates a "Data out of range" error,
    // *clamps the frequency to 200 kHz*, and carries on generating -- so a
    // script that never reads the error queue drives its DUT at fifty times
    // the wrong frequency and records nothing about it. checked() catches that
    // on an attached box; nothing catches it on a simulated one, which is
    // every CI run here.
    //
    // A logic_error rather than a runtime_error, for the reason RatingExceeded
    // is one: nothing about the bench changed, a script asked an instrument
    // for something the instrument is not. Not caught anywhere.
    //
    class SettingOutOfRange : public std::out_of_range
    {
        public:
            SettingOutOfRange( const std::string & instrument, const std::string_view setting,
                               const std::string & detail) :
                std::out_of_range(
                    instrument + ": " + std::string( setting) + " " + detail
                    + " -- see instruments/keysight_33522b/README.md")
            {}
    };

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
    // Wfg33522B's own comment.
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
        Wfg33522B &                                Instrument;
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

            WaveformBuilder( Wfg33522B & instrument, const unsigned channel) :
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
            // A percentage, not a quantity: core::quantities has no
            // dimensionless ratio, and a duty cycle is one of the few settings
            // on a bench instrument that genuinely has no unit. Same
            // distinction core::MeasureSetup draws for NPLC.
            //
            // Square and pulse only, and they reach two different commands --
            // FUNC:SQU:DCYC and FUNC:PULS:DCYC -- which is why the command is
            // on the shape tag rather than in the .cpp.
            //
            [[nodiscard]]
            auto dutyCycle( const double percent) const requires HasDutyCycle<Shape>
            {
                return this->with( &Config::DutyCycle, percent);
            }

            //
            // FUNC:RAMP:SYMM -- how much of each cycle the ramp spends rising.
            // 0% is a falling sawtooth, 100% a rising one, 50% a triangle.
            // Ramp only.
            //
            [[nodiscard]]
            auto symmetry( const double percent) const requires HasSymmetry<Shape>
            {
                return this->with( &Config::Symmetry, percent);
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

    //
    // One of the 33522B's two output channels, narrowed at compile time via
    // Wfg33522B::channel<N>(). It is the one place a channel number is written
    // in a script, and each of its methods opens a chain for one shape:
    //
    //     Apply( Wfg1.channel<2>().sine().frequency( 1_kHz).amplitude( 2_V));
    //     Remove( Wfg1.channel<2>().sine());
    //
    // There is deliberately no config() here, only on the builder -- so
    // Apply( Wfg1.channel<1>()), an Apply naming an output and no waveform at
    // all, is "no matching function" rather than a call that does nothing.
    //
    // A thin, transient view over Wfg33522B &, and nothing it returns holds a
    // reference to *it*: every method builds a WaveformBuilder bound to the
    // real instrument, with N copied into the config by value. That is the
    // half of the scope driver's channel arrangement worth keeping and the
    // half worth not -- hal::keysight_dsox1202g::Channel's own comment records
    // a dangling-reference bug from a Port that bound to the view, and its
    // remaining sharp edge (which channel is current is instrument state) does
    // not exist here, because a waveform generator has nothing to read back
    // and so needs no Port at all.
    //
    // The methods are declared here and defined below Wfg33522B: every one of
    // them needs the instrument to be a complete type.
    //
    template<unsigned N>
        requires ValidChannel<N>
    class Channel
    {
        public:
            static constexpr unsigned Number = N;

            explicit Channel( Wfg33522B & instrument) : mInstrument( instrument) {}

            [[nodiscard]] auto sine()     const -> WaveformBuilder<Sine>;
            [[nodiscard]] auto square()   const -> WaveformBuilder<Square>;
            [[nodiscard]] auto ramp()     const -> WaveformBuilder<Ramp>;
            [[nodiscard]] auto triangle() const -> WaveformBuilder<Triangle>;
            [[nodiscard]] auto pulse()    const -> WaveformBuilder<Pulse>;
            [[nodiscard]] auto noise()    const -> WaveformBuilder<Noise>;
            [[nodiscard]] auto dc()       const -> WaveformBuilder<Dc>;

        private:
            Wfg33522B & mInstrument;
    };

    //
    // -- Everything this driver puts on the wire ----------------------------
    //
    // Declared here and defined in src/keysight_33522b.cpp, which is where
    // every command and the whole ordering argument live, in one ordinary
    // translation unit a bench engineer can check against the programming
    // guide without reading a template.
    //
    // Free functions over plain data rather than out-of-line members, the same
    // split hal::keysight_edu36311a makes and for a related reason: the shape
    // is a template parameter, so a member would only link for the shapes the
    // .cpp explicitly instantiated, and this driver's tests exercise all seven.
    // Everything shape-dependent has been reduced to a string_view and a
    // double by the time it crosses this line.
    //
    namespace detail
    {
        //
        // One Apply, flattened: the commands to send, already free of every
        // template parameter. Values are in hertz, volts and percent -- the
        // units a SCPI argument is written in, which is what
        // core::quantities::Voltage::value() already holds.
        //
        struct Program
        {
            int              Channel{};
            std::string_view Function;            // "SIN", "SQU", ...

            //
            // Empty means the config named no termination, so the instrument
            // keeps whichever one it is in -- the same thing nullopt means for
            // every other setting here. "50" or "INF" otherwise.
            //
            std::string_view Load;

            std::optional<double> Hertz;
            std::optional<double> Volts;
            std::optional<double> OffsetVolts;

            //
            // The shape parameter and the command that carries it, together --
            // FUNC:SQU:DCYC, FUNC:PULS:DCYC or FUNC:RAMP:SYMM. Empty command
            // means this shape has none, or this config did not set it.
            //
            std::string_view      ShapeCommand;
            std::optional<double> ShapePercent;
        };

        //
        // A session on the generator at this address, not yet spoken to. See
        // Wfg33522B::session() on why opening is lazy and why this is not done
        // in a constructor.
        //
        [[nodiscard]]
        auto openSession( const Address & address) -> std::unique_ptr<io::ScpiSession>;

        //
        // The once-per-session exchange: drain whatever the last user left in
        // the error queue, then ask *IDN? and refuse a model this driver is
        // not for.
        //
        auto prepare( io::ScpiSession & session) -> void;

        // *IDN?, checked. What Wfg33522B::identity() answers with.
        [[nodiscard]]
        auto identify( io::ScpiSession & session) -> std::string;

        // One Apply, on the wire: the settings that were named, in the order
        // that makes them mean what the script said, then the output on.
        auto program( io::ScpiSession & session, const Program & program) -> void;

        // One Remove: the output off, every setting left alone.
        auto disableOutput( io::ScpiSession & session, int channel) -> void;

        // OUTPut[1|2]? -- whether this output is live, asked of the instrument.
        [[nodiscard]]
        auto outputIsOn( io::ScpiSession & session, int channel) -> bool;

        //
        // What safe() says down an already-open session, swallowing a
        // transport error rather than propagating it -- see Wfg33522B::safe(),
        // whose whole contract this carries out.
        //
        auto sendSafe( io::ScpiSession & session, int channel) -> void;
    } // namespace detail

    //
    // A Keysight (Agilent) 33522B Trueform waveform generator: two independent
    // 30 MHz outputs, 1 uHz resolution, 16-bit vertical resolution, and 1 mVpp
    // to 10 Vpp into 50 Ohm.
    //
    // One instance models the whole box, both channels, and that is the one
    // structural decision here that goes the other way from
    // hal::keysight_edu36311a -- where three outputs of one chassis are three
    // instruments with three ids. Worth stating why, because the two boxes
    // look alike from a distance:
    //
    //   - an EDU36311A's three outputs are *different* (6 V / 5 A against two
    //     of 30 V / 1 A), each feeds a different DUT rail through its own
    //     lead, and each has its own isolation relay to Connect and
    //     Disconnect. Three endpoints that are separately wired and separately
    //     rated are three instruments.
    //
    //   - a 33522B's two channels are identical, hard-cabled here, and are
    //     routinely used *together* -- a stimulus and its inverse, a clock and
    //     the signal it gates, two phases of one excitation. One id keeps
    //     "safe the generator" a single operation over both connectors, which
    //     is what a failed run needs, and keeps one address in the rig table
    //     rather than the same address written twice.
    //
    // So the channel is chosen at the call site, at compile time, the way the
    // scope's is -- see Channel above.
    //
    // -- What the generator is told, in full ---------------------------------
    //
    // For one Apply of a 3.3 Vpp 10 kHz square with a 25% duty cycle on
    // channel 1, into a high-impedance load:
    //
    //     *IDN?                      once per session -- refused if the model
    //                                is not this one (detail::prepare)
    //     SYST:ERR?                  once per session, until empty: whatever
    //                                the last user left queued is not ours
    //     OUTP1:LOAD INF             the termination first -- see
    //                                detail::program, this is what the volts
    //                                below are quoted against
    //     SYST:ERR?                  did it accept that
    //     SOUR1:FUNC SQU             then the shape, because the frequency and
    //                                amplitude limits are shape-dependent
    //     SYST:ERR?
    //     SOUR1:FREQ 10000           then the frequency
    //     SYST:ERR?
    //     SOUR1:FUNC:SQU:DCYC 25     then the duty cycle, whose own limits
    //                                depend on the frequency just set
    //     SYST:ERR?
    //     SOUR1:VOLT 3.3             then the amplitude
    //     SYST:ERR?
    //     SOUR1:VOLT:OFFS 0          then the offset, which setting the
    //                                amplitude can move
    //     SYST:ERR?
    //     OUTP1 ON                   and only then the output on
    //     SYST:ERR?
    //     *OPC?                      and wait until it has all been processed
    //
    // Which it does -- wire or remembered state -- is decided by the address
    // column of the rig's instrument table and by nothing else. hal::Simulated
    // means the state below is the instrument; a real address means the box at
    // it is. A script cannot tell the difference and no script should have to
    // (see isSimulated()).
    //
    // -- No ports, and that is not an omission ------------------------------
    //
    // This instrument measures nothing. It has no Port-returning members, no
    // rawMeasure, and no simulated-reading hooks, because there is no reading
    // to simulate: a 33522B can be asked what it was told (FREQ?, VOLT?) and
    // that is a readback of this driver's own instruction, not an observation
    // of the world. Putting one in a run journal would record a number no
    // instrument ever measured. What the generator is actually producing is
    // measured by the scope, through the fabric, like any other signal.
    //
    // -- Hard-cabled, and no Connect/Disconnect -----------------------------
    //
    // There is no isolation relay in this rig at all (Thomas, 2026-09-04), so
    // this instrument's outputs are cabled straight through and there is
    // nothing for Connect/Disconnect to move: this header declares no
    // connectDriver/disconnectDriver, and Connect( Wfg1.channel<1>().sine())
    // is "no matching function".
    //
    // Deliberately *not* done with hal::keysight_edu36311a's
    // DirectWiring/RelayIsolated tag pair, even though this is exactly the
    // DirectWiring case. That driver's own comment says a third driver wanting
    // the distinction is the trigger to hoist the tags into
    // hal/driver/instrument.hpp with an API bump -- and hoisting them to model
    // an axis this rig has no hardware for would be adding a template
    // parameter with one legal value. If a relay is ever fitted in this
    // generator's lead, that is the moment for the hoist, and this class grows
    // an Isolation parameter then.
    //
    // -- What is deliberately not modelled ----------------------------------
    //
    // arbitrary waveforms       the B in 33522B is the arb-capable model, and
    //                           DATA:ARBitrary plus the MMEMory subsystem is
    //                           how a point list gets into it. That is a
    //                           waveform *transfer*, not a setting -- it wants
    //                           the counted binary-block read hal::io::
    //                           ITransport still does not have (see
    //                           framework/hal/README.md), and it wants a
    //                           decision about where a rig's arb files live.
    //                           A real addition, and a large one.
    //
    // modulation and sweep      AM/FM/PM/PWM/FSK, SWEep, LIST. Each is a whole
    //                           subsystem with its own carrier-plus-modulator
    //                           vocabulary, and none of it is one more setter
    //                           on this builder.
    //
    // burst                     BURSt:NCYCles and friends -- N cycles on a
    //                           trigger. The nearest thing here to a verb this
    //                           framework already has (it is an Arm/Await
    //                           shape, like the scope's single-shot), and the
    //                           first thing to add if a script needs a
    //                           stimulus that stops.
    //
    // pulse edge and width      FUNC:PULS:WIDTh, :PERiod and the two
    //                           TRANsition times. A pulse here is programmed
    //                           by frequency and duty cycle, which is one of
    //                           the two ways this instrument allows (see
    //                           FUNC:PULS:HOLD); the other is width-based and
    //                           would be a second, mutually exclusive set of
    //                           setters on the pulse shape.
    //
    // VOLT:UNIT, HIGH/LOW       amplitude is Vpp here, always -- see
    //                           detail::program. Vrms and dBm are the same
    //                           signal quoted differently, and quoting an
    //                           amplitude two ways in one codebase is how a
    //                           factor of 2.83 gets into a test report.
    //
    // OUTP:POLarity, :SYNC      inversion and the sync connector. Neither is
    //                           part of the signal a DUT sees on this rig.
    //
    // channel coupling          FREQ:COUPle and VOLT:COUPle tie the two
    //                           channels' settings together. They would make
    //                           an Apply to one channel silently change the
    //                           other, which is precisely the property this
    //                           driver's one-instrument-two-channels shape
    //                           relies on not being true.
    //
    class Wfg33522B : public InstrumentTag
    {
        public:
            //
            // LAN, USB or GPIB. The first two are standard on every 33500B
            // Series unit; GPIB is the factory- or user-installed option
            // (guide, "USB, GPIB, and LAN remote interfaces (GPIB is optional
            // on some models)"), so it is in the list because the back panel
            // can genuinely have it -- unlike the DSOX1202G's absent LAN
            // connector, which no option adds and which its driver therefore
            // rejects at compile time. A rig whose unit has no GPIB module
            // finds that out when openTransport fails to reach it, which is
            // the right place for a fact about one particular box.
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Lan, Usb, Gpib>
            Wfg33522B( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}

            //
            // Where the PC reaches this generator -- and, since this driver has
            // a transport, the column that decides whether an Apply reaches
            // hardware or the remembered state below. See isSimulated().
            //
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            //
            // One of the two outputs, chosen at compile time. channel<0>() and
            // channel<3>() have no valid instantiation at all (ValidChannel),
            // which is a hard compile error rather than a runtime range check.
            //
            template<unsigned N>
                requires ValidChannel<N>
            [[nodiscard]]
            auto channel() -> Channel<N>
            {
                return Channel<N>{ *this };
            }

            //
            // Whether there is nothing at the other end -- a hal::Simulated
            // address and no transport handed in. A property of the *address*
            // rather than a mode a caller sets: a rig says what it has once,
            // in its instrument table. An injected transport wins over a
            // Simulated address, deliberately -- see useTransport().
            //
            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return !mSession && std::holds_alternative<Simulated>( mAddress);
            }

            //
            // Hand this driver a transport to talk through, instead of one
            // opened from its address. Two callers, and neither is a script: a
            // test hands in a fake and asserts the command strings, and a rig
            // reaching this box over a bus hal::io::openTransport() does not
            // implement hands in its own hal::io::ITransport.
            //
            // Replaces any session already open, closing it, and sends
            // nothing: the identity check and the error-queue drain belong to
            // the first *use* (see session()), so handing in a fake transport
            // is not a thing that can throw.
            //
            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void
            {
                mSession  = std::make_unique<io::ScpiSession>( std::move( transport));
                mPrepared = false;
            }

            //
            // The live SCPI session, opened on first use.
            //
            // Lazily, and that is not an optimisation -- a rig's instruments
            // are globals constructed before main() (see
            // hal/topology/active_instruments.hpp), so a constructor that
            // opened a socket would make every binary that links the rig try
            // to reach the bench at static-initialisation time, and would
            // throw from a constructor with nowhere to catch it.
            //
            // Public, because a bring-up session on a desk wants it: a
            // deployment test can send this generator a command this driver
            // has no accessor for -- a burst, a sweep, DISP:TEXT -- without
            // that becoming a reason to widen the driver. Throws
            // hal::io::TransportError if the box cannot be reached, and
            // hal::io::ScpiFault if it answers *IDN? with a model this driver
            // is not for.
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &
            {
                if( !mSession)
                {
                    mSession = detail::openSession( mAddress);
                }

                if( mPrepared)
                {
                    return *mSession;
                }

                //
                // The once-per-session exchange, here rather than beside the
                // construction above because a session arrives two ways --
                // opened from the address, or handed in by useTransport() --
                // and preparing it where it is *used* is what makes both go
                // through it.
                //
                // Not marked prepared until it has succeeded, so a generator
                // that failed its identity check is asked again on the next
                // command rather than being treated as verified.
                //
                detail::prepare( *mSession);

                mPrepared = true;

                return *mSession;
            }

            //
            // *IDN? -- "Keysight Technologies,33522B,MY12345678,..." off the
            // real instrument, opening the session if it is not already open.
            // What a run's traceability header should carry about an
            // instrument is what the instrument says it is, not what the rig
            // table hoped it was.
            //
            [[nodiscard]]
            auto identity() -> std::string
            {
                return detail::identify( session());
            }

            //
            // Drop the session, closing the socket. The next Apply opens a new
            // one. For a long-lived process that should not hold a bench
            // instrument between runs, and for recovering from a wedged one.
            // Not called by safe(): see there.
            //
            auto closeSession() -> void
            {
                mSession.reset();
                mPrepared = false;
            }

            //
            // What an Apply does. Public because applyDriver below is a free
            // function found by ADL, not because a script should call it --
            // scripts go through Apply (see core/verbs/source.hpp).
            //
            // Throws SettingOutOfRange before anything is sent if a setting is
            // outside what this model can produce for this shape.
            //
            // Returns when the instrument has *processed* the commands (the
            // *OPC? that ends detail::program), which for this box is very
            // nearly the same as the output having arrived: unlike a power
            // supply, whose rail takes tens or hundreds of milliseconds to
            // settle, a function generator's new waveform is present within a
            // cycle or two. The one thing that is not instantaneous is the
            // output relay OUTPut switches, and the guide notes the output
            // "may glitch for about a millisecond until signal stabilizes"
            // when it does.
            //
            template<typename Shape>
            auto applyWaveform( const WaveformConfig<Shape> & config) -> void
            {
                auto & output = outputFor( config.Channel);

                //
                // The termination the amplitude and offset below are quoted
                // against: whichever this config named, or -- if it named none
                // -- whichever the output is already in. The limits depend on
                // it, so the check has to resolve it exactly the way the
                // instrument will.
                //
                const auto termination = config.Load.value_or( output.Load);

                checkAgainstModel<Shape>( config, termination);

                //
                // Remembered before the wire, and remembered whether or not
                // there is a wire: these are what a journal line and a failure
                // message are written from, and an Apply that threw on its way
                // to the instrument still happened as far as a report is
                // concerned.
                //
                output.Function  = Shape::Name;
                output.Load      = termination;
                output.Enabled   = true;

                if( config.Frequency) { output.Frequency = config.Frequency; }
                if( config.Amplitude) { output.Amplitude = config.Amplitude; }
                if( config.Offset)    { output.Offset    = config.Offset;    }

                if( isSimulated())
                {
                    return;
                }

                detail::program( session(), flatten<Shape>( config, termination));
            }

            //
            // What a Remove does: the named output off, and nothing else.
            // Every setting is deliberately left where the script put it --
            // unlike safe(), a Remove is a step in a script that knows what it
            // is doing and may well re-enable the same output two lines later.
            //
            auto removeOutput( const unsigned channel) -> void
            {
                outputFor( channel).Enabled = false;

                if( isSimulated())
                {
                    return;
                }

                detail::disableOutput( session(), static_cast<int>( channel));
            }

            //
            // Both outputs off. This is the spelling the electrical interlock
            // detects an instrument-with-an-output by -- hal::energisedSourceAt
            // requires a no-argument removeOutput() and, having found one, a
            // no-argument const isEnabled() (see
            // framework/hal/src/verbs/interlock.cpp, where the pairing is a
            // static_assert with a message). A driver that offered only the
            // per-channel overloads would be skipped by that walk silently,
            // and a live generator would be reported as cold.
            //
            // hal::keysight_ac6834b::Ac6834B answers the same question the
            // same way across its three phases, for the same reason.
            //
            auto removeOutput() -> void
            {
                for( unsigned channel = 1; channel <= mOutputs.size(); ++channel)
                {
                    removeOutput( channel);
                }
            }

            //
            // -- Two answers to "is this output live", and why there are two --
            //
            // isEnabled() is what hal::energisedSourceAt() reads before a
            // low-impedance reading is routed to a pin this generator is
            // cabled onto. Its concept takes a *const* instrument, so this
            // reports what this driver last commanded and performs no I/O.
            // With no argument it means "either output", which is the right
            // reading of the question on a box whose two connectors share one
            // InstrumentId.
            //
            // outputIsOn() asks the instrument, and is what isEnergised()
            // below uses. One round trip, in exchange for being right about
            // the cases this process did not cause: an output left on by a
            // previous run, or switched on from the front panel. The whole
            // argument is written out in hal::keysight_edu36311a::EDU36311A's
            // shared comment on its own pair and is not repeated here.
            //
            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                for( const auto & output : mOutputs)
                {
                    if( output.Enabled)
                    {
                        return true;
                    }
                }

                return false;
            }

            [[nodiscard]]
            auto isEnabled( const unsigned channel) const -> bool
            {
                return outputFor( channel).Enabled;
            }

            [[nodiscard]]
            auto outputIsOn( const unsigned channel) -> bool
            {
                if( isSimulated())
                {
                    return isEnabled( channel);
                }

                return detail::outputIsOn( session(), static_cast<int>( channel));
            }

            //
            // Drop this generator to a known idle state, unconditionally --
            // see hal::safeRig() in hal/verbs/safing.hpp for who calls this
            // and why it takes no arguments and reads no state. Not Remove()
            // under another name: Remove is a test-script step, addressed
            // through a config and a builder chain, and reaching it requires
            // knowing what a script was in the middle of driving. safe() is
            // the opposite -- it is called when nobody knows what was running.
            //
            // Both channels, and the amplitude and offset zeroed as well as
            // the outputs disabled, for the reason
            // hal::keysight_edu36311a::EDU36311A::safe() zeroes its setpoint:
            // OUTPut OFF leaves the programmed signal where a test left it, so
            // a generator safed at 10 Vpp comes back at 10 Vpp the instant
            // anything enables the output again -- a front-panel press, a
            // half-initialised driver, a reconnecting console.
            //
            // On the wire, only down a session that is *already* open, and
            // never opening one. Safing runs when a script has already failed,
            // quite possibly because this box is unreachable; opening a
            // session at that moment would replace the run's real failure with
            // a transport error from the cleanup path, and hal::safeRig() does
            // not catch, so it would also abandon the safing of every
            // instrument after this one.
            //
            // The session is deliberately not closed either: its error queue
            // is the best evidence of what went wrong.
            //
            auto safe() -> void
            {
                for( auto & output : mOutputs)
                {
                    output.Enabled   = false;
                    output.Amplitude = std::nullopt;
                    output.Offset    = std::nullopt;
                }

                if( !mSession)
                {
                    return;
                }

                for( unsigned channel = 1; channel <= mOutputs.size(); ++channel)
                {
                    detail::sendSafe( *mSession, static_cast<int>( channel));
                }
            }

            // --- The last thing this driver was told, per output ---

            //
            // Kept on an attached generator as well as a simulated one -- it is
            // what describeConfig's journal line and a failure message are
            // written from, and re-querying the instrument to render a log
            // entry would be a round trip to learn what this process just said.
            //
            // Which makes these the *setting*, never a measurement: see this
            // class's own comment on why there are no ports here.
            //
            [[nodiscard]]
            auto function( const unsigned channel) const -> std::string_view
            {
                return outputFor( channel).Function;
            }

            [[nodiscard]]
            auto frequency( const unsigned channel) const -> std::optional<core::quantities::Frequency>
            {
                return outputFor( channel).Frequency;
            }

            [[nodiscard]]
            auto amplitude( const unsigned channel) const -> std::optional<core::quantities::Voltage>
            {
                return outputFor( channel).Amplitude;
            }

            [[nodiscard]]
            auto offset( const unsigned channel) const -> std::optional<core::quantities::Voltage>
            {
                return outputFor( channel).Offset;
            }

            //
            // The termination this output's numbers are quoted against. Never
            // nullopt: the instrument is always in one, and 50 Ohm is the one
            // it powers up in, so that is what this driver believes until an
            // Apply says otherwise.
            //
            [[nodiscard]]
            auto termination( const unsigned channel) const -> Termination
            {
                return outputFor( channel).Load;
            }

            // --- What this model can produce, as constants a caller can read ---

            //
            // Peak-to-peak, and it depends on the termination: 10 Vpp into
            // 50 Ohm, 20 Vpp into an open circuit. Not two different signals --
            // the same signal, quoted against two different assumed loads. See
            // Termination.
            //
            [[nodiscard]]
            static constexpr auto maxAmplitude( const Termination termination) -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ termination == Termination::HighImpedance ? 20.0 : 10.0 };
            }

            //
            // The peak voltage either polarity may reach, which is what bounds
            // the offset and couples it to the amplitude:
            // |offset| <= Vmax - Vpp/2.
            //
            [[nodiscard]]
            static constexpr auto maxPeak( const Termination termination) -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ termination == Termination::HighImpedance ? 10.0 : 5.0 };
            }

            [[nodiscard]]
            static constexpr auto minAmplitude() -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ 0.001 };
            }

            [[nodiscard]]
            static constexpr auto minFrequency() -> core::quantities::Frequency
            {
                return core::quantities::Frequency{ 1.0e-6 };
            }

        private:
            //
            // One output's remembered state. Not a simulation of the
            // instrument -- it is this driver's record of what it last said,
            // which is the same thing on a simulated box and a strictly
            // smaller thing on an attached one.
            //
            struct Output
            {
                std::string_view                            Function{};
                std::optional<core::quantities::Frequency>  Frequency{};
                std::optional<core::quantities::Voltage>    Amplitude{};
                std::optional<core::quantities::Voltage>    Offset{};

                //
                // The instrument's own power-on termination, so this driver
                // starts out believing what the box believes.
                //
                Termination                                 Load{ Termination::Ohms50 };
                bool                                        Enabled{ false };
            };

            //
            // Channel numbers are 1-based at every call site and in every SCPI
            // command, and 0-based in this array exactly once, here.
            //
            // .at() rather than operator[], which is the one place in this
            // driver a runtime bound is paid for. Every channel that arrives
            // through a config came from a ValidChannel-constrained Channel<N>
            // and cannot be wrong -- but the per-channel accessors above
            // (isEnabled, function, removeOutput and the rest) take a plain
            // unsigned, because they are called from tests and from journal
            // code that has a number rather than a template argument. Indexing
            // a two-element array with an unchecked one of those is undefined
            // behaviour on a driver whose whole job is refusing things that
            // cannot be. Note channel 0 underflows into a very large unsigned
            // and is caught by the same bound.
            //
            [[nodiscard]]
            auto outputFor( const unsigned channel) -> Output &
            {
                return mOutputs.at( channel - 1);
            }

            [[nodiscard]]
            auto outputFor( const unsigned channel) const -> const Output &
            {
                return mOutputs.at( channel - 1);
            }

            //
            // Refuse a setting this model cannot produce -- see
            // SettingOutOfRange, and note this runs on a simulated instrument
            // as well as an attached one, which is the entire point of it
            // existing at all.
            //
            template<typename Shape>
            auto checkAgainstModel( const WaveformConfig<Shape> & config, const Termination termination) const -> void
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
                                + std::string( Shape::Name) + "'s range on a 33522B ("
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

                        if( volts < minAmplitude().value() || volts > maxAmplitude( termination).value())
                        {
                            throw SettingOutOfRange( instrument, "amplitude",
                                "of " + core::describeValue( *config.Amplitude) + " peak-to-peak is outside "
                                + core::describeValue( minAmplitude()) + " to "
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

                    //
                    // And the coupled limit the guide states as
                    // |Voffset| < Vmax - Vpp/2: an offset and an amplitude that
                    // are each legal on their own can still ask for a peak the
                    // output cannot reach. Checked only when this config names
                    // both, because when it names one the other is whatever the
                    // instrument is already set to -- a number this driver has
                    // not necessarily been told, and would be guessing at.
                    //
                    // What an attached instrument does with the pair is the
                    // reason this is worth catching here: it does not refuse
                    // them. It *adjusts the offset* to the largest value the
                    // amplitude allows, generates "Data out of range", and
                    // carries on generating -- so the DUT sees a signal
                    // centred somewhere the script never asked for.
                    //
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
                                    + std::string( describeTermination( termination))
                                    + " -- the instrument allows |offset| <= "
                                    + core::describeValue( maxPeak( termination))
                                    + " - amplitude/2, so at most "
                                    + core::describeValue( core::quantities::Voltage{ headroom }));
                            }
                        }
                    }
                }

                if constexpr( HasDutyCycle<Shape>)
                {
                    //
                    // 0.01 to 99.99 percent, and the *achievable* range is
                    // narrower than that at speed: this model holds a 16 ns
                    // minimum pulse width, so at 1 MHz the duty cycle cannot go
                    // below 1.6% and at 10 MHz not below 16%. That narrowing is
                    // deliberately not modelled -- it is arithmetic on the
                    // frequency this config may not even carry -- so the check
                    // here is the absolute bound and the instrument keeps the
                    // frequency-dependent one.
                    //
                    checkPercent( instrument, "duty cycle", config.DutyCycle, 0.01, 99.99);
                }

                if constexpr( HasSymmetry<Shape>)
                {
                    checkPercent( instrument, "symmetry", config.Symmetry, 0.0, 100.0);
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

            //
            // The config, reduced to the plain data detail::program works in.
            // Every template parameter is spent here and none of it crosses
            // into the .cpp -- see detail's own comment.
            //
            template<typename Shape>
            [[nodiscard]]
            static auto flatten( const WaveformConfig<Shape> & config, const Termination termination) -> detail::Program
            {
                detail::Program flat;

                flat.Channel  = static_cast<int>( config.Channel);
                flat.Function = Shape::Function;

                //
                // Sent only when this config named one. An Apply that says
                // nothing about the termination leaves the instrument in
                // whichever one it is in -- and, since this driver's
                // remembered Load is then already that one, the limits were
                // checked against the same value the instrument will use.
                //
                if( config.Load)
                {
                    flat.Load = termination == Termination::HighImpedance ? "INF" : "50";
                }

                if( config.Frequency) { flat.Hertz       = config.Frequency->value(); }
                if( config.Amplitude) { flat.Volts       = config.Amplitude->value(); }
                if( config.Offset)    { flat.OffsetVolts = config.Offset->value();    }

                if constexpr( HasDutyCycle<Shape>)
                {
                    if( config.DutyCycle)
                    {
                        flat.ShapeCommand = Shape::DutyCycleCommand;
                        flat.ShapePercent = config.DutyCycle;
                    }
                }

                if constexpr( HasSymmetry<Shape>)
                {
                    if( config.Symmetry)
                    {
                        flat.ShapeCommand = Shape::SymmetryCommand;
                        flat.ShapePercent = config.Symmetry;
                    }
                }

                return flat;
            }

            //
            // "into 50 Ohm" / "into a high-impedance load" -- for the sentence
            // a SettingOutOfRange is, where the limit means nothing without the
            // termination it was derived from.
            //
            [[nodiscard]]
            static constexpr auto describeTermination( const Termination termination) -> std::string_view
            {
                return termination == Termination::HighImpedance ? "into a high-impedance load" : "into 50 Ohm";
            }

            InstrumentId  mId;
            Address       mAddress;

            //
            // Null until the first command that needs hardware, and null
            // forever on a simulated instrument -- see session() on why it
            // cannot be opened in the constructor, and isSimulated() on what
            // its being null means.
            //
            // A unique_ptr, which makes this class non-copyable, and that is
            // correct rather than incidental: a copy of a driver would be a
            // second object claiming the same generator, and one of the two
            // would hold the socket.
            //
            std::unique_ptr<io::ScpiSession>  mSession;

            //
            // Whether mSession has had the once-per-session exchange: the
            // error-queue drain and the identity check. A flag beside the
            // pointer rather than something done where the session is created,
            // because a session arrives two ways -- opened from the address, or
            // handed in by useTransport() -- and both have to be prepared.
            //
            bool                              mPrepared{ false };

            std::array<Output, 2>             mOutputs;
    };

    // ---------------------------------------------------------------------
    // Channel<N>'s entry points
    // ---------------------------------------------------------------------
    //
    // Below Wfg33522B rather than inside Channel, because every one of these
    // needs the instrument to be a complete type.
    //

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::sine() const -> WaveformBuilder<Sine>
    {
        return WaveformBuilder<Sine>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::square() const -> WaveformBuilder<Square>
    {
        return WaveformBuilder<Square>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::ramp() const -> WaveformBuilder<Ramp>
    {
        return WaveformBuilder<Ramp>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::triangle() const -> WaveformBuilder<Triangle>
    {
        return WaveformBuilder<Triangle>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::pulse() const -> WaveformBuilder<Pulse>
    {
        return WaveformBuilder<Pulse>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::noise() const -> WaveformBuilder<Noise>
    {
        return WaveformBuilder<Noise>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::dc() const -> WaveformBuilder<Dc>
    {
        return WaveformBuilder<Dc>{ mInstrument, N };
    }

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
    // Wfg33522B's own comment on why that is stated by their absence rather
    // than by a DirectWiring tag.
    //
} // namespace hal::keysight_33522b
