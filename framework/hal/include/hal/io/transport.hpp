#pragma once

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "hal/driver/address.hpp"

namespace hal::io
{
    //
    // How bytes actually leave the PC -- the half of an instrument that this
    // codebase modelled nowhere until now.
    //
    // hal/driver/address.hpp already says *where* an instrument is: which bus
    // kind, which host, which primary address. What it deliberately does not
    // say is how to get a command down that bus, and every driver in this tree
    // answered its readings out of its own mSimVoltage instead (see
    // hal::keysight_edu34450a::EDU34450A's simulation hooks, which are still
    // there and still what a test uses). This is the missing half: an Address
    // goes in, an object that can carry one SCPI line each way comes out.
    //
    // -- Why an interface, when there is exactly one implementation ----------
    //
    // Because there are three, and two of them are not sockets. A driver
    // reached over USBTMC frames its messages rather than terminating them; a
    // driver reached over 488 addresses its talker rather than connecting to
    // it; a driver under test wants neither and wants to be handed the reply
    // (see hal::io::ITransport's own users in
    // instruments/keysight_edu34450a/tests). All three are "a thing that
    // carries a command and hands back a response", and none of them is a
    // socket, so the socket cannot be the type a driver names.
    //
    // Which makes the fake the load-bearing case rather than a bonus. A driver
    // that talks to hardware through a concrete socket class can only be tested
    // against hardware, and a test that needs the bench is a test nobody runs
    // in CI -- so the SCPI strings a driver sends would be checked by a human
    // reading them, once, at the moment they were written. Against this
    // interface they are checked by an assertion: see
    // instruments/keysight_edu34450a/tests, which asserts the exact
    // "CONF:VOLT:DC 10,1.5E-6" the meter would have received.
    //
    // -- What this is not ---------------------------------------------------
    //
    // Not VISA, and not a replacement for it. VISA is a resource manager, a
    // discovery mechanism, an event/interrupt system and a formatted-I/O
    // library on top of a transport; this is the transport. A rig that wants
    // the rest can implement this interface over viWrite/viRead in about
    // thirty lines and keep every driver above it unchanged -- which is the
    // other reason the interface exists, and it is why openTransport() below
    // is a factory over hal::Address rather than a constructor a driver calls.
    //
    // Not a session, either. Nothing here knows what SCPI is: sending "*IDN?"
    // and reading a line back is this file's whole vocabulary, and the
    // question of what a reply means, what an error queue is and which
    // sentinel means "overload" belongs to hal/io/scpi.hpp above it.
    //
    class TransportError : public std::runtime_error
    {
        public:
            explicit TransportError( const std::string & what) : std::runtime_error( what) {}
    };

    //
    // The instrument did not answer inside the time it was given.
    //
    // Its own type rather than a flag on TransportError, because the two want
    // different reactions and only the caller knows which it can take. A
    // failed open is final -- there is nothing at that address, and retrying
    // reaches the same nothing. A timeout is not: a 4-wire ohms reading at
    // slow resolution takes about a second on the meter this landed for, an
    // autoranged capacitance reading can take several, and a caller that knows
    // which of those it asked for is entitled to wait again rather than fail a
    // test. Nothing in this tree retries yet, and that is a policy decision
    // deliberately left to whoever needs it rather than buried in here.
    //
    class TransportTimeout : public TransportError
    {
        public:
            explicit TransportTimeout( const std::string & what) : TransportError( what) {}
    };

    //
    // A bus kind this build cannot reach -- GPIB, USBTMC, or a serial port on
    // the platform that has no implementation for one.
    //
    // Its own type, and thrown from openTransport() rather than from the
    // driver, because the distinction it draws is the one a reader of the
    // failure most needs: "this rig's table says GPIB and nothing here speaks
    // GPIB" is a gap in *this library*, where a TransportError is a fact about
    // the bench. The message says which, and says what would close it.
    //
    // Note what this is not: a compile error. Which bus kinds a *driver* may be
    // addressed over is checked at compile time and always has been (see
    // hal::ReachableOver) -- that is a claim about the instrument's back panel,
    // which is fixed. Which bus kinds this *library* can open is a claim about
    // the library's own build, and a rig may legitimately declare an instrument
    // it never measures through; refusing to compile the table over it would
    // make an unbuilt half of the bench unbuildable.
    //
    class UnsupportedTransport : public TransportError
    {
        public:
            explicit UnsupportedTransport( const std::string & what) : TransportError( what) {}
    };

    //
    // How long the PC waits, and it is two numbers rather than one because
    // they answer to different hardware.
    //
    // Connect is a property of the network: an LXI box on the same subnet
    // answers a SYN in single-digit milliseconds, and a hostname that resolves
    // to nothing takes as long as the resolver takes. Two seconds is generous
    // for the first and short enough that an unattended run fails rather than
    // hangs on the second.
    //
    // Io is a property of the *measurement*, and is the one worth tuning per
    // instrument. Five seconds covers everything the EDU34450A does -- its
    // slowest reading is a 4-wire ohms at slow resolution, about a second --
    // and would be far too short for a scope averaging 1024 acquisitions or a
    // supply ramping to a settled output. A driver that knows it is asking for
    // something slow passes a bigger number rather than every driver paying
    // for the slowest one.
    //
    // Defaults on both, so a caller who has no opinion states none, and so the
    // numbers are written down exactly once.
    //
    struct TransportOptions
    {
        std::chrono::milliseconds ConnectTimeout{ 2000 };
        std::chrono::milliseconds IoTimeout{     5000 };
    };

    class ITransport
    {
        public:
            virtual ~ITransport() = default;

            //
            // Send one command. The *message framing* is the transport's, not
            // the caller's: a socket transport appends the newline an LXI
            // instrument's parser waits for, a USBTMC transport would wrap the
            // same bytes in a bulk-out header with an EOM bit instead, and a
            // caller that appended its own terminator would be wrong on one of
            // them. So callers pass "CONF:VOLT:DC 10" and never "CONF:VOLT:DC
            // 10\n" -- see hal::io::ScpiSession, which is the only caller in
            // this tree and enforces that by being the only one.
            //
            virtual auto send( std::string_view command) -> void = 0;

            //
            // Read one response, terminator stripped.
            //
            // One response and not "whatever has arrived": SCPI is strictly
            // request/response per query, so a read that returned two replies
            // in one string would have desynchronised the session by the time
            // anyone noticed. A transport that reads ahead therefore buffers
            // the remainder rather than handing it over (the socket one does).
            //
            // Throws TransportTimeout if nothing arrives in time, which is the
            // normal way a query for something the instrument refused to
            // answer fails -- an instrument that rejects a command queues an
            // error and stays silent, so the query it belonged to never
            // completes. hal::io::ScpiSession is what turns that silence into
            // the instrument's own words; see its own comment.
            //
            [[nodiscard]]
            virtual auto receive() -> std::string = 0;

            //
            // "Lan bench-dmm1:5025", "fake" -- for the text of an exception,
            // a journal line, or a bring-up log saying what was actually
            // opened. Deliberately the transport's own answer rather than
            // hal::to_string( address) at the call site: a transport that
            // resolved a hostname to a literal address, or that was handed in
            // by a test, knows something the table it came from does not.
            //
            [[nodiscard]]
            virtual auto description() const -> std::string = 0;
    };

    //
    // Open the transport an address names, or throw saying why not.
    //
    // A free function over hal::Address rather than a constructor per bus kind,
    // for the reason hal::Address is a variant in the first place: the address
    // is a *value* a rig table wrote down and a driver carries, and the driver
    // must not have to know which alternative it got. A driver holds an
    // Address, calls this once, and is done -- see
    // hal::keysight_edu34450a::EDU34450A::session().
    //
    // Returns a non-null pointer or throws. Never nullptr-on-failure: an
    // instrument that could not be opened is not a smaller instrument, it is a
    // rig fault, and a driver checking a pointer would be a driver that could
    // forget to.
    //
    // hal::Simulated throws UnsupportedTransport, and that is the one case
    // worth reading twice. It is not a gap: there is genuinely nothing at the
    // other end of a Simulated address, which is what the type means (see
    // hal/driver/address.hpp). A driver never reaches this with one, because a
    // driver checks for Simulated first and answers from its own simulation
    // hooks -- so this throw exists to catch the *driver* that forgot to,
    // rather than to describe the address.
    //
    [[nodiscard]]
    auto openTransport( const Address & address, const TransportOptions & options = {}) -> std::unique_ptr<ITransport>;

    //
    // Whether this build could open that address at all, without trying.
    //
    // For a bring-up tool or a rig self-check that wants to report "three of
    // these five instruments are reachable by this build" before touching any
    // of them -- as against calling openTransport() and catching, which
    // reaches the network to answer a question about the library.
    //
    [[nodiscard]]
    auto isSupported( const Address & address) -> bool;
} // namespace hal::io
