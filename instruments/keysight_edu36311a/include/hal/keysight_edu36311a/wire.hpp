#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "core/driver/describe.hpp"
#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/io/scpi.hpp"
#include "hal/topology/wiring.hpp"

#include "hal/keysight_edu36311a/vocabulary.hpp"

//
// what goes on the wire (declared)
//
// Part of hal/keysight_edu36311a.hpp -- include that, not this.
//

namespace hal::keysight_edu36311a
{
    //
    // -- Everything this driver puts on the wire ----------------------------
    //
    // Declared here and defined in src/keysight_edu36311a.cpp, which is where
    // every command, every query and the whole ordering argument live, in one
    // ordinary translation unit a bench engineer can check against the
    // programming guide without reading a template.
    //
    // Free functions over (session, channel) rather than out-of-line members
    // of the class below, and that is not a style choice -- it is what a class
    // *template* forces. EDU36311A has two type parameters, so out-of-line
    // members would only link for the combinations the .cpp explicitly
    // instantiated, and the six aliases below are not the whole set a rig may
    // legitimately name: this driver promises that a new isolation tag needs
    // no change here (see SwitchableIsolation), and its tests hold it to that
    // with a tag it has never heard of. Such a tag cannot appear in an
    // explicit instantiation list that already exists.
    //
    // So the split lands where it always wanted to land anyway. Nothing about
    // talking to this supply depends on which output tag or isolation tag the
    // caller picked -- the channel number is an int by the time it gets here
    // -- and the class below is left holding only the glue: which branch,
    // simulated or attached, and what to remember about it.
    // hal::keysight_edu34450a::Multimeter drew the same line and could afford
    // to draw it through a member, being a plain class.
    //
    namespace detail
    {
        //
        // Which of the two readback queries to send -- the whole of what
        // varies between them.
        //
        enum class Reading
        {
            Voltage,
            Current
        };

        //
        // The once-per-session exchange: drain whatever the last user left in
        // the error queue, then ask *IDN? and refuse a model this driver is
        // not for.
        //
        auto prepare( io::ScpiSession & session) -> void;

        //
        // *IDN?, checked. What DcSource::identity() answers with.
        //
        [[nodiscard]]
        auto identify( io::ScpiSession & session) -> std::string;

        //
        // One Apply, on the wire: the settings that were named, in the order
        // that makes them safe, then the output on, then *OPC?. Values are in
        // volts and amps -- the units a SCPI argument is written in, which is
        // what core::quantities::Voltage::value() already holds.
        //
        auto program( io::ScpiSession & session, int channel,
                      std::optional<double> volts,
                      std::optional<double> amps,
                      std::optional<double> overVolts) -> void;

        // One Remove: the output off, the setpoint left alone.
        auto disableOutput( io::ScpiSession & session, int channel) -> void;

        [[nodiscard]]
        auto measure( io::ScpiSession & session, int channel, Reading reading) -> double;

        // OUTP? -- whether this output is live, asked of the instrument.
        [[nodiscard]]
        auto outputIsOn( io::ScpiSession & session, int channel) -> bool;

        //
        // What safe() says down an already-open session, swallowing a
        // transport error rather than propagating it -- see
        // DcSource::safe(), whose whole contract this carries out.
        //
        auto sendSafe( io::ScpiSession & session, int channel) -> void;
    } // namespace detail
} // namespace hal::keysight_edu36311a
