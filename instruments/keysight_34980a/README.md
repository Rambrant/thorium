# hal::keysight_34980a::Chassis

A Keysight (Agilent) 34980A Multifunction Switch/Measure Unit **mainframe**:
eight slots of plug-in switching modules, one address, one session, and the
commands every module in it is driven by. `namespace hal::keysight_34980a`,
included as `"hal/keysight_34980a.hpp"`.

Target: `hal_keysight_34980a` / `Thorium::hal_keysight_34980a`. Depends on
`Thorium::hal` only. `STATIC`.

**The first thing under `instruments/` that is not an instrument.** It measures
nothing and sources nothing — it is switching hardware, and it carries no
`hal::InstrumentId`. `hal/fabric/switch_device.hpp` named this directory as the
destination for switch-card drivers before there were any:

> The seam if it stops being deliberate is the one `instruments/` already
> draws: when these cards get drivers (a real matrix and a real RF selector do
> not speak the same commands), each model becomes its own directory with its
> own header.

## The mainframe is the driver; a module is not

This is the one thing to understand before reading anything else here.

Every switching command is a **mainframe** command. `ROUT:CLOS (@3010)` goes to
the chassis, not to the module in slot 3, and the chassis speaks the same three
commands whatever is plugged into it. What a module contributes is which `ccc`
are legal and what they *mean* — a 34932A's `2304` is row 3, column 4 of a
matrix; a 34937A's is a relay number.

Which lands exactly on the split this codebase already has:

| | is | lives in |
|---|---|---|
| the mainframe | the thing that talks: one address, one session, `ROUT:CLOS`/`ROUT:OPEN` | **this package** |
| a module | a channel space: a `hal::SwitchDeviceModel` + `hal::detail::SwitchCardSpec`, and a slot | generic `hal` — **not built yet** |

And it is the shape the Racal rack already has, which is worth noticing before
assuming this box is a new kind of thing: four 1260 cards sit behind one
Option 01T controller, the PC opens the *controller's* one GPIB address, and the
card number rides inside the command (`CLOSE 3.0115`). One talker, several
channel spaces. A 34980A is that with the card number renamed to a slot and the
protocol modernised.

## Channel addressing

`(@sccc)` — slot 1-8, then the channel, three digits.

```cpp
hal::keysight_34980a::Chassis chassis{ hal::Lan( "bench-switch") };

chassis.close( hal::keysight_34980a::channel<1, 3>());        // ROUT:CLOS (@1003)
chassis.close( { { 1, 5 }, { 1, 13 }, { 2, 15 } });           // ROUT:CLOS (@1005,1013,2015)
```

The channel is padded to three digits and the slot is not padded at all — that
padding is what makes the boundary unambiguous, since without it slot 1
channel 3 and slot 13 channel *nothing* are the same four characters.

`channel<Slot, Number>()` is the compile-time spelling, the same shape
`hal::hop<Device, Channel>()` has. What it checks is asymmetric, deliberately:

- the **slot** is checked against the chassis, which this driver does know.
- the **channel** is checked for *shape* only — three digits, so it composes
  into a well-formed `sccc`. Whether the module in slot 3 has a channel 010 is a
  module fact, and no module is modelled yet.

`isSlot()` and `isAddressableChannel()` are the predicates those assertions are
written in terms of, and are what the tests check in both directions — a
`static_assert` in a function body is a diagnostic, not a constraint, so the
rejection cannot be asserted at the template. Same division
`rig/tests/test_switch_device.cpp` uses for `hal::hasChannel` against
`hal::hop`.

### Lists, never ranges

The instrument accepts `(@1005:1010)`. This driver never writes one, and that is
not frugality: a range **silently skips every Analog Bus relay it spans**, and
errors outright if one is an endpoint. So a range is a spelling whose meaning
depends on what is plugged in, where an explicit comma-separated list means what
it says on any rack.

The list form also earns its place on merit: a multi-channel `ROUT:CLOS` is
executed break-before-make within each bank on the 34925A FET multiplexer,
switches measurably faster than the same channels one at a time on the 34934A
high-density matrix, and is one error-queue check instead of *N*. Order is
preserved, because on a break-before-make module the last channel closed in a
bank is the one left closed — a list is not a set.

### The Analog Buses

Four two-wire buses running behind all eight slots — the mainframe's own
backplane, and how a multiplexer or matrix channel reaches the internal DMM
without a cable.

`analogBus( slot, bank, bus)` builds the relay's channel: **`9<bank><bus>`**, so
bank 1's four are 911-914 and bank 2's are 921-924. A module has one relay per
bus **per bank**, and which bank matters:

| module | banks that reach the buses | relays |
|---|---|---|
| multiplexer | both | 911-914 and 921-924 |
| **matrix, 2-wire** (34931A/34932A) | **Matrix 2 only** | **921-924 only** |
| RF mux (34941A/34942A) | none | none at all |

> This driver shipped with that wrong — it hard-coded 911-914 and so could only
> name bank 1. On a 34932A, which is what this rig is migrating onto, 911 is not
> a channel of the module at all. `hal::SwitchDeviceModel::Keysight34932A`'s
> channel space encodes this, and
> `framework/hal/tests/fabric/test_switch_card_spec.cpp` asserts both
> directions.

On a 34932A the four buses land on rows 5 through 8 — ABus1 on row 5, ABus2 on
row 6, and so on — which is Matrix 2 again, seen from the row axis rather than
the bank one.

An RF path cannot reach the internal DMM however the buses are switched: the RF
modules make every signal connection through an external cable.

Two rules that are invisible in the channel number:

- a **range** ignores them, and rejects one as an endpoint (see above — this
  driver writes no ranges, which sidesteps it entirely).
- a **scan** commandeers ABus1 and ABus2. Initiating one opens every ABus1 relay
  in every slot, and every ABus2 relay too if any scanned channel is 4-wire, and
  refuses manual use of them until the scan ends. ABus3 and ABus4 stay live —
  which the reference warns is a way to join a stray signal onto a measurement
  in progress.

## Switching

| | sends | notes |
|---|---|---|
| `close()` | `ROUT:CLOS` | one channel or a list |
| `open()` | `ROUT:OPEN` | **not universal — see below** |
| `closeExclusively()` | `ROUT:CLOS:EXCL` | open everything on the module, then close these |
| `isClosed()` | `ROUT:CLOS?` | asks the instrument, not this driver's memory |
| `openAll()` | `ROUT:OPEN:ALL` | one slot, or all of them |
| `waitForSwitching()` | `ROUT:MOD:WAIT?` | switching only, not scans |

Everything goes through `hal::io::ScpiSession::checked()`, never `write()`. A
SCPI instrument does not answer a bad command — it queues an error and carries
on — and on a switching device that is worse than the reading case
`checked()`'s own comment argues: a refused `ROUT:CLOS` is a relay that did not
move, so the measurement afterwards is not merely mis-ranged, it is **of a
different node**. Plausible, in tolerance often enough, and attributed to the
pin the script asked for. There is nothing downstream that can notice.

### RF modules cannot be opened

The RF multiplexer modules (34941A, 34942A) and the microwave switch modules
(34946A, 34947A) **do not respond to `ROUT:OPEN` at all** — the command
generates an error. They are tree-switched 1-of-4s with no "all open" state; to
leave a channel you close a *different channel in the same bank*.

Which is the same fact `rig/wiring.inc` already records about this rig's current
RF card from the other end: an E1472A's bank powers up into a 50 Ω park on
channel 00, and channel 00 is what a route "opens" to. `hal::SwitchFabric` does
not model this — it will happily hold two channels of one bank closed, and the
card will not — so it is a rig's business to wire an RF bank so its idle channel
is a defined one.

This driver does not refuse the open. It cannot know what is in the slot without
asking, and querying `SYST:CTYP?` before every open would pay a round trip per
relay to re-learn something a rig's table will state. So it sends the command
and lets the instrument's own error reach the caller — and offers
`closeExclusively()`, which is what an RF bank actually wants.

### `closeExclusively()` is per module, not per bank

That is the sharp edge. On a two-bank module it opens the *other* bank's
channels too, so a rig using one module for two independent routes cannot use it
to change one of them.

What it buys is atomicity: it is how you select a channel on a 1-of-N without a
window in which two are closed or none is. Right for an RF bank, and right for
any mux where two closed channels would short two DUT pins together — which on a
shared-common multiplexer is all of them.

## What is in the rack

`moduleIn( slot)` sends `SYST:CTYP?` and returns a `ModuleIdentity`.

This is worth more here than an `*IDN?` is on an instrument: what a 34980A *is*
depends entirely on what is plugged into it, so a bring-up run that prints
`modules()` has learned the rack. It is also what will check a rig's future
module rows against the hardware — a `hal::SwitchDeviceModel` saying 34932A in
slot 1 against a slot 1 that answers 34921A is a table describing a different
rack, which is exactly the mistake an instrument driver's identity check exists
to catch.

An empty slot is not an error: the instrument answers `"Agilent
Technologies,0,0,0"`. `ModuleIdentity::Empty` recognises that, so a caller
comparing `Model` against a part number does not have to know that `"0"` is a
sentinel.

## The internal DMM, from the switch side

Three accessors, and they belong on this face regardless of whether the DMM ever
gets one of its own — they are mainframe commands.

| | sends | |
|---|---|---|
| `internalDmmInstalled()` | `INST:DMM:INST?` | is the optional assembly fitted? |
| `internalDmmEnabled()` | `INST:DMM?` | is it on the Analog Buses? |
| `setInternalDmm()` | `INST:DMM ON\|OFF` | **issues a Factory Reset — see below** |

Installed and enabled are different questions, and the difference is electrical
rather than administrative: disabling the internal DMM "has the same effect as
physically removing the DMM assembly, including removing the connections to the
Analog Buses". So a disabled DMM is not a meter that refuses to read — it is a
meter that is **no longer on the backplane the multiplexers reach it through**,
which is a fact about the switching topology and the reason these live here.

> ⚠ **`setInternalDmm()` makes the instrument issue a `*RST`.** A Factory Reset
> on this box aborts any measurement, clears the scan list, and returns every
> measurement parameter to its factory setting. It does *not* open relays
> (latching modules keep their state), which is the one mercy in that list and
> also why the damage is easy to miss: the routing survives and the
> configuration does not. This is a bring-up operation, not something a script
> calls mid-run.

## A Janus, and the seam for its other face

The 34980A is a switch **and** a measure unit, so one box belongs in both of the
tables this repo deliberately keeps apart — `rig/devices.inc` for its modules,
`rig/instrument.inc` for its internal DMM. When that second face arrives it will
be a type in *this* package, named by an `INSTRUMENT()` row: one directory
holding a switch-device driver and an instrument driver, because one box is
both.

The one thing that must not happen then: **a second session.** Two objects
opening two connections to one mainframe is two error queues, two identity
checks, and a `*RST` from one arriving in the middle of the other's switching.
So the seam is already drawn — this class owns the session and hands out a
reference (`session()`), and the DMM face will be constructed from a
`Chassis &` rather than from an `hal::Address`.

`session()` is public for that reason as well as the usual one (a bring-up run
wants to send a command the driver has no accessor for — a scan, a DAC output,
the 34945A's drive settings — without that becoming a reason to widen the
driver).

## Not an `InstrumentTag`, and one gap that follows

`hal::InstrumentTag` is what `hal::safeRig()` reflects over, and inheriting it is
how a driver opts into being safed. This class does not, for the reason
`hal/fabric/switch_device.hpp` gives at length: a switching device is plumbing,
and `hal::InstrumentId` is what a *reading* is identified by. Adding the tag to
get safing would make every script-facing `Measure` and `Apply` overload accept a
chassis. Both facts are asserted in the tests, because "add `InstrumentTag` so
`safeRig()` picks it up" is exactly the plausible-looking change that would
undo it.

Which leaves a real gap, and it is worth naming precisely because it is the
first thing a bench engineer would ask. `hal::safeRig()` ends by calling
`hal::fabric.openAll()`, and that is **bookkeeping** — so on a rig whose
switching is a 34980A, a script that died with a rail routed to a DUT pin leaves
that relay closed. `openAll()` is the command that fixes it, and the day the
fabric talks is the day safing reaches the switching.

`closeSession()` does not open anything either: a latching matrix keeps every
crosspoint exactly where it was when the connection drops. That is hardware, not
something to paper over.

## No id, and what that says

A `Chassis` carries no id at all — no `hal::InstrumentId` (it measures nothing)
and no `hal::SwitchDeviceId` (whose enumerators are one per *card*, and a
mainframe is not a card: it has no channels of its own). What identifies one is
its address, and the tests assert that no id is accepted.

That is not an omission in this driver, it is the next question for the fabric,
and this repo has already predicted the answer — in `rig/instrument.inc`, about
the equivalent case on the instrument side:

> a named constant above this table per chassis, not a slot field on the address

A rig with a 34980A writes one address constant, and every module row in that
chassis repeats it — precisely as `DcP1..DcP4` repeat their mainframe's address
today, and `DcP5..DcP7` repeat theirs. `rig/devices.inc` now records this rack's
migration to a 34980A as its destination, including the module mapping.

## What is deliberately not here

- **module channel spaces.** The point of the exercise, deferred on purpose:
  which modules this rack has is a bench fact nobody has yet, and each one is a
  `hal::SwitchDeviceModel` enumerator and a spec row in generic `hal`. Until
  then a channel is checked for shape and not for existence, and the
  instrument's error queue is what catches a wrong one — which is exactly the
  trade `hal::hasChannel` exists to remove.
- **the fabric seam.** `hal::SwitchFabric::close()` increments a use count; its
  own comment says "on real hardware close()/open() would be GPIB/VXI writes to
  the relevant card." This class is what those writes will go through. Nothing
  calls it from the fabric yet, and making that call is a change to generic
  `hal`.
- **scanning** (`ROUT:SCAN`, `INIT`, `FETCh?`). The box's headline feature, and
  it belongs to the DMM face: a scan is a series of readings and this class has
  nothing to record them as. Note also that a scan takes over whole *banks*, so
  it is not something to bolt on beside `close()` without deciding what happens
  to a `Connect` held across it.
- **paired channels** (`ROUT:CLOS:PAIR`), for modules wired in 4-wire pairs.
  Wanted the day a rig routes a sense path through this box (`hal::WireRole` and
  `core::SensePath::Required` are the framework half, already there); needs the
  module set decided first, because which channels pair with which is a module
  fact.
- **the 34945A microwave switch driver, digital I/O, DAC, totaliser.** All real
  modules, none of them switching in the sense `hal::SwitchFabric` means, and
  each one a subsystem of its own.

## On the wire

One close, in full:

```
SYST:ERR?                once per session -- drain whatever the last user left
*IDN?                    and refused if the model is not a 34980A
ROUT:CLOS (@1003)        the crosspoint
SYST:ERR?                did it accept that
```

Three commands for the first close and two for each one after it.

The model only is checked, and the vendor field deliberately is not: this
mainframe was an Agilent product and is a Keysight one, the same unit either
way, and its own programmer's reference says "Agilent Technologies" on every
example — so a rack may hold two badges of one box. Contrast
`instruments/keysight_edu36311a`, which refuses its sibling on purpose: there
the two boxes really are different instruments sharing a command set.

### Bringing one up

GPIB, LAN and USB are all standard on this mainframe, so the constructor
constraint rules out only `hal::Serial`:

```cpp
hal::keysight_34980a::Chassis chassis{ hal::Lan( "bench-switch") };   // raw SCPI socket, port 5025
hal::keysight_34980a::Chassis chassis{ hal::Usb( "MY44001234") };     // through VISA
hal::keysight_34980a::Chassis chassis{ hal::Gpib( 0, 9) };            // through VISA
```

| Failure | Cause |
|---|---|
| `hal::io::TransportError` on the first command | nothing answered at that address |
| `hal::io::ScpiFault` on `*IDN?` | something answered and it is not a 34980A |
| `hal::io::ScpiFault` naming a `ROUT:` command | the instrument refused it — `-222` is a channel the module does not have; `-221` on an `open` is very likely an RF module (see above) |
| `NoSuchSlot` | a slot outside 1-8. Nothing was sent |
| "no VISA library found" | a `Usb` or `Gpib` address on a machine with no VISA installed — a missing dependency, not a missing instrument |

## Sources

- Agilent 34980A Multifunction Switch/Measure Unit Programmer's Reference,
  version 2.1 — <https://documentation.help/34980A/documentation.pdf>
  (every command, the channel-list punctuation, the Analog Bus relay numbers,
  the RF-module `ROUT:OPEN` exception, the empty-slot reply)
