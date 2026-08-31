#pragma once

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace hal
{
    //
    // How the PC reaches an instrument -- the one fact about a rig's
    // instruments that this codebase modelled nowhere until now. Everything
    // else was already here: which instruments the rig has
    // (rig/instrument.inc), which matrix/mux channel each one's leads land on
    // and which VPC pin the DUT sees them at (rig/wiring.inc, see
    // hal/topology/wiring.hpp). All of that describes the *signal* side, from the
    // instrument's terminals onwards. This describes the *control* side: the
    // bus, cable or socket a driver sends "VOLT 24,(@2)" down in the first
    // place.
    //
    // Both are rig facts and both live in the rig's own .inc files, but they
    // are independent facts, the same way an N6701A module's mainframe slot
    // is independent of its matrix channel (see hal::keysight_n6701a::N6701A's own comment):
    // re-cabling a supply's output to a different VPC pin doesn't change its
    // GPIB address, and moving the whole rack onto a different subnet doesn't
    // move a single relay.
    //
    // One struct per bus kind rather than one VISA resource string
    // ("GPIB0::14::INSTR"), and this is the same argument hal::racal1260::Parity/
    // hal::racal1260::StopBits already make against the legacy test language's
    // "BaudRate=9600 StopBits=1 WordLength=8 Parity=NONE" (see
    // hal/racal1260.hpp): a string is a runtime typo the rig can only
    // discover at the moment the port refuses to open, which on a test bench
    // is halfway into an unattended run. A struct is a typo the compiler
    // catches. If a real driver eventually wants the VISA spelling, it can
    // build one from these fields; it cannot get these fields back out of a
    // string it was handed.
    //
    // Values, not template parameters, deliberately -- see each driver's
    // constructor and hal::ReachableOver below for the one part of this that
    // *is* checked at compile time, and why that part is the transport
    // *kind* rather than the address itself.
    //

    //
    // IEEE-488. board is which GPIB interface the PC has (0 for the usual
    // single-interface machine) -- the index the 488 layer assigns it, not a
    // slot: these days the interface is far more likely a USB-attached
    // controller hanging off the side of the rack than a card inside the PC,
    // and the two are the same "gpib0" to everything above them. primary is
    // the instrument's own address on that bus, set by DIP switches or a
    // front-panel menu on the instrument, which is why it is a rig fact and
    // not a discoverable one. Secondary addressing is rare enough on modern
    // instruments to be optional, but a plug-in card cage that subdivides one
    // primary address does use it.
    //
    // Which makes this the clearest example of what these structs name: the
    // protocol the driver speaks, not the cabling it reaches the instrument
    // through. An instrument on a USB-GPIB controller is at a Gpib address --
    // it answers to 488 commands at a primary address, and the USB cable is
    // the controller's business, invisible to everything here. It is not a Usb
    // address, which means something else entirely (see below).
    //
    struct Gpib
    {
        int                board{ 0 };
        int                primary{};
        std::optional<int> secondary{};

        friend constexpr auto operator==( const Gpib &, const Gpib &) -> bool = default;
    };

    //
    // Raw SCPI over TCP -- what an LXI instrument's port 5023/5025 socket
    // speaks, and what VXI-11/HiSLIP would sit on top of. Host is a hostname
    // or a dotted quad; a hostname is worth preferring on a bench where the
    // rack gets its addresses from DHCP, since it survives a lease change
    // that a literal address in this table would not.
    //
    struct Lan
    {
        std::string_view host{};
        int              port{ 5025 };

        friend constexpr auto operator==( const Lan &, const Lan &) -> bool = default;
    };

    //
    // A serial port on the PC itself -- "/dev/ttyUSB0", "COM3". This is the
    // port the *driver* talks through, which stays a different fact from
    // hal::racal1260::Racal1260's RS232 framing (see hal/racal1260.hpp) even when the two
    // land on the same hardware: the framing is what gets switched onto the
    // DUT and configured as part of a test, this is how the PC reaches
    // whatever provides it.
    //
    // Ser1 on this rig is the case where they do coincide -- one PC port,
    // addressed here and framed there -- and its driver also accepts Gpib for
    // the arrangement where a chassis module provides the port instead. Which
    // of the two the bench has is not yet known; see that driver's own
    // constructor comment.
    //
    struct Serial
    {
        std::string_view device{};

        friend constexpr auto operator==( const Serial &, const Serial &) -> bool = default;
    };

    // USBTMC -- the instrument itself is the USB device, speaking the USB
    // Test & Measurement Class, which is a different thing from "reached
    // through something that happens to be on USB" (see Gpib above: an
    // instrument behind a USB-GPIB controller is at a Gpib address). If
    // unplugging the box removes one instrument, this is the right kind; if it
    // removes a whole bus worth of them, that bus's kind is.
    //
    // Identified by the instrument's own serial number rather than by a
    // bus/device number, because the latter is reassigned on every replug and
    // would make this table wrong the first time somebody moved a cable.
    //
    struct Usb
    {
        std::string_view serialNumber{};

        friend constexpr auto operator==( const Usb &, const Usb &) -> bool = default;
    };

    //
    // No instrument at the other end at all -- the driver's own simulation
    // hooks are the instrument (setSimulatedVoltage() and friends). Every
    // driver accepts this one regardless of what its real back panel has, see
    // ReachableOver below, and every driver test uses it: a test constructing
    // hal::keysight_l4411a::L4411A{ InstrumentId::Dmm1, Simulated{} } is saying something true
    // about what it is testing, where inventing a plausible GPIB address for
    // a thing that will never be opened would not be.
    //
    // Which also makes it the honest answer for a rig that has not been built
    // yet -- but this rig's instrument.inc deliberately does NOT use it (see
    // its own TODO(bench) comments). The addresses there are provisional and
    // marked as such, which a reviewer can act on; Simulated{} in that table
    // would read as a decision rather than a gap.
    //
    struct Simulated
    {
        friend constexpr auto operator==( const Simulated &, const Simulated &) -> bool = default;
    };

    //
    // Every bus kind an instrument on this rig can be reached over. A variant
    // rather than a base class with virtuals: an address is a value a driver
    // stores and eventually std::visit()s to open a session, never something
    // polymorphically owned, and this way it stays a literal type -- a rig
    // can declare `inline constexpr auto DcMainframe = Gpib( 0, 14);` above
    // its instrument table and hand the same constant to all four modules
    // that share that mainframe.
    //
    using Address = std::variant<Gpib, Lan, Serial, Usb, Simulated>;

    //
    // Which of those a given driver can actually be reached over -- the one
    // compile-time half of this. An L4411A is an LXI box with LAN and USB on
    // the back and no GPIB connector at all, so
    //
    //     INSTRUMENT( L4411A, Dmm1, Gpib( 0, 14))
    //
    // is not a configuration mistake to be discovered on the bench, it is a
    // sentence about hardware that does not exist. Each driver's constructor
    // is constrained by this concept naming its own back panel, so that line
    // fails to compile with "no matching constructor" -- the same guarantee
    // hal::keysight_n6701a::SwitchableIsolation gives Connect() on a relay-less supply (see
    // hal/keysight_n6701a.hpp), and for the same reason: the alternative is a runtime
    // throw partway into a run.
    //
    // Simulated satisfies this unconditionally, so a driver lists only the
    // connectors its real panel has and every driver is still constructible
    // for a test. That is a deliberate hole in the check and worth naming:
    // Simulated is the one address that cannot be wrong, because there is
    // nothing at the other end of it to disagree with.
    //
    // Note what is NOT compile-time here: the address itself. A GPIB address
    // is a number a technician set on a rear-panel switch, and the driver's
    // API is identical whichever number it is -- there is nothing for a type
    // to protect. Making it a template parameter would give every driver a
    // second parameter that changes nothing about what compiles, and would
    // multiply hal::keysight_n6701a::N6701A's two aliases (N6701ADirect/N6701ARelay, which
    // exist purely to keep instrument.inc readable) by one per transport.
    // Template parameter when it changes what compiles, constructor argument
    // when it does not, is the rule this codebase already follows.
    //
    template<typename AddressT, typename... Supported>
    concept ReachableOver = std::same_as<AddressT, Simulated> || ( std::same_as<AddressT, Supported> || ...);

    //
    // "Gpib 0::14", "Lan bench-dmm1:5025", "Simulated" -- for a log line, an
    // error message, or a run journal recording what the rig was actually
    // talking to. The bus kind's name comes from the alternative's own type
    // name by reflection (see the implementation), not from a second spelling
    // of "Gpib" in a string, the same reasoning as core::meta::to_string()
    // over an enum's enumerators.
    //
    [[nodiscard]]
    auto to_string( const Address & address) -> std::string;
} // namespace hal
