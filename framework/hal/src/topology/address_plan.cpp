#include "hal/topology/address_plan.hpp"

#include <charconv>
#include <cstdlib>
#include <deque>
#include <string>

#include "core/meta.hpp"

namespace hal
{
    namespace
    {
        //
        // Storage for anything this file reads out of the environment.
        //
        // getenv() hands back a pointer into the environment block, and
        // setenv()/putenv() are free to reallocate that block -- so a
        // std::string_view into it is a dangling reference waiting for the
        // first library that sets a variable. Copying into a deque rather
        // than a vector because a vector's reallocation would invalidate
        // every view already handed out; a deque never moves an element it
        // already holds.
        //
        // Never emptied. These live as long as the process on purpose: the
        // views into them are inside hal::Address values that the rig's
        // instruments hold for the whole run.
        //
        auto environmentStrings() -> std::deque<std::string> &
        {
            static std::deque<std::string> strings;

            return strings;
        }

        [[nodiscard]]
        auto interned( const char * value) -> std::string_view
        {
            return environmentStrings().emplace_back( value);
        }

        //
        // A whole non-negative decimal number, or nothing. Used for a GPIB
        // address and a LAN port, both of which are typed by a person at a
        // bench and both of which are worth refusing rather than silently
        // reading as zero.
        //
        [[nodiscard]]
        auto wholeNumber( const std::string_view text) -> std::optional<int>
        {
            if( text.empty())
            {
                return std::nullopt;
            }

            int        value  = 0;
            const auto result = std::from_chars( text.data(), text.data() + text.size(), value);

            if( result.ec != std::errc{} || result.ptr != text.data() + text.size() || value < 0)
            {
                return std::nullopt;
            }

            return value;
        }

        //
        // "dev-dmm-3" or "dev-dmm-3:5025" -> a Lan.
        //
        // Split on a colon only when there is exactly one of them, and the
        // count is the whole rule rather than a shortcut.
        //
        // A hostname cannot contain a colon and an IPv4 address cannot
        // either, so one colon is always host-and-port and no colons is
        // always a bare host. Two or more can only be an IPv6 literal, which
        // is nothing but colons -- and the tempting rule, "split on the last
        // colon when what follows is a number", gets "fe80::1" exactly wrong:
        // it reads as host "fe80:" on port 1, both plausible, neither what
        // anybody typed.
        //
        // Which leaves no spelling here for an IPv6 literal on a non-default
        // port. That is deliberate: the conventional spelling for it is
        // bracketed, and this refuses a bracketed address outright rather
        // than half-supporting one. A bench that needs it says so in its own
        // rig table, where the port is a separate field and no parsing
        // happens at all.
        //
        [[nodiscard]]
        auto parseLan( const std::string_view value) -> Address
        {
            if( value.empty())
            {
                throw AddressSyntaxError( "a lan address needs a hostname");
            }

            if( value.front() == '[')
            {
                throw AddressSyntaxError(
                    "bracketed IPv6 addresses are not supported here -- write \"lan:<address>\""
                    " for the default port, and put a non-default port in the rig's own table");
            }

            const auto colon = value.find( ':');

            if( colon != std::string_view::npos && value.find( ':', colon + 1) == std::string_view::npos)
            {
                const auto port = wholeNumber( value.substr( colon + 1));

                if( !port)
                {
                    throw AddressSyntaxError(
                        "\"" + std::string( value.substr( colon + 1)) + "\" is not a port number --"
                        " a lan address is written \"lan:<host>\" or \"lan:<host>:<port>\"");
                }

                return Lan{ value.substr( 0, colon), *port };
            }

            return Lan{ value };
        }

        //
        // "0,5" or "0,5,3" -> a Gpib. Board first, exactly as hal::Gpib
        // declares its own fields, so that an address read off this flag and
        // an address read out of a rig table are written the same way round.
        //
        [[nodiscard]]
        auto parseGpib( const std::string_view value) -> Address
        {
            std::vector<std::string_view> fields;

            for( std::size_t start = 0; start <= value.size(); )
            {
                const auto comma = value.find( ',', start);
                const auto end   = comma == std::string_view::npos ? value.size() : comma;

                fields.push_back( value.substr( start, end - start));

                if( comma == std::string_view::npos)
                {
                    break;
                }

                start = comma + 1;
            }

            if( fields.size() < 2 || fields.size() > 3)
            {
                throw AddressSyntaxError(
                    "a gpib address is written \"gpib:<board>,<primary>\" or"
                    " \"gpib:<board>,<primary>,<secondary>\"");
            }

            const auto board   = wholeNumber( fields[ 0]);
            const auto primary = wholeNumber( fields[ 1]);

            if( !board || !primary)
            {
                throw AddressSyntaxError( "a gpib board and primary address must both be whole numbers");
            }

            Gpib address{ *board, *primary, std::nullopt };

            if( fields.size() == 3)
            {
                const auto secondary = wholeNumber( fields[ 2]);

                if( !secondary)
                {
                    throw AddressSyntaxError( "a gpib secondary address must be a whole number");
                }

                address.secondary = *secondary;
            }

            return address;
        }

        //
        // The kind prefix -> an address of that kind. One function rather
        // than a table because each kind's payload is shaped differently,
        // which is the same reason hal::Address is a variant of four structs
        // and not one struct with four optional fields.
        //
        [[nodiscard]]
        auto parseAddress( const std::string_view text) -> Address
        {
            //
            // "sim" has no payload and so no colon, which is why the split
            // below cannot simply be required to find one.
            //
            if( text == "sim" || text == "simulated")
            {
                return Simulated{};
            }

            const auto colon = text.find( ':');

            if( colon == std::string_view::npos)
            {
                throw AddressSyntaxError(
                    "an address is written \"<kind>:<value>\" -- lan, usb, gpib, serial, or a bare \"sim\"");
            }

            const auto kind  = text.substr( 0, colon);
            const auto value = text.substr( colon + 1);

            if( kind == "lan")    return parseLan( value);
            if( kind == "usb")    return Usb{ value };
            if( kind == "serial") return Serial{ value };
            if( kind == "gpib")   return parseGpib( value);

            throw AddressSyntaxError(
                "\"" + std::string( kind) + "\" is not a bus kind -- lan, usb, gpib, serial, or a bare \"sim\"");
        }
    } // namespace

    //
    // The operator's spelling of each source, not the enumerator's -- which
    // is why this is a hand-written mapping where hal::to_string(
    // InstrumentId) is core::meta::to_string() over the enum.
    //
    // The difference is what the reader is being told. An InstrumentId's
    // enumerator name IS the thing a script writes, so reflecting it is the
    // only way to keep the two spellings from drifting. An AddressSource's
    // enumerator name is an internal label, and the banner wants the thing
    // the reader would have typed or opened: "--address" names the flag,
    // "table" and "site" name files.
    //
    auto to_string( const AddressSource source) -> std::string_view
    {
        switch( source)
        {
            case AddressSource::Table:    return "table";
            case AddressSource::Site:     return "site";
            case AddressSource::Pool:     return "pool";
            case AddressSource::Override: return "--address";
        }

        return "?";
    }

    auto parseOverride( const std::string_view text) -> std::pair<InstrumentId, Address>
    {
        const auto equals = text.find( '=');

        if( equals == std::string_view::npos)
        {
            throw AddressSyntaxError(
                "an address override is written \"<Instrument>=<kind>:<value>\", for example"
                " \"Dmm1=lan:dev-dmm-3\" -- \"" + std::string( text) + "\" has no '='");
        }

        const auto name = text.substr( 0, equals);
        const auto id   = core::meta::fromString<InstrumentId>( name);

        if( !id)
        {
            //
            // Names every instrument this rig has rather than only saying the
            // one given is wrong: the person who mistyped it is at a bench and
            // the list is short, so the diagnostic can simply be the answer.
            //
            std::string known;

            for( const auto candidate : core::meta::values<InstrumentId>)
            {
                known += known.empty() ? "" : ", ";
                known += to_string( candidate);
            }

            throw AddressSyntaxError(
                "\"" + std::string( name) + "\" is not an instrument on this rig -- it has " + known);
        }

        return { *id, parseAddress( text.substr( equals + 1)) };
    }

    //
    // One getenv() per instrument this rig has, rather than a walk of environ:
    // the id list is short and fixed, environ is neither portable to walk nor
    // guaranteed to be sorted, and asking for exactly the names that could
    // mean something keeps an unrelated THORIUM_ADDRESS_TYPO silently ignored
    // rather than diagnosed as an instrument nobody has.
    //
    // That last part is a real choice and the opposite of what parseOverride()
    // does with the same mistake. A flag is something a person just typed and
    // is watching the result of; an environment variable is something a bench
    // PC's profile set months ago, and failing every run on it would make a
    // stale export somebody forgot into an outage.
    //
    auto environmentOverrides() -> std::vector<std::pair<InstrumentId, Address>>
    {
        std::vector<std::pair<InstrumentId, Address>> overrides;

        for( const auto id : core::meta::values<InstrumentId>)
        {
            const auto variable = "THORIUM_ADDRESS_" + std::string( to_string( id));

            if( const char * const value = std::getenv( variable.c_str()))
            {
                overrides.emplace_back( id, parseAddress( interned( value)));
            }
        }

        return overrides;
    }

    auto environmentSite() -> std::string_view
    {
        if( const char * const value = std::getenv( "THORIUM_SITE"))
        {
            return interned( value);
        }

        return {};
    }
} // namespace hal
