#include "dut/measure.hpp"

#include "dut/device_x_profile.hpp"
#include "hal/instruments.hpp"

MeasureEngine Measure{ hal::fabric, hal::routes, DeviceX_StdAdapter };
