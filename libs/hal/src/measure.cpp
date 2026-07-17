#include "hal/measure.hpp"

#include "hal/active_instruments.hpp"
#include "hal/wiring.hpp"

//
// The actual wiring data (which channel each instrument/connector pin is
// on) -- see hal/wiring.inc. This is the only place instrumentWiring/
// connectorWiring need to be populated; MeasureEngine itself no longer
// needs any DUT-specific data at all (unlike before AdapterPointTag baked
// a point's location/quantity into its type -- see core/adapter.hpp), so
// unlike the earlier design, this file has no dependency -- not even a
// textual one -- on anything under dut/.
//
#include "libs/hal/wiring.inc"

MeasureEngine Measure{ hal::fabric, hal::instrumentWiring, hal::connectorWiring };
