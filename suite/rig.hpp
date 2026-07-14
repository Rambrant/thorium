#pragma once

#include "core/session.hpp"
#include "dut/bench.hpp"
#include "dut/measure.hpp"
#include "hal/instrument.hpp"

//
// The rig this suite's scripts measure through: the fixed, static list of
// instruments this bench actually has, plus the Bench (fabric + Route table
// + Adapter + session) scripts call dut::Measure through. Global by design:
// a catalog script's signature is fixed to (group, test) -> bool (see
// core/test_catalog.hpp), so there is no per-call parameter to pass a device
// handle through -- routing is resolved statically inside the script, the
// same way GROUP/CRIT/MATRIX already are.
//
// Script unit tests swap testSession() to a core::ScriptedSession before
// calling a script directly, then call useDefault() afterward to restore
// live routing -- see suite/tests/test_supply_rail_script.cpp for the
// pattern. That's the one seam that's swappable at runtime; the instruments,
// fabric, routes and adapter are the same fixed rig facts whether a script
// runs for real or under test.
//
namespace rig
{
    [[nodiscard]] auto dmm1() -> hal::Dmm &;
    [[nodiscard]] auto dmm2() -> hal::Dmm &;
    [[nodiscard]] auto osc1() -> hal::Oscilloscope &;
    [[nodiscard]] auto powerSupply1() -> hal::PowerSupply &;

    [[nodiscard]] auto testSession() -> core::SwitchableSession &;

    [[nodiscard]] auto bench() -> dut::Bench &;
} // namespace rig
