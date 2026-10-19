#pragma once

#include "hal/driver/api_version.hpp"

//
// Which hal API version this driver was written against. A driver and the hal
// it is compiled against are distributed separately -- this directory is meant
// to be zipped and dropped into another rig's instruments/ as it stands (see
// instruments/README.md) -- so the two can disagree, and this line is what
// makes that disagreement one readable diagnostic instead of a failure deep
// inside a template instantiation. A literal, never THORIUM_HAL_API_VERSION
// itself, which would only assert that this hal matches this hal. See
// hal/driver/api_version.hpp for what the number means and when it moves.
//
THORIUM_REQUIRE_HAL_API( 2);

//
// This driver's own namespace, nested inside hal.
//
// Everything a driver declares used to land directly in hal, and two problems
// followed. Satellite types had to invent long unique names to stay out of each
// other's way -- AcConfig and its siblings below carried this
// model's own name purely for that -- and the ones that did not, because
// nothing had claimed the word yet, ended up owning names no single instrument
// has any business owning. A namespace of its own retires both: the prefixes
// are gone from the names below, and what is left cannot collide with another
// driver at all.
//
// The name carries the manufacturer as well as the model, because a model
// number is not unique either -- two vendors may both ship a "1260" or a
// "4411". Here the 6834B is a Keysight product; the
// token follows the company as it is today rather than the badge on the front
// panel, so that one vendor does not appear under two spellings.
//
// Nested inside hal rather than beside it, which is what keeps the bodies below
// unchanged: unqualified lookup still reaches hal::describeSetting,
// hal::InstrumentId, hal::Simulated and the rest of the driver kit from in
// here. The ADL customization points are now found in *this* namespace on this
// driver's own config types, rather than as one more overload in a pile shared
// with every other instrument.
//

//
// -- Where everything is ----------------------------------------------------
//
// This header is the driver's one public name -- the rig's instrument table
// (through the generated include list), the tests and src/ all include it and
// nothing else -- and it holds no code of its own. The instrument itself is
// keysight_ac6834b_ac_source.hpp and its parts are in
// keysight_ac6834b/, in the order the types need each other in:
//
//   keysight_ac6834b/vocabulary.hpp     phases, symmetry, voltage ranges
//   keysight_ac6834b/builder.hpp        AcConfig + AcBuilder
//   keysight_ac6834b_ac_source.hpp      the instrument class
//   keysight_ac6834b/customization.hpp  the ADL hooks the verbs and journal find
//

#include "hal/keysight_ac6834b/vocabulary.hpp"
#include "hal/keysight_ac6834b/builder.hpp"
#include "hal/keysight_ac6834b_ac_source.hpp"
#include "hal/keysight_ac6834b/customization.hpp"
