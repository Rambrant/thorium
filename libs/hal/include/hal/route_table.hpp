#pragma once

#include <vector>

#include "core/quantity_kind.hpp"
#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/vpc_location.hpp"

namespace hal
{
    //
    // One wiring fact: from this VpcLocation, this instrument's port (named by
    // InstrumentId + QuantityKind, since e.g. a Dmm has both a Voltage and a
    // Current port) is reached by closing exactly this ordered path of switch
    // elements. Several entries can share a VpcLocation -- that's the case
    // where more than one instrument can reach the same DUT pin, each through
    // its own path.
    //
    struct RouteEntry
    {
        VpcLocation                   location;
        InstrumentId                  instrument;
        core::QuantityKind            kind;
        std::vector<SwitchElementId>  path;
    };

    //
    // A flat, hand-authored table of every route this rig's matrix/mux
    // fabric actually wires up. Deliberately not a pathfinder -- routes are
    // physical facts about the bench, decided once by whoever wired it, not
    // something to compute -- so this is just storage plus a lookup.
    //
    // TODO(reflection): once the reflection-based get<"id">() work in
    // core/criterion.hpp is verified on GCC 16, this lookup is a candidate to
    // move from a runtime find() (throwing when a route is missing) to a
    // consteval search that hard-fails compilation instead -- the same
    // upgrade path as the dut::Adapter lookup in dut/measure.hpp. Until then, a
    // script that asks for an instrument/pin pairing that isn't wired into the
    // rig fails at the point Measure is called, not at compile time.
    //
    class RouteTable
    {
        public:
            auto addRoute( VpcLocation location, InstrumentId instrument, core::QuantityKind kind, std::vector<SwitchElementId> path) -> void;

            //
            // Throws std::runtime_error if no route connects this instrument's
            // port to this location -- i.e. the rig simply isn't wired that
            // way. See the TODO above for why this is a runtime check today.
            //
            [[nodiscard]]
            auto find( VpcLocation location, InstrumentId instrument, core::QuantityKind kind) const -> const std::vector<SwitchElementId> &;

        private:
            std::vector<RouteEntry> mEntries;
    };
} // namespace hal
