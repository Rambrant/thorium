#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "hal/io/transport.hpp"

namespace hal::io
{
    //
    // SCPI through a vendor's VISA library -- which is how this codebase
    // reaches USB, GPIB and serial instruments, and the reason it does not
    // implement any of those three itself.
    //
    // -- Why VISA, having just argued for a raw socket ------------------------
    //
    // hal/io/socket_transport.hpp makes the case for talking to an LXI box over
    // its own port 5025 with no library in the way, and that case still holds:
    // the bytes are the protocol, and a dependency to carry them would be a
    // dependency for nothing. It does not transfer to the other three buses,
    // and this is the difference.
    //
    // A socket is a thing the operating system already provides. A GPIB
    // controller, a USBTMC endpoint and (on Windows) a COM port are things a
    // *driver* provides, and on the machines this rig runs on that driver is
    // installed as part of a VISA distribution -- Keysight's IO Libraries Suite
    // or NI-VISA, both of which cover Windows and Linux. Reimplementing the
    // client side of it means either duplicating what is already installed, or
    // fighting it:
    //
    //   USBTMC over libusb   works on Linux and macOS, and on Windows only
    //                        after re-binding the instrument away from the
    //                        vendor's own USBTMC driver (Zadig, WinUSB) -- at
    //                        which point the meter disappears from Connection
    //                        Expert and from every other VISA program on the
    //                        machine. That is not a trade a test rig should
    //                        make on the operator's behalf.
    //
    //   GPIB                 is a 488 library either way. linux-gpib and
    //                        NI-488.2 are both perfectly good, and both are
    //                        also *already reachable through VISA* on a machine
    //                        that has either.
    //
    //   serial               is termios or SetCommState, which is genuinely
    //                        easy -- and is the one of the three where writing
    //                        it natively is still defensible. See
    //                        visaResourceFor() on why a device path is the one
    //                        address this file cannot always translate.
    //
    // So one implementation of ITransport buys three bus kinds, on both target
    // platforms, using software the bench already has. That is the whole
    // argument, and it is why hal::io::openTransport() routes `Lan` to a
    // socket and the other three here.
    //
    // -- Loaded, not linked --------------------------------------------------
    //
    // No visa.h, no visa32.lib, no find_package: the library is opened at
    // runtime with dlopen/LoadLibrary and its seven entry points are looked up
    // by name (see the .cpp). Three things follow, and all three matter more
    // than the small amount of ugliness it costs.
    //
    // A build needs nothing installed. `hal` compiles and links on a machine
    // with no VISA at all -- a CI runner, this developer's Mac -- and
    // `openTransport` simply keeps refusing GPIB, USB and serial, now saying
    // that no VISA was found rather than that none is implemented. A
    // build-time dependency would have made the whole framework unbuildable
    // without vendor software.
    //
    // MinGW stops being a problem. The Windows presets here use mingw64, and
    // linking a Microsoft-toolchain import library from MinGW is a known
    // nuisance; GetProcAddress has no such trouble.
    //
    // And the version installed stops mattering. VISA's ABI is stable and
    // ancient -- these seven functions have had the same signatures since
    // VISA 2.0 -- so a library built against any of it works, and a rig can
    // upgrade IO Libraries without rebuilding this.
    //
    // What it costs: the signatures below are declared by hand rather than
    // included from the vendor, so a typo in one is a crash rather than a
    // compile error. They are written out in the .cpp with the vendor's own
    // typedefs beside them for exactly that reason.
    //
    // -- What this cannot be tested against ----------------------------------
    //
    // Itself, mostly, and that is worth saying plainly rather than leaving to
    // be discovered. VISA is closed vendor software that is not present on any
    // machine this repository is developed or tested on, so no test here opens
    // a session through it. What *is* tested is the part that is ordinary code
    // and the part that historically goes wrong: the resource strings built
    // from a hal::Address (visaResourceFor below), the serial-number matching
    // that finds a USB instrument among what VISA enumerated (usbSerialOf),
    // and the refusal path when no library loads.
    //
    // The rest -- whether viOpen on a real USBTMC meter behaves -- is
    // confirmed by running it on the bench, exactly as the socket transport's
    // SCPI was. See dev/README.md's table of which questions are answered
    // where.
    //
    class VisaTransport final : public ITransport
    {
        public:
            //
            // Opens `resource` through the VISA library, or throws.
            //
            // Takes a resource string rather than a hal::Address, and the
            // separation is deliberate: turning an address into a resource is
            // pure text (visaResourceFor) or an enumeration (openVisa below for
            // USB), both of which are testable, and this class is the part that
            // cannot be. Handing it the string keeps the untestable half as
            // small as it can be made.
            //
            VisaTransport( std::string_view resource, const TransportOptions & options);

            ~VisaTransport() override;

            VisaTransport( const VisaTransport &)                     = delete;
            auto operator=( const VisaTransport &) -> VisaTransport &  = delete;
            VisaTransport( VisaTransport &&)                          = delete;
            auto operator=( VisaTransport &&) -> VisaTransport &       = delete;

            auto send( std::string_view command) -> void override;

            [[nodiscard]]
            auto receive() -> std::string override;

            [[nodiscard]]
            auto description() const -> std::string override;

        private:
            std::string   mResource;
            std::string   mPending;

            //
            // The VISA session handle. A ViSession is a ViUInt32 on every
            // platform -- not a pointer, and not widened on 64-bit, which is
            // one of the few places VISA's ABI is surprising. Zero is VI_NULL.
            //
            std::uint32_t mSession{ 0 };
    };

    //
    // Which VISA library was loaded, if any -- "visa64.dll",
    // "/usr/lib/libvisa.so.0", or nothing.
    //
    // For a bring-up report saying what this build can reach before it tries
    // anything, and for a test that has to skip what it cannot exercise. The
    // library is loaded once per process on the first call and kept, so asking
    // is cheap after the first time.
    //
    // Names the file rather than answering a bool, because "which one" is the
    // question an operator with both Keysight's and NI's stacks installed
    // actually has -- two VISAs on one machine is a common and confusing state,
    // and the one that loaded is the one whose Connection Expert the
    // instrument has to appear in.
    //
    [[nodiscard]]
    auto visaLibrary() -> std::optional<std::string>;

    //
    // The VISA resource string for an address, where one can be written down
    // without asking VISA anything.
    //
    //     Gpib( 0, 14)              GPIB0::14::INSTR
    //     Gpib( 0, 14, 3)           GPIB0::14::3::INSTR
    //     Lan( "bench-dmm1")        TCPIP0::bench-dmm1::5025::SOCKET
    //     Serial( "COM3")           ASRL3::INSTR
    //     Usb( "MY60012345")        -- nothing: see below
    //     Simulated{}               -- nothing: there is no instrument
    //
    // std::nullopt means "not from the address alone", which is the case for
    // exactly two of the five and for two quite different reasons.
    //
    // A USB resource needs the instrument's vendor and product ids as well as
    // its serial number -- USB0::0x2A8D::0x1401::MY60012345::INSTR -- and
    // hal::Usb carries only the serial, on purpose (a bus/device number is
    // reassigned on every replug, and a vendor id is a fact about the model
    // rather than about the rig). So a USB instrument is *found* rather than
    // addressed: see openVisa(), which enumerates and matches on the serial.
    // That the address type turned out to carry exactly the field enumeration
    // needs is luck, but it is the good kind: it was chosen because it is the
    // field that stays true.
    //
    // A serial resource needs a port *number* on Windows -- ASRL3 -- and
    // hal::Serial carries whatever the operating system calls the port. "COM3"
    // translates; "/dev/ttyUSB0" does not, because the mapping from a Unix
    // device path to a VISA ASRL index is a fact about the VISA installation's
    // own configuration and not something derivable here. A path that is
    // already spelled as a resource ("ASRL1::INSTR") is passed through, which
    // is the escape hatch for a rig that knows its own aliases.
    //
    // A free function over the variant rather than a member, and returning
    // text rather than opening anything, so that the whole of this mapping is
    // testable on a machine with no VISA installed -- which is every machine
    // this repository is developed on. See tests/io/test_visa_transport.cpp.
    //
    [[nodiscard]]
    auto visaResourceFor( const Address & address) -> std::optional<std::string>;

    //
    // The serial number out of a VISA USB resource string -- field four of
    // "USB0::0x2A8D::0x1401::MY60012345::INSTR", so "MY60012345".
    //
    // Public because it is half of how a USB instrument is found and the half
    // that can be tested: openVisa() enumerates USB resources through VISA and
    // matches each one's serial against the address's. Empty for a string that
    // is not shaped like a USB resource, which the caller treats as "not a
    // match" rather than as an error -- VISA enumerating something unexpected
    // is not a reason to fail a run.
    //
    [[nodiscard]]
    auto usbSerialOf( std::string_view resource) -> std::string_view;

    //
    // Open an address through VISA: resolve it to a resource -- enumerating if
    // it is a USB serial number -- and connect.
    //
    // Throws UnsupportedTransport if no VISA library is installed, or if the
    // address is one VISA cannot be given (see visaResourceFor), and
    // TransportError if VISA is there and the instrument is not. The
    // not-found message lists the USB instruments VISA *did* enumerate, which
    // is the difference between "no meter" and "a different meter's serial
    // number in the rig table".
    //
    // hal::io::openTransport() is the normal way to reach this; it is public so
    // that a rig can force VISA for an address this build would otherwise
    // serve itself (a LAN instrument reached over HiSLIP rather than a raw
    // socket, say).
    //
    [[nodiscard]]
    auto openVisa( const Address & address, const TransportOptions & options = {}) -> std::unique_ptr<ITransport>;
} // namespace hal::io
