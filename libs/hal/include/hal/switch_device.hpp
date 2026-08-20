#pragma once

#include <cstddef>

#include "core/meta.hpp"

#include "hal/address.hpp"

namespace hal
{
    //
    // Which kind of switching hardware a device is.
    //
    // RfMux is a genuinely separate physical device from Mux, not just a
    // naming distinction: RF signal paths need controlled impedance
    // (typically 50 ohm) end to end, which an LF matrix/mux card's relays
    // aren't built to preserve -- an RF signal routed through an LF mux
    // would see reflections/loss a real RF measurement can't tolerate. So
    // the two never share hardware, and a route through the fabric mixes
    // them only in the sense that hal::Path can hold hops on either kind of
    // device in the same chain -- e.g. an LF mux hop narrowing down to a
    // matrix column is a different case from an RF signal's own, separate
    // RfMux chain; nothing here enforces that an RF point is only ever
    // reached through RfMux hops (see this project's decision not to build a
    // compile-time-checked point-category gate for that kind of DUT-specific
    // judgment call) -- that's left to whoever wires the rig's wiring.inc
    // (rig/wiring.inc in this repo) and writes the test getting it right,
    // the same way it already is for Matrix vs Mux today.
    //
    // Purely descriptive as far as this library is concerned -- every kind is
    // commanded identically (open/close a channel) -- and kept so a route's
    // path reads clearly and so error messages can say "Matrix2 channel 14"
    // rather than an opaque pair.
    //
    enum class SwitchDeviceKind
    {
        Matrix, // RACAL 1260 VXI matrix card
        Mux,    // LF multiplexer -- VXI modules configured as 8x12->1 or 2x48->1 2-wire multiplexers
        RfMux   // HF/RF selector -- Agilent E1472A VXI modules, each 6 x 1x4 multiplexers
    };

    //
    // The fixed, static list of switching devices a rig actually has -- the
    // matrix cards, LF muxes and RF selectors its signal chain is built from.
    // Generated from THORIUM_DEVICE_TABLE, the linking rig's own devices.inc
    // (see rig/devices.inc for this repo's), exactly the way
    // hal::InstrumentId comes from THORIUM_INSTRUMENT_TABLE -- this header is
    // generic and shared by every rig, so the enumerators (Matrix2, Mux1,
    // ...) are not listed here at all.
    //
    // These are deliberately NOT hal::InstrumentId enumerators, and that is
    // the whole reason for a second enum rather than a bigger first one. A
    // switching device is plumbing: it has no quantity type and nothing to
    // read, so the type system can never let a script try to Measure a mux
    // channel (see hal::SwitchElementId's own comment in
    // hal/switch_fabric.hpp, which has said as much since long before this
    // enum existed). InstrumentId is what a recorded sample and a wiring
    // entry identify a *reading* by; a mux channel produces no readings.
    // Folding the two lists together would hand every script-facing
    // Measure/Apply overload a set of ids that cannot answer them.
    //
    // The same split is already visible in how the globals are declared:
    // rig/active_instruments.hpp declares Dmm1 unqualified because a test
    // script writes that name itself, and hal::fabric qualified because
    // nothing but the measure/apply assembly ever names it.
    //
    // What replaced what: SwitchElementId used to carry a bare
    // std::string_view device name, so wiring.inc named cards in strings that
    // nothing validated. HOP( Matrix, "Matrix22", 14) -- a typo -- was
    // accepted, and the fabric would create that element, close it, open it,
    // and route nothing, because the fabric had no idea which cards existed.
    // Worse, kind rode on every hop rather than on the card, so
    // HOP( Matrix, "Matrix2", 14) and HOP( Mux, "Matrix2", 14) were two
    // distinct SwitchElementIds for one physical crosspoint, each with its own
    // use count in hal::SwitchFabric -- connect through one and disconnect
    // through the other and the relay never opened, with nothing complaining.
    // Declaring each device once, here, makes both unrepresentable rather than
    // merely unlikely.
    //
#pragma push_macro( "SWITCH_DEVICES")
#pragma push_macro( "SWITCH_DEVICE")
#pragma push_macro( "END_SWITCH_DEVICES")
#undef SWITCH_DEVICES
#undef SWITCH_DEVICE
#undef END_SWITCH_DEVICES

#define SWITCH_DEVICES
#define SWITCH_DEVICE( kind, id, address) id,
#define END_SWITCH_DEVICES

    enum class SwitchDeviceId
    {
        #include THORIUM_DEVICE_TABLE
    };

#pragma pop_macro( "END_SWITCH_DEVICES")
#pragma pop_macro( "SWITCH_DEVICE")
#pragma pop_macro( "SWITCH_DEVICES")

    [[nodiscard]]
    auto to_string( SwitchDeviceId device) -> std::string_view;

    namespace detail
    {
        //
        // The two facts each declared device carries besides its own identity.
        // Not a class with behaviour: a switching device is commanded through
        // hal::SwitchFabric, which addresses channels rather than cards (see
        // hal/switch_fabric.hpp), so there is nothing for a per-device object
        // to do here yet. When there is -- a real matrix card and a real RF
        // selector do not speak the same commands, so each card model
        // eventually wants its own driver the way each instrument model has
        // one -- this is the table those drivers get constructed from.
        //
        struct SwitchDeviceInfo
        {
            SwitchDeviceKind Kind;
            Address          Where;
        };

        //
        // devices.inc read a second time, with SWITCH_DEVICE redefined to
        // build one row each. Two reads of one file rather than one, and
        // unlike the reads reflection replaced elsewhere in this codebase
        // (see core::meta::all() in core/criterion.hpp, hal::safeRig()) this
        // second one cannot go the same way: reflection over SwitchDeviceId
        // can recover an enumerator's *name*, which is all safeRig() needed,
        // but a device's kind and address are data the table states and
        // nothing about the enum implies. There is nothing to reflect them
        // out of.
        //
        // Both reads see the same rows in the same order, which is what makes
        // indexing this array by the enumerator's own value correct. The
        // static_assert below is the guard on that -- it fires if the two
        // expansions ever disagree on how many rows there were.
        //
#pragma push_macro( "SWITCH_DEVICES")
#pragma push_macro( "SWITCH_DEVICE")
#pragma push_macro( "END_SWITCH_DEVICES")
#undef SWITCH_DEVICES
#undef SWITCH_DEVICE
#undef END_SWITCH_DEVICES

#define SWITCH_DEVICES
#define SWITCH_DEVICE( kind, id, address) SwitchDeviceInfo{ SwitchDeviceKind::kind, hal::address },
#define END_SWITCH_DEVICES

        inline constexpr SwitchDeviceInfo switchDevices[] =
        {
            #include THORIUM_DEVICE_TABLE
        };

#pragma pop_macro( "END_SWITCH_DEVICES")
#pragma pop_macro( "SWITCH_DEVICE")
#pragma pop_macro( "SWITCH_DEVICES")

        static_assert( std::size( switchDevices) == core::meta::values<SwitchDeviceId>.size(),
                       "THORIUM_DEVICE_TABLE's two expansions disagree on how many devices this rig has");
    } // namespace detail

    //
    // What kind of hardware a declared device is, and where the PC reaches it.
    //
    // kindOf() is why SwitchElementId no longer carries a kind of its own: the
    // kind is a property of the card, stated once where the card is declared,
    // so a hop cannot contradict another hop about the same card.
    //
    // addressOf() is the fabric's half of what hal/address.hpp does for
    // instruments, and closes the gap that header's own comment named: closing
    // a relay on real hardware is a GPIB/VXI write to a card, which has to be
    // addressed exactly as an instrument does. Nothing reads it yet, for the
    // same reason nothing reads an instrument's address yet -- hal::
    // SwitchFabric only tracks relay state -- but a rig can now state the
    // fact, which it could not before.
    //
    [[nodiscard]]
    constexpr auto kindOf( const SwitchDeviceId device) -> SwitchDeviceKind
    {
        return detail::switchDevices[ static_cast<std::size_t>( device)].Kind;
    }

    [[nodiscard]]
    constexpr auto addressOf( const SwitchDeviceId device) -> const Address &
    {
        return detail::switchDevices[ static_cast<std::size_t>( device)].Where;
    }
} // namespace hal
