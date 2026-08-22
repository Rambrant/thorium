#pragma once

#include <cstddef>
#include <meta>
#include <vector>

#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/vpc_location.hpp"

namespace hal
{
    //
    // Two independent static wiring facts, per the framing that motivated
    // this split: an instrument's matrix/mux path is fixed regardless of
    // which DUT pin is being measured, and a connector pin's path is fixed
    // regardless of which instrument is doing the measuring. A route is
    // their composition at the moment a measurement is taken -- see
    // core/measure.hpp -- not a fact either table stores per (instrument,
    // pin) pair, which is what the combined RouteTable this replaces used
    // to do.
    //
    // Each entry's Path (see hal/switch_fabric.hpp) is a chain, not
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
    // AdapterPointTag being measured -- see core/adapter.hpp), which looked
    // like a straightforward reflection upgrade: give ConnectorWiring a
    // find<Loc>() so a missing wiring entry is a compile error wherever
    // Measure() is actually called, not just when that one test happens to
    // run. It isn't that simple: core::MeasureEngine (core/measure.hpp) is
    // instantiated in whichever script calls Measure() -- e.g.
    // suite/scripts/fuse_register_script.cpp -- and those files never
    // #include this rig's wiring.inc, on purpose (see this file's own
    // comment on CONNECTOR_WIRING below, and core/measure.hpp's own comment
    // on why ConnectorWiringT's concrete type is only ever named in
    // hal/measure.cpp). A consteval check needs the wiring *data* itself
    // visible as a compile-time constant in the very translation unit
    // performing the check -- unlike an ordinary runtime call, constant
    // evaluation can't reach across translation units the way linking can --
    // so find<Loc>() could only ever fire where wiring.inc is already
    // visible (hal/measure.cpp, hal/route.cpp), never at the actual script
    // call sites where a typo would matter. Threading it through anyway
    // would mean exposing this rig's wiring table through a header
    // everywhere Measure()/Connect() are called, undoing that isolation on
    // purpose.
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
    //     plain 2-wire resistance() call on the same hal::L4411A must not
    //     touch sense at all. That's a per-measurement, per-call choice
    //     (see core::Port::requiresSensePath in core/port.hpp and
    //     core::MeasureEngine's own comment in core/measure.hpp), so
    //     force and sense need to be looked up independently and combined
    //     only when asked for -- findSense() below exists for exactly
    //     that, kept separate from find()/findAll() rather than folded in.
    //
    //   - A power supply's remote-sense leads (if this rig's supply has
    //     them wired at all) are a fixed fact of the bench, not a
    //     per-Apply choice -- if they exist, Connect/Disconnect should
    //     always close/open them together with the force path, the same
    //     way hal::Ac6834B's phases and ground already do. findAll()
    //     below deliberately does NOT filter by role for this reason: it
    //     means "every entry registered for this instrument, force and
    //     sense alike, closed together" -- see hal::N6701A's own
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
            // (hal::Ac6834B's three phases plus ground/neutral return, or
            // a power supply's remote-sense leads if this rig has them).
            // Single-connection instruments with no sense wiring
            // (hal::DSO8064A, hal::L4411A) keep using find() above.
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

    //
    // A third static wiring fact, and the only one that is not a fabric path
    // at all: which VPC pin a fixed-wired source instrument's output is
    // permanently cabled onto.
    //
    // This used to exist only as prose, in three files that each deferred it
    // to another one -- rig/wiring.inc noted that DcP1/DcP2 have no entry;
    // dut/adapter.inc noted that some of its points are where a supply lands
    // but that which one was "not yet recorded"; hal::N6701A noted that the
    // DUT adapter documents it. Nobody recorded it, and the gap had a cost:
    // dut/tests/test_wiring_coverage.cpp requires every declared point to be
    // wired, there was no way to say "this pin is not routed", and so mux
    // channels got invented for landing pins purely to make the build pass.
    // A fabricated route to a rail is worse than a missing one -- closing it
    // puts a supply onto signal relays that were never meant to carry it.
    //
    // Deliberately NOT keyed by, or derived from, hal::DirectWiring/
    // hal::RelayIsolated (see hal/n6701a.hpp). Those say whether there is an
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
            auto addLanding( InstrumentId instrument, VpcLocation location) -> void;

            //
            // Which instrument lands on this pin. Throws std::runtime_error
            // if none does -- a caller asking this about an ordinary routed
            // pin has already gone wrong somewhere.
            //
            [[nodiscard]]
            auto find( VpcLocation location) const -> InstrumentId;

            //
            // Every pin this instrument lands on. A list, not one location:
            // hal::Ac6834B/AcP1 lands on four (three phases plus the
            // neutral/ground return), the same way it already has four
            // independent InstrumentWiring entries. Empty if this
            // instrument is not a fixed-wired source at all -- unlike
            // find() above this does not throw, because "no landing pins"
            // is the ordinary, correct answer for every routed instrument
            // on the rig.
            //
            [[nodiscard]]
            auto findAll( InstrumentId instrument) const -> std::vector<VpcLocation>;

        private:
            std::vector<SourceWiringEntry> mEntries;
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
// either -- it opens the fixed `dut` struct (see hal/adapter.hpp) -- the
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
//       WIRE_INSTRUMENT( Dmm1, HOP( Matrix2, 14))
//       WIRE_INSTRUMENT( Osc1, HOP( Mux2, 65), HOP( Matrix2, 10))
//       WIRE_INSTRUMENT_SENSE( Dmm1, HOP( Matrix2, 15))
//   END_INSTRUMENT_WIRING
//
//   CONNECTOR_WIRING
//       WIRE_CONNECTOR( A, 1, 3, HOP( Mux1, 3))
//       WIRE_CONNECTOR( A, 3, 1, HOP( Mux1, 9), HOP( Mux2, 65), HOP( Matrix2, 0))
//       WIRE_CONNECTOR_SENSE( A, 1, 3, HOP( Mux1, 4))
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
// something that file states once per card (see hal/switch_device.hpp for what
// carrying it per hop used to allow).
//
#define HOP( device, channel) \
    hal::SwitchElementId{ hal::SwitchDeviceId::device, channel }

//
// INSTRUMENT_WIRING's expansion builds three things from the one set of
// WIRE_INSTRUMENT/WIRE_INSTRUMENT_SENSE lines, not one -- the same shape
// CONNECTOR_WIRING uses below, and for the same reasons: the ordinary runtime
// hal::instrumentWiring, a compile-time instrument/role key table, and
// isInstrumentWired() itself. One list of rows, three readers, so no rig can
// wire a lead for the runtime table and not for the check.
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
// CONNECTOR_WIRING's expansion builds three things from the one set of
// WIRE_CONNECTOR/WIRE_CONNECTOR_SENSE lines below, not two: the ordinary
// runtime hal::connectorWiring (unchanged from before), a compile-time
// location/role key table, and isWired() itself -- see this file's own
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
        std::size_t count = 0;                                                  \
        for( const auto & entry : detail::sourceWiringEntries)                  \
        {                                                                        \
            if( entry.location == location)                                     \
            {                                                                    \
                ++count;                                                         \
            }                                                                    \
        }                                                                        \
        return count;                                                            \
    }                                                                            \
    inline consteval auto isSourceWired( VpcLocation location) -> bool           \
    {                                                                            \
        return sourcesAt( location) > 0;                                        \
    }                                                                            \
    }
