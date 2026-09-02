#include "hal/io/transport.hpp"

#include <concepts>
#include <type_traits>
#include <variant>

#include "hal/io/socket_transport.hpp"
#include "hal/io/visa_transport.hpp"

namespace hal::io
{
    namespace
    {
        //
        // The one address kind that needs no vendor software, and the one
        // message left that is not about VISA.
        //
        // hal::Simulated is not a gap: there is genuinely nothing at the other
        // end, which is what the type means. A driver reaching here with one
        // has skipped its own simulation hooks, and the message says so rather
        // than describing a missing feature.
        //
        constexpr auto kNoSimulated =
            "a Simulated address has nothing at the other end -- that is what the type means. A driver "
            "reaching hal::io::openTransport() with one has skipped its own simulation hooks; check for "
            "hal::Simulated before opening a session (see hal/driver/address.hpp).";
    } // namespace

    auto openTransport( const Address & address, const TransportOptions & options) -> std::unique_ptr<ITransport>
    {
        //
        // Two transports, and which one an address gets is a rule worth stating
        // in one place because it is the whole shape of this layer:
        //
        //   Lan                    a socket, always. Nothing needs installing,
        //                          the bytes are the protocol, and it is the
        //                          one transport here that a test can drive
        //                          against a real listener (see
        //                          tests/io/test_socket_transport.cpp).
        //
        //   Gpib, Usb, Serial      VISA. All three are reached through a driver
        //                          rather than through the operating system,
        //                          and on the machines this rig runs on --
        //                          Windows and Linux -- that driver arrives as
        //                          part of Keysight IO Libraries or NI-VISA.
        //                          Reimplementing the client side of software
        //                          the bench already has would mean, for USB on
        //                          Windows, taking the instrument away from the
        //                          vendor's own driver. See
        //                          hal/io/visa_transport.hpp.
        //
        //   Simulated              nothing, and not because it is unfinished.
        //
        // std::visit over the alternatives rather than a chain of
        // std::holds_alternative, for the reason every visit in this codebase
        // is one: adding a bus kind to hal::Address then fails to compile here
        // until this function has an answer for it. A chain of ifs would
        // silently fall through to whatever the last branch was.
        //
        return std::visit( [ &]( const auto & kind) -> std::unique_ptr<ITransport>
        {
            using KindT = std::decay_t<decltype( kind)>;

            if constexpr( std::same_as<KindT, Lan>)
            {
                return std::make_unique<SocketTransport>( kind.host, kind.port, options);
            }
            else if constexpr( std::same_as<KindT, Gpib>
                            || std::same_as<KindT, Usb>
                            || std::same_as<KindT, Serial>)
            {
                return openVisa( address, options);
            }
            else
            {
                static_assert( std::same_as<KindT, Simulated>,
                    "hal::io::openTransport() has no answer for this address kind -- add one");

                throw UnsupportedTransport( to_string( address) + std::string( ": ") + kNoSimulated);
            }
        }, address);
    }

    auto isSupported( const Address & address) -> bool
    {
        //
        // Answered without touching the network or the instrument, which is
        // the point: a bring-up report says which of a rack's instruments this
        // build could reach before it tries any of them.
        //
        // LAN always, since the socket needs nothing installed. The other three
        // depend on a VISA library being present -- so this is the one answer
        // here that differs between two machines running the same binary, and
        // a Simulated address is never openable at all.
        //
        if( std::holds_alternative<Lan>( address))
        {
            return true;
        }

        if( std::holds_alternative<Simulated>( address))
        {
            return false;
        }

        //
        // Serial is the one that can still be refused with VISA present: a
        // device path VISA cannot be given a resource name for (see
        // visaResourceFor). Asking for the resource is how that is known, and
        // it opens nothing.
        //
        if( std::holds_alternative<Serial>( address) && !visaResourceFor( address))
        {
            return false;
        }

        return visaLibrary().has_value();
    }
} // namespace hal::io
