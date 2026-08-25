// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench has no DUT, and this is that stated in the one place the
// framework asks.
//
// An adapter with no points, for the reason dev/rig/devices.inc is an empty
// table rather than a missing one: ADAPTER opens the fixed `dut` struct that
// every script's at( ...) resolves through (see hal/adapter.hpp), and a
// deployment that omitted it would fail to compile the moment anything
// included the suite's prelude -- rather than compiling into a suite where
// at( ...) simply has nothing to offer, which is the truth here.
//
// There is nothing to declare because there is nothing wired: a POINT names a
// VPC pin, a pin is reached through the fabric, and this bench has no fabric
// (dev/rig/wiring.inc). Declaring one anyway would break the build, and
// correctly -- dut/tests/test_wiring_coverage.cpp reflects over every POINT and
// requires a matching connector-wiring row, so an unwired point is a compile
// error. That check is doing its job here by having nothing to check.
//
// What a dev script measures instead is the instrument itself:
//
//     const auto reading = Measure( Dmm1.voltage());
//
// -- no at( ...), no route, nothing closed. See core::MeasureEngine's
// point-free operator() for why that is a first-class reading rather than a
// shortcut, and dev/README.md for what it costs in coverage.
//
// The day this desk grows a probe fixture with named pins, it grows a card in
// dev/rig/devices.inc first, then a wiring row, then a POINT here -- in that
// order, because each of those three is what makes the next one legal.
//
ADAPTER( "Dev bench -- an instrument on a desk, no adapter and no DUT")
END_ADAPTER
