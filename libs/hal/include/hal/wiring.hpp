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
    struct InstrumentWiringEntry
    {
        InstrumentId  instrument;
        Path          path;
    };

    class InstrumentWiring
    {
        public:
            //
            // Single-hop convenience -- the common case (one instrument,
            // one dedicated matrix channel). Equivalent to
            // addWire(instrument, Path{ channel}) below.
            //
            auto addWire( InstrumentId instrument, SwitchElementId channel) -> void;

            //
            // The general form -- one whole chain, closed/opened together,
            // for one fixed instrument connection. See WIRE_INSTRUMENT
            // below for the declarative form (one or more HOP(...)
            // elements, no separate "chain" macro).
            //
            auto addWire( InstrumentId instrument, Path path) -> void;

            //
            // The first matching entry's Path. Throws std::runtime_error
            // if this instrument has no fixed path on this rig's fabric.
            // For an instrument with more than one independent fixed
            // connection (see findAll() below), which entry "first" means
            // is unspecified -- use findAll() instead in that case.
            //
            [[nodiscard]]
            auto find( InstrumentId instrument) const -> Path;

            //
            // Every matching entry's Path, flattened into one combined
            // Path -- for an instrument fixed-wired with more than one
            // independent physical connection (e.g. hal::Ac6677A's three
            // phases plus ground/neutral return, see that header's own
            // comment), each its own addWire() entry under the same
            // InstrumentId, and Connect/Disconnect need all of them
            // together, not just one. Single-connection instruments
            // (hal::N6701A, hal::L4411A, hal::DSO8064) keep using find()
            // above. Throws std::runtime_error if this instrument has no
            // fixed path at all.
            //
            [[nodiscard]]
            auto findAll( InstrumentId instrument) const -> Path;

        private:
            std::vector<InstrumentWiringEntry> mEntries;
    };

    struct ConnectorWiringEntry
    {
        VpcLocation  location;
        Path         path;
    };

    class ConnectorWiring
    {
        public:
            //
            // Single-hop convenience -- see InstrumentWiring::addWire()'s
            // own comment; same shape, same reasoning.
            //
            auto addWire( VpcLocation location, SwitchElementId channel) -> void;

            //
            // The general form -- see WIRE_CONNECTOR below for the
            // declarative form (one or more HOP(...) elements, no separate
            // "chain" macro).
            //
            auto addWire( VpcLocation location, Path path) -> void;

            //
            // Throws std::runtime_error if this VPC pin has no fixed path
            // on this rig's fabric.
            //
            [[nodiscard]]
            auto find( VpcLocation location) const -> Path;

        private:
            std::vector<ConnectorWiringEntry> mEntries;
    };
} // namespace hal

//
// INSTRUMENT_WIRING / WIRE_INSTRUMENT / END_INSTRUMENT_WIRING and
// CONNECTOR_WIRING / WIRE_CONNECTOR / END_CONNECTOR_WIRING: declarative
// wiring tables, mirroring CRITERIA/CRIT and ADAPTER/POINT -- see
// hal/wiring.inc. Each rig has exactly one of each table (unlike ADAPTER,
// which is named per DUT profile, or CRITERIA, which has several groups per
// file), so these macros build one fixed, namespaced global apiece rather
// than taking a name argument.
//
// Every wiring entry is a Path -- one hop or several, there's no separate
// "chain" form: WIRE_INSTRUMENT/WIRE_CONNECTOR both just take one or more
// HOP(...) elements and hand them straight to Path{...}.
//
//   INSTRUMENT_WIRING
//       WIRE_INSTRUMENT( Dmm1, HOP( Matrix, "Matrix2", 14))
//       WIRE_INSTRUMENT( Osc1, HOP( Mux, "Mux22", 65), HOP( Matrix, "Matrix2", 10))
//   END_INSTRUMENT_WIRING
//
//   CONNECTOR_WIRING
//       WIRE_CONNECTOR( A, 1, 3, HOP( Mux, "Mux1", 3))
//       WIRE_CONNECTOR( A, 3, 1, HOP( Mux, "Mux31", 9), HOP( Mux, "Mux22", 65), HOP( Matrix, "Matrix11", 0))
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

#define END_INSTRUMENT_WIRING \
        return w;              \
    }(); }

#define CONNECTOR_WIRING                                 \
    namespace hal { inline const ConnectorWiring connectorWiring = [] \
    {                                                      \
        ConnectorWiring w;

#define WIRE_CONNECTOR( rack, connector, pin, ...) \
        w.addWire( VpcLocation{ VpcRack::rack, connector, pin }, Path{ __VA_ARGS__ });

#define END_CONNECTOR_WIRING \
        return w;              \
    }(); }
