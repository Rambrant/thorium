//
// hal::keysight_34980a::Chassis's real I/O: what this mainframe is actually
// told, and what its answers mean.
//
// The fourth .cpp in instruments/ and the first belonging to something that is
// not an instrument -- see the header's preamble on why a switch/measure
// mainframe lands in this directory at all, and on why the mainframe rather
// than its modules is the thing with a driver.
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
        if( !mSession)
        {
            //
            // Opened from the address the rig wrote down. A hal::Simulated
            // address reaching here is a bug in this driver rather than in the
            // rig -- every caller checks isSimulated() first -- and
            // hal::io::openTransport says exactly that in the exception it
            // throws for one.
            //
            mSession = std::make_unique<io::ScpiSession>( io::openTransport( mAddress));
        }

        if( mPrepared)
        {
            return *mSession;
        }

        //
        // Whatever the last user of this mainframe left in its error queue is
        // not this run's, and would otherwise be reported against this run's
        // first command. Drained before the identity query rather than after,
        // so a stale entry cannot be mistaken for the identity query having
        // failed.
        //
        mSession->clearErrors();

        //
        // And then the question worth asking before anything switches: what are
        // you? An address in a rig table is a fact nothing checks, and on a
        // switching device the failure that produces is quiet in a way a
        // wrong reading is not -- "ROUT:CLOS (@1003)" is a command a great many
        // boxes will accept, and none of them will have closed the crosspoint
        // the route needed.
        //
        // Not marked prepared until it has succeeded, so a mainframe that
        // failed its identity check is asked again on the next command rather
        // than being treated as verified.
        //
        static_cast<void>( verifyIdentity());

        mPrepared = true;

        return *mSession;
    }

    auto Chassis::verifyIdentity() -> std::string
    {
        const std::string identity = mSession->identify();
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
            throw io::ScpiFault( mSession->description(), "*IDN?",
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

        if( isSimulated())
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

        if( isSimulated())
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

        if( isSimulated())
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

        if( isSimulated())
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
        if( isSimulated())
        {
            mSimClosed.clear();

            return;
        }

        session().checked( "ROUT:OPEN:ALL " + std::string( kAllSlots));
    }

    auto Chassis::openAll( const int slot) -> void
    {
        validate( slot);

        if( isSimulated())
        {
            std::erase_if( mSimClosed,
                [ slot]( const ChannelAddress closed) { return closed.Slot == slot; });

            return;
        }

        session().checked( "ROUT:OPEN:ALL " + std::to_string( slot));
    }

    auto Chassis::waitForSwitching() -> void
    {
        if( isSimulated())
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

        if( isSimulated())
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

        if( isSimulated())
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
        if( isSimulated())
        {
            return mSimDmmInstalled;
        }

        return session().queryNumber( "INST:DMM:INST?") != 0.0;
    }

    auto Chassis::internalDmmEnabled() -> bool
    {
        if( isSimulated())
        {
            return mSimDmmEnabled;
        }

        return session().queryNumber( "INST:DMM?") != 0.0;
    }

    auto Chassis::setInternalDmm( const bool enabled) -> void
    {
        if( isSimulated())
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
    // ---------------------------------------------------------------------

    auto Chassis::simulatedClose( const std::vector<ChannelAddress> & channels) -> void
    {
        for( const auto channel : channels)
        {
            if( std::ranges::find( mSimClosed, channel) == mSimClosed.end())
            {
                mSimClosed.push_back( channel);
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
} // namespace hal::keysight_34980a
