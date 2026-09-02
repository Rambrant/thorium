//
// hal::keysight_edu36311a::EDU36311A's real I/O: what this supply is actually
// told, in the order it is told it, and what its answers mean.
//
// The third .cpp in instruments/, after the meter's and the scope's, and the
// first belonging to a *source*. Everything here is ordinary code over an
// int channel number and two optional doubles -- no templates -- so the whole
// of what this driver puts on the wire is one file a bench engineer can check
// against the programming guide with the guide open beside it. The header's
// own comment on namespace detail says why that split had to be free functions
// here rather than out-of-line members.
//
// -- The documents this is written against -------------------------------
//
// Keysight E36300 Series Programming Guide, for every command and query below:
//
//     https://www.batronix.com/files/Keysight/Netzgeraete/E36300-Programming.pdf
//
// Keysight EDU36311A Triple Output DC Power Supply Data Sheet, for the three
// outputs' ratings and settling times (the ones in the header's Output1..
// Output3 and in applyOutput's own comment):
//
//     https://www.batronix.com/files/Keysight/Netzgeraete/EDU36311A-Data_Sheet.pdf
//
// Two documents rather than one, and the split is not tidiness -- it is the
// one thing about this model a driver can get wrong by reading only the
// obvious source. The programming guide is written for the E36300 series, and
// the EDU36311A runs that series' command set: every command here is straight
// out of it and none of them is adapted. But the guide's *range table* is the
// E36311A's -- 6 V / 5 A, +25 V / 1 A, -25 V / 1 A, one of them a negative
// rail -- and this instrument is 6 V / 5 A, 30 V / 1 A, 30 V / 1 A with no
// negative output at all. So: commands from the guide, numbers from the data
// sheet, and the rule this repo already works under holds unchanged (check the
// manual, not another program's source).
//
// -- Why every command here names its channel as a number ----------------
//
// The series offers two ways to say which output a command is for. There is a
// modal one -- INSTrument:SELect P6V, then commands with no channel on them at
// all -- and there is the channel list, "(@2)", carried by each command.
//
// This driver only ever uses the second, for two reasons that are both about
// this model rather than about taste. The output *names* the modal form takes
// are the E36311A's: P6V, P25V and N25V, of which the last is a -25 V rail
// this box does not have. A driver written around those names would be
// spelling a channel in a vocabulary that does not describe the instrument it
// is talking to. And a modal selection is state on the instrument that
// survives between commands -- so three drivers sharing one chassis (DcP5,
// DcP6 and DcP7 do, see rig/instrument.inc) would be three objects taking
// turns changing one global, and an Apply on one output would be wrong
// whenever another had selected itself in between. A channel list makes each
// command say what it is for, which is exactly what three independent drivers
// behind one address need.
//
// The guide's own trap, encoded below: a channel list on a *query* must be
// separated from the "?" by a space ("MEAS:VOLT? (@2)"), or the instrument
// answers -103 "invalid separator". A channel list on a *command* is preceded
// by a comma ("OUTP 1, (@2)"). The two are not the same punctuation and the
// difference is not a mistake in this file.
//
#include "hal/keysight_edu36311a.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "hal/io/transport.hpp"

namespace hal::keysight_edu36311a::detail
{
    namespace
    {
        //
        // "(@2)" -- what every command below ends with.
        //
        [[nodiscard]]
        auto channelList( const int channel) -> std::string
        {
            return "(@" + std::to_string( channel) + ")";
        }

        //
        // "VOLT 24, (@2)" -- a setting command with its value and its channel.
        //
        // The value goes down the wire through hal::io::ScpiSession::number,
        // which is the shortest exact form in the C locale: std::to_string
        // would send "24.000000" and a locale-dependent formatter could send a
        // comma where the instrument expects a point, which on a command whose
        // arguments are already comma separated is not a small mistake.
        //
        [[nodiscard]]
        auto setting( const std::string_view command, const double value, const int channel) -> std::string
        {
            return std::string( command) + " " + io::ScpiSession::number( value) + ", " + channelList( channel);
        }

        //
        // The model field of an *IDN? reply -- field two of four, comma
        // separated: "Keysight Technologies,EDU36311A,CN61130007,1.0.2-1.0.1".
        //
        // Returns an empty view for a reply that is not shaped like an *IDN?
        // answer at all, which the caller reports as a refusal rather than
        // guessing at. An instrument that cannot say what it is is not an
        // instrument to drive a DUT rail with.
        //
        [[nodiscard]]
        auto modelOf( const std::string_view identity) -> std::string_view
        {
            const auto first = identity.find( ',');

            if( first == std::string_view::npos)
            {
                return {};
            }

            const auto second = identity.find( ',', first + 1);

            return identity.substr( first + 1,
                second == std::string_view::npos ? std::string_view::npos : second - first - 1);
        }
    } // namespace

    auto openSession( const Address & address) -> std::unique_ptr<io::ScpiSession>
    {
        return std::make_unique<io::ScpiSession>( io::openTransport( address));
    }

    auto prepare( io::ScpiSession & session) -> void
    {
        //
        // Whatever the last user of this supply left in its error queue is not
        // this run's, and would otherwise be reported against this run's first
        // command -- the queue survives *RST, and this driver sends no *RST
        // (see program() on why). Drained before the identity query rather
        // than after, so that a stale entry cannot be mistaken for the
        // identity query having failed.
        //
        session.clearErrors();

        //
        // And then the single most useful question a session can ask, before
        // anything is programmed: what are you?
        //
        // A hostname in a rig table is a fact nothing checks -- a re-cabled
        // rack, a DHCP lease that moved, a copied row -- and on a *source* the
        // failure that produces is worse than a wrong reading. "VOLT 30, (@2)"
        // sent to whatever else answered at that address is a command some
        // instruments will accept.
        //
        const std::string identity = session.identify();
        const auto        model    = modelOf( identity);

        //
        // This model and no other, which is a narrower rule than the meter
        // driver's (hal::keysight_edu34450a::EDU34450A accepts its
        // professional sibling too, because the two really are one instrument
        // in two boxes). Here the sibling is the difference: an E36311A shares
        // this command set exactly and has a -25 V rail on channel 3 and a
        // 25 V limit on channel 2 where this driver believes there is 30 V.
        // A driver that accepted one would program a rail past its rating on
        // channel 2 and reverse the polarity a DUT sees on channel 3, and
        // would do it silently, because both commands are legal.
        //
        // The other E36300 models are refused for the same reason and not for
        // a worse one: an E36312A or E36313A is a triple-output supply with
        // its own ranges, and a rig that has one wants a driver whose Output
        // tags say so.
        //
        if( model != "EDU36311A")
        {
            throw io::ScpiFault( session.description(), "*IDN?",
                io::ScpiError{ 0,
                    "expected an EDU36311A and found \"" + identity
                    + "\" -- check this instrument's address in the rig's instrument table. "
                      "Note an E36311A is NOT this instrument: same commands, different "
                      "ranges, and a negative rail on output 3" });
        }
    }

    auto identify( io::ScpiSession & session) -> std::string
    {
        return session.identify();
    }

    auto program( io::ScpiSession & session, const int channel,
                  const std::optional<double> volts,
                  const std::optional<double> amps,
                  const std::optional<double> overVolts) -> void
    {
        //
        // The order is the whole content of this function, and it is not the
        // order the builder chain happens to be written in.
        //
        //   1  the current limit, because a limit that arrives after the
        //      voltage has already been applied is a limit that was not in
        //      force while the rail came up -- which is exactly the moment a
        //      DUT with a short across it draws everything the supply has.
        //
        //   2  the overvoltage trip level, because it has to be above the
        //      setpoint before the setpoint exists. Sent the other way round,
        //      a config raising both (say 5 V and a 7 V trip, from a previous
        //      3 V and 4 V) would set 5 V against a 4 V trip that is still in
        //      place, and the instrument would trip its own output off inside
        //      5 ms.
        //
        //   3  the voltage setpoint.
        //
        //   4  and only then the output on. An output enabled before its
        //      setpoint is programmed delivers whatever the last setpoint was
        //      -- which after a safe() is zero, and after somebody else's run
        //      is anything.
        //
        // What is deliberately not sent first: *RST. It would zero all three
        // outputs, and the other two belong to two other drivers behind this
        // same address (see this file's own comment) -- so a reset on an Apply
        // to DcP6 would drop DcP5's rail. The meter driver can afford CONFigure
        // resetting its function before every reading precisely because a
        // meter has one input; a shared chassis cannot.
        //
        // Each through checked(), never write(): a SCPI instrument does not
        // answer a bad command, it queues an error and carries on -- so a
        // refused "VOLT 30, (@2)" followed by an accepted "OUTP 1, (@2)" is a
        // rail at the *old* voltage with a script believing it set a new one,
        // and on a source that is a DUT under the wrong conditions rather than
        // merely a wrong number in a report. See hal::io::ScpiSession::checked,
        // whose own comment argues this at length for the reading case.
        //
        // One checked() per command, and not a single drain at the end, which
        // is the shape that would save round trips. The ordering above is the
        // whole safety argument, and it only holds if each step is known to
        // have landed before the next is sent: a refused current limit
        // followed by a voltage and an enable is a rail coming up with the
        // *previous* limit in force, and a drain afterwards would report that
        // as one error against a sequence already carried out. The cost is
        // four SYST:ERR? round trips per Apply, about a millisecond each on a
        // LAN box against the 80 ms the rail itself takes to settle.
        //
        if( amps)
        {
            session.checked( setting( "CURR", *amps, channel));
        }

        if( overVolts)
        {
            session.checked( setting( "VOLT:PROT", *overVolts, channel));
        }

        //
        // A config that named no voltage still programs one, and it is zero --
        // the same thing an Apply of a bare DcP6.dc() has always meant (see
        // hal::keysight_n6701a::applyDriver, which does the value_or in the
        // ADL target). Sent rather than skipped, so that "Apply with nothing
        // set" is a defined rail rather than whatever the output was last left
        // at.
        //
        session.checked( setting( "VOLT", volts.value_or( 0.0), channel));

        session.checked( setting( "OUTP", 1.0, channel));

        //
        // And wait until the instrument has finished with all of it.
        //
        // This is the call hal::io::ScpiSession::waitForComplete() was put
        // there for and that no driver had yet used -- its own comment names
        // this case exactly: "a supply told to ramp, or a scope told to run,
        // accepts the command and returns immediately, and a script that
        // measured straight afterwards would measure the old state."
        //
        // What *OPC? does and does not buy, stated precisely, because the
        // difference is a whole class of flaky test:
        //
        //   it does   guarantee the instrument has parsed and executed all
        //             four commands. Command processing on this model is under
        //             10 ms, so this returns promptly and the next line cannot
        //             race the enable.
        //
        //   it does not guarantee the rail has arrived. Up-programming settles
        //             to within 1% of the excursion in about 80 ms;
        //             down-programming into no load takes about 500 ms, since
        //             nothing but the output capacitor's own leakage is
        //             discharging it. A criterion checked immediately after an
        //             Apply that lowers a rail can legitimately fail on a rail
        //             still on its way down.
        //
        // The second is hardware and belongs to the script, which is the only
        // thing that knows what it is waiting for. This driver's job is to
        // stop being the reason a script cannot tell the two apart.
        //
        session.waitForComplete();
    }

    auto disableOutput( io::ScpiSession & session, const int channel) -> void
    {
        //
        // The output off, and nothing else -- the setpoint stays where the
        // script put it (see EDU36311A::removeOutput on why that differs from
        // safe()).
        //
        // *OPC? afterwards for the same reason program() ends with one, and it
        // matters more here than it looks: the line after a Remove is very
        // often a Disconnect, which moves the isolation relay in this rail's
        // lead. The interlock asks the instrument whether the output is live
        // before allowing that (see EDU36311A::isEnabled), and an OUTP 0 that
        // had not yet been executed would be answered by an instrument that is
        // still on -- which is the correct answer to the question asked and
        // the wrong outcome for the script. Waiting here is what makes "off,
        // then open the relay" mean what it says.
        //
        session.checked( setting( "OUTP", 0.0, channel));

        session.waitForComplete();
    }

    auto measure( io::ScpiSession & session, const int channel, const Reading reading) -> double
    {
        //
        // MEASure asks for a fresh reading of the output's own sense point --
        // not the setpoint, and not a cached value. Note the space before the
        // channel list, which a query needs and a command must not have (see
        // this file's own comment).
        //
        // No checked() and no error-queue read: a query the instrument
        // rejected does not reply at all, so the failure arrives as a
        // TransportTimeout rather than as anything a SYST:ERR? here could turn
        // into a better message. queryNumber does the parse, and throws rather
        // than returning a sentinel if the reply is not a number -- which
        // would mean the session had desynchronised, and every reading after
        // it would be one reply behind.
        //
        // No overload sentinel to check for either, unlike the meter's read
        // (see hal::keysight_edu34450a). A supply measures its own output on
        // its own fixed range; there is no input that can be beyond it, so
        // there is no +9.9E+37 to arrive here and nothing for
        // core::UnmeasurableReading to carry.
        //
        const std::string_view query = reading == Reading::Voltage ? "MEAS:VOLT? " : "MEAS:CURR? ";

        return session.queryNumber( std::string( query) + channelList( channel));
    }

    auto outputIsOn( io::ScpiSession & session, const int channel) -> bool
    {
        //
        // "OUTP? (@2)" answers 1 or 0. Asked of the instrument rather than
        // remembered, because the cases that make this question worth asking
        // are the ones this process did not cause: an output left on by a
        // previous run, switched on from the front panel, or brought up by a
        // coupled channel.
        //
        // One documented exception, and it is worth knowing rather than
        // guarding against: with output sequencing configured, this query
        // returns the *configured* state rather than the actual one, so an
        // output with a 10 s rise delay answers 1 the moment it is told to
        // turn on. This driver programs no delays (see the header on what is
        // deliberately not modelled), and the direction of the error is the
        // safe one anyway -- an output about to come up reads live, and the
        // interlock treats a relay in its path as hot.
        //
        return session.queryNumber( "OUTP? " + channelList( channel)) != 0.0;
    }

    auto sendSafe( io::ScpiSession & session, const int channel) -> void
    {
        //
        // Off first, then the setpoint zeroed -- in that order, because the
        // point of safing is that the DUT stops being driven as soon as
        // possible and only then that an accidental re-enable finds nothing
        // waiting for it. Zeroing first would leave the output on while it
        // ramped down, which is the same end state reached the slower way.
        //
        // write(), not checked(): checked() reads the error queue, which is a
        // reply to wait for, and this runs on a path where the instrument may
        // already be unreachable. It would also throw hal::io::ScpiFault --
        // which is not a TransportError and so would escape the catch below,
        // out of a safing pass that hal::safeRig() does not catch, abandoning
        // the safing of every instrument after this one.
        //
        // Both sends are inside one try rather than one each, deliberately: if
        // the first failed because the supply is gone, the second will fail the
        // same way, and there is nothing for a second attempt to achieve.
        //
        try
        {
            session.write( setting( "OUTP", 0.0, channel));
            session.write( setting( "VOLT", 0.0, channel));
        }
        catch( const io::TransportError &)
        {
            //
            // Swallowed on purpose. A supply that cannot be reached cannot be
            // safed over the wire, and saying so here would replace the run's
            // real failure with this one. What is left is the front panel and
            // the mains switch, which is a bench procedure rather than
            // something this function can do.
            //
        }
    }
} // namespace hal::keysight_edu36311a::detail
