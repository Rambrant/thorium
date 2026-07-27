#pragma once

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
    // TODO(reflection): find() below is a runtime linear search over a
    // constexpr-eligible table -- the *data* is a fixed wiring fact decided
    // once by whoever wired the bench, but the lookup itself is still a
    // runtime operation, because it's keyed by an InstrumentId that only the
    // running program knows (two Dmms, same C++ type, different runtime id).
    // ConnectorWiring::find() below is keyed by a VpcLocation that IS known
    // at compile time at every real call site (it's a template parameter on
    // the AdapterPointTag being measured -- see core/adapter.hpp) if this
    // engine chooses to look it up as one; today it stays a runtime lookup
    // like InstrumentWiring's, deferred as a documented next step now that
    // Loc is available to do so with.
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

#define CONNECTOR_WIRING                                 \
    namespace hal { inline const ConnectorWiring connectorWiring = [] \
    {                                                      \
        ConnectorWiring w;

#define WIRE_CONNECTOR( rack, connector, pin, ...) \
        w.addWire( VpcLocation{ VpcRack::rack, connector, pin }, Path{ __VA_ARGS__ });

#define WIRE_CONNECTOR_SENSE( rack, connector, pin, ...) \
        w.addWire( VpcLocation{ VpcRack::rack, connector, pin }, Path{ __VA_ARGS__ }, WireRole::Sense);

#define END_CONNECTOR_WIRING \
        return w;              \
    }(); }
