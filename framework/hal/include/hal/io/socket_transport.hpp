#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "hal/io/transport.hpp"

namespace hal::io
{
    //
    // SCPI over a raw TCP socket -- the transport an LXI instrument's port
    // 5025 speaks, and the only one this build implements.
    //
    // "Raw" is the operative word and is a choice rather than a shortcut. An
    // LXI box typically offers four ways in: VXI-11 (RPC, needs a portmapper
    // and an XDR layer), HiSLIP (its own handshake, message ids and
    // subaddressing), a Telnet-style command shell on 5023 that echoes and
    // prints prompts, and a raw socket on 5025 that carries exactly the bytes
    // written to it. The last one needs no library, no IDL and no dependency
    // -- a newline-terminated command in, a newline-terminated reply out --
    // and it is what every SCPI example in every Keysight programmer's
    // reference is written against.
    //
    // What it costs, stated plainly because a bench eventually meets both:
    // there is no service request, no status-byte poll and no device clear on
    // a raw socket. An instrument wedged mid-transfer is recovered by closing
    // the socket, which is what this class's destructor does, and not by the
    // 488.2 device-clear a VXI-11 or GPIB session would send. For a DMM
    // answering READ? that is a distinction without a difference; for an
    // instrument that signals asynchronously it would not be, and that
    // instrument's transport is a different implementation of the same
    // interface rather than a change here.
    //
    // -- Why the platform headers are not in this header --------------------
    //
    // The socket is an intptr_t below rather than a SOCKET or an int, and
    // there is no <winsock2.h> or <netdb.h> in sight. That is not tidiness:
    // <winsock2.h> defines a macro called `Rectangle`, redefines `min` and
    // `max` unless asked not to, and pulls a large fraction of the Win32 API
    // in behind it -- and this header is reachable from every driver that
    // opens a session, and therefore from the whole rig. The include belongs
    // in one .cpp, which is where it is.
    //
    class SocketTransport final : public ITransport
    {
        public:
            //
            // Connects, and throws TransportError if it cannot. There is no
            // open()/isOpen() pair and no disconnected state: an object of
            // this type is a connection, so a caller that holds one holds a
            // socket that was open at least once, and the two-phase
            // alternative would put an "am I connected" check at every call
            // site, where the answer is only ever discovered by trying.
            //
            // host is copied rather than kept as a view -- hal::Lan carries a
            // string_view into whatever the rig table wrote, and this object
            // outlives nothing in particular, so the one thing it must not do
            // is assume the view does.
            //
            SocketTransport( std::string_view host, int port, const TransportOptions & options);

            ~SocketTransport() override;

            SocketTransport( const SocketTransport &)                     = delete;
            auto operator=( const SocketTransport &) -> SocketTransport &  = delete;
            SocketTransport( SocketTransport &&)                          = delete;
            auto operator=( SocketTransport &&) -> SocketTransport &       = delete;

            auto send( std::string_view command) -> void override;

            [[nodiscard]]
            auto receive() -> std::string override;

            [[nodiscard]]
            auto description() const -> std::string override;

        private:
            //
            // Everything received and not yet handed out. A single receive()
            // from the kernel can carry the tail of one reply and the whole of
            // the next -- nothing about TCP respects message boundaries, which
            // is the one thing about a raw socket that catches people -- so the
            // line splitting happens here and the remainder waits for the next
            // call. See ITransport::receive() on why handing over both at once
            // would be worse than buffering.
            //
            std::string       mPending;

            std::string       mHost;
            int               mPort{};
            TransportOptions  mOptions;

            //
            // The OS socket handle, as the widest integer either platform's
            // own type fits in: an int on POSIX, a SOCKET (an unsigned pointer-
            // sized handle) on Windows. -1 is "closed" on both, since Windows'
            // own INVALID_SOCKET is (SOCKET)(~0), which is what -1 becomes.
            //
            std::intptr_t     mSocket{ -1 };
    };
} // namespace hal::io
