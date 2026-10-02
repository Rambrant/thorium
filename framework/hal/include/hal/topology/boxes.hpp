#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/meta.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_device.hpp"

//
// Boxes: the physical units a rig's rows are faces of.
//
// Every row in instrument.inc and devices.inc starts with a box name -- the
// thing on the bench a person can point at -- and every row with the same name
// is a face of the same box:
//
//     INSTRUMENT(    Psu1, keysight_edu36311a::DirectOutput1, DcP5, Usb( "CN65100272"))
//     INSTRUMENT(    Psu1, keysight_edu36311a::DirectOutput2, DcP6, Usb( "CN65100272"))
//     SWITCH_DEVICE( RacalRack, Racal1260_45, Matrix1, Gpib( 0, 7), Card( 1))
//
// The box is what an address belongs to. Before it had a name, an address did
// three jobs at once -- how the PC reaches a box, how rows were grouped into
// one (a driver inferring "same address, same box" for itself), and what
// preflight claimed a box by -- and each of this repo's addressing problems was
// one of those jobs getting in another's way: three --address flags for one
// supply, a pool that could not serve a three-output supply because each row
// claimed the box for itself, and a switch/measure mainframe that could not
// have two faces without two sessions to it.
//
// So the box is now the unit of everything address-shaped: --address
// Psu1=usb:..., THORIUM_ADDRESS_Psu1, POOL( Psu1, ...) and SITE( s, Psu1, ...)
// all name the box, and preflight binds, acquires and claims a box once and
// fans the result out to every row of it (see hal/verbs/preflight.hpp).
//
// The address is still written on every row, rather than once in a table of
// its own -- an address.inc would be one more file to keep beside the two that
// already say what each box is. Writing it per row is only safe because two
// rules make it impossible to get wrong, and they are checked here, at compile
// time, across both tables:
//
//   one box, one address  rows naming the same box must say the same address
//                         -- a row copied and half-edited is a build failure,
//                         not a supply whose third output talks to a
//                         different host. An instrument row saying
//                         hal::Simulated is exempt: it is a face of the box
//                         that is not there -- a 34980A's internal DMM on a
//                         mainframe without one, or one left out on purpose
//                         -- and opens nothing, so it cannot reach a
//                         different unit. The box's other rows still have to
//                         agree with each other. A device row gets no such
//                         exemption: nothing opens a device row's address (see
//                         below), so Simulated on one would only be a claim
//                         about a card the box's real session still drives.
//
//                         Box-wide sources still reach a simulated face: an
//                         --address, a SITE row and a POOL all name the box,
//                         so each of them binds every face of it, this one
//                         included. --address <row>=sim is how one run leaves
//                         a single face out without editing the table (see
//                         hal::parseFaceSimulation()).
//
//   one address, one box  rows naming different boxes must not say the same
//                         fixed address -- which is what a typo in a box name
//                         (Psu1 and PSU1) produces, and what would otherwise
//                         quietly open two sessions to one supply. hal::
//                         Simulated is exempt: it names no box at all, and a
//                         bench whose instruments are mostly not plugged in
//                         says it on most of its rows.
//
// Device rows carry a box and are held to both rules, and their address is
// one the tables check rather than one anything opens: the fabric reaches a
// card through the instrument on its box that switches cards (see
// hal/fabric/rig_switching.hpp), so it is that instrument's binding -- its
// --address, its pool -- that decides where the card is.
//
namespace hal
{
    //
    // One row of either table, as far as boxes are concerned: which box, which
    // row (the id, as written), and the address it says.
    //
    struct BoxRow
    {
        std::string_view Box;
        std::string_view Row;
        Address          Value;

        // A devices.inc row rather than an instrument.inc one -- which
        // decides whether Simulated on it is exempt from the first rule.
        bool             Device{ false };
    };

    namespace detail
    {
#pragma push_macro( "INSTRUMENTS")
#pragma push_macro( "INSTRUMENT")
#pragma push_macro( "END_INSTRUMENTS")
#undef INSTRUMENTS
#undef INSTRUMENT
#undef END_INSTRUMENTS

#define INSTRUMENTS
#define INSTRUMENT( box, type, id, address, ...) BoxRow{ #box, #id, hal::address },
#define END_INSTRUMENTS

        //
        // In table order, which is InstrumentId's order -- so a row is found
        // by its id's value, as switchDevices does for SwitchDeviceId.
        //
        inline constexpr std::array<BoxRow, core::meta::values<InstrumentId>.size()> instrumentBoxRows =
        {
            #include THORIUM_INSTRUMENT_TABLE
        };

#pragma pop_macro( "END_INSTRUMENTS")
#pragma pop_macro( "INSTRUMENT")
#pragma pop_macro( "INSTRUMENTS")

#pragma push_macro( "SWITCH_DEVICES")
#pragma push_macro( "SWITCH_DEVICE")
#pragma push_macro( "END_SWITCH_DEVICES")
#undef SWITCH_DEVICES
#undef SWITCH_DEVICE
#undef END_SWITCH_DEVICES

#define SWITCH_DEVICES
#define SWITCH_DEVICE( box, model, id, address, card) BoxRow{ #box, #id, hal::address, true },
#define END_SWITCH_DEVICES

        inline constexpr std::array<BoxRow, core::meta::values<SwitchDeviceId>.size()> deviceBoxRows =
        {
            #include THORIUM_DEVICE_TABLE
        };

#pragma pop_macro( "END_SWITCH_DEVICES")
#pragma pop_macro( "SWITCH_DEVICE")
#pragma pop_macro( "SWITCH_DEVICES")

        //
        // The first violation of either rule in these rows, as the sentence
        // the build fails with -- or empty when there is none. Pure, and over
        // any rows, so both rules can be tested in both directions on rows a
        // test makes up (see framework/hal/tests/topology/test_boxes.cpp); the
        // rig's own rows are checked by the static_assert below.
        //
        consteval auto simulatedFace( const BoxRow & row) -> bool
        {
            return !row.Device && std::holds_alternative<Simulated>( row.Value);
        }

        consteval auto boxConflict( std::span<const BoxRow> rows) -> std::string
        {
            for( std::size_t first = 0; first < rows.size(); ++first)
            {
                for( std::size_t second = first + 1; second < rows.size(); ++second)
                {
                    const auto & a = rows[ first];
                    const auto & b = rows[ second];

                    if( a.Box == b.Box && !( a.Value == b.Value) && !simulatedFace( a) && !simulatedFace( b))
                    {
                        return "rows " + std::string( a.Row) + " and " + std::string( b.Row)
                             + " are both faces of box " + std::string( a.Box)
                             + " but say different addresses. One box has one address: make the two"
                               " rows agree, or give one of them its own box name if it really is a"
                               " different unit. (An instrument face that is not there says"
                               " Simulated{} and is exempt; a device row is not.)";
                    }

                    if( a.Box != b.Box && a.Value == b.Value && !std::holds_alternative<Simulated>( a.Value))
                    {
                        return "rows " + std::string( a.Row) + " (box " + std::string( a.Box) + ") and "
                             + std::string( b.Row) + " (box " + std::string( b.Box)
                             + ") say the same address, so they reach the same unit under two box names --"
                               " a typo in one of the names, or a copied row. Give them one box name if"
                               " they are one unit.";
                    }
                }
            }

            return {};
        }

        consteval auto allBoxRows() -> std::vector<BoxRow>
        {
            std::vector<BoxRow> rows( instrumentBoxRows.begin(), instrumentBoxRows.end());

            rows.insert( rows.end(), deviceBoxRows.begin(), deviceBoxRows.end());

            return rows;
        }

        consteval auto rigBoxConflict() -> std::string
        {
            const auto rows = allBoxRows();

            return boxConflict( rows);
        }
    } // namespace detail

    static_assert( detail::rigBoxConflict().empty(), detail::rigBoxConflict());

    //
    // Which box an instrument is a face of.
    //
    [[nodiscard]]
    constexpr auto boxOf( const InstrumentId instrument) -> std::string_view
    {
        return detail::instrumentBoxRows[ static_cast<std::size_t>( instrument)].Box;
    }

    //
    // Whether any instrument row names this box -- the boxes an --address, a
    // pool or a site can name. A box with only device rows has no address
    // anything resolves yet (see this file's comment).
    //
    [[nodiscard]]
    constexpr auto hasInstrumentsIn( const std::string_view box) -> bool
    {
        for( const auto & row : detail::instrumentBoxRows)
        {
            if( row.Box == box)
            {
                return true;
            }
        }

        return false;
    }

    //
    // The instruments a box has, in table order.
    //
    [[nodiscard]]
    constexpr auto instrumentsIn( const std::string_view box) -> std::vector<InstrumentId>
    {
        std::vector<InstrumentId> faces;

        for( const auto id : core::meta::values<InstrumentId>)
        {
            if( boxOf( id) == box)
            {
                faces.push_back( id);
            }
        }

        return faces;
    }

    //
    // Every box with an instrument row, once each, in the order the table first
    // names them -- what an --address can name, and what the environment is
    // searched for (THORIUM_ADDRESS_<box>).
    //
    [[nodiscard]]
    auto instrumentBoxNames() -> std::vector<std::string_view>;
} // namespace hal
