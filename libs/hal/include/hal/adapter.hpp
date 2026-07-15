#pragma once

#include "core/adapter.hpp"
#include "core/quantity_kind.hpp"
#include "hal/vpc_location.hpp"

//
// ADAPTER / POINT / END_ADAPTER: declarative, Excel-readable adapter wiring
// tables, mirroring CRITERIA/CRIT/END_CRITERIA in core/criterion.hpp. Each
// POINT becomes a genuine static constexpr member of the group struct --
// exactly like CRIT -- so a misspelled point name at a Measure() call site
// is a real "no such member" compile error, and a quantity mismatch
// (Measure(dmm1.current(), Output5V) where Output5V is Voltage-tagged) is
// an overload-resolution failure -- see core/measure.hpp. Both come for
// free from ordinary C++ once the point's location and quantity are baked
// into its *type* (core::AdapterPointTag<Loc, Kind>) rather than stored as
// runtime data -- no reflection needed.
//
// Point identifiers can't reuse a spec label verbatim when it starts with a
// digit (e.g. "5VOutput"): C++ identifiers can't start with a digit. Use a
// legal rearrangement instead (Output5V) -- see dut/device_x_profile.inc.
//
//   ADAPTER( DeviceX_StdAdapter, "Device X on standard adapter")
//       POINT( Output5V, A, 1, 3, Voltage, "5Vdc supply port")
//   END_ADAPTER
//
//   Measure( Dmm1.voltage(), DeviceX_StdAdapter::Output5V);
//
#define ADAPTER( groupName, desc)                                      \
    struct groupName                                                   \
    {                                                                  \
        static constexpr std::string_view Description = desc;

#define POINT( id, rack, connector, pin, kind, desc)                                                                          \
        static constexpr ::core::AdapterPointTag<::hal::VpcLocation{ ::hal::VpcRack::rack, connector, pin }, ::core::QuantityKind::kind> id{ #id, desc };

#define END_ADAPTER };
