#pragma once

#include <vector>

#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/vpc_location.hpp"

namespace hal
{
    //
    // Two independent static wiring facts, per the framing that motivated
    // this split: an instrument's matrix/mux channel is fixed regardless of
    // which DUT pin is being measured, and a connector pin's channel is
    // fixed regardless of which instrument is doing the measuring. A route
    // is their composition at the moment a measurement is taken -- see
    // core/measure.hpp -- not a fact either table stores per (instrument,
    // pin) pair, which is what the combined RouteTable this replaces used
    // to do.
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
        InstrumentId     instrument;
        SwitchElementId  channel;
    };

    class InstrumentWiring
    {
        public:
            auto addWire( InstrumentId instrument, SwitchElementId channel) -> void;

            //
            // Throws std::runtime_error if this instrument has no fixed
            // channel on this rig's fabric.
            //
            [[nodiscard]]
            auto find( InstrumentId instrument) const -> SwitchElementId;

            //
            // Same table, every matching channel rather than just the
            // first -- for an instrument fixed-wired with more than one
            // physical connection (e.g. hal::Ac6677A's three phases plus
            // ground/neutral return, see that header's own comment), each
            // one is its own addWire() entry under the same InstrumentId,
            // and Connect/Disconnect need all of them together, not just
            // one. Single-connection instruments (hal::N6701A,
            // hal::L4411A, hal::DSO8064) keep using find() above; this
            // exists alongside it rather than replacing it so their call
            // sites don't have to deal with a one-element vector for no
            // reason. Throws std::runtime_error if this instrument has no
            // fixed channel at all.
            //
            [[nodiscard]]
            auto findAll( InstrumentId instrument) const -> std::vector<SwitchElementId>;

        private:
            std::vector<InstrumentWiringEntry> mEntries;
    };

    struct ConnectorWiringEntry
    {
        VpcLocation      location;
        SwitchElementId  channel;
    };

    class ConnectorWiring
    {
        public:
            auto addWire( VpcLocation location, SwitchElementId channel) -> void;

            //
            // Throws std::runtime_error if this VPC pin has no fixed channel
            // on this rig's fabric.
            //
            [[nodiscard]]
            auto find( VpcLocation location) const -> SwitchElementId;

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
//   INSTRUMENT_WIRING
//       WIRE_INSTRUMENT( Dmm1, Matrix, "Matrix2", 14)
//   END_INSTRUMENT_WIRING
//
//   CONNECTOR_WIRING
//       WIRE_CONNECTOR( A, 1, 3, Mux, "Mux1", 3)
//   END_CONNECTOR_WIRING
//
#define INSTRUMENT_WIRING                                  \
    namespace hal { inline const InstrumentWiring instrumentWiring = [] \
    {                                                       \
        InstrumentWiring w;

#define WIRE_INSTRUMENT( instrument, deviceKind, device, channel) \
        w.addWire( InstrumentId::instrument, SwitchElementId{ SwitchDeviceKind::deviceKind, device, channel });

#define END_INSTRUMENT_WIRING \
        return w;              \
    }(); }

#define CONNECTOR_WIRING                                 \
    namespace hal { inline const ConnectorWiring connectorWiring = [] \
    {                                                      \
        ConnectorWiring w;

#define WIRE_CONNECTOR( rack, connector, pin, deviceKind, device, channel) \
        w.addWire( VpcLocation{ VpcRack::rack, connector, pin }, SwitchElementId{ SwitchDeviceKind::deviceKind, device, channel });

#define END_CONNECTOR_WIRING \
        return w;              \
    }(); }
