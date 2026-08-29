// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's wiring: none of it, four times over.
//
// The first three tables are empty because dev/rig/devices.inc declares no
// cards -- every row in any of those blocks names a hal::SwitchDeviceId, and
// there are none to name. See that file for what follows from it. The fourth,
// TAP_WIRING, is empty for a different reason and has its own comment below.
//
// Written out as four empty blocks rather than left out, and this is not a
// formality: hal::instrumentWiring, hal::connectorWiring, hal::sourceWiring and
// hal::tapWiring are defined by these expansions and declared elsewhere, so a
// missing block is a link error rather than an empty table. hal/topology/wiring.hpp's own comment on
// END_SOURCE_WIRING makes the same point for the rig that has no fixed-wired
// sources.
//
// Note what an empty INSTRUMENT_WIRING does *not* mean. It does not mean Dmm1
// is unreachable -- it means Dmm1 is reached over its own LAN address and not
// through a relay, which is the ordinary state of an instrument on a bench with
// no fabric. hal::InstrumentWiring::find() still throws for it, and that is
// correct: nothing here composes a route, so nothing here asks.
//
INSTRUMENT_WIRING
END_INSTRUMENT_WIRING

CONNECTOR_WIRING
END_CONNECTOR_WIRING

SOURCE_WIRING
END_SOURCE_WIRING

//
// The fourth table, and empty like the other three -- but for a different
// reason, which is the only interesting thing about it.
//
// The three blocks above are empty because they name cards and this bench has
// none. This one names no card: a WIRE_TAP row is (instrument, VPC pin) with
// no path at all (see hal::TapWiring), so nothing about a fabric-free desk
// stops one being written. What stops it is that this deployment has no DUT
// and no adapter -- dev/dut/adapter.inc declares no POINT, so there is no pin
// to name.
//
// That makes this the table to write in if this desk ever grows a fixture.
// Today the order dev/dut/adapter.inc gives is card, then wiring row, then
// POINT, because a point had to be routed to be covered. It no longer does:
// dut/tests/test_wiring_coverage.cpp accepts isWired() *or* isTapWired(), so
// a POINT here plus one WIRE_TAP row below is a complete, checked
// declaration with no switching hardware anywhere in it. What that buys is
// the at( ...) half of the framework in the dev loop -- the coverage check,
// point-keyed session and journal keys, and the electrical interlock if the
// meter and a supply ever share a pin. What it still does not buy is the
// fabric: path composition and hal::SwitchFabric need a real card, and one
// card is still what dev/README.md says to put on the desk for them.
//
TAP_WIRING
END_TAP_WIRING
