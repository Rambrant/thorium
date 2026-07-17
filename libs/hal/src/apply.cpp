#include "hal/apply.hpp"

#include "hal/active_instruments.hpp"
#include "hal/wiring.hpp"

//
// Same wiring data hal/measure.cpp already includes -- both TUs including
// this inline-defined table is fine (and required, since each is a separate
// translation unit that needs its own declaration of it); see wiring.inc's
// own comment and hal/measure.cpp for why this isn't duplicated data.
//
#include "libs/hal/wiring.inc"

ApplyEngine      Apply{};
RemoveEngine     Remove{};
ConnectEngine    Connect{ hal::fabric, hal::instrumentWiring, hal::connectorWiring };
DisconnectEngine Disconnect{ hal::fabric, hal::instrumentWiring, hal::connectorWiring };
