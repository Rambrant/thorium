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
// what goes on the wire (declared)
//
// Part of hal/keysight_33522b.hpp -- include that, not this.
//

namespace hal::keysight_33522b
{
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
        // The once-per-session exchange: drain whatever the last user left in
        // the error queue, then ask *IDN? and refuse a model this driver is
        // not for.
        //
        auto prepare( io::ScpiSession & session) -> void;

        // *IDN?, checked. What WaveformGenerator::identity() answers with.
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
        // transport error rather than propagating it -- see WaveformGenerator::safe(),
        // whose whole contract this carries out.
        //
        auto sendSafe( io::ScpiSession & session, int channel) -> void;
    } // namespace detail
} // namespace hal::keysight_33522b
