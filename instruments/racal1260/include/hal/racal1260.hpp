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
// followed. The satellite types had to invent long unique names to avoid each
// other -- Racal1260Config, Racal1260Builder -- and the ones that did not,
// because nothing had claimed the name yet, were sitting on words no single
// instrument has any business owning: Parity and StopBits below would collide
// with the very next serial instrument anybody wrote.
//
// The namespace carries the manufacturer as well as the model, because a model
// number is not unique either -- two vendors may both ship a "1260". This one
// already reads as Racal's, which is why it is racal1260 and not
// racal_1260 the way the Keysight drivers are keysight_<model>.
//
// Nested inside hal rather than beside it, which is what keeps the driver
// bodies unchanged: unqualified lookup still reaches hal::describeSetting,
// hal::InstrumentId, hal::Simulated and the rest of the driver kit from in
// here. And the ADL customization points below -- setupDriver, readDriver,
// writeDriver, connectDriver, describeConfig -- are now found in *this*
// namespace on this driver's own config type, rather than as one more overload
// in a pile shared with every other instrument.
//

//
// -- Where everything is ----------------------------------------------------
//
// This header is the driver's one public name -- the rig's instrument table
// (through the generated include list), the tests and src/ all include it and
// nothing else -- and it holds no code of its own. The parts are in
// racal1260/, in the order the types need each other in:
//
//   racal1260/vocabulary.hpp             parity, stop bits
//   racal1260/builder.hpp                Rs232Config + Rs232Builder
//   racal1260/racal1260.hpp              the instrument class
//   racal1260/customization.hpp          the ADL hooks the verbs and journal find
//

#include "hal/racal1260/vocabulary.hpp"
#include "hal/racal1260/builder.hpp"
#include "hal/racal1260/racal1260.hpp"
#include "hal/racal1260/customization.hpp"
