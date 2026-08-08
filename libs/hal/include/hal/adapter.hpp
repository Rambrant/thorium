#pragma once

#include "core/adapter.hpp"
#include "core/quantity_kind.hpp"
#include "hal/vpc_location.hpp"

//
// ADAPTER / POINT / END_ADAPTER: declarative, Excel-readable adapter wiring
// tables, mirroring CRITERIA/CRIT/END_CRITERIA in core/criterion.hpp. Each
// POINT becomes a genuine static constexpr member of the group struct --
// exactly like CRIT -- so a misspelled point name at a Measure() call site
// is a real "no such member" compile error. That comes for free from ordinary
// C++ once the point's location is baked into its *type*
// (core::AdapterPointTag<Loc>) rather than stored as runtime data -- no
// reflection needed.
//
// A POINT names a pin and says where it is. It does NOT say what is measured
// there -- see core::AdapterPointTag's own comment for why binding a quantity
// to a pin turned out to forbid legitimate measurements (inrush current at a
// supply pin, ripple frequency at a rail) without protecting against anything.
//
// Point identifiers can't reuse a spec label verbatim when it starts with a
// digit (e.g. "5VOutput"): C++ identifiers can't start with a digit. Use a
// legal rearrangement instead (Output5V) -- see dut/adapter.inc.
//
//   ADAPTER( DeviceX, "Device X on standard adapter")
//       POINT( Output5V, A, 1, 3, "5Vdc supply port")
//   END_ADAPTER
//
//   Measure( Dmm1.voltage(), at( DeviceX::Output5V));
//   Measure( Dmm1.current(), at( DeviceX::Output5V));   // same pin, fine
//
#define ADAPTER( groupName, desc)                                      \
    struct groupName                                                   \
    {                                                                  \
        static constexpr std::string_view Description = desc;

#define POINT( id, rack, connector, pin, desc)                                                                                \
        static constexpr ::core::AdapterPointTag<::hal::VpcLocation{ ::hal::VpcRack::rack, connector, pin }> id{ #id, desc };

#define END_ADAPTER };
