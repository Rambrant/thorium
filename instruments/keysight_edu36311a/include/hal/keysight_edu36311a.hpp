#pragma once

#include "hal/driver/api_version.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when
// it moves, and instruments/README.md for why a driver package has to say this
// at all (it travels separately from the hal it compiles against).
//
// Three, because this driver opens a real session and so needs hal/io/ -- the
// same number hal::keysight_edu34450a::Multimeter and
// hal::keysight_dsox1202g::Oscilloscope ask for.
//
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. The name carries the
// manufacturer as well as the model, and here that matters more than usual:
// "36311A" alone does not even distinguish this supply from the E36311A whose
// programming guide the .cpp is written against, and the two differ in exactly
// the numbers a driver has to get right (see Output2 below).
//

//
// -- Where everything is ----------------------------------------------------
//
// This header is the driver's one public name -- the rig's instrument table
// (through the generated include list), the tests and src/ all include it and
// nothing else -- and it holds no code of its own. The instrument itself is
// keysight_edu36311a_dc_source.hpp and its parts are in
// keysight_edu36311a/, in the order the types need each other in:
//
//   keysight_edu36311a/vocabulary.hpp     wiring tags, outputs, rating errors
//   keysight_edu36311a/builder.hpp        DcConfig + DcBuilder
//   keysight_edu36311a/wire.hpp           what goes on the wire (declared)
//   keysight_edu36311a_dc_source.hpp      the instrument class and its row aliases
//   keysight_edu36311a/customization.hpp  the ADL hooks the verbs and journal find
//

#include "hal/keysight_edu36311a/vocabulary.hpp"
#include "hal/keysight_edu36311a/builder.hpp"
#include "hal/keysight_edu36311a/wire.hpp"
#include "hal/keysight_edu36311a_dc_source.hpp"
#include "hal/keysight_edu36311a/customization.hpp"
