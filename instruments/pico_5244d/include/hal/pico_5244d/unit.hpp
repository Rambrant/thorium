#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "hal/driver/box_connection.hpp"

#include "hal/pico_5244d/library.hpp"
#include "hal/pico_5244d/vocabulary.hpp"

//
// The unit behind the box: what the scope and its signal generator share.
// Part of hal/pico_5244d.hpp.
//
// A PicoScope 5244D is one USB device with two faces -- the oscilloscope and
// the built-in function generator -- and the SDK gives it one handle, which a
// second OpenUnit would be refused. So the faces share it the way the DSOX1202G's
// share an SCPI session: through hal::BoxConnection, on the box their rows name
// (see the rig's instrument table). What is shared here is not a session but a
// vendor library and the handle it opened, which is the box's *native* object
// (BoxConnection::native).
//
namespace hal::pico_5244d
{
    //
    // A unit that is open: the handle, and what it said it was. Closed by the
    // destructor, which is how the box releasing it releases the hardware.
    //
    // Nothing here is a face's own: what each channel was last given belongs to
    // the oscilloscope, what the generator last played to the generator, and
    // each notices a unit that is not the one it configured by its Generation.
    //
    struct Unit
    {
        std::shared_ptr<Ps5000a>    Api;
        std::int16_t                Handle{ 0 };
        std::int16_t                AdcMax{ 0 };
        std::string                 Variant;
        std::string                 Serial;
        std::string                 Driver;

        // The resolution the unit is in, which a face asks for and the first
        // one to open the unit may not have known.
        Resolution                  Bits{ Resolution::Bits8 };

        // Distinguishes this open from any other, for a face that remembers
        // what it configured.
        std::uint64_t               Generation{ 0 };

        Unit() = default;
        Unit( const Unit &)                      = delete;
        auto operator=( const Unit &) -> Unit &  = delete;

        ~Unit();
    };

    //
    // What the box holds: the library, handed in by a test or loaded on first
    // use, and the unit once open.
    //
    struct Hardware
    {
        std::shared_ptr<Ps5000a>    Api;
        std::shared_ptr<Unit>       Open;
    };

    namespace detail
    {
        void check( const char * call, Status status);

        //
        // The box's open unit, opened and checked to be a 5244D on first use by
        // either face. `resolution` is what the asking face wants the unit to
        // open in, if it has an opinion (the generator has none).
        //
        // Throws PicoFault with the SDK's reason if the unit will not open, and
        // io::TransportError if it opens as some other model.
        //
        [[nodiscard]]
        auto openedUnit( BoxConnection & connection, std::optional<Resolution> resolution) -> Unit &;

        //
        // Put this library on the box in place of whatever it holds, releasing
        // an open unit -- the test seam, as useTransport() is on the SCPI drivers.
        //
        auto installApi( BoxConnection & connection, std::shared_ptr<Ps5000a> api) -> void;

        //
        // Stop and release the box's open unit, for every face, if there is one.
        // The library stays: it is the process's, not the unit's.
        //
        auto closeUnit( BoxConnection & connection) -> void;

        //
        // The unit if some face of the box has opened it, never opening one.
        // What a safe() uses.
        //
        [[nodiscard]]
        auto heldUnit( BoxConnection & connection) -> Unit *;

        // "Pico Technology,5244D,<serial>,<driver version>" -- shaped like an *IDN?.
        [[nodiscard]]
        auto identityOf( const Unit & unit) -> std::string;
    } // namespace detail
} // namespace hal::pico_5244d
