//
// hal::keysight_34980a::Chassis's real I/O: what this mainframe is actually
// told, and what its answers mean.
//
// The fourth .cpp in instruments/ and the first belonging to a switching
// device -- an instrument row that measures and sources nothing. See the
// header's preamble on why a switch/measure mainframe lands in this directory
// at all, why the mainframe rather than its modules is the thing with a
// driver, and why it became an instrument row.
//
// -- The document this is written against ---------------------------------
//
// Agilent 34980A Multifunction Switch/Measure Unit Programmer's Reference,
// version 2.1:
//
//     https://documentation.help/34980A/documentation.pdf
//
// Every command below, the channel-list punctuation, the Analog Bus relay
// numbers, the RF-module ROUT:OPEN exception and the empty-slot reply are from
// that document. The rule this repo works under holds unchanged: check the
// manual, not another program's source.
//
// Keysight's own asset link for this manual
// (keysight.com/us/en/assets/9018-61230/...) serves an HTML landing page to
// curl and WebFetch, the same as every other Keysight manual this repo cites.
//
// -- Two punctuations, and they are not interchangeable -------------------
//
// A channel list is written "(@sccc)" -- slot 1-8, then the channel, three
// digits. Two things about how it attaches to a command bite, and both are the
// instrument's rule rather than a style choice here:
//
//   a command takes the list as its argument with a space and no comma:
//   "ROUT:CLOS (@1003)". Unlike the E36300-series supplies (see
//   instruments/keysight_edu36311a), where a setting command puts a comma
//   before its channel list because the *value* comes first. Here there is no
//   value -- the list is the whole argument.
//
//   a *range* is written "(@1005:1010)" and this driver never writes one. Not
//   frugality: a range silently skips every Analog Bus relay it spans, and
//   errors outright if one is an endpoint, so a range is a spelling whose
//   meaning depends on what is plugged in. An explicit comma-separated list
//   means what it says on any rack.
//
#include "hal/keysight_34980a.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hal/io/transport.hpp"

#include "core/session/bench.hpp"

namespace hal::keysight_34980a
{
    namespace
    {
        //
        // "1003" -- one channel as the four digits the mainframe reads, slot
        // first.
        //
        // Note the channel is padded to three digits and the slot is not
        // padded at all: "(@1003)" is slot 1 channel 003, and "(@13)" would be
        // slot 1 channel 3 written as two digits, which the instrument reads as
        // slot 1, channel 3 -- or as nothing, depending on how it splits. The
        // padding is what makes the boundary unambiguous, and it is why this is
        // a function rather than string concatenation at four call sites.
        //
        [[nodiscard]]
        auto digitsOf( const ChannelAddress channel) -> std::string
        {
            std::string number = std::to_string( channel.Number);

            //
            // Three digits, left-padded. A channel above 999 cannot be written
            // at all, which channel<>() rejects at compile time and validate()
            // does not check -- it checks the slot. This is the last line of
            // defence and it truncates nothing: a wider number simply comes out
            // wider, reaches the instrument as a malformed list, and is refused
            // by name. Silently dropping a digit would be worse than that in
            // every way.
            //
            while( number.size() < 3)
            {
                number.insert( number.begin(), '0');
            }

            return std::to_string( channel.Slot) + number;
        }

        //
        // The vendor/model/serial/firmware fields of a comma-separated
        // identity reply -- *IDN?'s and SYST:CTYP?'s are the same shape.
        //
        // Returns as many fields as there were, so a caller can tell a reply
        // that is not shaped like an identity at all (fewer than two fields)
        // from one that is merely short.
        //
        [[nodiscard]]
        auto fieldsOf( const std::string_view reply) -> std::vector<std::string>
        {
            std::vector<std::string> fields;

            //
            // The instrument encloses SYST:CTYP?'s answer in double quotes and
            // does not enclose *IDN?'s. Stripped here rather than in one of the
            // two callers, so that neither has to know which of them it is.
            //
            std::string_view body = reply;

            if( body.size() >= 2 && body.front() == '"' && body.back() == '"')
            {
                body = body.substr( 1, body.size() - 2);
            }

            for( std::size_t start = 0; start <= body.size(); )
            {
                const auto comma = body.find( ',', start);

                fields.emplace_back( body.substr(
                    start, comma == std::string_view::npos ? std::string_view::npos : comma - start));

                if( comma == std::string_view::npos)
                {
                    break;
                }

                start = comma + 1;
            }

            return fields;
        }

        //
        // Surrounding whitespace off one field. The transport already trims the
        // reply as a whole (see hal::io::ScpiSession::query), but the fields
        // between the commas are the instrument's own spacing.
        //
        [[nodiscard]]
        auto trimmed( const std::string & field) -> std::string
        {
            const auto first = field.find_first_not_of( " \t");

            if( first == std::string::npos)
            {
                return {};
            }

            const auto last = field.find_last_not_of( " \t");

            return field.substr( first, last - first + 1);
        }

        //
        // ROUT:MOD:WAIT and ROUT:OPEN:ALL both take {1-8|SLOT1-SLOT8|ALL}, and
        // both default to ALL. Written as the bare number rather than as
        // "SLOT3", which the instrument accepts equally -- the number is what
        // the rest of this driver and a rig's future slot column will hold, and
        // one spelling is one thing to get right.
        //
        constexpr std::string_view kAllSlots = "ALL";
    } // namespace

    auto channelList( const ChannelAddress channel) -> std::string
    {
        return "(@" + digitsOf( channel) + ")";
    }

    auto channelList( const std::vector<ChannelAddress> & channels) -> std::string
    {
        //
        // An empty list is not written as "(@)" -- which the instrument would
        // refuse -- and it is not silently turned into a no-op command either,
        // because a caller that computed an empty path has a bug worth seeing.
        // The callers below never send anything for an empty list; this is what
        // that decision looks like from here.
        //
        std::string list = "(@";

        for( std::size_t index = 0; index < channels.size(); ++index)
        {
            if( index != 0)
            {
                list += ",";
            }

            list += digitsOf( channels[ index]);
        }

        return list + ")";
    }

    auto Chassis::validate( const int slot) -> void
    {
        if( !isSlot( slot))
        {
            throw NoSuchSlot( slot);
        }
    }

    auto Chassis::validate( const std::vector<ChannelAddress> & channels) -> void
    {
        //
        // Every channel checked before any command is built, so a list with one
        // bad slot in it sends nothing at all rather than closing the good half
        // and then throwing. On a switching device a half-executed route is
        // worse than none: it is a path nobody wrote down.
        //
        for( const auto channel : channels)
        {
            validate( channel.Slot);
        }
    }

    auto Chassis::session() -> io::ScpiSession &
    {
        //
        // The box's session (see hal::BoxConnection), opened on first use by
        // any face of it and prepared once for this driver: whatever the last
        // user left in the error queue drained first, so that a stale entry
        // cannot be mistaken for the identity query failing, then the model
        // checked. Not marked prepared until both succeed, so an instrument
        // that failed its identity check is asked again next time.
        //
        return mConnection.session( "keysight_34980a", [ this]( io::ScpiSession & opened)
        {
            opened.clearErrors();

            static_cast<void>( verifyIdentity( opened));
        });
    }

    auto Chassis::verifyIdentity( io::ScpiSession & opened) -> std::string
    {
        return detail::verifyMainframe( opened);
    }

    auto detail::verifyMainframe( io::ScpiSession & opened) -> std::string
    {
        const std::string identity = opened.identify();
        const auto        fields   = fieldsOf( identity);

        //
        // Field two of four is the model. A reply with fewer than two fields is
        // not an identity at all and is refused rather than guessed at: a box
        // that cannot say what it is is not a box to route a DUT through.
        //
        const std::string model = fields.size() >= 2 ? trimmed( fields[ 1]) : std::string{};

        //
        // The model only, and the vendor field deliberately not checked. This
        // mainframe was an Agilent product and is a Keysight one, the same unit
        // either way, and its own programmer's reference says "Agilent
        // Technologies" on every example -- so a rack may hold two badges of
        // one box and refusing either would be inventing a difference the
        // hardware does not have. Contrast
        // hal::keysight_edu36311a::EDU36311A, which refuses its sibling on
        // purpose: there the two boxes really are different instruments
        // sharing a command set.
        //
        // What is refused is everything else, which is the case worth catching:
        // a DMM, a scope or a supply answering at the address a rig's switching
        // was written against.
        //
        if( model != "34980A")
        {
            throw io::ScpiFault( opened.description(), "*IDN?",
                io::ScpiError{ 0,
                    "expected a 34980A and found \"" + identity
                    + "\" -- check this chassis's address against the rack" });
        }

        return identity;
    }

    auto Chassis::identity() -> std::string
    {
        //
        // session() has already asked and already checked the answer, so this
        // asks again rather than caching it -- one round trip on a call nothing
        // makes per operation, against a cached string that would be a second
        // thing to keep true if the socket were reopened onto a different box.
        //
        return session().identify();
    }

    // ---------------------------------------------------------------------
    // The switching face
    // ---------------------------------------------------------------------

    auto Chassis::simulating() const -> bool
    {
        return isSimulated() || !core::bench().isAttached();
    }

    auto Chassis::close( const ChannelAddress channel) -> void
    {
        close( std::vector<ChannelAddress>{ channel });
    }

    auto Chassis::close( const std::vector<ChannelAddress> & channels) -> void
    {
        validate( channels);

        if( channels.empty())
        {
            return;
        }

        if( simulating())
        {
            simulatedClose( channels);

            return;
        }

        session().checked( "ROUT:CLOS " + channelList( channels));
    }

    auto Chassis::open( const ChannelAddress channel) -> void
    {
        open( std::vector<ChannelAddress>{ channel });
    }

    auto Chassis::open( const std::vector<ChannelAddress> & channels) -> void
    {
        validate( channels);

        if( channels.empty())
        {
            return;
        }

        if( simulating())
        {
            simulatedOpen( channels);

            return;
        }

        //
        // Sent unconditionally, including to a slot holding an RF multiplexer
        // that will refuse it -- see the header's own comment on open() for why
        // this driver does not try to know better, and what to use instead.
        // checked() is what turns that refusal into a sentence naming the
        // command rather than a relay that quietly did not move.
        //
        session().checked( "ROUT:OPEN " + channelList( channels));
    }

    auto Chassis::closeExclusively( const ChannelAddress channel) -> void
    {
        closeExclusively( std::vector<ChannelAddress>{ channel });
    }

    auto Chassis::closeExclusively( const std::vector<ChannelAddress> & channels) -> void
    {
        validate( channels);

        if( channels.empty())
        {
            return;
        }

        if( simulating())
        {
            //
            // "Exclusive" is per module, so the simulation has to drop every
            // channel in each affected *slot* before closing these -- not the
            // whole chassis, and not just the named channels' own numbers. A
            // simulation that only added the new channels would make this
            // indistinguishable from close(), which is precisely the difference
            // a test of an RF bank is checking.
            //
            std::vector<ChannelAddress> survivors;

            for( const auto closed : mSimClosed)
            {
                const bool sameSlot = std::ranges::any_of( channels,
                    [ closed]( const ChannelAddress wanted) { return wanted.Slot == closed.Slot; });

                if( !sameSlot)
                {
                    survivors.push_back( closed);
                }
            }

            mSimClosed = std::move( survivors);

            simulatedClose( channels);

            return;
        }

        session().checked( "ROUT:CLOS:EXCL " + channelList( channels));
    }

    auto Chassis::isClosed( const ChannelAddress channel) -> bool
    {
        validate( channel.Slot);

        if( simulating())
        {
            return std::ranges::find( mSimClosed, channel) != mSimClosed.end();
        }

        //
        // "1" if closed, "0" if open. A space before the channel list, as every
        // query on every SCPI instrument in this tree needs -- and note this is
        // the one command here whose list punctuation differs from the commands
        // above, which take theirs after a space too but are not queries. The
        // difference that bites on other boxes (a comma before a command's
        // list) does not arise here, because a switching command has no value
        // in front of the list.
        //
        return session().queryNumber( "ROUT:CLOS? " + channelList( channel)) != 0.0;
    }

    auto Chassis::openAll() -> void
    {
        if( simulating())
        {
            mSimClosed.clear();

            return;
        }

        session().checked( "ROUT:OPEN:ALL " + std::string( kAllSlots));
    }

    auto Chassis::openAll( const int slot) -> void
    {
        validate( slot);

        if( simulating())
        {
            std::erase_if( mSimClosed,
                [ slot]( const ChannelAddress closed) { return closed.Slot == slot; });

            return;
        }

        session().checked( "ROUT:OPEN:ALL " + std::to_string( slot));
    }

    auto Chassis::waitForSwitching() -> void
    {
        if( simulating())
        {
            //
            // Nothing to wait for, and nothing to pretend: a simulated relay
            // moved the instant it was asked to. Worth an explicit branch
            // rather than falling through to a query, so that a detached run
            // does not open a session purely to wait for hardware that is not
            // there.
            //
            return;
        }

        //
        // The query form, not the command form. Both wait; the query returns
        // "1" when the wait is over, which means a caller finds out that the
        // mainframe answered rather than only that the bytes were sent. Same
        // reasoning as *OPC? over *WAI.
        //
        static_cast<void>( session().queryNumber( "ROUT:MOD:WAIT? " + std::string( kAllSlots)));
    }

    auto Chassis::waitForSwitching( const int slot) -> void
    {
        validate( slot);

        if( simulating())
        {
            return;
        }

        static_cast<void>( session().queryNumber( "ROUT:MOD:WAIT? " + std::to_string( slot)));
    }

    // ---------------------------------------------------------------------
    // What is in the rack
    // ---------------------------------------------------------------------

    auto Chassis::moduleIn( const int slot) -> ModuleIdentity
    {
        validate( slot);

        if( simulating())
        {
            if( const auto found = mSimModules.find( slot); found != mSimModules.end())
            {
                return found->second;
            }

            //
            // An empty slot, which is what a simulated mainframe nobody has
            // told about any modules honestly has in all eight.
            //
            return ModuleIdentity{ "", "", "", "", true };
        }

        const std::string reply  = session().queryChecked( "SYST:CTYP? " + std::to_string( slot));
        const auto        fields = fieldsOf( reply);

        ModuleIdentity module;

        module.Vendor   = fields.size() > 0 ? trimmed( fields[ 0]) : std::string{};
        module.Model    = fields.size() > 1 ? trimmed( fields[ 1]) : std::string{};
        module.Serial   = fields.size() > 2 ? trimmed( fields[ 2]) : std::string{};
        module.Firmware = fields.size() > 3 ? trimmed( fields[ 3]) : std::string{};

        //
        // The instrument's way of saying "nothing here" is
        // "Agilent Technologies,0,0,0" -- an identity reply with zeros in it,
        // not an error and not an empty string. Recognised here so that a
        // caller comparing Model against a part number does not have to know
        // that "0" is a sentinel, which is exactly the kind of thing that gets
        // read once and forgotten.
        //
        module.Empty = module.Model.empty() || module.Model == "0";

        if( module.Empty)
        {
            module.Model.clear();
            module.Serial.clear();
            module.Firmware.clear();
        }

        return module;
    }

    auto Chassis::modules() -> std::vector<ModuleIdentity>
    {
        std::vector<ModuleIdentity> rack;

        rack.reserve( kSlots);

        for( int slot = 1; slot <= kSlots; ++slot)
        {
            rack.push_back( moduleIn( slot));
        }

        return rack;
    }

    auto Chassis::setSimulatedModule( const int slot, ModuleIdentity module) -> void
    {
        validate( slot);

        mSimModules[ slot] = std::move( module);
    }

    // ---------------------------------------------------------------------
    // The internal DMM, from the switch side
    // ---------------------------------------------------------------------

    auto Chassis::internalDmmInstalled() -> bool
    {
        if( simulating())
        {
            return mSimDmmInstalled;
        }

        return session().queryNumber( "INST:DMM:INST?") != 0.0;
    }

    auto Chassis::internalDmmEnabled() -> bool
    {
        if( simulating())
        {
            return mSimDmmEnabled;
        }

        return session().queryNumber( "INST:DMM?") != 0.0;
    }

    auto Chassis::setInternalDmm( const bool enabled) -> void
    {
        if( simulating())
        {
            //
            // A simulated chassis with no DMM fitted cannot enable one, which
            // is the same refusal the real instrument makes -- with the DMM
            // absent, a command directed at it generates an error. Modelled
            // rather than ignored so that a test of the future DMM face's
            // installed-check behaves the same way against both.
            //
            mSimDmmEnabled = enabled && mSimDmmInstalled;

            return;
        }

        //
        // Note what this does on the real instrument beyond what it says: the
        // mainframe issues a Factory Reset when this state changes, aborting
        // any measurement, clearing the scan list and returning every
        // measurement parameter to its factory setting. See the header's own
        // comment; this is a bring-up operation, not something a script calls
        // mid-run.
        //
        session().checked( std::string( "INST:DMM ") + ( enabled ? "ON" : "OFF"));
    }

    // ---------------------------------------------------------------------
    // The simulated half
    auto Chassis::relayCycles( const ChannelAddress channel) -> long
    {
        validate( channel.Slot);

        if( simulating())
        {
            const auto found = mSimCycles.find( channel);

            return found == mSimCycles.end() ? 0 : found->second;
        }

        //
        // One channel, one count back. The list form would answer a
        // comma-separated count per channel, which nothing here asks for.
        //
        return static_cast<long>( session().queryNumber( "DIAG:REL:CYCL? " + channelList( channel)));
    }

    auto Chassis::safeRelays() -> void
    {
        if( isSimulated())
        {
            mSimClosed.clear();

            return;
        }

        //
        // Only down a session that is already open -- a chassis nobody used
        // this run has nothing closed by this run, and one that cannot be
        // reached must not turn safing into a transport error. See the
        // header's comment, and hal::keysight_edu36311a::detail::sendSafe for
        // the same rule on a supply.
        //
        auto * const open = mConnection.openSession();

        if( !open)
        {
            return;
        }

        try
        {
            open->write( "ROUT:OPEN:ALL " + std::string( kAllSlots));
        }
        catch( const io::TransportError &)
        {
            //
            // Gone, which on a safing pass is the one outcome to survive:
            // the instruments after this one still need their turn.
            //
        }
    }

    // ---------------------------------------------------------------------

    auto Chassis::simulatedClose( const std::vector<ChannelAddress> & channels) -> void
    {
        for( const auto channel : channels)
        {
            if( std::ranges::find( mSimClosed, channel) == mSimClosed.end())
            {
                mSimClosed.push_back( channel);
                ++mSimCycles[ channel];
            }
        }

        //
        // Sorted after every change, so that a test comparing the whole set
        // does not depend on the order the closes happened in -- see the
        // header's comment on this member. Cheap at this size, and the
        // alternative (a std::set) prints less readably in a gtest failure.
        //
        std::ranges::sort( mSimClosed);
    }

    auto Chassis::simulatedOpen( const std::vector<ChannelAddress> & channels) -> void
    {
        for( const auto channel : channels)
        {
            std::erase( mSimClosed, channel);
        }
    }

    // ---------------------------------------------------------------------
    // InternalDmm
    // ---------------------------------------------------------------------

    namespace
    {
        //
        // The MEASure query for each function, and its name for a message.
        // No channel list: the bare form measures the Analog Buses -- see the
        // header's comment on InternalDmm, including what is not yet confirmed
        // about it.
        //
        struct DmmCommand
        {
            std::string_view Query;
            std::string_view Name;
            bool             TakesRange;
        };

        constexpr auto commandFor( const InternalDmm::Function function) -> DmmCommand
        {
            switch( function)
            {
                case InternalDmm::Function::DcVoltage:          return { "MEAS:VOLT:DC?", "DC voltage",       true  };
                case InternalDmm::Function::AcVoltage:          return { "MEAS:VOLT:AC?", "AC voltage",       true  };
                case InternalDmm::Function::Resistance:         return { "MEAS:RES?",     "resistance",       true  };
                case InternalDmm::Function::FourWireResistance: return { "MEAS:FRES?",    "4-wire resistance", true  };

                //
                // No range: FREQ's range argument on this meter is the
                // *voltage* range of the signal being counted, not a frequency,
                // and a Frequency port's range() is a frequency. Sending one as
                // the other would be a unit error the instrument cannot see.
                //
                case InternalDmm::Function::Frequency:          return { "MEAS:FREQ?",    "frequency",        false };
            }

            return { "", "", false };
        }

        //
        // "1" or "0", checked -- INST:DMM:INST? and INST:DMM? both answer so.
        //
        [[nodiscard]]
        auto answersYes( io::ScpiSession & session, const std::string_view question) -> bool
        {
            return session.queryNumber( question) != 0.0;
        }
    } // namespace

    auto InternalDmm::prepare( io::ScpiSession & session) -> void
    {
        session.clearErrors();

        static_cast<void>( detail::verifyMainframe( session));

        //
        // The two facts a meter on this box depends on, asked before the
        // first reading rather than discovered by one: a box with no meter
        // fitted answers every MEASure with an error that says less than this.
        //
        if( !answersYes( session, "INST:DMM:INST?"))
        {
            throw io::ScpiFault( session.description(), "INST:DMM:INST?",
                io::ScpiError{ 0,
                    "this 34980A has no internal DMM fitted -- a row naming keysight_34980a::InternalDmm"
                    " needs the optional DMM assembly" });
        }

        //
        // Disabled is not refused here by enabling it: INST:DMM ON makes the
        // mainframe issue a factory reset, and this face shares the box with
        // the switching (see Chassis::setInternalDmm). So it says so and stops.
        //
        if( !answersYes( session, "INST:DMM?"))
        {
            throw io::ScpiFault( session.description(), "INST:DMM?",
                io::ScpiError{ 0,
                    "this 34980A's internal DMM is disabled, so it is not on the Analog Buses. Enable"
                    " it from the front panel or a bring-up session (INST:DMM ON) -- this driver"
                    " will not, because enabling it issues a factory reset" });
        }
    }

    auto InternalDmm::session() -> io::ScpiSession &
    {
        return mConnection.session( "keysight_34980a.dmm", []( io::ScpiSession & opened)
        {
            prepare( opened);
        });
    }

    auto InternalDmm::identity() -> std::string
    {
        return session().identify();
    }

    auto InternalDmm::read( const Function function, const std::optional<double> range) -> double
    {
        auto &     scpi    = session();
        const auto command = commandFor( function);

        std::string query{ command.Query };

        if( range && command.TakesRange)
        {
            query += " " + io::ScpiSession::number( *range);
        }

        //
        // A query the instrument refuses gets no reply, so the refusal shows
        // up first as a timeout -- and a timeout alone says nothing about why.
        // So on one, ask the error queue: an entry there is the instrument's
        // own reason, reported against the query that caused it; an empty
        // queue is a real timeout, rethrown as it was.
        //
        double reading = 0.0;

        try
        {
            reading = scpi.queryNumbers( query).front();
        }
        catch( const io::TransportTimeout &)
        {
            if( const auto refusal = scpi.nextError())
            {
                throw io::ScpiFault( scpi.description(), query, *refusal);
            }

            throw;
        }

        //
        // And one that did answer may still have queued an error -- a range
        // the meter clamped, say -- which is the reading being of something
        // other than what was asked for.
        //
        if( const auto error = scpi.nextError())
        {
            throw io::ScpiFault( scpi.description(), query, *error);
        }

        if( io::ScpiSession::isOverload( reading))
        {
            const std::string where = range && command.TakesRange
                ? "the " + io::ScpiSession::number( *range) + " range"
                : "autoranging";

            throw core::UnmeasurableReading(
                std::string( command.Name) + " overload on the Analog Bus -- the input is beyond " + where);
        }

        return reading;
    }
} // namespace hal::keysight_34980a
