#pragma once

#include "core/adapter.hpp"
#include "hal/vpc_location.hpp"

namespace hal
{
    //
    // The concrete instantiation of core::Adapter for this project: adapters
    // here are keyed by hal::VpcLocation, since that's the coordinate system
    // this rig's VPC90 connector array uses. core::Adapter itself has no
    // idea hal::VpcLocation exists -- see core/adapter.hpp -- this is the
    // "build stage" injection point, the same role hal/measure.hpp's
    // MeasureEngine alias plays for core::MeasureEngine.
    //
    using AdapterPoint = core::AdapterPoint<VpcLocation>;
    using Adapter      = core::Adapter<VpcLocation>;
} // namespace hal

//
// ADAPTER / POINT / END_ADAPTER: declarative, Excel-readable adapter wiring
// tables, mirroring CRITERIA/CRIT/END_CRITERIA in core/criterion.hpp -- see
// e.g. dut/device_x_profile.inc. Unlike CRITERIA (which expands into a
// struct of named static members, so a typo in a criterion id is a compile
// error), POINT expands into an ordinary runtime vector entry: hal::Adapter
// is a runtime-searched lookup (see core/adapter.hpp's own comment on why),
// so there is no compile-time member name yet for a macro to stamp out --
// misspelling a POINT name is still only caught when Measure() is called
// against it. See the matching TODO(reflection) in core/measure.hpp for
// what upgrades this later.
//
//   ADAPTER( DeviceX_StdAdapter, "Device X on standard adapter")
//       POINT( "5VOutput", A, 1, 3, Voltage, "5Vdc supply port")
//   END_ADAPTER
//
// Fully qualified (::hal::..., ::core::...) throughout, unlike CRITERIA/CRIT
// -- an adapter profile file needs no "using namespace" wrapper of its own,
// so POINT can be used directly from a plain #include with nothing else in
// scope; see dut/device_x_profile.inc, which has none.
//
#define ADAPTER( varName, desc)           \
    inline const ::hal::Adapter varName{ #varName, desc, {

#define POINT( name, rack, connector, pin, kind, desc)                                                                    \
        ::hal::AdapterPoint{ name, ::hal::VpcLocation{ ::hal::VpcRack::rack, connector, pin }, ::core::QuantityKind::kind, desc },

#define END_ADAPTER \
    }};
