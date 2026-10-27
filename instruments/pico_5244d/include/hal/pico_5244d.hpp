#pragma once

#include "hal/driver/api_version.hpp"

//
// Which hal API version this driver was written against: a literal, never
// THORIUM_HAL_API_VERSION itself (see hal/driver/api_version.hpp and
// instruments/README.md). Two -- it uses hal::ConfigBuilder, the address kinds
// and the InstrumentTag/preflight contract, and nothing from hal/io/: this is
// the one driver here that opens no SCPI session (the unit has no SCPI) -- but
// it does ask for 7, the native slot on hal::BoxConnection, because that is
// how its two faces share the one open unit (see unit.hpp).
//
THORIUM_REQUIRE_HAL_API( 7);

//
// hal::pico_5244d: the Pico Technology PicoScope 5244D, driven through the
// vendor's ps5000a library. See instruments/pico_5244d/README.md.
//
// The name is the manufacturer and the model as the badge has it, per the rule
// in instruments/README.md. "PicoScope 5244D" is the product; the 5244D MSO is
// a different variant with sixteen digital inputs, and is refused at the
// identity check for the reason the 33522B refuses its siblings.
//
// The rig names the type in full, once:
//
//     INSTRUMENT( Pico1, pico_5244d::Oscilloscope,      Osc2, Usb( "JO123/0456"))
//     INSTRUMENT( Pico1, pico_5244d::WaveformGenerator, Wfg3, Usb( "JO123/0456"))
//
// -- The two faces of the box ------------------------------------------------
//
// One 5244D is two instruments behind one USB connector, and this header says
// so by including exactly two things, as hal/keysight_dsox1202g.hpp does:
//
//   pico_5244d_oscilloscope.hpp        Oscilloscope: the digitiser, a sensor
//   pico_5244d_waveform_generator.hpp  WaveformGenerator: the built-in function
//                                      generator, a source
//
// The two are siblings: neither is the "main" one. They share the open unit
// through hal::BoxConnection (unit.hpp), and the generator reaches into the
// scope for its back panel, never the other way round.
//
// Include this header, not the ones under pico_5244d/: they are its parts.
//
#include "hal/pico_5244d_oscilloscope.hpp"
#include "hal/pico_5244d_waveform_generator.hpp"
