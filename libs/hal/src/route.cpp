#include "hal/route.hpp"

#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/wiring.hpp"

//
// Same wiring data hal/measure.cpp already includes -- both TUs including
// this inline-defined table is fine (and required, since each is a separate
// translation unit that needs its own declaration of it); see wiring.inc's
// own comment and hal/measure.cpp for why this isn't duplicated data.
// THORIUM_WIRING_TABLE is the linking rig's wiring.inc (rig/wiring.inc in
// this repo) -- see hal/instrument.hpp's own comment on
// THORIUM_INSTRUMENT_TABLE for why this indirection exists at all: this file
// is generic hal code, shared by every rig, so it cannot name any one
// rig's wiring.inc directly.
//
// The sourcing verbs need none of this and are defined next door in
// hal/source.cpp -- see core/source.hpp on why the fabric half is the only
// half with rig facts to be handed.
//
#include THORIUM_WIRING_TABLE

ConnectEngine    Connect{ hal::fabric, hal::instrumentWiring, hal::connectorWiring };
DisconnectEngine Disconnect{ hal::fabric, hal::instrumentWiring, hal::connectorWiring };
