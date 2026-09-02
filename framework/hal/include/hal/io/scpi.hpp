#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "hal/io/transport.hpp"

namespace hal::io
{
    //
    // One entry out of an instrument's error queue: the number and the words.
    //
    // Both, not just the number, and not just the words. The number is what a
    // driver can branch on -- -113 is "undefined header", which means this
    // driver sent a command this model does not have, and is a different bug
    // from -222 "data out of range", which means it sent a legal command with
    // a value this model refuses. The words are what a bench engineer reads.
    // Keeping only one of the two has an obvious failure in each direction.
    //
    struct ScpiError
    {
        int         Code{};
        std::string Message;

        friend auto operator==( const ScpiError &, const ScpiError &) -> bool = default;
    };

    //
    // The instrument rejected something this driver sent.
    //
    // Not a TransportError, deliberately, even though it is thrown from the
    // same call. The bytes arrived, the instrument understood them well enough
    // to complain about them, and the socket is fine -- so the failure is in
    // the *driver* (a command this model does not have, a value outside its
    // range), or in a rig table that pointed a driver at the wrong model. A
    // caller that catches TransportError to mean "the bench is unreachable"
    // must not also catch this one, which would report a wrong command as a
    // missing instrument.
    //
    // Carries the command that provoked it as well as the queue entry, because
    // an error queue read some commands later is otherwise a number with no
    // subject: "-222,\"Data out of range\"" says nothing about *which* range,
    // where "CONF:VOLT:DC 5000 -> -222,\"Data out of range\"" says the whole
    // fault -- 5000 V on a 1000 V meter.
    //
    class ScpiFault : public std::runtime_error
    {
        public:
            ScpiFault( const std::string & instrument, const std::string_view command, ScpiError error) :
                std::runtime_error(
                    instrument + " rejected \"" + std::string( command) + "\": "
                    + std::to_string( error.Code) + ",\"" + error.Message + "\""),
                mCommand( command),
                mError(   std::move( error))
            {}

            [[nodiscard]]
            auto command() const -> std::string_view
            {
                return mCommand;
            }

            [[nodiscard]]
            auto error() const -> const ScpiError &
            {
                return mError;
            }

        private:
            std::string mCommand;
            ScpiError   mError;
    };

    //
    // SCPI on top of a transport: the layer that knows what a reply means.
    //
    // hal::io::ITransport carries a line of text each way and knows nothing
    // else. This knows the six things every SCPI instrument in this rig shares
    // -- how to identify itself, how to be reset, how to be asked whether it
    // has finished, how its error queue works, what its overload sentinel is,
    // and how a number is written into a command -- and nothing about any one
    // instrument. A driver adds the model-specific half: which subsystem, which
    // ranges, which functions (see
    // instruments/keysight_edu34450a/src/keysight_edu34450a.cpp for the first
    // one).
    //
    // -- Why the error queue is checked here rather than per driver ----------
    //
    // Because a driver that forgets is silent, and silently wrong.
    //
    // A SCPI instrument does not answer a bad command: it queues an error and
    // carries on. So a driver that sends "CONF:VOLT:DC 5000" to a 1000 V meter
    // and then sends "READ?" gets a perfectly good reading -- on whatever range
    // the meter was already using, which is not the range the script asked for
    // and is nowhere recorded. The reading is plausible, in-tolerance often
    // enough, and completely untraceable. That is the worst failure mode a test
    // rig has, and it is one line of SYST:ERR? away from being an exception
    // naming the command.
    //
    // Hence checked() below, and hence its being the shape a driver reaches
    // for by default rather than an option it remembers. The cost is one extra
    // round trip per configuration -- about a millisecond on a LAN box, against
    // the ~20 ms a single 5.5-digit reading takes -- and it is not paid per
    // reading: a driver configures once and reads many times (see the
    // EDU34450A driver, which checks after CONF and not after READ?).
    //
    // -- What is deliberately not here --------------------------------------
    //
    // Status-byte polling, service requests and device clear. All three need a
    // transport that has a control channel beside the data one -- a 488 line, a
    // VXI-11 RPC, a USBTMC control endpoint -- and the one transport this build
    // implements is a raw socket, which has none (see
    // hal/io/socket_transport.hpp). *OPC? below is the raw-socket way of
    // asking the same question as an SRQ-on-complete, and it is what an
    // instrument's own programming examples use.
    //
    // Binary block transfer (#800001024<bytes>) is not here either, and that
    // one is a real gap rather than a design decision: it is how a scope hands
    // over a waveform, and core::Waveform already exists to receive one (see
    // core/quantities/waveform.hpp). It needs a transport that can read a
    // counted number of bytes rather than a line, which is one more virtual on
    // ITransport, and it needs no instrument here yet.
    //
    class ScpiSession
    {
        public:
            //
            // Takes ownership of the transport, which is the whole reason this
            // is a class and not a set of free functions over an ITransport &:
            // a session is the thing whose lifetime the *connection* follows,
            // and a driver holding one holds the socket. Handing in an
            // already-open transport rather than an address is what lets a test
            // hand in a fake one -- see
            // instruments/keysight_edu34450a/tests.
            //
            explicit ScpiSession( std::unique_ptr<ITransport> transport);

            //
            // Send a command that has no reply. Unchecked: see checked() for
            // the one that reads the error queue afterwards, and this class's
            // own comment for why that is the one a driver should usually
            // reach for.
            //
            auto write( std::string_view command) -> void;

            //
            // Send a command and read the instrument's error queue afterwards,
            // throwing ScpiFault naming the command if anything is in it.
            //
            // Drains the queue rather than reading one entry: a single bad
            // command can queue two errors (a parse error and a settings
            // conflict behind it), and leaving the second one there would
            // attribute it to whatever command was sent next.
            //
            auto checked( std::string_view command) -> void;

            //
            // Send a query and return the reply with surrounding whitespace
            // removed.
            //
            // Not error-checked, and that is not an oversight: a query that the
            // instrument rejected does not reply at all, so the failure arrives
            // as a TransportTimeout from the transport rather than as anything
            // this function could inspect. Which is a poor diagnosis on its
            // own -- "timed out" for what is really "undefined header" -- so
            // see queryChecked() below, which turns the second into the first
            // by asking the queue once the timeout has happened.
            //
            [[nodiscard]]
            auto query( std::string_view question) -> std::string;

            //
            // query(), but a timeout is followed by a look at the error queue,
            // so a command this model does not have is reported as the
            // instrument's own words rather than as silence.
            //
            // Worth the extra round trip only on the timeout path, which is why
            // it is one function rather than a flag: the queue is read after
            // the failure, never before, so a query that works costs exactly
            // what query() costs.
            //
            [[nodiscard]]
            auto queryChecked( std::string_view question) -> std::string;

            //
            // A reply parsed as a number -- "+1.86850000E-03" is what a SCPI
            // instrument sends and 0.0018685 is what a driver wants.
            //
            // Throws rather than returning an optional or a NaN: a reply that
            // is not a number where a number was asked for means the session
            // has desynchronised (a reply left unread by an earlier query, most
            // likely), and every reading after it would be one reply behind.
            // That is not a condition to carry on through.
            //
            [[nodiscard]]
            auto queryNumber( std::string_view question) -> double;

            //
            // A reply parsed as a comma-separated list of numbers, which is
            // what SCPI sends when one query has several answers.
            //
            // Its own function rather than queryNumber() being lenient about
            // trailing text, and the reason is a trap worth knowing before
            // writing any driver here. "READ?" on the EDU34450A normally
            // returns one reading -- and returns *two*, comma-separated, if the
            // instrument's secondary display happens to be on, which is a front
            // panel state a previous user can leave behind and which no
            // documented SCPI command turns off. A strict queryNumber() then
            // fails with "not a number", which is a confusing report of a real
            // condition; a lenient one would silently take the primary and
            // never say that the reply had a second half. This says both: the
            // caller gets the list and decides.
            //
            // Never empty -- an empty or unparseable reply throws, for the same
            // desynchronisation reason queryNumber() does.
            //
            [[nodiscard]]
            auto queryNumbers( std::string_view question) -> std::vector<double>;

            //
            // *IDN? -- "Keysight Technologies,34450A,MY12345678,01.00-01.00".
            //
            // Which is the single most useful thing a bring-up run can do, and
            // the reason it is here rather than in a driver: it is the same
            // command on every SCPI instrument ever made, and asking it is how
            // you find out that the hostname in the rig table points at the
            // scope rather than the meter. A driver that cares can check the
            // model field (the EDU34450A's does -- see its verifyIdentity()).
            //
            [[nodiscard]]
            auto identify() -> std::string;

            //
            // *RST then *CLS: the instrument's own factory-reset state, and an
            // empty error queue and status register behind it.
            //
            // Both, in that order, and not one of them. *RST alone leaves
            // whatever was in the error queue before -- the queue is explicitly
            // not cleared by a reset -- so the first checked() after it would
            // attribute somebody else's error to this driver's command. *CLS
            // alone leaves the instrument configured however the last session
            // left it, which is the front panel's business and not something a
            // run should inherit.
            //
            auto reset() -> void;

            //
            // *OPC? -- blocks until every overlapped command has finished.
            //
            // On a DMM this is nearly always unnecessary, since READ? does not
            // return until it has a reading. It earns its place on the
            // instruction side: a supply told to ramp, or a scope told to run,
            // accepts the command and returns immediately, and a script that
            // measured straight afterwards would measure the old state. The
            // sourcing drivers do not use it yet because they do not open
            // sessions yet; this is the function they will use.
            //
            auto waitForComplete() -> void;

            //
            // One entry from SYST:ERR?, or nothing if the queue is empty.
            //
            [[nodiscard]]
            auto nextError() -> std::optional<ScpiError>;

            //
            // Read the queue until it is empty, discarding what was in it.
            //
            // For the start of a session, where whatever is queued belongs to
            // whoever had the instrument last and must not be attributed to
            // this run's first command.
            //
            auto clearErrors() -> void;

            [[nodiscard]]
            auto description() const -> std::string;

            //
            // The transport underneath -- for a driver that needs something
            // this class deliberately does not offer, and for a test that wants
            // to look at what was sent. A reference, not the pointer: the
            // session owns it and hands out no way to outlive it.
            //
            [[nodiscard]]
            auto transport() -> ITransport &;

            //
            // What an out-of-range reading comes back as, and how to spot one.
            //
            // "±9.9E+37" is what a Keysight DMM sends when the input is beyond
            // the range it was told to use (the front panel shows "OL"), and
            // 9.9E+37 is the SCPI-standard "not a number" sentinel for a
            // measurement that could not be made -- an Infiniium answers
            // 9.99999E+37 for an unmeasurable rise time in the same way (see
            // core::UnmeasurableReading, which is where a driver should turn
            // this into a reading a report can carry).
            //
            // Compared with a threshold rather than for equality, because the
            // exact sentinel differs by instrument family and every one of them
            // is far outside any real reading: no DMM measures 1e37 of
            // anything.
            //
            static constexpr double kOverloadThreshold = 9.0e37;

            [[nodiscard]]
            static auto isOverload( double reading) -> bool;

            //
            // A double written the way a SCPI command argument is written --
            // "0.1", "1000", "1.5E-06".
            //
            // Its own function because the default C++ spelling of a double is
            // wrong here in both directions: std::to_string gives
            // "0.100000" (six decimals, always) and std::format's shortest
            // representation can give "1e-06" in a locale-dependent way. This
            // gives the shortest exact form in the C locale, which is what an
            // instrument's parser expects.
            //
            [[nodiscard]]
            static auto number( double value) -> std::string;

        private:
            std::unique_ptr<ITransport> mTransport;
    };
} // namespace hal::io
