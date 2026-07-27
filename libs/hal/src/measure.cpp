#include "hal/measure.hpp"

#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/wiring.hpp"

//
// The actual wiring data (which channel each instrument/connector pin is
// on) -- see the linking rig's wiring.inc (rig/wiring.inc in this repo),
// reached via THORIUM_WIRING_TABLE rather than a hardcoded path since this
// file is generic hal code, shared by every rig -- see hal/instrument.hpp's
// own comment on THORIUM_INSTRUMENT_IDS for the same reasoning. This is the
// only place instrumentWiring/connectorWiring need to be populated;
// MeasureEngine itself no longer needs any DUT-specific data at all (unlike
// before AdapterPointTag baked a point's location/quantity into its type --
// see core/adapter.hpp), so unlike the earlier design, this file has no
// dependency -- not even a textual one -- on anything under dut/.
//
#include THORIUM_WIRING_TABLE

MeasureEngine Measure{ hal::fabric, hal::instrumentWiring, hal::connectorWiring };
