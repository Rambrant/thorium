#pragma once

#include "core/adapter.hpp"
#include "hal/vpc_location.hpp"

namespace dut
{
    //
    // The concrete instantiation of core::Adapter for this project: adapters
    // here are keyed by hal::VpcLocation, since that's the coordinate system
    // this rig's VPC90 connector array uses. core::Adapter itself has no
    // idea hal::VpcLocation exists -- see core/adapter.hpp -- this is the
    // "build stage" injection point, the same role dut/measure.hpp's
    // MeasureEngine alias plays for core::MeasureEngine.
    //
    using AdapterPoint = core::AdapterPoint<hal::VpcLocation>;
    using Adapter      = core::Adapter<hal::VpcLocation>;
} // namespace dut
