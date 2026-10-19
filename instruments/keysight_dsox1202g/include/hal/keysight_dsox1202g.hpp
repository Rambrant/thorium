#pragma once

#include "hal/driver/api_version.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when
// it moves, and instruments/README.md for why a driver package has to say this
// at all.
//
// Three, not two, and it moved for exactly the reason the version log gives:
// this driver now opens a session (hal/io/), where it used to answer every
// reading from its own simulation hooks and ask only for hal::ConfigBuilder.
// The number is still the oldest hal that serves this driver -- see the log in
// api_version.hpp, and this driver's own README on what the transport step
// changed.
//
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. Everything here would
// otherwise collide with hal::keysight_dso8064a, which declares its own
// Bandwidth, TriggerSlope, TimebaseReference and six more under the same
// names: two scopes in one tree is precisely the case the nesting was
// introduced for, and this is the first tree to actually hold two.
//
// The name carries the manufacturer, as every driver package here does, and
// the model as Keysight spells it on the front panel: DSOX1202G, the
// 2-channel InfiniiVision 1000 X-Series scope with the built-in waveform
// generator.
//
// -- The two faces of the box ------------------------------------------------
//
// One DSOX1202G is two instruments behind one back panel, and this header says
// so by including exactly two things:
//
//   keysight_dsox1202g_oscilloscope.hpp        Oscilloscope: the scope, a sensor
//   keysight_dsox1202g_waveform_generator.hpp  WaveformGenerator: the built-in
//                                              generator, a source
//
// This header is the driver's one public name -- the rig's instrument table
// (through the generated include list, see cmake/InstrumentDrivers.cmake), the
// tests and src/ all include it and nothing else -- and it holds no code of
// its own. The two faces are siblings: neither is the "main" one. The
// generator reaches into the scope for its box and session type, never the
// other way round.
//
// The files in keysight_dsox1202g/ are the scope's parts, which the oscilloscope face
// pulls in (the generator has none; it is one file):
//
//   keysight_dsox1202g/vocabulary.hpp     the enums, kUnmeasurable, ValidChannel
//   keysight_dsox1202g/trigger.hpp        :TRIGger     config + builder
//   keysight_dsox1202g/timebase.hpp       :TIMebase    config + builder
//   keysight_dsox1202g/acquisition.hpp    :ACQuire     config + builder
//   keysight_dsox1202g/channel.hpp        :CHANnel<N>  config + builder
//   keysight_dsox1202g/single.hpp         :SINGle      config + builder (Arm, Await)
//   keysight_dsox1202g/fetch.hpp          :WAVeform    config + builder (Fetch the record)
//   keysight_dsox1202g/channel_view.hpp   Channel<N>, and the Ports it hands out
//   keysight_dsox1202g/customization.hpp  the ADL hooks the verbs and the journal find
//
// Config and builder share a file because they change together: a setting is
// a field on one and a setter on the other, and a reader following one wants
// the other in view. The order of the includes inside the oscilloscope face is the
// order the types need each other in.
//
// -- What a Setup call on this instrument boils down to --------------------
//
// Four config types, one per SCPI subsystem this scope has -- :TRIGger,
// :TIMebase, :ACQuire and :CHANnel<N> -- rather than one flattened "scope
// settings" bag, for the reasons hal::keysight_dso8064a's own configs give:
// each Setup call then says which part of the instrument it touches, and the
// split matches how the settings actually interact.
//
// Every field is std::optional and unset means "leave whatever is already
// configured" -- the same convention core::MeasureSetup uses on the sensing
// side. A script that sets only the trigger level must not silently reset the
// slope to whatever this driver would have defaulted it to.
//
// -- The fluent chains a script builds before handing them to a verb -------
//
// Six of them on the oscilloscope face, one per subsystem file above, and all six get their
// copy-modify-return shape from hal::ConfigBuilder (see
// hal/driver/builder.hpp) -- so "how do I set X" reads the same way whether X
// is sourced, sensed, framed or triggered, and a setter is one line naming the
// field it sets.
//
// The one exception is AcquisitionBuilder::averagedOver(), which sets two
// fields because on this instrument they are one decision -- see it for why
// the pairing belongs in the config rather than in the driver.
//

#include "hal/keysight_dsox1202g_oscilloscope.hpp"
#include "hal/keysight_dsox1202g_waveform_generator.hpp"
