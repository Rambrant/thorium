#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/api_version.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when
// it moves, and instruments/README.md for why a driver package has to say this
// at all (it travels separately from the hal it compiles against).
//
// Three, for hal/io/. Note what this driver does NOT include and therefore does
// not need: hal/driver/instrument.hpp. See this file's own comment on why.
//
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. The name carries the
// manufacturer as well as the model, and it follows the company as it is today
// rather than the badge on the front panel: this mainframe shipped as an
// Agilent product, its programmer's reference still says Agilent on every page,
// and a rack may well hold one of each badge. Same treatment the DSO8064A,
// L4411A and N6701A already get.
//

//
// -- The two faces of the box ------------------------------------------------
//
// One 34980A is two instruments behind one back panel, and this header says
// so by including exactly two things:
//
//   keysight_34980a_switch_unit.hpp  SwitchUnit: the relays, the slots, the safing
//   keysight_34980a_multimeter.hpp          Multimeter: the internal 6.5-digit meter
//
// This header is the driver's one public name and holds no code of its own.
// The files in keysight_34980a/ are what the faces share (vocabulary.hpp, the
// types a channel is spelled in; mainframe.hpp, the identity check both make).
// The Multimeter reaches into the SwitchUnit for its box and session type, never the
// other way round.
//
// -- The preamble ------------------------------------------------------------
//
//
// ===================================================================
// The first switch-device driver in this tree, and a Janus
// ===================================================================
//
// Everything under instruments/ so far has been an *instrument* -- a thing
// a script names, that measures or sources, carrying an hal::InstrumentId.
// This is not one, and the difference is the whole reason this file needs a
// preamble rather than just a class.
//
// A Keysight 34980A is a switch/measure mainframe: eight slots of plug-in
// switching modules, and an optional internal 6.5-digit DMM. So it is
// exactly the box hal/fabric/switch_device.hpp's own comment predicted
// would arrive one day, and it predicted this directory too:
//
//     "The seam if it stops being deliberate is the one instruments/
//      already draws: when these cards get drivers (a real matrix and a
//      real RF selector do not speak the same commands), each model
//      becomes its own directory with its own header, and this enum
//      becomes the tokens naming them, exactly as rig/instrument.inc names
//      driver types today."
//
// -- Why the *mainframe* is the driver, and a module is not --------------
//
// Because that is how the box divides the work, and the division is the
// single most useful thing to understand about it.
//
// Every switching command is a *mainframe* command. "ROUT:CLOS (@3010)" is
// sent to the chassis, not to the module in slot 3, and the chassis speaks
// the same three commands whatever is plugged into it. What a module
// contributes is which "ccc" are legal, and what they mean -- a 34932A's
// 2304 is row 3, column 4 of a matrix; a 34937A's is a relay number.
//
// Which maps onto the split this codebase already has, exactly:
//
//     the mainframe   is the thing that talks: one address, one session,
//                     ROUT:CLOS/ROUT:OPEN. That is this file.
//
//     a module        is a channel space: hal::SwitchDeviceModel plus a
//                     hal::detail::SwitchCardSpec, and a slot to sit in.
//                     That is generic hal, and it is deliberately NOT here
//                     yet -- see "What is deliberately not here" below.
//
// And it is the same shape the Racal rack already has, which is worth
// noticing before assuming this box is a new kind of thing: four 1260 cards
// sit behind one Option 01T controller, the PC opens the *controller's* one
// GPIB address, and the card number rides inside the command
// ("CLOSE 3.0115"). One talker, several channel spaces. A 34980A is that
// with the card number renamed to a slot and the protocol modernised.
//
// -- The structural gap this exposes, stated rather than papered over ----
//
// There is no *switch-device* id for a chassis. hal::SwitchDeviceId's
// enumerators come from a rig's devices.inc, one per *card*, and a
// mainframe is not a card: it has no channels of its own. What it has is
// an hal::InstrumentId -- a row in instrument.inc, see below -- which is
// what preflight, --address and safing are keyed on. The cards still have
// no rows, and that is the gap this heading is about.
//
// That is not an oversight in this driver, it is the next question for the
// fabric, and the tables already have the answer: a box. Every row --
// instrument or device -- names the unit it is a face of in its first
// column, so this chassis's module rows will say Swu1 like its own row
// does, and the build holds a box's rows to one address (see
// hal/topology/boxes.hpp). See rig/devices.inc, which records that as this
// rack's destination.
//
// And the fabric drives it: hal::SwitchFabric, handed the rig's driver,
// moves a relay on its first use and its last release through the
// instrument on the card's box that switches cards -- this class, by
// closeOnCard()/openOnCard() (see hal/fabric/rig_switching.hpp). So a
// routed Measure or a Connect over a card in one of these slots is real
// ROUT:CLOS/ROUT:OPEN down the box's one session.
//
// -- An instrument row, and why it became one ----------------------------
//
// This class inherits hal::InstrumentTag and takes an hal::InstrumentId,
// so a rig names it in instrument.inc like anything else it talks to:
//
//     INSTRUMENT( Swu1, keysight_34980a::SwitchUnit, Swu1, Usb( "MY12345678"))
//
// It did not start that way. The first version argued that a switching
// device is plumbing, that hal::InstrumentId is what a reading is
// identified by, and that the tag would make every Measure and Apply
// overload accept a chassis. The last of those was never true -- Measure
// takes a core::Port and Apply a builder, and a chassis has neither, so
// nothing script-facing can be handed one -- and the cost of staying out
// was real: no preflight identity check, no --address, and, worst, safing
// that never reached the relays. A failed run left them where it left them.
//
// What joining costs is one ordering rule, and it is the reason for
// safeRelays() rather than an openAll() inside safe(): relays have to move
// after every source is off, and hal::safeRig() calls safe() in
// declaration order. So safe() is empty and safeRelays() opens everything,
// in a second pass safeRig() makes once every output is down -- see
// hal::RelayHoldingInstrument.
//
// What it does not change: a chassis is still not an hal::SwitchDeviceId.
// Those are one per *card*, and when module rows arrive in devices.inc
// they will name this row's box.
//
// -- The Janus half that is not here yet ---------------------------------
//
// The internal DMM is an ordinary 6.5-digit meter sharing this chassis's
// address, and when it arrives it will be an hal::InstrumentId row in
// rig/instrument.inc pointing at a type in *this* package -- one directory
// holding a switch-device driver and an instrument driver, because one box
// is both.
//
// The one thing that must not happen when it does: a second session. Two
// objects opening two connections to one mainframe is two error queues,
// two identity checks, and a *RST from one arriving in the middle of the
// other's switching. So the seam is already drawn -- this class owns the
// session and hands out a reference (see session()), and the DMM face will
// be constructed from a SwitchUnit & rather than from an hal::Address. What is
// here now for it: internalDmmInstalled(), internalDmmEnabled() and
// setInternalDmm(), which are mainframe commands and belong on this side
// regardless.
//
// -- What is deliberately not here ---------------------------------------
//
// module channel spaces   The point of the exercise, deferred on purpose:
//                         which modules this rack has is a bench fact
//                         nobody has yet, and each one is a channel space
//                         in generic hal (a hal::SwitchDeviceModel
//                         enumerator and a spec row). Until then a channel
//                         is checked for shape and not for existence --
//                         see channelList() and the note on validate()
//                         about which half of the check is missing and
//                         what catches it instead.
//
// scanning                ROUT:SCAN, INIT, FETCh? -- the mainframe's own
//                         sequencer, which walks a list of multiplexer
//                         channels taking a DMM reading at each. It is the
//                         box's headline feature and it belongs to the DMM
//                         face, not this one: a scan is a series of
//                         readings, and this class has nothing to record
//                         them as. Note also that a scan takes over whole
//                         banks (see the programmer's reference on what
//                         may and may not be switched during one), so it
//                         is not something to bolt on beside close()
//                         without deciding what happens to a Connect held
//                         across it.
//
// the 34945A driver,      Microwave switch driver, digital I/O, DAC,
// digital and analog      totaliser. All real modules, none of them
// modules                 switching in the sense hal::SwitchFabric means,
//                         and each one a subsystem of its own.
//
// paired channels         ROUT:CLOS:PAIR, for modules wired in 4-wire
//                         pairs. Wanted the day a rig routes a sense path
//                         through this box (hal::WireRole and
//                         core::SensePath::Required are the framework half,
//                         already there); it needs the module set decided
//                         first, because which channels pair with which is
//                         a module fact.
//

#include "hal/keysight_34980a_switch_unit.hpp"
#include "hal/keysight_34980a_multimeter.hpp"
