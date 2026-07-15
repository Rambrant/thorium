#include "hal/measure.hpp"

#include "hal/adapter.hpp"
#include "hal/instruments.hpp"

//
// DeviceX_StdAdapter is DUT-specific data (see libs/dut/README.md), pulled
// in by a plain #include -- ADAPTER/POINT (see hal/adapter.hpp) expand
// fully qualified, so nothing else needs to be in scope for this to work.
// This is the one place in hal/ that names anything living under dut/, and
// it is a file dependency only: nothing here names dut:: as a C++ symbol.
//
#include "libs/dut/device_x_profile.inc"

MeasureEngine Measure{ hal::fabric, hal::routes, DeviceX_StdAdapter };
