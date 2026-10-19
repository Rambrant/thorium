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
// same number the meter, the scope and the supply ask for.
//
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it.
//
// -- Why the directory says 33522b and not 33500b -------------------------
//
// "33500B" is a *series*, not a model, and the eight members of it differ in
// exactly the three things this driver has to get right:
//
//     33509B   20 MHz   1 channel   no arb
//     33510B   20 MHz   2 channels  no arb
//     33511B   20 MHz   1 channel   arb
//     33512B   20 MHz   2 channels  arb
//     33519B   30 MHz   1 channel   no arb
//     33520B   30 MHz   2 channels  no arb
//     33521B   30 MHz   1 channel   arb
//     33522B   30 MHz   2 channels  arb
//
// (Keysight Trueform Series Operating and Service Guide, "Models and
// Options".) They share one command set and one guide, which is exactly what
// makes a series-wide name tempting and wrong: a driver called keysight_33500b
// would have to either believe 30 MHz on a 20 MHz box or believe 20 MHz on a
// 30 MHz one, and would let a rig write down channel<2>() on a single-channel
// model. So the package names the model on the bench -- the same call
// keysight_edu36311a makes about its E36311A sibling, from the same
// programming guide, for the same reason.
//
// If a second member of the series turns up, the move is a second package
// (they are three constants apart, and a package here is meant to be cheap),
// not a template parameter added to this one.
//
// -- Why the class carries a Wfg prefix -----------------------------------
//
// "33522B" is not a C++ identifier. hal::keysight_ac6834b::AcSource has the
// same problem and solved it the same way: a short prefix saying what kind of
// box it is, and then the model number exactly as the badge spells it.
//
// The manufacturer token is keysight even though this unit very likely says
// Agilent on the front -- the series launched as an Agilent product and the
// company is Keysight now. That is instruments/README.md's rule (the 34980A
// and the Ac6834B are the same case), and it is also why verifyIdentity()
// below checks the *model* field of *IDN? and not the manufacturer one: both
// badges answer with the same model number, and either is this instrument.
//

//
// -- Where everything is ----------------------------------------------------
//
// This header is the driver's one public name -- the rig's instrument table
// (through the generated include list), the tests and src/ all include it and
// nothing else -- and it holds no code of its own. The instrument itself is
// keysight_33522b_waveform_generator.hpp and its parts are in
// keysight_33522b/, in the order the types need each other in:
//
//   keysight_33522b/vocabulary.hpp          shapes, termination, range errors
//   keysight_33522b/builder.hpp             WaveformConfig + WaveformBuilder
//   keysight_33522b/wire.hpp                what goes on the wire (declared)
//   keysight_33522b_waveform_generator.hpp  the instrument class
//   keysight_33522b/channel_view.hpp        Channel<N> and its entry points
//   keysight_33522b/customization.hpp       the ADL hooks the verbs and journal find
//

#include "hal/keysight_33522b/vocabulary.hpp"
#include "hal/keysight_33522b/builder.hpp"
#include "hal/keysight_33522b/wire.hpp"
#include "hal/keysight_33522b_waveform_generator.hpp"
#include "hal/keysight_33522b/channel_view.hpp"
#include "hal/keysight_33522b/customization.hpp"
