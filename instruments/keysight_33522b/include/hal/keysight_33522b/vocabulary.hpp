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

//
// shapes, termination, range errors
//
// Part of hal/keysight_33522b.hpp -- include that, not this.
//

namespace hal::keysight_33522b
{
    class WaveformGenerator;

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

    template<unsigned N>
        requires ValidChannel<N>
    class Channel;   // defined in channel_view.hpp, which needs Wfg33522B complete
} // namespace hal::keysight_33522b
