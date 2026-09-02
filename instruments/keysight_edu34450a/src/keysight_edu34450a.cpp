//
// hal::keysight_edu34450a::EDU34450A's real I/O: what this meter is actually
// told, and what its answers mean.
//
// The first .cpp in instruments/ -- every driver here was header-only, because
// a driver's Port-returning members are consumed as types by the call site and
// so have to be visible (see instruments/keysight_edu34450a/CMakeLists.txt,
// whose own comment anticipated this file). None of what is in here is: the
// command table, the range and resolution rules and the overload check depend
// on which *function* is being read and not on which C++ quantity type the
// answer will be wrapped in, so all of it is ordinary code over
// hal::keysight_edu34450a::Function.
//
// Which is worth more than the compile time it saves. Everything this driver
// puts on the wire is in one file of about two hundred lines, in the order the
// instrument sees it, and a bench engineer with the programmer's reference open
// can check it against the manual without reading a single template.
//
// -- The document this is written against ---------------------------------
//
// Keysight 34450A Programmer's Reference (the EDU34450A shares its command set
// -- the EDU is the education-market packaging of the same instrument, not a
// different SCPI dialect):
//
//     https://www.batronix.com/files/Keysight/DMM/34450A/34450A-Programming.pdf
//
// Every command below, every range value, the three resolution values and the
// overload sentinel are from that document, and the same rule the scope driver
// works under applies here: check the manual, not another program's source.
//
#include "hal/keysight_edu34450a.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "hal/io/transport.hpp"

namespace hal::keysight_edu34450a
{
    namespace
    {
        //
        // One row per measurement function: what to configure it with, what to
        // set its resolution with, and what to call it in a message.
        //
        // A table rather than a switch with the strings inline, for the reason
        // every table in this codebase is one -- it reads as the list it is,
        // and the two things that differ per function (whether a discrete
        // resolution applies, and what the SENSe path is called) sit in the same
        // row as the function they belong to instead of in a second switch
        // somewhere else.
        //
        struct FunctionCommands
        {
            //
            // The CONFigure command, short form. CONFigure both selects the
            // function and resets that function's measurement and trigger
            // parameters to their defaults -- which is why this driver needs no
            // *RST of its own: whatever the front panel or a previous run left
            // set on this function is overwritten before every reading.
            //
            std::string_view Configure;

            //
            // The SENSe path its resolution is set through, for the one case
            // where it cannot ride along on the CONFigure command -- see
            // resolutionFor() and read() below. Empty for the two functions
            // that have no discrete resolution at all.
            //
            std::string_view Resolution;

            //
            // Whether this function takes one of the three discrete resolution
            // values. Two do not, and neither is an oversight:
            //
            //   Capacitance  is fixed at 3.5 digits on this meter. There is no
            //                resolution parameter to send, and sending one is
            //                an error rather than a no-op.
            //
            //   Frequency    takes a resolution in *hertz* rather than one of
            //                the three fractions-of-range the other six take,
            //                so Resolution::Slow/Medium/Fast has nothing to say
            //                about it. Its CONFigure <range> argument is not a
            //                range either -- it is the approximate frequency of
            //                the input signal, which is what a
            //                core::MeasureSetup<Frequency>::Range happens to be
            //                exactly the right shape to carry (see the
            //                frequency() accessor).
            //
            bool             HasDiscreteResolution;

            //
            // What to call this function in an exception a human reads.
            // Deliberately the front panel's words rather than the SCPI
            // mnemonic: "DC voltage" is what the person holding the meter is
            // looking at, where "VOLT:DC" is what this file already says twice.
            //
            std::string_view Name;
        };

        [[nodiscard]]
        constexpr auto commandsFor( const Function function) -> FunctionCommands
        {
            switch( function)
            {
                case Function::DcVoltage:          return { "CONF:VOLT:DC", "VOLT:DC:RES", true,  "DC voltage"           };
                case Function::AcVoltage:          return { "CONF:VOLT:AC", "VOLT:AC:RES", true,  "AC voltage"           };
                case Function::DcCurrent:          return { "CONF:CURR:DC", "CURR:DC:RES", true,  "DC current"           };
                case Function::AcCurrent:          return { "CONF:CURR:AC", "CURR:AC:RES", true,  "AC current"           };
                case Function::Resistance:         return { "CONF:RES",     "RES:RES",     true,  "2-wire resistance"    };
                case Function::FourWireResistance: return { "CONF:FRES",    "FRES:RES",    true,  "4-wire resistance"    };
                case Function::Frequency:          return { "CONF:FREQ",    "",            false, "frequency"            };
                case Function::Capacitance:        return { "CONF:CAP",     "",            false, "capacitance"          };
            }

            //
            // Unreachable for any declared enumerator, and present so that
            // adding one to Function without adding a row here is a warning
            // this build turns into an error (-Wswitch under -Werror) rather
            // than a function that falls off its end.
            //
            return { "", "", false, "unknown function" };
        }

        //
        // The three values this meter's <resolution> parameter accepts, and
        // nothing else -- SCPI VOLT:DC:RES takes 1.5E-6, 2.0E-5 or 3.0E-5 and
        // answers -222 for anything in between.
        //
        // The mapping onto Slow/Medium/Fast is the one thing on this page the
        // programmer's reference does not state outright, so here is the
        // derivation rather than an assertion. The manual gives 1.50E-6 as the
        // default and labels it "5.5 digits", and says MIN selects the smallest
        // value accepted ("the highest resolution") and MAX the largest ("the
        // least resolution"). A coarser resolution is a shorter integration, so
        // least resolution is fastest: 1.5E-6 slow, 3.0E-5 fast, and 2.0E-5 the
        // one left in the middle. Which agrees with the data sheet's three
        // reading rates for the three front-panel speeds.
        //
        // Written as literals in the manual's own spelling rather than
        // formatted from a double -- these are three fixed tokens out of a
        // document, not computed values, and "1.5E-06" out of a formatter is a
        // needless second spelling of one of them.
        //
        [[nodiscard]]
        constexpr auto resolutionFor( const EDU34450A::Resolution resolution) -> std::string_view
        {
            switch( resolution)
            {
                case EDU34450A::Resolution::Slow:   return "1.5E-6";
                case EDU34450A::Resolution::Medium: return "2.0E-5";
                case EDU34450A::Resolution::Fast:   return "3.0E-5";
            }

            return "1.5E-6";
        }

        //
        // The model field of an *IDN? reply -- field two of five, comma
        // separated: "Keysight Technologies,EDU34450A,MY12345678,01.00-01.00".
        //
        // Returns an empty view for a reply that is not shaped like an *IDN?
        // answer at all, which the caller reports as a refusal rather than
        // guessing at. An instrument that cannot say what it is is not an
        // instrument to take readings from.
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

    auto EDU34450A::session() -> io::ScpiSession &
    {
        if( !mSession)
        {
            //
            // Opened from the address the rig table wrote down. A Simulated
            // address reaching here is a bug in this driver rather than in the
            // table -- rawMeasure() checks isSimulated() first -- and
            // hal::io::openTransport says so in the exception it throws for one.
            //
            mSession = std::make_unique<io::ScpiSession>( io::openTransport( mAddress));
        }

        if( mPrepared)
        {
            return *mSession;
        }

        //
        // The once-per-session exchange, and it happens here rather than beside
        // the construction above because a session arrives two ways: opened
        // from the address, or handed in by useTransport(). Preparing it where
        // it is *used* is what makes both go through this.
        //
        // Not marked prepared until both have succeeded, so a meter that failed
        // its identity check is asked again on the next reading rather than
        // being treated as verified. Which is the right way round for a bench:
        // the instrument that was off when the run started may be on now, and
        // the run has already failed the readings it took meanwhile.
        //
        // Whatever the last user of this meter left in its error queue is not
        // this run's, and would otherwise be reported against this run's first
        // command (the queue survives *RST, and this driver does not send one
        // -- see FunctionCommands::Configure). Drained before the identity
        // query rather than after, so that a stale entry cannot be mistaken for
        // the identity query having failed.
        //
        mSession->clearErrors();

        //
        // And then the single most useful question a session can ask, before
        // any reading is taken: what are you?
        //
        // A hostname in a rig table is a fact nothing checks -- a re-cabled
        // rack, a DHCP lease that moved, a copied row -- and the failure it
        // produces without this is a run full of readings from the wrong
        // instrument. Some of them would even pass. See verifyIdentity() for
        // what is accepted.
        //
        static_cast<void>( verifyIdentity());

        mPrepared = true;

        return *mSession;
    }

    auto EDU34450A::verifyIdentity() -> std::string
    {
        const std::string identity = mSession->identify();
        const auto        model    = modelOf( identity);

        //
        // Both models, and this driver is honestly for both. The EDU34450A and
        // the 34450A share one command set -- the whole reason this driver was
        // written against the latter's programmer's reference -- and differ in
        // packaging, the back panel's connectors and the front panel's badge.
        // A rig that has the rack-mount one wired to this driver is not making
        // a mistake, and refusing it would be this file inventing a difference
        // the instrument does not have.
        //
        // What is refused is everything else, which is the case worth catching:
        // an L4411A, a scope, or a power supply answering at the hostname this
        // row named.
        //
        if( model != "EDU34450A" && model != "34450A")
        {
            throw io::ScpiFault( mSession->description(), "*IDN?",
                io::ScpiError{ 0,
                    "expected an EDU34450A or a 34450A and found \"" + identity
                    + "\" -- check this instrument's address in the rig's instrument table" });
        }

        return identity;
    }

    auto EDU34450A::identity() -> std::string
    {
        //
        // session() has already asked and already checked the answer, so this
        // asks again rather than caching it. One extra round trip on a call
        // nothing takes per reading, against a cached string that would be a
        // second thing to keep true if the socket were ever reopened onto a
        // different box.
        //
        return session().identify();
    }

    auto EDU34450A::read( const Function function, const std::optional<double> range) -> double
    {
        auto &     scpi     = session();
        const auto commands = commandsFor( function);

        //
        // "CONF:VOLT:DC", "CONF:VOLT:DC 10", or "CONF:VOLT:DC 10,1.5E-6".
        //
        // The three forms are not a style choice, they are the instrument's
        // rule. Its <resolution> argument may only accompany an explicit
        // <range>: combined with autoranging it is refused, because the meter
        // cannot fix an integration time for a range it has not chosen yet. So
        // a reading with a range gets both in one command, and a reading
        // without one gets the resolution separately (below) or not at all.
        //
        std::string configure{ commands.Configure };

        if( range)
        {
            configure += " ";
            configure += io::ScpiSession::number( *range);

            if( commands.HasDiscreteResolution)
            {
                configure += ",";
                configure += resolutionFor( mResolution);
            }
        }

        //
        // checked(), not write(): a CONFigure this meter refused would
        // otherwise leave it measuring whatever it was measuring before, and
        // the READ? two lines down would return a perfectly plausible reading
        // of the wrong thing. That is the failure hal::io::ScpiSession::checked
        // exists for and its own comment argues at length; this is the call
        // site it was written for.
        //
        scpi.checked( configure);

        //
        // The autoranging case: the resolution could not ride along above, so
        // it goes as its own SENSe command -- and only when it is not already
        // what CONFigure just set. CONFigure resets the function's parameters
        // to their defaults, and this meter's default resolution is 5.5 digits,
        // which is exactly Resolution::Slow. So the common case sends nothing.
        //
        if( !range && commands.HasDiscreteResolution && mResolution != Resolution::Slow)
        {
            scpi.checked( std::string( commands.Resolution) + " " + std::string( resolutionFor( mResolution)));
        }

        //
        // READ? is CONFigure's documented partner: it moves the instrument from
        // idle to wait-for-trigger, takes the reading (the trigger source is
        // Immediate, which CONFigure has just set), and returns it.
        //
        // queryNumbers rather than queryNumber, for one reason that is entirely
        // about this instrument: READ? answers with *two* comma-separated
        // readings when the meter's secondary display is on, which is a front
        // panel state a previous user can leave behind and which this model's
        // SCPI offers no documented way to turn off. The primary reading is the
        // first, and the second is discarded -- this driver exposes no port for
        // it (see the class comment on the secondary display).
        //
        const double reading = scpi.queryNumbers( "READ?").front();

        if( io::ScpiSession::isOverload( reading))
        {
            //
            // The instrument's third answer: not a number, not a fault, and not
            // something to substitute a plausible value for. core::MeasureEngine
            // catches this, records the reading as NaN with this reason beside
            // it, fails whatever criterion was checking it, and carries on to
            // the next check -- see core::UnmeasurableReading, which exists for
            // exactly this and was written for a scope's unmeasurable rise
            // time.
            //
            // The reason names the range, because the range is the fix. An
            // overload on a manual range is a script asking for 100 V on the
            // 10 V range; an overload while autoranging is a real input beyond
            // 1000 V, or an open circuit on the ohms function, which is a
            // different conversation.
            //
            const std::string where = range
                ? "the " + io::ScpiSession::number( *range) + " range"
                : "autoranging";

            throw core::UnmeasurableReading(
                std::string( commands.Name) + " overload -- the input is beyond " + where);
        }

        return reading;
    }
} // namespace hal::keysight_edu34450a
