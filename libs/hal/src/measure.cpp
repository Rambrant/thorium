#include "hal/measure.hpp"

#include "hal/acquire.hpp"

#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/wiring.hpp"

//
// The actual wiring data (which channel each instrument/connector pin is
// on) -- see the linking rig's wiring.inc (rig/wiring.inc in this repo),
// reached via THORIUM_WIRING_TABLE rather than a hardcoded path since this
// file is generic hal code, shared by every rig -- see hal/instrument.hpp's
// own comment on THORIUM_INSTRUMENT_TABLE for the same reasoning. This is the
// only place instrumentWiring/connectorWiring need to be populated;
// MeasureEngine itself no longer needs any DUT-specific data at all (unlike
// before AdapterPointTag baked a point's location/quantity into its type --
// see core/adapter.hpp), so unlike the earlier design, this file has no
// dependency -- not even a textual one -- on anything under dut/.
//
#include THORIUM_WIRING_TABLE

MeasureEngine Measure{ hal::fabric, hal::instrumentWiring, hal::connectorWiring };

//
// Constructed from Measure's own session bank, and in this same translation
// unit so that "Measure exists first" is guaranteed rather than hoped for --
// see hal/measure.hpp's own comment.
//
core::ReadEngine Read{ Measure.sessions() };

//
// The third reading verb, off the same bank and for the same reason -- see
// hal/acquire.hpp, and core::SessionBank on why one run has to mean one
// ordered recording across all three seams rather than three of them.
//
// Arm is here purely to keep the pair together; it holds no state at all and
// would have been just as correct beside Apply in hal/source.cpp.
//
ArmEngine   Arm{};
AwaitEngine Await{ Measure.sessions() };
