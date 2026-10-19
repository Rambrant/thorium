#pragma once

#include "hal/driver/api_version.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when it
// moves, and instruments/README.md for why a driver package has to say this at
// all (it travels separately from the hal it compiles against).
//
// Three, not one: this is the first driver here that opens a real session, so
// it is the first that needs hal/io/ (see the version log in api_version.hpp).
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. The name carries the
// manufacturer as well as the model, and here that matters more than usual:
// "34450A" alone would not even distinguish this meter from its own sibling,
// the Keysight 34450A whose command set it shares.
//

//
// -- Where everything is ----------------------------------------------------
//
// This header is the driver's one public name -- the rig's instrument table
// (through the generated include list), the tests and src/ all include it and
// nothing else -- and it holds no code of its own. The instrument itself is
// keysight_edu34450a_multimeter.hpp and its parts are in
// keysight_edu34450a/, in the order the types need each other in:
//
//   keysight_edu34450a/vocabulary.hpp  the measurement functions
//   keysight_edu34450a_multimeter.hpp  the instrument class
//

#include "hal/keysight_edu34450a/vocabulary.hpp"
#include "hal/keysight_edu34450a_multimeter.hpp"
