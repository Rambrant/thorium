//
// hal::keysight_33522b::Wfg33522B's real I/O: what this generator is actually
// told, in the order it is told it, and what its answers mean.
//
// The fourth .cpp in instruments/, after the meter's, the scope's and the
// supply's, and the second belonging to a *source*. Everything here is
// ordinary code over an int channel number and a struct of optional doubles --
// no templates -- so the whole of what this driver puts on the wire is one
// file a bench engineer can check against the programming guide with the guide
// open beside it. The header's own comment on namespace detail says why the
// split had to be free functions here rather than out-of-line members.
//
// -- The document this is written against --------------------------------
//
// Keysight Trueform Series Operating and Service Guide (part number
// 33500-90901), whose "SCPI Programming Reference" chapter is the command
// reference for the 33500B and 33600A Series -- there is no separate
// programmer's manual for this family:
//
//     https://www.ee.columbia.edu/sites/default/files/content/docs/Keysight_33500B_Manual.pdf
//
// One document rather than two, which is the opposite of the EDU36311A's
// situation next door (commands from the E36300 guide, numbers from the
// EDU36311A data sheet) and worth saying so that nobody goes looking for the
// second one. This guide covers both series and both bandwidth groups, and it
// states which numbers belong to which: its "Models and Options" chapter has
// a maximum-frequency table per group, and the 33522B's are the 33500 Series
// higher-frequency ones (30 MHz sine/square/pulse/noise, 200 kHz
// ramp/triangle). Those live on the shape tags in the header; everything in
// this file is a command.
//
// -- Why every command here names its channel in the header --------------
//
// This instrument spells a channel as a *suffix on the keyword*, not as a
// channel list: SOURce1:/SOURce2: for the signal settings, OUTPut1/OUTPut2 for
// the connector. There is no "(@1)" here and no INSTrument:SELect either --
// which is worth stating explicitly, because the supply driver two directories
// over spends a long comment choosing between exactly those two forms, and a
// reader arriving from it will look for the same choice. There is none to
// make: this family has one way to say which channel, it is per-command, and
// it leaves no modal state behind between commands.
//
// The [SOURce[1|2]:] prefix is optional in the guide's syntax and this driver
// always writes it. "FREQ 1000" is legal and means channel 1; "SOUR2:FREQ
// 1000" is the only way to say channel 2, and a file where half the commands
// carry a channel and half imply one is a file where a copied line silently
// programs the wrong output.
//
// -- Why not APPLy -------------------------------------------------------
//
// The guide offers APPLy:SINusoid <freq>,<ampl>,<offset> as a one-command way
// to do most of what program() below does, and it is the wrong tool here for
// three separate reasons, each of which alone would settle it.
//
// It discards the shape parameters. "For square waveforms, APPLy:SQUare
// replaces the current duty cycle setting with 50%", and APPLy:RAMP "overrides
// the current symmetry setting and selects 100%" -- so a driver built on APPLy
// could not honour .dutyCycle( 25.0) at all, whatever order it sent things in.
//
// It enables the output as part of the same command, which takes away the one
// ordering guarantee that matters: that nothing appears at the connector until
// every setting is in place.
//
// And it reaches settings nobody asked it to. APPLy turns off any modulation,
// sweep or burst in force, forces the trigger source to IMMediate, and
// overrides the voltage autorange setting. Those are reasonable defaults for a
// front-panel-like convenience command and they are not this driver's to
// impose: a rig that has set something up outside this driver's vocabulary
// (see session(), which exists for exactly that) would find it silently
// undone by the next Apply.
//
#include "hal/keysight_33522b.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "hal/io/transport.hpp"

namespace hal::keysight_33522b::detail
{
    namespace
    {
        //
        // "SOUR2:" -- the prefix every signal setting carries.
        //
        [[nodiscard]]
        auto source( const int channel) -> std::string
        {
            return "SOUR" + std::to_string( channel) + ":";
        }

        //
        // "OUTP2" -- the connector, whose channel is part of the keyword
        // rather than a subsystem prefix. Note the asymmetry with source()
        // above is the instrument's own and not a slip: OUTPut[1|2] is a root
        // keyword with a numeric suffix, where SOURce[1|2] is a prefix on
        // whatever follows it.
        //
        [[nodiscard]]
        auto output( const int channel) -> std::string
        {
            return "OUTP" + std::to_string( channel);
        }

        //
        // "SOUR1:FREQ 10000" -- a setting command with its value.
        //
        // The value goes down the wire through hal::io::ScpiSession::number,
        // which is the shortest exact form in the C locale: std::to_string
        // would send "10000.000000" and a locale-dependent formatter could
        // send a comma where the instrument expects a point.
        //
        [[nodiscard]]
        auto setting( const std::string_view command, const double value) -> std::string
        {
            return std::string( command) + " " + io::ScpiSession::number( value);
        }

        //
        // The model field of an *IDN? reply -- field two of four, comma
        // separated: "Keysight Technologies,33522B,MY59003130,0.179-1.19-8.88-52-00".
        //
        // Returns an empty view for a reply that is not shaped like an *IDN?
        // answer at all, which the caller reports as a refusal rather than
        // guessing at.
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
        // Whatever the last user of this generator left in its error queue is
        // not this run's, and would otherwise be reported against this run's
        // first command. Drained before the identity query rather than after,
        // so that a stale entry cannot be mistaken for the identity query
        // having failed.
        //
        session.clearErrors();

        const std::string identity = session.identify();
        const auto        model    = modelOf( identity);

        //
        // The model field only, and deliberately not the manufacturer one.
        // This series shipped as an Agilent product and is a Keysight product
        // now, so the same instrument answers "Agilent Technologies,33522B,..."
        // or "Keysight Technologies,33522B,..." depending on when it was
        // built and what firmware is on it. Both are this instrument. Insisting
        // on the badge would refuse a box on the strength of an acquisition.
        //
        // This model and no other, which is the same narrow rule
        // hal::keysight_edu36311a::EDU36311A applies to its own sibling, and
        // for a sharper reason: every other member of the 33500B Series speaks
        // this exact command set, so *nothing this driver sends would be
        // refused* by the wrong one. A 33521B (one channel) would accept every
        // SOUR1: command and answer -113 only for SOUR2:, so half a rig would
        // work. A 33512B (20 MHz) would accept SOUR1:FREQ 25e6, clamp it, queue
        // a "Data out of range" -- and this driver, believing 30 MHz, would
        // have let the script ask for it in the first place. The identity
        // check is the only thing standing between those and a run that looks
        // fine.
        //
        if( model != "33522B")
        {
            throw io::ScpiFault( session.description(), "*IDN?",
                io::ScpiError{ 0,
                    "expected a 33522B and found \"" + identity
                    + "\" -- check this instrument's address in the rig's instrument table. "
                      "Note the rest of the 33500B Series speaks the same commands and would "
                      "accept most of what this driver sends: the other models differ in "
                      "channel count (33509B/11B/19B/21B have one) and in bandwidth "
                      "(the 20 MHz models, where this driver believes 30)" });
        }
    }

    auto identify( io::ScpiSession & session) -> std::string
    {
        return session.identify();
    }

    auto program( io::ScpiSession & session, const Program & waveform) -> void
    {
        //
        // The order is the whole content of this function, and it is not the
        // order the builder chain happens to be written in. Every step below
        // is here because the step after it means something different if it
        // has not happened yet.
        //
        //   1  the termination, because it is the frame of reference for every
        //      voltage that follows. The guide: "The specified value is used
        //      for amplitude, offset, and high/low level settings", and
        //      changing it re-scales them by two. Sent afterwards instead, a
        //      3 Vpp amplitude programmed against 50 Ohm silently becomes
        //      6 Vpp the moment the load is set to INF -- the same signal at
        //      the connector, and not the one the script asked for.
        //
        //   2  the function, because the frequency and amplitude limits are
        //      function-dependent. This instrument does not refuse an
        //      out-of-range frequency and stop; it clamps. So a FREQ sent
        //      while the previous shape is still selected is validated against
        //      the *previous* shape's ceiling, and then clamped again when the
        //      function changes under it -- twice wrong, and both times
        //      silently.
        //
        //   3  the frequency.
        //
        //   4  then the shape parameter -- duty cycle or symmetry -- because a
        //      duty cycle's own limits depend on the frequency just set: this
        //      model holds a 16 ns minimum pulse width, so the achievable
        //      range narrows from 0.01-99.99% at low frequencies to 16-84% at
        //      10 MHz. Sent before the frequency, a legal 25% at 1 kHz would be
        //      clamped by a later move to 10 MHz.
        //
        //   5  the amplitude.
        //
        //   6  and only then the offset, because setting the amplitude can
        //      move it: "Setting amplitude from the remote interface can change
        //      the offset in order to achieve the desired amplitude." The
        //      other way round, an offset the script chose is overwritten by
        //      one the instrument chose, and the instrument does not consider
        //      that an error worth stopping for.
        //
        //   7  the output on, last. OUTPut closes a relay "without zeroing
        //      output voltage", so whatever is programmed at that instant is
        //      what appears at the connector -- which after a safe() is
        //      nothing, and after somebody else's run is anything.
        //
        // What is deliberately not sent first: *RST. It would reset *both*
        // channels, and the two belong to one driver only in the sense that
        // one instrument owns them -- a script driving a stimulus on channel 1
        // and a clock on channel 2 would lose the clock on its next Apply. The
        // meter driver can afford CONFigure resetting its function before
        // every reading precisely because a meter has one input.
        //
        // Each through checked(), never write(): a SCPI instrument does not
        // answer a bad command, it queues an error and carries on. On this
        // instrument that is worse than usual, because "carries on" means
        // "keeps generating" -- a refused FREQ followed by an accepted OUTP ON
        // is a DUT being driven at the last run's frequency while the script
        // believes it set a new one.
        //
        // One checked() per command, and not a single drain at the end, which
        // is the shape that would save round trips. The ordering above is the
        // whole argument, and it only holds if each step is known to have
        // landed before the next is sent.
        //
        const auto prefix    = source( waveform.Channel);
        const auto connector = output( waveform.Channel);

        if( !waveform.Load.empty())
        {
            session.checked( connector + ":LOAD " + std::string( waveform.Load));
        }

        session.checked( prefix + "FUNC " + std::string( waveform.Function));

        if( waveform.Hertz)
        {
            session.checked( setting( prefix + "FREQ", *waveform.Hertz));
        }

        if( waveform.ShapePercent && !waveform.ShapeCommand.empty())
        {
            session.checked( setting( prefix + std::string( waveform.ShapeCommand), *waveform.ShapePercent));
        }

        //
        // Amplitude in volts peak-to-peak, and the units are never sent.
        //
        // VOLTage takes an optional unit suffix ("VOLT 3.0 VRMS") and
        // VOLTage:UNIT sets a persistent default for commands that omit one --
        // which means a bare "VOLT 3" means whatever the last user of this box
        // left VOLT:UNIT at. This driver does not send VOLT:UNIT (a per-Apply
        // mode change that outlives the Apply is exactly the modal state this
        // file avoids elsewhere); it puts the unit on the value instead, where
        // it cannot be undone by anything.
        //
        // core::quantities::Voltage carries no notion of peak-to-peak against
        // rms, so "VPP" here is this driver stating which of the two its
        // builder means -- see the header, where .amplitude() says so too.
        //
        if( waveform.Volts)
        {
            session.checked( setting( prefix + "VOLT", *waveform.Volts) + " VPP");
        }

        if( waveform.OffsetVolts)
        {
            session.checked( setting( prefix + "VOLT:OFFS", *waveform.OffsetVolts));
        }

        session.checked( connector + " ON");

        //
        // And wait until the instrument has finished with all of it.
        //
        // *OPC? guarantees the commands above have been parsed and executed,
        // which on this instrument is very nearly the same as the output having
        // arrived -- a generator's new waveform is present within a cycle or
        // two, where a power supply's rail is still on its way for tens or
        // hundreds of milliseconds after its own *OPC? returns (see
        // hal::keysight_edu36311a's program(), which has to say the opposite).
        //
        // What it does not cover is the output relay OUTPut ON just closed:
        // the guide notes the output "may glitch for about a millisecond until
        // signal stabilizes". A scope armed on the very first edge after an
        // Apply can catch that glitch, which is a fact about the hardware and
        // belongs to the script -- this driver's job is to stop being the
        // reason a script cannot tell the two apart.
        //
        session.waitForComplete();
    }

    auto disableOutput( io::ScpiSession & session, const int channel) -> void
    {
        //
        // The connector off, and nothing else -- every setting stays where the
        // script put it (see Wfg33522B::removeOutput on why that differs from
        // safe()).
        //
        // *OPC? afterwards for the same reason program() ends with one: the
        // interlock asks whether an output is live before a contact in its
        // path moves, and an OUTP OFF that had not yet been executed would be
        // answered by an instrument that is still on.
        //
        session.checked( output( channel) + " OFF");

        session.waitForComplete();
    }

    auto outputIsOn( io::ScpiSession & session, const int channel) -> bool
    {
        //
        // "OUTP2?" answers 1 or 0. Asked of the instrument rather than
        // remembered, because the cases that make this question worth asking
        // are the ones this process did not cause: an output left on by a
        // previous run, or switched on from the front panel.
        //
        // One case where an attached instrument turns its own output off and
        // this query is how a driver finds out: the guide notes that excessive
        // external voltage applied to a channel connector disables the output
        // and posts an error. A generator whose output has been back-driven by
        // the DUT reads 0 here while this driver's remembered state says 1,
        // and the instrument is right.
        //
        return session.queryNumber( output( channel) + "?") != 0.0;
    }

    auto sendSafe( io::ScpiSession & session, const int channel) -> void
    {
        //
        // Off first, then the signal collapsed -- in that order, because the
        // point of safing is that the DUT stops being driven as soon as
        // possible and only then that an accidental re-enable finds nothing
        // waiting for it.
        //
        // This is deliberately the reverse of the guide's own advice for
        // ordinary operation, which is to minimise the amplitude and zero the
        // offset *before* changing the output state, so that the output relay
        // does not glitch. That advice is about signal integrity on a working
        // bench. Safing runs after a failure, where a millisecond of glitch on
        // a connector being switched off is not the hazard and a DUT still
        // being driven is.
        //
        // VOLT MIN rather than VOLT 0: this instrument's minimum amplitude is
        // 1 mVpp and zero is not a legal amplitude, so "VOLT 0" would be
        // refused -- which on this path would be refused *silently*, since
        // write() does not read the queue. MIN is the instrument's own word
        // for the bottom of its range and cannot be out of date.
        //
        // write(), not checked(): checked() reads the error queue, which is a
        // reply to wait for, and this runs on a path where the instrument may
        // already be unreachable. It would also throw hal::io::ScpiFault --
        // which is not a TransportError and so would escape the catch below,
        // out of a safing pass that hal::safeRig() does not catch, abandoning
        // the safing of every instrument after this one.
        //
        // All three sends are inside one try rather than one each,
        // deliberately: if the first failed because the generator is gone, the
        // others will fail the same way.
        //
        const auto prefix = source( channel);

        try
        {
            session.write( output( channel) + " OFF");
            session.write( prefix + "VOLT MIN");
            session.write( prefix + "VOLT:OFFS 0");
        }
        catch( const io::TransportError &)
        {
            //
            // Swallowed on purpose. A generator that cannot be reached cannot
            // be safed over the wire, and saying so here would replace the
            // run's real failure with this one. What is left is the front
            // panel and the mains switch, which is a bench procedure rather
            // than something this function can do.
            //
        }
    }
} // namespace hal::keysight_33522b::detail
