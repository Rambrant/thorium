#pragma once

#include <cstddef>
#include <meta>
#include <optional>
#include <string>
#include <vector>

#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/vpc_location.hpp"

namespace hal
{
    //
    // Two independent static wiring facts, per the framing that motivated
    // this split: an instrument's matrix/mux path is fixed regardless of
    // which DUT pin is being measured, and a connector pin's path is fixed
    // regardless of which instrument is doing the measuring. A route is
    // their composition at the moment a measurement is taken -- see
    // core/verbs/measure.hpp -- not a fact either table stores per (instrument,
    // pin) pair, which is what the combined RouteTable this replaces used
    // to do.
    //
    // Each entry's Path (see hal/fabric/switch_fabric.hpp) is a chain, not
    // necessarily a single element -- most instruments and most connector
    // pins are one hop (WIRE_INSTRUMENT/WIRE_CONNECTOR below), but real
    // wiring can be several muxes deep before reaching a matrix, or an
    // instrument's own path can pass through more than one relay before
    // its dedicated matrix column (WIRE_INSTRUMENT/WIRE_CONNECTOR below
    // take one or more hops either way) -- both are just a longer Path to
    // close or open together, not a different kind of fact.
    //
    // InstrumentWiring::find() below is a runtime linear search over a
    // constexpr-eligible table, and stays one: the *data* is a fixed wiring
    // fact decided once by whoever wired the bench, but the lookup itself is
    // keyed by an InstrumentId that only the running program knows (two
    // Dmms, same C++ type, different runtime id) -- there is no compile-time
    // value to check it against.
    //
    // ConnectorWiring::find() is keyed by a VpcLocation that IS known at
    // compile time at every real call site (it's a template parameter on the
    // AdapterPointTag being measured -- see core/topology/adapter.hpp), which looked
    // like a straightforward reflection upgrade: give ConnectorWiring a
    // find<Loc>() so a missing wiring entry is a compile error wherever
    // Measure() is actually called, not just when that one test happens to
    // run. It isn't that simple: core::MeasureEngine (core/verbs/measure.hpp) is
    // instantiated in whichever script calls Measure() -- e.g.
    // suite/scripts/fuse_register_script.cpp -- and those files never
    // #include this rig's wiring.inc, on purpose (see this file's own
    // comment on CONNECTOR_WIRING below, and core/verbs/measure.hpp's own comment
    // on why ConnectorWiringT's concrete type is only ever named in
    // hal/src/verbs/measure.cpp). A consteval check needs the wiring *data* itself
    // visible as a compile-time constant in the very translation unit
    // performing the check -- unlike an ordinary runtime call, constant
    // evaluation can't reach across translation units the way linking can --
    // so find<Loc>() could only ever fire where wiring.inc is already
    // visible (hal/src/verbs/measure.cpp, hal/src/verbs/route.cpp), never at
    // the actual script call sites where a typo would matter. Threading it
    // through anyway would mean exposing this rig's wiring table through a
    // header everywhere Measure()/Connect() are called, undoing that isolation
    // on purpose.
    //
    // isWired() below is the alternative that actually fits: a compile-time
    // predicate usable from anywhere this rig's own CONNECTOR_WIRING has
    // already run (which is exactly the file that also has the DUT's
    // ADAPTER/POINT data to check it against -- see
    // dut/tests/test_wiring_coverage.cpp), rather than something
    // core::MeasureEngine calls generically. It checks once, for every
    // declared point, not per Measure() call -- catching a forgotten wiring
    // entry the moment anyone builds that check, whether or not any script
    // currently exercises that point.
    //
    // Every entry also carries a WireRole -- Force (the default, and what
    // find()/WIRE_INSTRUMENT/WIRE_CONNECTOR always mean) or Sense. Two
    // different needs turned out to want this, not one:
    //
    //   - A 4-wire (Kelvin) DMM measurement needs its sense leads routed
    //     *in addition to* force, but only for that one measurement -- a
    //     plain 2-wire resistance() call on the same hal::keysight_edu34450a::EDU34450A must not
    //     touch sense at all. That's a per-measurement, per-call choice
    //     (see core::Port::requiresSensePath in core/driver/port.hpp and
    //     core::MeasureEngine's own comment in core/verbs/measure.hpp), so
    //     force and sense need to be looked up independently and combined
    //     only when asked for -- findSense() below exists for exactly
    //     that, kept separate from find()/findAll() rather than folded in.
    //
    //   - A power supply's remote-sense leads (if this rig's supply has
    //     them wired at all) are a fixed fact of the bench, not a
    //     per-Apply choice -- if they exist, Connect/Disconnect should
    //     always close/open them together with the force path, the same
    //     way hal::keysight_ac6834b::Ac6834B's phases and ground already do. findAll()
    //     below deliberately does NOT filter by role for this reason: it
    //     means "every entry registered for this instrument, force and
    //     sense alike, closed together" -- see hal::keysight_edu36311a::EDU36311A's own
    //     connectDriver/disconnectDriver.
    //
    enum class WireRole
    {
        Force,
        Sense
    };

    struct InstrumentWiringEntry
    {
        InstrumentId  instrument;
        WireRole      role;
        Path          path;
    };

    class InstrumentWiring
    {
        public:
            //
            // Single-hop convenience -- the common case (one instrument,
            // one dedicated matrix channel). Equivalent to
            // addWire(instrument, Path{ channel}, role) below. role
            // defaults to Force since that's what nearly every instrument
            // has and all that WIRE_INSTRUMENT ever declares -- only
            // WIRE_INSTRUMENT_SENSE passes Sense explicitly.
            //
            auto addWire( InstrumentId instrument, SwitchElementId channel, WireRole role = WireRole::Force) -> void;

            //
            // The general form -- one whole chain, closed/opened together,
            // for one fixed instrument connection. See WIRE_INSTRUMENT/
            // WIRE_INSTRUMENT_SENSE below for the declarative form (one or
            // more HOP(...) elements, no separate "chain" macro).
            //
            auto addWire( InstrumentId instrument, Path path, WireRole role = WireRole::Force) -> void;

            //
            // The first matching Force-role entry's Path. Throws
            // std::runtime_error if this instrument has no fixed Force
            // path on this rig's fabric. For an instrument with more than
            // one independent fixed connection (see findAll() below),
            // which entry "first" means is unspecified -- use findAll()
            // instead in that case.
            //
            [[nodiscard]]
            auto find( InstrumentId instrument) const -> Path;

            //
            // Every matching entry's Path, force and sense alike (no role
            // filtering -- see this class's own comment on WireRole for
            // why), flattened into one combined Path -- for an instrument
            // fixed-wired with more than one independent physical
            // connection that should always be closed/opened together
            // (hal::keysight_ac6834b::Ac6834B's three phases plus ground/neutral return, or
            // a power supply's remote-sense leads if this rig has them).
            // Single-connection instruments with no sense wiring
            // (hal::keysight_dsox1202g::DSOX1202G, hal::keysight_edu34450a::EDU34450A) keep using find() above.
            // Throws std::runtime_error if this instrument has no fixed
            // path at all.
            //
            [[nodiscard]]
            auto findAll( InstrumentId instrument) const -> Path;

            //
            // The Sense-role entries only, flattened -- for the
            // per-measurement case (a 4-wire DMM reading), not the
            // always-together case findAll() covers. Throws
            // std::runtime_error if this instrument has no registered
            // Sense-role path -- a 4-wire measurement attempted on an
            // instrument nobody wired sense leads for should fail loudly,
            // not silently measure 2-wire.
            //
            [[nodiscard]]
            auto findSense( InstrumentId instrument) const -> Path;

        private:
            std::vector<InstrumentWiringEntry> mEntries;
    };

    struct ConnectorWiringEntry
    {
        VpcLocation  location;
        WireRole     role;
        Path         path;
    };

    class ConnectorWiring
    {
        public:
            //
            // Single-hop convenience -- see InstrumentWiring::addWire()'s
            // own comment; same shape, same reasoning.
            //
            auto addWire( VpcLocation location, SwitchElementId channel, WireRole role = WireRole::Force) -> void;

            //
            // The general form -- see WIRE_CONNECTOR/WIRE_CONNECTOR_SENSE
            // below for the declarative form (one or more HOP(...)
            // elements, no separate "chain" macro).
            //
            auto addWire( VpcLocation location, Path path, WireRole role = WireRole::Force) -> void;

            //
            // Throws std::runtime_error if this VPC pin has no fixed
            // Force path on this rig's fabric.
            //
            [[nodiscard]]
            auto find( VpcLocation location) const -> Path;

            //
            // The Sense-role path for this VPC pin -- see
            // InstrumentWiring::findSense()'s own comment; same shape,
            // same reasoning, for the connector side of a 4-wire
            // measurement's sense leads. Throws std::runtime_error if this
            // pin has no registered Sense-role path.
            //
            [[nodiscard]]
            auto findSense( VpcLocation location) const -> Path;

        private:
            std::vector<ConnectorWiringEntry> mEntries;
    };

    namespace detail
    {
        //
        // The right-hand column of the wiring grid: a table of (instrument,
        // pin) rows with no fabric path between them.
        //
        // Two tables have that shape -- hal::SourceWiring, which records where
        // a supply's output lands, and hal::TapWiring, which records where a
        // meter's leads are bolted (see both below for what each fact is and
        // why neither is an InstrumentWiring or a ConnectorWiring entry). They
        // are different facts about different halves of the bench, but the
        // questions asked of a set of (instrument, pin) rows are the same five
        // whichever fact the rows record, and they were previously written out
        // twice -- locationsOf() in particular character for character.
        //
        // Shared as a member rather than a base class, and that is the whole of
        // why the two classes below still exist as their own types instead of
        // being two aliases for this one. A landing is not a tap: the questions
        // each *asks* differ (a source is looked up by pin, a tap is confirmed
        // against one), the vocabulary differs (addLanding against addTap, for
        // the reason each of those names gives), and one of them refuses two
        // rows on a pin while the other's whole point is permitting them (see
        // hal::sourcesAt against hal::tapsAt). Composition keeps the five
        // bodies in one place and leaves all of that where it belongs.
        //
        // Templated on the row type so each table stores its own -- a
        // SourceWiringEntry and a TapWiringEntry are structurally identical and
        // deliberately still two types, so that a compile-time array of one can
        // never be counted as the other.
        //
        template<typename EntryT>
        class PinTable
        {
            public:
                auto add( const InstrumentId instrument, const VpcLocation location) -> void
                {
                    mRows.push_back( EntryT{ instrument, location });
                }

                //
                // Which instrument is on this pin, or nothing if none is. The
                // first row wins, which matters only where more than one is
                // legal -- see hal::TapWiring on why that is the tap side and
                // not the source side.
                //
                [[nodiscard]]
                auto instrumentAt( const VpcLocation location) const -> std::optional<InstrumentId>
                {
                    for( const auto & row : mRows)
                    {
                        if( row.location == location)
                        {
                            return row.instrument;
                        }
                    }

                    return std::nullopt;
                }

                //
                // Every pin this instrument is on. Empty rather than an error
                // for an instrument with no rows at all: that is the ordinary,
                // correct answer for every routed instrument on a rack rig, and
                // for every measuring instrument on the source table.
                //
                [[nodiscard]]
                auto locationsOf( const InstrumentId instrument) const -> std::vector<VpcLocation>
                {
                    std::vector<VpcLocation> locations;

                    for( const auto & row : mRows)
                    {
                        if( row.instrument == instrument)
                        {
                            locations.push_back( row.location);
                        }
                    }

                    return locations;
                }

                // Does this instrument appear at all?
                [[nodiscard]]
                auto covers( const InstrumentId instrument) const -> bool
                {
                    for( const auto & row : mRows)
                    {
                        if( row.instrument == instrument)
                        {
                            return true;
                        }
                    }

                    return false;
                }

                // Does this instrument appear on *this* pin?
                [[nodiscard]]
                auto covers( const InstrumentId instrument, const VpcLocation location) const -> bool
                {
                    for( const auto & row : mRows)
                    {
                        if( row.instrument == instrument && row.location == location)
                        {
                            return true;
                        }
                    }

                    return false;
                }

                //
                // Every pin this instrument is on, as text, for a message.
                //
                // "none" rather than an empty string, because the caller is a
                // refusal (see core::MeasureEngine) and an empty tail there
                // would read as a truncated sentence rather than as an answer.
                // That caller only asks about instruments covers() said yes to,
                // so "none" is not reachable through it -- it is what this
                // returns for anyone else who asks, rather than a case the
                // message has to guard.
                //
                [[nodiscard]]
                auto describe( const InstrumentId instrument) const -> std::string
                {
                    const auto locations = locationsOf( instrument);

                    if( locations.empty())
                    {
                        return "none";
                    }

                    std::string described;

                    for( const auto & location : locations)
                    {
                        if( ! described.empty())
                        {
                            described += ", ";
                        }

                        described += to_string( location);
                    }

                    return described;
                }

            private:
                std::vector<EntryT> mRows;
        };

        //
        // The same two questions at compile time, over one of the static row
        // arrays the SOURCE_WIRING/TAP_WIRING macros build (see the bottom of
        // this file). Written here so that sourcesAt(), tapsAt() and
        // isTapWiredInstrument() are each one line of macro rather than a
        // counting loop apiece.
        //
        // Generic over the container rather than taking a std::span of a named
        // row type, so neither the macro nor this has to spell whatever
        // std::define_static_array handed back.
        //
        template<typename RowsT>
        [[nodiscard]]
        consteval auto rowsAt( const RowsT & rows, const VpcLocation location) -> std::size_t
        {
            std::size_t count = 0;

            for( const auto & row : rows)
            {
                if( row.location == location)
                {
                    ++count;
                }
            }

            return count;
        }

        template<typename RowsT>
        [[nodiscard]]
        consteval auto rowsFor( const RowsT & rows, const InstrumentId instrument) -> bool
        {
            for( const auto & row : rows)
            {
                if( row.instrument == instrument)
                {
                    return true;
                }
            }

            return false;
        }
    } // namespace detail

    //
    // A third static wiring fact, and the only one that is not a fabric path
    // at all: which VPC pin a fixed-wired source instrument's output is
    // permanently cabled onto.
    //
    // This used to exist only as prose, in three files that each deferred it
    // to another one -- rig/wiring.inc noted that DcP1/DcP2 have no entry;
    // dut/adapter.inc noted that some of its points are where a supply lands
    // but that which one was "not yet recorded"; hal::keysight_edu36311a::EDU36311A noted that the
    // DUT adapter documents it. Nobody recorded it, and the gap had a cost:
    // dut/tests/test_wiring_coverage.cpp requires every declared point to be
    // wired, there was no way to say "this pin is not routed", and so mux
    // channels got invented for landing pins purely to make the build pass.
    // A fabricated route to a rail is worse than a missing one -- closing it
    // puts a supply onto signal relays that were never meant to carry it.
    //
    // Deliberately NOT keyed by, or derived from, hal::keysight_edu36311a::DirectWiring/
    // hal::keysight_edu36311a::RelayOutput3Isolated (see hal/keysight_edu36311a.hpp). Those say whether there is an
    // isolation relay in an instrument's path -- whether Connect/Disconnect
    // have anything to do. This says where the output lands. They are
    // independent: DcP3/DcP4 have a relay AND a hard-cabled landing pin, and
    // AcP1 lands on four. Keying this off the isolation tag would record
    // three of this rig's five sources as having no landing pin at all.
    //
    // Entries are (instrument, location) with no Path, because there is no
    // path -- the whole content of the fact is that the fabric is not
    // involved. That also makes SourceWiringEntry a structural type outright
    // (an enum plus a VpcLocation of enum/integers), so unlike the connector
    // side it needs no separate key struct to reach compile time; see
    // detail::ConnectorWiringKey's own comment for what forces that split
    // there and not here.
    //
    struct SourceWiringEntry
    {
        InstrumentId  instrument;
        VpcLocation   location;

        friend constexpr auto operator==( SourceWiringEntry, SourceWiringEntry) -> bool = default;
    };

    class SourceWiring
    {
        public:
            //
            // See WIRE_SOURCE below for the declarative form. Named
            // addLanding rather than addWire because what it records is not
            // a wire through the fabric -- keeping the verb distinct from
            // InstrumentWiring/ConnectorWiring's addWire() is the point.
            //
            auto addLanding( const InstrumentId instrument, const VpcLocation location) -> void
            {
                mLandings.add( instrument, location);
            }

            //
            // Which instrument lands on this pin, or nothing if none does.
            //
            // The non-throwing half of find() below, added for the electrical
            // interlock (see hal/verbs/interlock.hpp): "is a source cabled onto this
            // pin at all" is that caller's *first* question and an ordinary no
            // is its commonest answer, so an exception would be control flow.
            // Distinct from find() rather than replacing it, because the two
            // callers genuinely differ -- code that has already established a
            // pin is a landing pin wants the throw, since reaching it means an
            // invariant broke somewhere upstream.
            //
            [[nodiscard]]
            auto findLanding( const VpcLocation location) const -> std::optional<InstrumentId>
            {
                return mLandings.instrumentAt( location);
            }

            //
            // Which instrument lands on this pin. Throws std::runtime_error
            // if none does -- a caller asking this about an ordinary routed
            // pin has already gone wrong somewhere.
            //
            [[nodiscard]]
            auto find( VpcLocation location) const -> InstrumentId;

            //
            // Every pin this instrument lands on. A list, not one location:
            // hal::keysight_ac6834b::Ac6834B/AcP1 lands on four (three phases plus the
            // neutral/ground return), the same way it already has four
            // independent InstrumentWiring entries. Empty if this
            // instrument is not a fixed-wired source at all -- unlike
            // find() above this does not throw, because "no landing pins"
            // is the ordinary, correct answer for every routed instrument
            // on the rig.
            //
            [[nodiscard]]
            auto findAll( const InstrumentId instrument) const -> std::vector<VpcLocation>
            {
                return mLandings.locationsOf( instrument);
            }

        private:
            detail::PinTable<SourceWiringEntry> mLandings;
    };

    //
    // The fourth static wiring fact, and the mirror image of SourceWiring
    // above: which VPC pin a *measuring* instrument's input is permanently
    // cabled onto.
    //
    // This is the table that makes a rig with no switching hardware a
    // first-class deployment rather than a degraded one. Before it, the four
    // cells of the wiring grid were three:
    //
    //                  | -> Path              | -> VpcLocation
    //   ---------------+----------------------+----------------
    //   by instrument  | InstrumentWiring     | SourceWiring / TapWiring
    //   by location    | ConnectorWiring      | --
    //
    // and the missing one had a cost that showed up as soon as anyone tried
    // to describe a PSU/DMM/scope bench with the instruments bolted straight
    // to the DUT. A dut POINT must be covered by a WIRE_CONNECTOR row (see
    // dut/tests/test_wiring_coverage.cpp), a WIRE_CONNECTOR row needs a
    // HOP( ...), a HOP names a card, and a bench with no cards has none -- so
    // such a rig could declare no points at all, and every reading on it had
    // to be spelled point-free. What that loses is not routing, which such a
    // bench never wanted: it is the DUT vocabulary. The reading keys as
    // "Dmm1.Voltage" rather than "Output5V", the criteria hang off instrument
    // names, and a script is not movable between that bench and a rack --
    // which is the whole thing dev/rig/instrument.inc's choice of `Dmm1` over
    // `DevDmm` exists to protect.
    //
    // Entries are (instrument, location) with no Path, for exactly the reason
    // SourceWiringEntry has none: the whole content of the fact is that the
    // fabric is not involved. That also makes TapWiringEntry structural
    // outright (an enum plus a VpcLocation of enum/integers), so like the
    // source side and unlike the connector side it needs no reduced key
    // struct to reach compile time -- see detail::ConnectorWiringKey.
    //
    // -- What a tap row means, and what it does not -----------------------
    //
    // It opens a *name*, not a route. Nothing composes, nothing closes,
    // core::MeasureEngine's fabric is never touched for a tapped reading.
    // That is the same distinction SourceWiring already defends and it holds
    // here for the same reason -- which is worth stating because "at( ...)
    // now works on a rig with no fabric" reads at first like a route being
    // opened to a pin that has none.
    //
    // The risk it does carry is different, and quieter: a mislabelled
    // journal. `at( dut::Output5V)` on a direct-wired bench means "the lead
    // somebody bolted to Output5V", so if the lead moves and this table does
    // not, every reading is confidently wrong about which pin it came from. A
    // mux channel has the same failure mode, but a bad HOP is caught by
    // hal::hasChannel against the card's real channel space, and a bad
    // VpcLocation is just another plausible pin. Two things buy most of that
    // back -- the pairing check in core::MeasureEngine (see isTappedBy below,
    // and that class's routed overload), and the rule in
    // rig/tests/test_wiring_uniqueness.cpp that an instrument may not be both
    // routed and tapped.
    //
    // -- Why two taps on one pin is legal, where two sources are not -------
    //
    // hal::sourcesAt() is checked <= 1 because two supplies cabled onto one
    // pin are shorted together. The same count here is deliberately NOT
    // capped, and the asymmetry is real rather than an oversight: a scope
    // probe and a meter lead on one node are two high-impedance taps in
    // parallel, which is the ordinary way that bench is built and very often
    // the point of building it. Nor is it ambiguous, because a tapped reading
    // names both halves at the call site -- the port says which instrument,
    // at( ...) says which pin -- so Measure( Dmm1.voltage(), at( p)) and
    // Measure( Osc1.channel<1>().vpp(), at( p)) are two different, correct
    // readings of one node.
    //
    // -- Keyed by InstrumentId alone, and what that cannot check -----------
    //
    // A multichannel meter cabled to eight pins is eight rows under one
    // InstrumentId, the same shape AcP1's four landing pins already have. So
    // the pairing check below answers "is this instrument cabled to this pin
    // at all" and not "on which channel" -- because a core::Port does not
    // carry its channel. A scope channel is instrument state set by
    // the scope driver's Channel::method() and the port's qualifier holds the
    // measurement mode ("Vpp"), not the channel; making the channel visible
    // here would mean threading it through core::Port and every driver.
    //
    // That is the same limit rig/wiring.inc already states for Osc1 on the
    // routed side ("wiring is keyed by InstrumentId, and an instrument is one
    // key"), and it is recorded here rather than worked around because the
    // check that remains is still the one that catches the mistake people
    // actually make -- pointing at a pin this instrument is not on at all.
    //
    struct TapWiringEntry
    {
        InstrumentId  instrument;
        VpcLocation   location;

        friend constexpr auto operator==( TapWiringEntry, TapWiringEntry) -> bool = default;
    };

    class TapWiring
    {
        public:
            //
            // See WIRE_TAP below for the declarative form. Named addTap
            // rather than addWire for the reason SourceWiring::addLanding is:
            // what it records is not a wire through the fabric, and keeping
            // the verb distinct from InstrumentWiring/ConnectorWiring's
            // addWire() is the point.
            //
            auto addTap( const InstrumentId instrument, const VpcLocation location) -> void
            {
                mTaps.add( instrument, location);
            }

            //
            // Is this instrument cabled straight onto anything at all?
            //
            // core::MeasureEngine asks this first, and the answer decides
            // which kind of rig this instrument lives on: false means routed
            // (compose a path, close it, read, open it) and true means direct
            // (no fabric, and the pin had better be the one below). An
            // instrument cannot be both -- see
            // rig/tests/test_wiring_uniqueness.cpp, which makes that a build
            // error rather than leaving it to this ordering to resolve
            // silently.
            //
            [[nodiscard]]
            auto taps( const InstrumentId instrument) const -> bool
            {
                return mTaps.covers( instrument);
            }

            //
            // Is this instrument cabled onto *this* pin?
            //
            // The pairing check, and the reason this table is not simply a
            // lookup: at a tapped call site both halves are already known --
            // the port names the instrument, at( ...) names the pin -- so
            // there is nothing to resolve, only something to confirm. Getting
            // it wrong is the quiet failure this table introduces (a reading
            // taken from whichever pin the lead is actually on, filed under
            // the pin the script asked for), so core::MeasureEngine refuses
            // rather than reads.
            //
            [[nodiscard]]
            auto isTappedBy( const InstrumentId instrument, const VpcLocation location) const -> bool
            {
                return mTaps.covers( instrument, location);
            }

            //
            // Every pin this instrument taps. Empty for every routed
            // instrument on the rig, which is the ordinary answer and not an
            // error -- the same contract SourceWiring::findAll() has.
            //
            [[nodiscard]]
            auto findAll( const InstrumentId instrument) const -> std::vector<VpcLocation>
            {
                return mTaps.locationsOf( instrument);
            }

            //
            // Where this instrument actually is cabled, as text, for the
            // refusal core::MeasureEngine raises when a script points a
            // tapped instrument at the wrong pin.
            //
            // Here rather than in core because core::MeasureEngine is generic
            // over the location type and has no idea how to spell one --
            // exactly the reasoning that keeps to_string( instrumentId) an
            // unqualified ADL call in that header. This hands it a finished
            // string instead.
            //
            [[nodiscard]]
            auto describeTaps( const InstrumentId instrument) const -> std::string
            {
                return mTaps.describe( instrument);
            }

        private:
            detail::PinTable<TapWiringEntry> mTaps;
    };

    //
    // True if this rig's CONNECTOR_WIRING table (see rig/wiring.inc) has a
    // fixed path for this location/role -- see this file's own comment
    // above for why this is a standalone predicate rather than a
    // ConnectorWiring::find<Loc>() member. Declared here so the signature
    // is visible without rig/wiring.inc; DEFINED by the CONNECTOR_WIRING/
    // END_CONNECTOR_WIRING macro pair below, since the rig-specific key
    // table it checks against only exists once that macro has actually run.
    // Calling it from a translation unit that never included rig/wiring.inc
    // itself fails to compile -- the same way calling any other function
    // whose definition isn't visible would.
    //
    [[nodiscard]]
    consteval auto isWired( VpcLocation location, WireRole role = WireRole::Force) -> bool;

    //
    // The same question for the instrument side: does this rig give this
    // instrument a fixed path for this role at all?
    //
    // Exists for the same reason isWired() does -- so a rig's own wiring can be
    // checked once, at build time, rather than discovered by a script throwing
    // three hours into a run. What it is NOT is a per-measurement check:
    // whether a given reading needs the sense role is a property of the port's
    // type (see core::Port::requiresSensePath), while this is a property of the
    // rig, and core::MeasureEngine is generic code that can see neither rig nor
    // wiring table. The two facts can only be brought together where both are
    // visible, which is a rig-side check -- see dut/tests/test_wiring_coverage.cpp.
    //
    [[nodiscard]]
    consteval auto isInstrumentWired( InstrumentId instrument, WireRole role = WireRole::Force) -> bool;

    //
    // How many fixed-wired sources land on this pin, per this rig's
    // SOURCE_WIRING table. Exists for the same build-time reason isWired()
    // does, and is DEFINED by the SOURCE_WIRING/END_SOURCE_WIRING macro pair
    // below for the same reason -- see isWired()'s own comment above.
    //
    // A count rather than a bool because the two questions
    // dut/tests/test_wiring_coverage.cpp asks of it are different: "is this
    // pin a landing pin" (below) and "do two instruments claim it", which is
    // as quiet a mistake here as two POINTs sharing a pin already is on the
    // adapter side -- both tables would look complete and the rig would be
    // shorting two supplies together.
    //
    [[nodiscard]]
    consteval auto sourcesAt( VpcLocation location) -> std::size_t;

    //
    // Whether any source lands on this pin at all. The predicate nearly
    // every caller wants; sourcesAt() above is for the one that has to
    // count.
    //
    [[nodiscard]]
    consteval auto isSourceWired( VpcLocation location) -> bool;

    //
    // How many measuring instruments are cabled straight onto this pin, per
    // this rig's TAP_WIRING table. The tap-side counterpart of sourcesAt()
    // above, DEFINED by the TAP_WIRING/END_TAP_WIRING macro pair below for
    // the same reason -- see isWired()'s own comment.
    //
    // A count, like sourcesAt(), but for a different caller: nothing checks
    // it <= 1, because two taps on one node is the ordinary way a direct-wired
    // bench is built (see hal::TapWiring's own comment on why that asymmetry
    // with the source side is real). It is a count rather than a bool so a
    // rig-side check that wants to say something about how a pin is reached
    // can, without this file having to guess in advance which question that
    // is.
    //
    [[nodiscard]]
    consteval auto tapsAt( VpcLocation location) -> std::size_t;

    //
    // Whether any measuring instrument is cabled onto this pin at all.
    //
    // This is the predicate that makes a fabric-free rig declarable: a dut
    // POINT is covered when the rig either routes to it or cables to it, and
    // dut/tests/test_wiring_coverage.cpp asks exactly that (isWired ||
    // isTapWired). Before this existed, "covered" meant "routed", so a bench
    // with no cards could declare no points -- see hal::TapWiring's own
    // comment for what that cost.
    //
    [[nodiscard]]
    consteval auto isTapWired( VpcLocation location) -> bool;

    //
    // Whether this instrument is cabled straight onto anything, per this
    // rig's TAP_WIRING table -- the build-time twin of TapWiring::taps().
    //
    // Exists for one check and is worth having only because of it: an
    // instrument must not appear in both INSTRUMENT_WIRING and TAP_WIRING.
    // core::MeasureEngine asks "does this instrument tap anything" first and
    // never composes a path if the answer is yes, so a rig that says an
    // instrument is both routed and cabled has written a matrix path that can
    // never be closed. Pairing this with isInstrumentWired() above catches
    // that at build time; see rig/tests/test_wiring_uniqueness.cpp.
    //
    [[nodiscard]]
    consteval auto isTapWiredInstrument( InstrumentId instrument) -> bool;

    //
    // ---------------------------------------------------------------------
    // Who owns a fabric element
    // ---------------------------------------------------------------------
    // Every table above answers "what does this endpoint's route close". The
    // question nothing could ask was the other direction -- "whose relay is
    // this" -- and it is the one a copy-pasted row gets wrong: two
    // WIRE_CONNECTOR entries on one mux channel leave both tables looking
    // complete while a measurement at one pin quietly reads the other. That
    // is the exact twin of two POINTs sharing a VPC pin, which
    // dut/tests/test_wiring_coverage.cpp has caught for a while; this is the
    // data that lets the fabric side be checked the same way (see
    // rig/tests/test_wiring_uniqueness.cpp, which does the checking).
    //
    // "Whose" is not simply "appears in one entry only", because a real rig
    // shares relays on purpose: this rig's ten routed pins all pass through
    // the one crosspoint that puts the mux common on the measurement bus, and
    // hal::SwitchFabric's use counting exists precisely so several routes can
    // hold one relay at once. What cannot be shared is the relay at the
    // *endpoint* end of a path -- the one that says "this pin, not its
    // neighbour".
    //
    // Which makes a convention load-bearing that was until now only implied
    // by every table written so far: **a Path is written endpoint-first**.
    // Hop zero is the element nearest the instrument or the pin, and each hop
    // after it is one step further along the shared trunk. HOP( Mux1, 3),
    // CROSSPOINT( Matrix1, 0, 3, 0) is the DUT pin's own mux channel and then
    // the crosspoint everything shares -- not the other way round. Written
    // backwards, a rig gets a spurious failure from the check rather than
    // silence, which is the right way for a convention to be wrong.
    //
    enum class WiringSide
    {
        Instrument,
        Connector
    };

    //
    // A tagged pair rather than a variant, because this has to survive
    // std::define_static_array and std::variant is not a structural type
    // (see detail::ConnectorWiringKey's own comment for the same constraint
    // biting one field over). `instrument` is meaningful when side is
    // Instrument and `location` when it is Connector; the other holds
    // whatever its type default is and means nothing.
    //
    struct WiringOwner
    {
        WiringSide   side;
        InstrumentId instrument;
        VpcLocation  location;
        WireRole     role;

        friend constexpr auto operator==( WiringOwner, WiringOwner) -> bool = default;
    };

    //
    // One hop of one entry, flattened out of the tables above so the whole
    // rig can be walked element by element. `entry` is the row's index within
    // its own table, which with `owner.side` identifies the row -- and a row,
    // not an owner, is the unit here: hal::keysight_ac6834b::Ac6834B's AcP1 has four rows of its
    // own and they must not collide with each other either, which comparing
    // owners would have allowed.
    //
    // `identifies` marks hop zero -- the endpoint's own relay, per the
    // convention above.
    //
    struct WiringHop
    {
        WiringOwner     owner;
        std::size_t     entry;
        SwitchElementId element;
        bool            identifies;

        friend constexpr auto operator==( WiringHop, WiringHop) -> bool = default;
    };

    namespace detail
    {
        //
        // location + role only -- deliberately not the full
        // ConnectorWiringEntry (location + role + Path): SwitchElementId's
        // std::string_view device member isn't a structural type on this
        // standard library (its data members are private), so a
        // std::vector<ConnectorWiringEntry> can't be promoted to a genuine
        // compile-time array the way std::define_static_array() needs --
        // see isWired()'s own use of this below. A location/role pair is
        // all isWired() needs to answer "does a fixed path exist", so that's
        // all this keeps; the actual Path still only ever lives in the
        // ordinary runtime hal::connectorWiring above.
        //
        struct ConnectorWiringKey
        {
            VpcLocation location;
            WireRole    role;

            friend constexpr auto operator==( ConnectorWiringKey, ConnectorWiringKey) -> bool = default;
        };

        //
        // The instrument-side counterpart of ConnectorWiringKey above.
        // Simpler, because both members are enums: an InstrumentId/WireRole
        // pair is a structural type outright, so it needs none of the care
        // that keeps a Path (with its std::string_view device names) out of
        // the connector key.
        //
        struct InstrumentWiringKey
        {
            InstrumentId instrument;
            WireRole     role;

            friend constexpr auto operator==( InstrumentWiringKey, InstrumentWiringKey) -> bool = default;
        };

        [[nodiscard]]
        consteval auto keysOf( const std::vector<InstrumentWiringEntry> & entries) -> std::vector<InstrumentWiringKey>
        {
            std::vector<InstrumentWiringKey> keys;

            for( const auto & entry : entries)
            {
                keys.push_back( { entry.instrument, entry.role });
            }

            return keys;
        }

        [[nodiscard]]
        consteval auto keysOf( const std::vector<ConnectorWiringEntry> & entries) -> std::vector<ConnectorWiringKey>
        {
            std::vector<ConnectorWiringKey> keys;

            for( const auto & entry : entries)
            {
                keys.push_back( { entry.location, entry.role });
            }

            return keys;
        }

        //
        // The same entries flattened one level, into one WiringHop per hop
        // rather than one row per entry -- see hal::WiringHop above for what
        // the flattening is for. Two overloads rather than a template for the
        // same reason keysOf has two: the owner is built differently on each
        // side, and that difference is the whole content of the function.
        //
        [[nodiscard]]
        consteval auto hopsOf( const std::vector<InstrumentWiringEntry> & entries) -> std::vector<WiringHop>
        {
            std::vector<WiringHop> hops;

            for( std::size_t index = 0; index < entries.size(); ++index)
            {
                const auto & entry = entries[ index];
                const auto   owner = WiringOwner{ WiringSide::Instrument, entry.instrument, VpcLocation{}, entry.role };

                for( std::size_t hop = 0; hop < entry.path.size(); ++hop)
                {
                    hops.push_back( WiringHop{ owner, index, entry.path[ hop], hop == 0 });
                }
            }

            return hops;
        }

        [[nodiscard]]
        consteval auto hopsOf( const std::vector<ConnectorWiringEntry> & entries) -> std::vector<WiringHop>
        {
            std::vector<WiringHop> hops;

            for( std::size_t index = 0; index < entries.size(); ++index)
            {
                const auto & entry = entries[ index];
                const auto   owner = WiringOwner{ WiringSide::Connector, InstrumentId{}, entry.location, entry.role };

                for( std::size_t hop = 0; hop < entry.path.size(); ++hop)
                {
                    hops.push_back( WiringHop{ owner, index, entry.path[ hop], hop == 0 });
                }
            }

            return hops;
        }
    } // namespace detail
} // namespace hal

//
// INSTRUMENT_WIRING / WIRE_INSTRUMENT / WIRE_INSTRUMENT_SENSE /
// END_INSTRUMENT_WIRING and CONNECTOR_WIRING / WIRE_CONNECTOR /
// WIRE_CONNECTOR_SENSE / END_CONNECTOR_WIRING (and SOURCE_WIRING /
// WIRE_SOURCE / END_SOURCE_WIRING further down): declarative wiring tables,
// mirroring CRITERIA/CRIT and ADAPTER/POINT -- see a rig's own wiring.inc
// (rig/wiring.inc in this repo). Each rig has exactly one of each table
// (unlike CRITERIA, which has several groups per file), so these macros
// build one fixed, namespaced global apiece rather than taking a name
// argument. ADAPTER is the same one-per-build case and takes no name
// either -- it opens the fixed `dut` struct (see hal/topology/adapter.hpp) -- the
// difference being only that its members have to be struct members for
// POINT to get its compile-time misspelling check.
//
// Every wiring entry is a Path -- one hop or several, there's no separate
// "chain" form: WIRE_INSTRUMENT/WIRE_CONNECTOR both just take one or more
// HOP(...) elements and hand them straight to Path{...}. WIRE_INSTRUMENT_SENSE/
// WIRE_CONNECTOR_SENSE are the same shape again, just tagged WireRole::Sense
// -- see hal::InstrumentWiring's own comment on WireRole for when a real
// entry needs one at all (only instruments/points with genuine sense
// leads -- most don't).
//
//   INSTRUMENT_WIRING
//       WIRE_INSTRUMENT( Dmm1, CROSSPOINT( Matrix1, 0, 0, 0))
//       WIRE_INSTRUMENT( Ser1, HOP( Spst1, 16))
//       WIRE_INSTRUMENT_SENSE( Dmm1, CROSSPOINT( Matrix1, 1, 0, 0))
//   END_INSTRUMENT_WIRING
//
//   CONNECTOR_WIRING
//       WIRE_CONNECTOR( A, 1, 3, HOP( Mux1, 0), CROSSPOINT( Matrix1, 0, 3, 0))
//       WIRE_CONNECTOR( A, 1, 2, BANK( RfMux1, 0, 1))
//       WIRE_CONNECTOR_SENSE( A, 1, 3, HOP( Mux1, 1))
//   END_CONNECTOR_WIRING
//
// HOP(...) below builds one SwitchElementId -- the one thing every
// WIRE_INSTRUMENT/WIRE_CONNECTOR entry is made of, whether there's one or
// several, so a one-hop entry and a multi-hop chain read the same way at
// each individual hop.
//
// Two arguments, and both are checked. The device is a hal::SwitchDeviceId
// enumerator, so it has to be a card the rig's devices.inc declares -- a
// mistyped one is a compile error rather than a route to a card that isn't
// there -- and there is no kind argument any more, because the kind is
// something that file states once per card (see hal/fabric/switch_device.hpp for what
// carrying it per hop used to allow). The channel is checked against the
// card's own numbering, which is a fact the model column in that same file
// carries: HOP( Spdt1, 300) on an 80-channel relay card is now a compile
// error naming the card and the channels it does have (see hal::hop).
//
// CROSSPOINT(...) and BANK(...) are that same hop written the way a composite
// channel number is actually made up -- <group><row><column> on a matrix,
// <bank><channel> on a banked RF mux. Preferred over HOP wherever a card has
// one: the parts say at the call site what the digits mean, and a leading
// zero in the packed form (0300) is an octal literal, which the parts cannot
// be. A card whose spec carries no such scheme rejects them both.
//
#define HOP( device, channel) \
    hal::hop<hal::SwitchDeviceId::device, channel>()

#define CROSSPOINT( device, group, row, column) \
    hal::crosspoint<hal::SwitchDeviceId::device, group, row, column>()

// The parameter is bankNumber rather than bank because a macro parameter
// substitutes everywhere in its own replacement list -- including into
// hal::bank -- and a macro that eats the name of the function it expands to
// fails in a way the error message does not mention.
#define BANK( device, bankNumber, channel) \
    hal::bank<hal::SwitchDeviceId::device, bankNumber, channel>()

//
// ROW_COLUMN(...) is CROSSPOINT's two-part sibling, for a matrix that numbers
// a crosspoint by a row and a column and nothing else -- a Keysight 34932A's
// ROW_COLUMN( Matrix1, 3, 15) is channel 315. Not CROSSPOINT with a group of
// zero: see hal::rowColumn for why a group the card does not have is worse
// written as one than left out.
//
#define ROW_COLUMN( device, row, column) \
    hal::rowColumn<hal::SwitchDeviceId::device, row, column>()

//
// INSTRUMENT_WIRING's expansion builds four things from the one set of
// WIRE_INSTRUMENT/WIRE_INSTRUMENT_SENSE lines, not one -- the same shape
// CONNECTOR_WIRING uses below, and for the same reasons: the ordinary runtime
// hal::instrumentWiring, a compile-time instrument/role key table,
// isInstrumentWired() itself, and hal::instrumentWiringHops (the same rows
// flattened one hop at a time, for the ownership check -- see hal::WiringHop).
// One list of rows, four readers, so no rig can wire a lead for the runtime
// table and not for the checks.
//
// buildInstrumentWiringEntries() is constexpr rather than consteval on
// purpose, and tightening it would not compile -- see CONNECTOR_WIRING's
// comment below for the full reason, which applies unchanged here: the
// hal::instrumentWiring IIFE calls this builder at ordinary runtime.
//
#define INSTRUMENT_WIRING                                                              \
    namespace hal { namespace detail {                                                  \
    constexpr auto buildInstrumentWiringEntries() -> std::vector<InstrumentWiringEntry> \
    {                                                                                   \
        std::vector<InstrumentWiringEntry> entries;

#define WIRE_INSTRUMENT( instrument, ...) \
        entries.push_back( InstrumentWiringEntry{ InstrumentId::instrument, WireRole::Force, Path{ __VA_ARGS__ } });

#define WIRE_INSTRUMENT_SENSE( instrument, ...) \
        entries.push_back( InstrumentWiringEntry{ InstrumentId::instrument, WireRole::Sense, Path{ __VA_ARGS__ } });

#define END_INSTRUMENT_WIRING                                                          \
        return entries;                                                                \
    }                                                                                   \
    inline constexpr auto instrumentWiringKeys =                                       \
        std::define_static_array( keysOf( buildInstrumentWiringEntries()));            \
    } /* namespace detail */                                                            \
    inline constexpr auto instrumentWiringHops =                                        \
        std::define_static_array( detail::hopsOf( detail::buildInstrumentWiringEntries())); \
    inline const InstrumentWiring instrumentWiring = []                                 \
    {                                                                                   \
        InstrumentWiring w;                                                            \
        for( const auto & entry : detail::buildInstrumentWiringEntries())              \
        {                                                                               \
            w.addWire( entry.instrument, entry.path, entry.role);                       \
        }                                                                               \
        return w;                                                                       \
    }();                                                                                \
    inline consteval auto isInstrumentWired( InstrumentId instrument, WireRole role) -> bool \
    {                                                                                   \
        for( const auto & key : detail::instrumentWiringKeys)                          \
        {                                                                               \
            if( key.instrument == instrument && key.role == role)                       \
            {                                                                           \
                return true;                                                            \
            }                                                                           \
        }                                                                               \
        return false;                                                                   \
    }                                                                                   \
    }

//
// CONNECTOR_WIRING's expansion builds four things from the one set of
// WIRE_CONNECTOR/WIRE_CONNECTOR_SENSE lines below: the ordinary runtime
// hal::connectorWiring (unchanged from before), a compile-time location/role
// key table, isWired() itself, and hal::connectorWiringHops -- see this file's own
// comment on WireRole/isWired above for why the key table needs its own
// builder rather than reusing hal::connectorWiring's storage.
// buildConnectorWiringEntries() is the one place __VA_ARGS__ (the HOP(...)
// chain) is actually spelled out; both hal::connectorWiring and
// detail::connectorWiringKeys are derived from calling it, not from
// separately re-stating each WIRE_CONNECTOR line. It's constexpr rather than
// consteval on purpose: hal::connectorWiring's own IIFE below calls it at
// ordinary runtime (building a std::vector<ConnectorWiringEntry> ready for
// ConnectorWiring::addWire(), an ordinary non-constexpr call) -- a consteval
// builder would force that whole IIFE to become an immediate function too
// (calling a consteval function anywhere in a function body promotes it),
// which then fails to compile since addWire() itself isn't constexpr.
// connectorWiringKeys below still gets it evaluated at compile time, the
// same as any constexpr function used to initialize a constexpr variable.
//
#define CONNECTOR_WIRING                                                            \
    namespace hal { namespace detail {                                              \
    constexpr auto buildConnectorWiringEntries() -> std::vector<ConnectorWiringEntry> \
    {                                                                                \
        std::vector<ConnectorWiringEntry> entries;

#define WIRE_CONNECTOR( rack, connector, pin, ...) \
        entries.push_back( ConnectorWiringEntry{ VpcLocation{ VpcRack::rack, connector, pin }, WireRole::Force, Path{ __VA_ARGS__ } });

#define WIRE_CONNECTOR_SENSE( rack, connector, pin, ...) \
        entries.push_back( ConnectorWiringEntry{ VpcLocation{ VpcRack::rack, connector, pin }, WireRole::Sense, Path{ __VA_ARGS__ } });

#define END_CONNECTOR_WIRING                                                        \
        return entries;                                                            \
    }                                                                               \
    inline constexpr auto connectorWiringKeys =                                     \
        std::define_static_array( keysOf( buildConnectorWiringEntries()));          \
    } /* namespace detail */                                                        \
    inline constexpr auto connectorWiringHops =                                     \
        std::define_static_array( detail::hopsOf( detail::buildConnectorWiringEntries())); \
    inline const ConnectorWiring connectorWiring = []                               \
    {                                                                                \
        ConnectorWiring w;                                                          \
        for( const auto & entry : detail::buildConnectorWiringEntries())            \
        {                                                                           \
            w.addWire( entry.location, entry.path, entry.role);                     \
        }                                                                           \
        return w;                                                                   \
    }();                                                                            \
    inline consteval auto isWired( VpcLocation location, WireRole role) -> bool      \
    {                                                                                \
        for( const auto & key : detail::connectorWiringKeys)                        \
        {                                                                           \
            if( key.location == location && key.role == role)                       \
            {                                                                       \
                return true;                                                        \
            }                                                                       \
        }                                                                           \
        return false;                                                              \
    }                                                                               \
    }

//
// SOURCE_WIRING / WIRE_SOURCE / END_SOURCE_WIRING: the third table, naming
// which VPC pin each fixed-wired source instrument's output is cabled onto
// -- see hal::SourceWiring above for what the fact is and why it is neither
// an InstrumentWiring nor a ConnectorWiring entry.
//
//   SOURCE_WIRING
//       WIRE_SOURCE( DcP1, A, 1, 3)
//       WIRE_SOURCE( AcP1, A, 3, 1)   // phase A
//       WIRE_SOURCE( AcP1, A, 3, 3)   // phase B
//   END_SOURCE_WIRING
//
// One row per pin, so a source landing on several (AcP1's phases and its
// neutral/ground return) is several rows under one InstrumentId -- the same
// shape WIRE_INSTRUMENT already uses for that instrument's four matrix
// channels, and for the same reason: four independent physical connections,
// not one four-element chain.
//
// No HOP(...), and no Path: there is nothing in the fabric to name. That is
// the entire content of the fact.
//
// Same three-readers-from-one-list expansion as the two tables above -- the
// runtime hal::sourceWiring, a compile-time entry array, and sourcesAt()/
// isSourceWired() themselves -- so no rig can record a landing for the
// runtime table and not for the build-time check. Unlike the connector
// table, the compile-time array holds whole entries rather than a reduced
// key struct, because SourceWiringEntry is already structural (see its own
// comment).
//
// A rig with no fixed-wired sources still writes the empty table rather than
// omitting it: sourcesAt()/isSourceWired() are declared unconditionally
// above and defined only here, so leaving it out would make any translation
// unit that asks the question fail to link rather than answer "no".
//
// buildSourceWiringEntries() is constexpr rather than consteval for the same
// reason its two counterparts are, and tightening it would not compile --
// see CONNECTOR_WIRING's comment above.
//
#define SOURCE_WIRING                                                           \
    namespace hal { namespace detail {                                          \
    constexpr auto buildSourceWiringEntries() -> std::vector<SourceWiringEntry> \
    {                                                                            \
        std::vector<SourceWiringEntry> entries;

#define WIRE_SOURCE( instrument, rack, connector, pin) \
        entries.push_back( SourceWiringEntry{ InstrumentId::instrument, VpcLocation{ VpcRack::rack, connector, pin } });

#define END_SOURCE_WIRING                                                       \
        return entries;                                                        \
    }                                                                           \
    inline constexpr auto sourceWiringEntries =                                 \
        std::define_static_array( buildSourceWiringEntries());                  \
    } /* namespace detail */                                                    \
    inline const SourceWiring sourceWiring = []                                 \
    {                                                                            \
        SourceWiring w;                                                         \
        for( const auto & entry : detail::buildSourceWiringEntries())           \
        {                                                                        \
            w.addLanding( entry.instrument, entry.location);                    \
        }                                                                        \
        return w;                                                                \
    }();                                                                         \
    inline consteval auto sourcesAt( VpcLocation location) -> std::size_t        \
    {                                                                            \
        return detail::rowsAt( detail::sourceWiringEntries, location);           \
    }                                                                            \
    inline consteval auto isSourceWired( VpcLocation location) -> bool           \
    {                                                                            \
        return sourcesAt( location) > 0;                                        \
    }                                                                            \
    }

//
// TAP_WIRING / WIRE_TAP / END_TAP_WIRING: the fourth table, naming which VPC
// pin each direct-wired *measuring* instrument's input is cabled onto -- see
// hal::TapWiring above for what the fact is, and why it is neither an
// InstrumentWiring entry (there is no path) nor a SourceWiring one (nothing
// is being driven).
//
//   TAP_WIRING
//       WIRE_TAP( Dmm1, A, 1, 5)
//       WIRE_TAP( Osc1, A, 1, 5)   // scope and meter on one node
//   END_TAP_WIRING
//
// One row per pin, so a multichannel meter cabled to eight pins is eight rows
// under one InstrumentId -- the same shape WIRE_SOURCE already uses for
// AcP1's four landing pins, and with the same limit: this is keyed by
// instrument, not by (instrument, channel). See hal::TapWiring's own comment
// for why the channel is out of reach and what the check that remains still
// catches.
//
// No HOP( ...) and no Path, exactly as WIRE_SOURCE has none. That is the
// entire content of the fact.
//
// Same three-readers-from-one-list expansion as the three tables above -- the
// runtime hal::tapWiring, a compile-time entry array, and tapsAt()/
// isTapWired()/isTapWiredInstrument() themselves -- so no rig can record a
// tap for the runtime table and not for the build-time checks.
//
// A rig with no direct-wired instruments still writes the empty table rather
// than omitting it, for the reason every other block here gives: the three
// predicates are declared unconditionally above and defined only here, so
// leaving it out makes a translation unit that asks the question fail to link
// rather than answer "no". That is the ordinary case today -- a rack rig
// routes everything and writes an empty block (see rig/wiring.inc).
//
// buildTapWiringEntries() is constexpr rather than consteval for the same
// reason its three counterparts are -- see CONNECTOR_WIRING's comment above.
//
#define TAP_WIRING                                                              \
    namespace hal { namespace detail {                                          \
    constexpr auto buildTapWiringEntries() -> std::vector<TapWiringEntry>       \
    {                                                                            \
        std::vector<TapWiringEntry> entries;

#define WIRE_TAP( instrument, rack, connector, pin) \
        entries.push_back( TapWiringEntry{ InstrumentId::instrument, VpcLocation{ VpcRack::rack, connector, pin } });

#define END_TAP_WIRING                                                          \
        return entries;                                                        \
    }                                                                           \
    inline constexpr auto tapWiringEntries =                                    \
        std::define_static_array( buildTapWiringEntries());                     \
    } /* namespace detail */                                                    \
    inline const TapWiring tapWiring = []                                       \
    {                                                                            \
        TapWiring w;                                                            \
        for( const auto & entry : detail::buildTapWiringEntries())              \
        {                                                                        \
            w.addTap( entry.instrument, entry.location);                        \
        }                                                                        \
        return w;                                                                \
    }();                                                                         \
    inline consteval auto tapsAt( VpcLocation location) -> std::size_t           \
    {                                                                            \
        return detail::rowsAt( detail::tapWiringEntries, location);              \
    }                                                                            \
    inline consteval auto isTapWired( VpcLocation location) -> bool              \
    {                                                                            \
        return tapsAt( location) > 0;                                           \
    }                                                                            \
    inline consteval auto isTapWiredInstrument( InstrumentId instrument) -> bool \
    {                                                                            \
        return detail::rowsFor( detail::tapWiringEntries, instrument);           \
    }                                                                            \
    }
