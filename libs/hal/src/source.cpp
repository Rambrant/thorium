#include "hal/source.hpp"

//
// Four globals and nothing else. Unlike hal/route.cpp and hal/measure.cpp
// beside it, this translation unit expands neither THORIUM_ACTIVE_INSTRUMENTS
// nor THORIUM_WIRING_TABLE: the sourcing engines hold no state at all (see
// core/source.hpp -- routing, and with it every rig fact, moved out from under
// Apply), so there is nothing here for a rig to supply.
//
// It still belongs to hal_rig rather than to generic hal, and that is a
// deliberate call about the driver API boundary rather than a technical
// necessity -- see libs/hal/CMakeLists.txt, which explains what a driver is
// allowed to assume and enforces it. A driver reaching for an Apply is the
// mistake that target split exists to catch, and moving these four into hal
// just because they now compile there would hand it to every driver.
//
ApplyEngine  Apply{};
RemoveEngine Remove{};
SetupEngine  Setup{};
WriteEngine  Write{};
