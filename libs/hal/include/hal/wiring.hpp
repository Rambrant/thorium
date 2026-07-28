#pragma once

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
    // visible (hal/measure.cpp, hal/apply.cpp), never at the actual script
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
    //     (see core::MeasureSetup::RequiresSensePath in core/port.hpp and
    //     core::MeasureEngine's own comment in core/measure.hpp), so
    //     force and sense need to be looked up independently and combined
    //     only when asked for -- findSense() below exists for exactly
    //     that, kept separate from find()/findAll() rather than folded in.
    //
    //   - A power supply's remote-sense leads (if this rig's supply has
    //     them wired at all) are a fixed fact of the bench, not a
    //     per-Apply choice -- if they exist, Connect/Disconnect should
    //     always close/open them together with the force path, the same
    //     way hal::Ac6677A's phases and ground already do. findAll()
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
            // (hal::Ac6677A's three phases plus ground/neutral return, or
            // a power supply's remote-sense leads if this rig has them).
            // Single-connection instruments with no sense wiring
            // (hal::DSO8064, hal::L4411A) keep using find() above.
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
// WIRE_CONNECTOR_SENSE / END_CONNECTOR_WIRING: declarative wiring tables,
// mirroring CRITERIA/CRIT and ADAPTER/POINT -- see a rig's own wiring.inc
// (rig/wiring.inc in this repo). Each rig has exactly one of each table
// (unlike ADAPTER, which is named per DUT
// profile, or CRITERIA, which has several groups per file), so these
// macros build one fixed, namespaced global apiece rather than taking a
// name argument.
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
//       WIRE_INSTRUMENT( Dmm1, HOP( Matrix, "Matrix2", 14))
//       WIRE_INSTRUMENT( Osc1, HOP( Mux, "Mux22", 65), HOP( Matrix, "Matrix2", 10))
//       WIRE_INSTRUMENT_SENSE( Dmm1, HOP( Matrix, "Matrix2", 15))
//   END_INSTRUMENT_WIRING
//
//   CONNECTOR_WIRING
//       WIRE_CONNECTOR( A, 1, 3, HOP( Mux, "Mux1", 3))
//       WIRE_CONNECTOR( A, 3, 1, HOP( Mux, "Mux31", 9), HOP( Mux, "Mux22", 65), HOP( Matrix, "Matrix11", 0))
//       WIRE_CONNECTOR_SENSE( A, 1, 3, HOP( Mux, "Mux1", 4))
//   END_CONNECTOR_WIRING
//
// HOP(...) below builds one SwitchElementId -- the one thing every
// WIRE_INSTRUMENT/WIRE_CONNECTOR entry is made of, whether there's one or
// several, so a one-hop entry and a multi-hop chain read the same way at
// each individual hop.
//
#define HOP( deviceKind, device, channel) \
    hal::SwitchElementId{ hal::SwitchDeviceKind::deviceKind, device, channel }

#define INSTRUMENT_WIRING                                  \
    namespace hal { inline const InstrumentWiring instrumentWiring = [] \
    {                                                       \
        InstrumentWiring w;

#define WIRE_INSTRUMENT( instrument, ...) \
        w.addWire( InstrumentId::instrument, Path{ __VA_ARGS__ });

#define WIRE_INSTRUMENT_SENSE( instrument, ...) \
        w.addWire( InstrumentId::instrument, Path{ __VA_ARGS__ }, WireRole::Sense);

#define END_INSTRUMENT_WIRING \
        return w;              \
    }(); }

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
