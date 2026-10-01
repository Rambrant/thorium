#include "hal/verbs/preflight.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <meta>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_device.hpp"
#include "hal/io/transport.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/topology/address_tables.hpp"
#include "hal/topology/boxes.hpp"
#include "hal/topology/wiring.hpp"

#include "core/meta.hpp"
#include "core/session/bench.hpp"

//
// The linking rig's wiring, expanded here for one purpose only: the pool veto
// below. This is the fourth translation unit to read it (see
// hal/src/verbs/route.cpp, measure.cpp and interlock.cpp), and the first to
// read it for something other than composing or checking a route.
//
// What it needs is the compile-time half -- isInstrumentWired(),
// isTapWiredInstrument() and the source entries -- none of which touches the
// runtime hal::instrumentWiring object at all. See hal/topology/wiring.hpp on
// why those predicates are consteval and why each table builds a key array
// beside its runtime one.
//
#include THORIUM_WIRING_TABLE

namespace hal
{
    //
    // Anonymous, not hal::detail, and hal/src/verbs/interlock.cpp makes the
    // same choice for the same reason: everything below is this translation
    // unit's own, and several of the names are ones another verb in this
    // directory also wants. Two files defining hal::membersOfInfo
    // would be one definition too many for a name with external linkage,
    // however identical the two bodies are.
    //
    namespace
    {
        //
        // The same two helpers hal/src/verbs/safing.cpp and interlock.cpp
        // declare, for the same reflection over the same global namespace.
        // Repeated rather than shared because they are three lines that exist
        // to work around std::meta::info being consteval-only, and a header
        // exporting them would put a reflection utility into hal's public
        // surface for the benefit of three callers in one directory.
        //
        consteval auto membersOfInfo( std::meta::info scope) -> std::vector<std::meta::info>
        {
            return std::meta::members_of( scope, std::meta::access_context::current());
        }

        template<std::meta::info Scope>
        constexpr auto members = std::define_static_array( membersOfInfo( Scope));

        //
        // Whether this deployment declares anything that tells this
        // instrument apart from an identical one -- the pool veto's whole
        // question.
        //
        // Four tables can answer yes, and all four are the same fact in
        // different shapes: this instrument's leads go somewhere specific. A
        // routed wire (either role), a source landing, or a direct tap all
        // mean a pool would be choosing which node gets measured.
        //
        // Note what is deliberately not consulted: whether the instrument has
        // an entry in rig/devices.inc or a channel anywhere. A pool is about
        // the instrument's own terminals, not about the fabric it reaches
        // them through.
        //
        consteval auto isDistinguished( const InstrumentId instrument) -> bool
        {
            return isInstrumentWired( instrument, WireRole::Force)
                || isInstrumentWired( instrument, WireRole::Sense)
                || detail::rowsFor( detail::sourceWiringEntries, instrument)
                || isTapWiredInstrument( instrument);
        }

        consteval auto firstWiredPool() -> std::optional<InstrumentId>
        {
            for( const auto pooled : detail::pooledInstruments)
            {
                if( isDistinguished( pooled))
                {
                    return pooled;
                }
            }

            return std::nullopt;
        }

        //
        // The static_assert's own text, computed rather than fixed so that it
        // can name the row. Worth the small amount of machinery: the mistake
        // this catches is one where the two halves of the evidence live in
        // two different files, and a diagnostic that made the reader find the
        // offending id themselves would be most of the work.
        //
        consteval auto poolVetoMessage() -> std::string
        {
            const auto offender = firstWiredPool();

            if( !offender)
            {
                return {};
            }

            return "instrument " + std::string( core::meta::to_string( *offender))
                 + " is a face of box " + std::string( boxOf( *offender))
                 + ", which has an address pool, and it is also wired. A pool says its candidates are"
                   " interchangeable, and a wiring row says this instrument's leads go somewhere"
                   " specific -- both cannot be true, and binding it from a pool would be a coin"
                   " flip on which node gets measured. Remove its POOL rows, or remove the wiring"
                   " row if the leads really are not fixed.";
        }

        //
        // The veto itself, and it is a claim about two of this deployment's
        // tables rather than about any code here -- which is why it sits at
        // namespace scope in the one translation unit that has both of them
        // open, and not inside either table's own expansion.
        //
        static_assert( !firstWiredPool().has_value(), poolVetoMessage());

        //
        // The driver's own qualified type name, minus this namespace --
        // "keysight_edu34450a::EDU34450A" rather than
        // "hal::keysight_edu34450a::EDU34450A".
        //
        // Off the reflection rather than out of a table: the INSTRUMENT row's
        // type column and this are the same fact, and this is the copy that
        // cannot drift. Every "hal::" is trimmed, not only a leading one,
        // because every driver in this tree is in that namespace -- so the
        // prefix is five characters of column width distinguishing nothing,
        // and each row's own package name, which is the half a reader uses to
        // find the driver, survives the trim.
        //
        // The trim matters most where it looks like it should matter least.
        // rig/instrument.inc writes DcP5's type as
        // keysight_edu36311a::DirectOutput1, and an alias is exactly what
        // reflection does not preserve: what arrives here is the underlying
        // specialisation, and untrimmed it reads
        // "keysight_edu36311a::EDU36311A<hal::keysight_edu36311a::Output1,
        // hal::keysight_edu36311a::DirectWiring>" -- a hundred columns to
        // make five rows of this banner unreadable. Trimmed it is
        // "keysight_edu36311a::EDU36311A<Output1, DirectWiring>", which is
        // both narrow enough and says more than the alias did: which physical
        // output, and whether it has an isolation relay.
        //
        template<typename InstrumentT>
        [[nodiscard]]
        consteval auto typeNameOf() -> std::string_view
        {
            std::string name{ std::meta::display_string_of( ^^InstrumentT) };

            const auto eraseEvery = []( std::string & text, const std::string_view what, const std::size_t from)
            {
                if( what.empty())
                {
                    return;
                }

                for( auto found = text.find( what, from); found != std::string::npos; found = text.find( what, found))
                {
                    text.erase( found, what.size());
                }
            };

            eraseEvery( name, "hal::", 0);

            //
            // Then the driver package's own name, everywhere after the first
            // time it is said. Whatever is left of the qualifier once "hal::"
            // is gone is the package (keysight_edu36311a::), and a template
            // argument from the same package repeating it says nothing the
            // row has not already said two words earlier.
            //
            const auto arguments = name.find( '<');
            const auto qualifier = name.rfind( "::", arguments);

            if( qualifier != std::string::npos)
            {
                const auto package = name.substr( 0, qualifier + 2);

                eraseEvery( name, package, package.size());
            }

            return std::define_static_string( name);
        }

        //
        // "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00" -> the
        // serial. Field three of four, comma separated, exactly as
        // hal::keysight_33522b::modelOf() takes field two -- and returning an
        // empty view for a reply that is not shaped like an *IDN? at all, for
        // the same reason: an instrument that answers something unexpected is
        // not this function's problem to diagnose.
        //
        [[nodiscard]]
        auto serialOf( const std::string_view identity) -> std::string_view
        {
            std::size_t start = 0;

            for( int field = 0; field < 2; ++field)
            {
                const auto comma = identity.find( ',', start);

                if( comma == std::string_view::npos)
                {
                    return {};
                }

                start = comma + 1;
            }

            const auto end = identity.find( ',', start);

            return identity.substr( start, end == std::string_view::npos ? end : end - start);
        }

        //
        // The rule from hal/topology/address_plan.hpp, applied. Everything
        // that can move an address goes through here, so there is exactly
        // one place where a resolved address meets the driver's back panel.
        //
        auto requireAccepted(
            const Binding &        binding,
            const Address &        resolved,
            const std::string_view origin) -> void
        {
            if( binding.Panel.allows( resolved))
            {
                return;
            }

            const auto row = std::string( to_string( binding.Id));

            //
            // Names the connectors the instrument actually has, because that
            // is the answer and the person reading it is at a bench. Naming
            // the driver too, since "a DSOX1202G has no GPIB connector" is
            // the fact behind the refusal, and a reader who disagrees with it
            // needs to know which datasheet to check.
            //
            throw AddressKindMismatch(
                row + " is a " + std::string( binding.Type) + " and " + std::string( origin)
                + " gives it a " + std::string( kindOf( resolved)) + " address. That instrument's"
                  " back panel has: " + std::string( binding.Panel.Kinds)
                + ". The same list is what the compiler checks the rig's instrument table against"
                  " (see hal::BackPanel), so this is not a restriction the command line adds --"
                  " it is the one the table is already held to, applied to an address that"
                  " arrived too late for the compiler to see it.");
        }

        //
        // Priority, highest first: override, site, pool, table. Each level may
        // only replace the payload of the one below it.
        //
        // A pool is marked and left unresolved -- its Value stays whatever the
        // table said until contactInstruments() acquires a real one -- because
        // acquiring is I/O and this pass has none. That is also what makes a
        // detached run's banner honest about a pooled row: it prints the
        // table's address and says it was never contacted, rather than
        // inventing a choice nobody made.
        //
        auto resolve( Binding & binding, const AddressPlan & plan) -> void
        {
            for( const auto & [ box, address ] : plan.Overrides)
            {
                if( box == binding.Box)
                {
                    requireAccepted( binding, address, "--address");

                    binding.Value  = address;
                    binding.Source = AddressSource::Override;

                    return;
                }
            }

            if( !plan.Site.empty())
            {
                if( const auto address = siteAddressFor( plan.Site, binding.Box))
                {
                    requireAccepted( binding, *address,
                        "the site table's " + std::string( plan.Site) + " row");

                    binding.Value  = *address;
                    binding.Source = AddressSource::Site;

                    return;
                }
            }

            const auto candidates = poolFor( binding.Box);

            if( !candidates.empty())
            {
                //
                // Every candidate's kind is checked now, before any of them is
                // tried, so that a mistyped pool table fails at startup rather
                // than on whichever candidate the shelf happens to leave free.
                //
                for( const auto & candidate : candidates)
                {
                    requireAccepted( binding, candidate, "the pool table");
                }

                binding.Source = AddressSource::Pool;
            }
        }

        [[nodiscard]]
        auto bindingFor( std::vector<Binding> & bindings, const InstrumentId id) -> Binding &
        {
            const auto found = std::ranges::find( bindings, id, &Binding::Id);

            //
            // Cannot miss: both loops walk the same reflection over the same
            // globals, so pass two asks for exactly the ids pass one recorded.
            // std::logic_error rather than one of this file's own exception
            // types for exactly that reason -- reaching it would be a bug
            // here, not a rig fault, and the two should not be reported to an
            // operator as though they were the same kind of thing.
            //
            if( found == bindings.end())
            {
                throw std::logic_error(
                    "preflight: no binding for " + std::string( to_string( id))
                    + " -- pass two found an instrument pass one did not");
            }

            return *found;
        }

        //
        // Which box answered with which serial -- the claim a box makes on the
        // unit it reached, so that no other box can make it too.
        //
        struct Claim
        {
            std::string      Serial;
            std::string_view Box;
        };

        //
        // The box that already claimed this serial, if it is not this one.
        // An empty serial is never a claim -- a reply with no third field
        // says nothing about which unit answered, and two of them are not
        // evidence of one unit under two names.
        //
        [[nodiscard]]
        auto claimedByAnother( const std::vector<Claim> & claimed, const std::string_view serial,
                               const std::string_view box) -> std::optional<std::string_view>
        {
            if( serial.empty())
            {
                return std::nullopt;
            }

            for( const auto & claim : claimed)
            {
                if( claim.Serial == serial && claim.Box != box)
                {
                    return claim.Box;
                }
            }

            return std::nullopt;
        }

        //
        // What the first face of a box contacted -- so its other faces bind to
        // the same unit rather than each reaching for one of their own.
        //
        struct Contacted
        {
            std::string_view                    Box;
            Address                             Value;
            std::string                         Serial;
            std::optional<std::pair<int, int>>  PoolPosition;
        };

        //
        // Acquire one pooled box, through its first face. Candidate order is
        // table order.
        //
        template<ContactableInstrument InstrumentT>
        auto acquireFromPool(
            InstrumentT &         instrument,
            Binding &             binding,
            std::vector<Claim> &  claimed) -> void
        {
            const auto candidates = poolFor( binding.Box);

            int position = 0;

            for( const auto & candidate : candidates)
            {
                ++position;

                instrument.useAddress( candidate);

                try
                {
                    const auto identity = instrument.identity();
                    const auto serial   = std::string( serialOf( identity));

                    //
                    // Already taken by another box. Skipped here rather than
                    // left to the instrument refusing a second socket, so that
                    // "two boxes, one free meter" is diagnosed as that instead
                    // of as a connection failure three layers down.
                    //
                    if( claimedByAnother( claimed, serial, binding.Box))
                    {
                        instrument.closeSession();

                        continue;
                    }

                    binding.Value        = candidate;
                    binding.Identity     = identity;
                    binding.PoolPosition = std::pair{ position, static_cast<int>( candidates.size()) };

                    claimed.push_back( Claim{ serial, binding.Box });

                    return;
                }
                catch( const io::TransportError &)
                {
                    //
                    // Busy, powered off, or not there. All three mean "not
                    // this one", and none of them is this row's failure yet --
                    // the exhausted pool below is.
                    //
                    instrument.closeSession();
                }
            }

            throw io::TransportError(
                "no free instrument for box " + std::string( binding.Box) + ": all "
                + std::to_string( candidates.size())
                + " candidates in its pool were busy or unreachable");
        }

        //
        // Every card row on this instrument's box, checked against what its
        // slot says is there.
        //
        // An exact match on the spec's Reports, and a spec with nothing to
        // report fails too: a Racal card's row on a box whose chassis can
        // identify its slots is a row on the wrong box, whatever the slot
        // holds.
        //
        template<CardIdentifyingInstrument InstrumentT>
        auto checkCardsOn( InstrumentT & instrument, const std::string_view box) -> void
        {
            for( std::size_t row = 0; row < detail::switchDevices.size(); ++row)
            {
                const auto & device = detail::switchDevices[ row];

                if( detail::deviceBoxRows[ row].Box != box || !device.Card)
                {
                    continue;
                }

                const auto expected = specOf( device.Model).Reports;
                const auto reported = instrument.cardModel( *device.Card);

                if( reported == expected)
                {
                    continue;
                }

                const auto id = core::meta::values<SwitchDeviceId>[ row];

                throw std::runtime_error(
                    std::string( to_string( id)) + " is declared as a " + std::string( specOf( device.Model).Part)
                    + " in slot " + std::to_string( *device.Card) + " of box " + std::string( box) + ", but "
                    + ( reported.empty() ? "that slot is empty" : "that slot holds a " + reported)
                    + " -- the rig's card table describes a different rack. Fix the row's model or"
                      " slot, or move the card.");
            }
        }
    } // namespace

    auto bindAddresses( const AddressPlan & plan) -> std::vector<Binding>
    {
        //
        // Checked before anything is bound, because a --site nobody declares
        // would otherwise resolve nothing and look exactly like a site whose
        // rows all happen to match the table. A bench PC configured for a
        // fleet it is not part of is a mistake worth hearing about at startup.
        //
        if( !plan.Site.empty())
        {
            const auto declared = siteNames();

            if( std::ranges::find( declared, plan.Site) == declared.end())
            {
                std::string known;

                for( const auto & name : declared)
                {
                    known += known.empty() ? "" : ", ";
                    known += name;
                }

                throw AddressSyntaxError(
                    "\"" + std::string( plan.Site) + "\" is not a site this deployment declares"
                    + ( known.empty()
                            ? " -- it declares none at all, so every instrument's address comes"
                              " from its own row in the rig's instrument table"
                            : " -- it declares " + known));
            }
        }

        std::vector<Binding> bindings;

        //
        // ^^:: for the reason hal/src/verbs/safing.cpp gives:
        // hal/topology/active_instruments.hpp declares Dmm1/Osc1/... unqualified
        // in the global namespace, so that is where they are to reflect over.
        // Filtering on InstrumentTag rather than on "has a useAddress()" is
        // deliberate and is the same argument -- only a type that explicitly
        // opted in ever matches.
        //
        template for( constexpr auto member : members<^^::>)
        {
            if constexpr( std::meta::is_variable( member))
            {
                using InstrumentT = [: std::meta::type_of( member) :];

                if constexpr( std::derived_from<InstrumentT, InstrumentTag>)
                {
                    static_assert( AddressableInstrument<InstrumentT>,
                        "every InstrumentTag-derived instrument needs id(), address() and"
                        " useAddress() -- see hal::AddressableInstrument in hal/verbs/preflight.hpp");

                    auto & instrument = [: member :];

                    Binding binding{
                        .Id         = instrument.id(),
                        .Box        = boxOf( instrument.id()),
                        .Type       = typeNameOf<InstrumentT>(),
                        .Value      = instrument.address(),
                        .Source     = AddressSource::Table,
                        .Panel      = InstrumentT::Buses::info(),
                        .CanContact = ContactableInstrument<InstrumentT>
                    };

                    resolve( binding, plan);

                    if( binding.Source == AddressSource::Site)
                    {
                        binding.Site = plan.Site;
                    }

                    //
                    // A pooled row is left alone: its address is not decided
                    // yet, and writing the table's back into the driver would
                    // be writing something nobody chose.
                    //
                    if( binding.Source != AddressSource::Pool)
                    {
                        instrument.useAddress( binding.Value);
                    }

                    bindings.push_back( std::move( binding));
                }
            }
        }

        return bindings;
    }

    namespace
    {
        //
        // "table", "site Bench02", "pool 2 of 3", "--address" -- the
        // provenance column.
        //
        // A pool says which candidate of how many, because that is the column
        // where a lab notices its shelf is nearly empty: a row that reads
        // "pool 1 of 3" every morning and "pool 3 of 3" today has had two
        // meters taken out from under it, and nothing else in the report
        // would say so.
        //
        [[nodiscard]]
        auto provenanceOf( const Binding & binding) -> std::string
        {
            std::string text{ to_string( binding.Source) };

            if( binding.Source == AddressSource::Site && !binding.Site.empty())
            {
                text += " " + std::string( binding.Site);
            }

            if( binding.PoolPosition)
            {
                text += " " + std::to_string( binding.PoolPosition->first)
                      + " of " + std::to_string( binding.PoolPosition->second);
            }

            return text;
        }

        //
        // The last column: what the instrument itself said, or precisely why
        // it said nothing.
        //
        // Three outcomes and three different sentences, rather than one blank
        // column standing for all of them. Which of the three applies is the
        // difference between "this run measured through that box", "this run
        // touched no hardware" and "this row never had a box behind it", and
        // a reader who cannot tell those apart cannot use the header at all.
        //
        [[nodiscard]]
        auto identityOf( const Binding & binding) -> std::string
        {
            if( !binding.Identity.empty())
            {
                return binding.Identity;
            }

            if( !binding.CanContact)
            {
                return "no session -- answers from its own hooks";
            }

            //
            // Checked after CanContact and before the detached case, so each
            // row gets the most specific true statement about it. AcP1 has a
            // real GPIB address and no session at all, which is the first
            // case; Dmm1 has a driver that connects and an address that says
            // there is nothing to connect to, which is this one.
            //
            if( std::holds_alternative<Simulated>( binding.Value))
            {
                return "simulated -- no instrument at this address";
            }

            return "not contacted -- detached run";
        }

        [[nodiscard]]
        auto padded( const std::string & text, const std::size_t width) -> std::string
        {
            return text + std::string( width - std::min( width, text.size()), ' ');
        }
    } // namespace

    auto bannerLines( const std::vector<Binding> & bindings) -> std::vector<std::string>
    {
        //
        // Widths measured rather than fixed, because every column here is a
        // deployment's own vocabulary: a rig with one instrument called Dmm1
        // and a rig with DcP1..DcP4 behind a mainframe want different ones,
        // and a constant would be tuned for whichever bench happened to be
        // built first. The last column is not padded at all -- nothing
        // follows it.
        //
        std::size_t boxWidth        = 0;
        std::size_t idWidth         = 0;
        std::size_t typeWidth       = 0;
        std::size_t addressWidth    = 0;
        std::size_t provenanceWidth = 0;

        for( const auto & binding : bindings)
        {
            boxWidth        = std::max( boxWidth,        binding.Box.size());
            idWidth         = std::max( idWidth,         to_string( binding.Id).size());
            typeWidth       = std::max( typeWidth,       binding.Type.size());
            addressWidth    = std::max( addressWidth,    to_string( binding.Value).size());
            provenanceWidth = std::max( provenanceWidth, provenanceOf( binding).size());
        }

        std::vector<std::string> lines;

        for( const auto & binding : bindings)
        {
            lines.push_back(
                padded( std::string( binding.Box),              boxWidth)        + "  "
              + padded( std::string( to_string( binding.Id)),   idWidth)         + "  "
              + padded( std::string( binding.Type),             typeWidth)       + "  "
              + padded( to_string( binding.Value),              addressWidth)    + "  "
              + padded( provenanceOf( binding),         provenanceWidth) + "  "
              + identityOf( binding));
        }

        return lines;
    }

    auto contactInstruments( std::vector<Binding> & bindings) -> void
    {
        //
        // Nothing here for a detached run, mirroring hal::safeRig()'s own
        // guard -- see its comment on why "reach the bench anyway, it is
        // harmless" is the one thing a detached run must never do. A --replay
        // at a desk would otherwise open a socket to whatever bench this
        // binary's table names.
        //
        if( !core::bench().isAttached())
        {
            return;
        }

        std::vector<Claim>     claimed;
        std::vector<Contacted> contacted;

        template for( constexpr auto member : members<^^::>)
        {
            if constexpr( std::meta::is_variable( member))
            {
                using InstrumentT = [: std::meta::type_of( member) :];

                if constexpr( std::derived_from<InstrumentT, InstrumentTag>)
                {
                    //
                    // Branched, not asserted -- see hal::ContactableInstrument
                    // on why a driver with no session is a fact this reports
                    // rather than a build it refuses.
                    //
                    if constexpr( ContactableInstrument<InstrumentT>)
                    {
                        auto & instrument = [: member :];
                        auto & binding    = bindingFor( bindings, instrument.id());

                        //
                        // A hal::Simulated row has nothing at the other end
                        // by definition, and openTransport() says so by
                        // throwing UnsupportedTransport for it (see
                        // hal/io/transport.hpp). Skipped rather than caught,
                        // because those are different sentences: catching it
                        // would report "could not reach Dmm1" about a row that
                        // was never claiming to be reachable, and would make
                        // every simulated bench fail its own preflight.
                        //
                        // This is the row rig/instrument.inc's Simulated{}
                        // rule produces, and it is currently most of that
                        // table -- so it is the common case here rather than
                        // an edge one. The banner still prints the row, and
                        // says which of the ways of having no identity this
                        // one is.
                        //
                        if( std::holds_alternative<Simulated>( binding.Value))
                        {
                            continue;
                        }

                        //
                        // A later face of a box an earlier face already
                        // reached: bind to that unit, and make no claim of
                        // its own -- the box has made it. A pooled box's
                        // later faces take the candidate its first face
                        // acquired, which is the whole of what a pool per box
                        // buys over a pool per row. Each face still asks
                        // *IDN? itself, which is the driver's model check for
                        // that face and the identity its banner line shows,
                        // and the serial it answers with has to be the box's:
                        // a face that reached a different unit is a box that
                        // is not one.
                        //
                        const auto earlier = std::ranges::find( contacted, binding.Box, &Contacted::Box);

                        if( earlier != contacted.end())
                        {
                            if( binding.Source == AddressSource::Pool)
                            {
                                instrument.useAddress( earlier->Value);

                                binding.Value        = earlier->Value;
                                binding.PoolPosition = earlier->PoolPosition;
                            }

                            binding.Identity = instrument.identity();

                            const auto serial = serialOf( binding.Identity);

                            if( !serial.empty() && !earlier->Serial.empty() && serial != earlier->Serial)
                            {
                                throw std::runtime_error(
                                    std::string( to_string( binding.Id)) + " is a face of box "
                                    + std::string( binding.Box) + ", but answered with serial "
                                    + std::string( serial) + " where the box's first face answered "
                                    + earlier->Serial + " -- the rows name one box and reach two units");
                            }

                            continue;
                        }

                        if( binding.Source == AddressSource::Pool)
                        {
                            acquireFromPool( instrument, binding, claimed);
                        }
                        else
                        {
                            //
                            // identity() opens the session, which runs the
                            // driver's own model check on the way (see any
                            // driver's prepare()). So a fixed row is verified
                            // here by the code that was already going to
                            // verify it -- just earlier, and for every row at
                            // once rather than whenever a script first
                            // touches it.
                            //
                            binding.Identity = instrument.identity();

                            const auto serial = std::string( serialOf( binding.Identity));

                            //
                            // The runtime half of boxes.hpp's "one address,
                            // one box": two box names whose addresses differ
                            // in the table -- Lan on one, Usb on the other --
                            // can still reach one unit, and only its serial
                            // says so.
                            //
                            if( const auto other = claimedByAnother( claimed, serial, binding.Box))
                            {
                                throw std::runtime_error(
                                    "boxes " + std::string( *other) + " and " + std::string( binding.Box)
                                    + " both answered with serial " + serial
                                    + " -- one unit under two box names. Give their rows one box name.");
                            }

                            claimed.push_back( Claim{ serial, binding.Box });
                        }

                        contacted.push_back( Contacted{
                            binding.Box, binding.Value, std::string( serialOf( binding.Identity)), binding.PoolPosition });
                    }
                }
            }
        }

        //
        // Then the cards, once every box is bound and its identity checked --
        // so a slot is asked of the unit the box actually reached, and a
        // chassis that is not a 34980A has already been refused by its own
        // model check rather than misread here. A simulated chassis is skipped
        // for the reason its contact was: there is nothing in its slots to ask
        // about.
        //
        template for( constexpr auto member : members<^^::>)
        {
            if constexpr( std::meta::is_variable( member))
            {
                using InstrumentT = [: std::meta::type_of( member) :];

                if constexpr( std::derived_from<InstrumentT, InstrumentTag>
                           && CardIdentifyingInstrument<InstrumentT>)
                {
                    auto &       instrument = [: member :];
                    const auto & binding    = bindingFor( bindings, instrument.id());

                    if( !std::holds_alternative<Simulated>( binding.Value))
                    {
                        checkCardsOn( instrument, binding.Box);
                    }
                }
            }
        }
    }
} // namespace hal
