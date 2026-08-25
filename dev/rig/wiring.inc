// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's wiring: none of it, three times over.
//
// All three tables are empty because dev/rig/devices.inc declares no cards --
// every row in any of these blocks names a hal::SwitchDeviceId, and there are
// none to name. See that file for what follows from it.
//
// Written out as three empty blocks rather than left out, and this is not a
// formality: hal::instrumentWiring, hal::connectorWiring and hal::sourceWiring
// are defined by these expansions and declared elsewhere, so a missing block
// is a link error rather than an empty table. hal/wiring.hpp's own comment on
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
