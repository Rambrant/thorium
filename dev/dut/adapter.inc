// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench has no DUT, and this is that stated in the one place the
// framework asks.
//
// An adapter with no points, for the reason dev/rig/devices.inc is an empty
// table rather than a missing one: ADAPTER opens the fixed `dut` struct that
// every script's at( ...) resolves through (see hal/topology/adapter.hpp), and a
// deployment that omitted it would fail to compile the moment anything
// included the suite's prelude -- rather than compiling into a suite where
// at( ...) simply has nothing to offer, which is the truth here.
//
// There is nothing to declare because there is nothing wired -- and note that
// "wired" is the word doing the work, not "routed". A POINT names a VPC pin,
// dut/tests/test_wiring_coverage.cpp reflects over every one and requires the
// rig to be able to reach it, and there are two ways a rig can: a
// connector-wiring row through the fabric, or a tap row saying an instrument's
// leads are cabled straight onto the pin (see hal::TapWiring). This bench has
// no fabric, so the first is out of reach -- but the second needs no card at
// all, and dev/rig/wiring.inc's TAP_WIRING block is empty only because there
// is no fixture on the desk to name.
//
// So a POINT here is not forbidden, it is unwritten. Declaring one without a
// row in either table would still break the build, and correctly. That check
// is doing its job here by having nothing to check.
//
// What a dev script measures instead is the instrument itself:
//
//     const auto reading = Measure( Dmm1.voltage());
//
// -- no at( ...), no route, nothing closed. See core::MeasureEngine's
// point-free operator() for why that is a first-class reading rather than a
// shortcut, and dev/README.md for what it costs in coverage.
//
// The day this desk grows a probe fixture with named pins, it needs two rows,
// not three: a POINT here and a WIRE_TAP row in dev/rig/wiring.inc naming the
// meter and that pin. No card, and no order to get right -- the two rows are
// checked against each other and against nothing else.
//
// A card is still what the *routed* half costs. One in dev/rig/devices.inc,
// plus a WIRE_INSTRUMENT and a WIRE_CONNECTOR row, buys path composition,
// hal::SwitchFabric and the interlock's fabric side; see dev/README.md. What
// the tap row buys without it is the at( ...) vocabulary -- point-keyed
// journal and session keys, and the coverage check -- which is the half a
// script's portability actually depends on.
//
ADAPTER( "Dev bench -- an instrument on a desk, no adapter and no DUT")
END_ADAPTER
