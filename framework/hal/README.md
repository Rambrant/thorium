# hal/ -- generic instrument drivers, switching fabric, Measure, and Apply/Remove

This directory holds the mechanism, not any one rig's facts: the VPC90
connector array's coordinate system, the matrix/mux switching fabric, the
`InstrumentWiring`/`ConnectorWiring` machinery, the API instrument drivers are
written against, and the `Measure` and `Apply`/`Remove` objects every test
script calls through. Meant to be
linked by many rigs testing many DUTs, not just this repo's -- so nothing
here knows what "Device X" is (DUT-specific data lives under `dut/`,
see its README) or what instruments a given rig actually has, what switching
hardware sits between them, how they're wired, or what to call any of it
(that's `rig/`, see its own README). A rig supplies those as four file paths
at configure time -- see `THORIUM_ACTIVE_INSTRUMENTS`/
`THORIUM_INSTRUMENT_TABLE`/`THORIUM_DEVICE_TABLE`/`THORIUM_WIRING_TABLE` in
this directory's `CMakeLists.txt` -- the same
compile-definition-swap mechanism `core/criteria/active_criteria.hpp` already uses
for `THORIUM_CRITERIA_VARIANT_TABLES`. This repo's own top-level `CMakeLists.txt`
sets those four to point at `rig/`, since this repo is (for now) both the
library and its one rig; a separate rig repo pulling this library in later
would set the same four variables pointing at its own `rig/`-equivalent
instead.

The concrete driver classes no longer live here at all. Each of the four is its
own independently packageable directory under `instruments/` at the repo root,
carrying its own tests -- see `instruments/README.md`. What is left here is the
API they are written against.

## Two targets: `hal` and `hal_rig`

Not a packaging detail -- it is the boundary that makes an `instruments/` tree
possible, and it is worth understanding before changing anything here.

| | Sources | Compiles against |
|---|---|---|
| `hal` | `vpc_location` `switch_fabric` `instrument` `wiring` | `core`, and `rig/instrument.inc` for `InstrumentId`'s enumerators |
| `hal_rig` | `apply` `measure` `safing` | all of the above, plus `rig/active_instruments.hpp` and so every driver behind it |

The asymmetry was always there and one target hid it. A driver must compile
against `hal`, so `hal` has to be configurable before any driver is; but
`active_instruments.hpp` reaches the drivers, so anything expanding it must come
after them. The `instruments/` tree is configured between the two.

What this buys, beyond ordering: what a driver may assume is now exactly what
`hal` exports, checked by the build rather than by reviewers. Note that the
check in `CMakeLists.txt` is explicit and has to be -- the cycle a driver closes
by linking `hal_rig` is one CMake tolerates in silence.

A consumer picks by what it names. Scripts and rig-level code name instrument
globals and call `Measure`/`Apply`/`safeRig`, so they link `hal_rig` (`hal` comes
transitively). `dut/`'s tests use only `hal::Adapter` and the wiring machinery,
so they link `hal`.

This directory's own tests are now on the `hal` side of that line, which they
were not before: the five files that named concrete instruments moved to
`rig/tests/`, and `hal_tests` links plain `hal`. So the split is checked here
too — a test added to `tests/` that reaches an instrument global or an `Apply`
no longer compiles, and belongs in `rig/tests/` (see `rig/README.md`).

Three more followed them later, and the reason names the limit of that check:
`test_switch_device.cpp`, `test_switch_fabric.cpp` and `test_wiring.cpp` all
named this bench's cards — `SwitchDeviceId::Matrix1`, `Mux1`, `Spst1` — which is
rig content, and the link line could not see it. An enumerator is not a global,
so a test asserting the *contents* of `rig/devices.inc` linked plain `hal`
perfectly happily. What found them was a second deployment: `dev/` declares no
switching hardware, so there was no `Matrix1` to ask about and `hal_tests`
stopped compiling (see `dev/README.md`). The rule this leaves is worth stating
directly, since the build cannot enforce it: **a test in `framework/hal/tests/` may
name hal's own types, and no enumerator that comes from a rig's `.inc` file.**

## Layout

```
framework/hal/
    include/hal/
        driver/                # the kit an instrument driver is written against
            api_version.hpp    # THORIUM_HAL_API_VERSION + the gate each driver asserts through
            address.hpp        # Gpib/Lan/Serial/Usb/Simulated -- how the PC reaches an instrument
            instrument.hpp     # InstrumentId -- enumerators generated from THORIUM_INSTRUMENT_TABLE
            describe.hpp       # the describe customization point drivers hook into
        fabric/                # the switching hardware itself
            switch_device.hpp  # SwitchDeviceKind/Model + card specs, SwitchDeviceId (from THORIUM_DEVICE_TABLE), kindOf/addressOf/cardOf/hasChannel
            switch_fabric.hpp  # SwitchElementId, SwitchFabric (matrix/mux relay state)
        topology/              # how this bench is put together
            vpc_location.hpp   # VpcLocation/VpcRack -- the VPC90 coordinate system
            adapter.hpp        # ADAPTER/POINT/END_ADAPTER macros
            bundle.hpp         # BUNDLE/LINE -- the pins that are one physical interface
            wiring.hpp         # the four wiring tables + WIRE macros
        verbs/                 # this rig's instantiation of core's generic engines
            measure.hpp        # MeasureEngine alias + extern Measure/Read
            route.hpp          # Connect/Disconnect -- the fabric-only verbs
            source.hpp         # Apply/Remove/Setup/Write -- the instrument-I/O verbs
            acquire.hpp        # Arm/Await -- the triggered-capture pair
            trace.hpp          # Fetch -- the whole captured record off an instrument
            interlock.hpp      # energisedSourceAt -- the rig half of the electrical interlock
            safing.hpp         # safeRig()
```

`src/` and `tests/` mirror those four folders.

Three of the names are shared with `framework/core/include/core/`, and the
pairing is exact rather than thematic. `verbs/` holds one header per
`core/verbs/` header of the same name -- `hal/verbs/measure.hpp` is the
instantiation of `core/verbs/measure.hpp` for this rig, and so on for
route/source/acquire/trace/interlock. `topology/adapter.hpp` is the concrete
half of `core/topology/adapter.hpp`, which parameterises the location
generically (`template<auto Loc>`) precisely so that the generic half can stay
in core -- `core/verbs/route.hpp` and `core/verbs/measure.hpp` both include it,
so it could never move here. `driver/` names the same kit as `core/driver/`
from the other side.

`verbs/` is also exactly the `hal_rig` target, and the other three are exactly
`hal` -- so the folder split and the target split are the same line, drawn once
in the directory tree and once in `CMakeLists.txt`. That line is what the
`instruments/` tree depends on: a driver compiles against `hal`, and
`hal_rig` compiles against the drivers.

Note what `driver/` does *not* claim. It is the set of headers that are about
being a driver rather than about this bench, not the whole surface a driver may
use -- that is everything the `hal` target exports, and the drivers do reach
`fabric/` and `topology/` as well, because `connectDriver`/`disconnectDriver`
take a `SwitchFabric &` and both wiring tables. The contract is enforced by the
target graph, not by the folder name.

The `fabric/` and `topology/` line is hardware versus map: `fabric/` models the
switching hardware and knows nothing about VPC pins or DUTs, and `topology/`
maps this bench onto it. `wiring.hpp` sits in `topology/` for that reason --
it includes `fabric/switch_fabric.hpp` and not the reverse. It holds all three
of the rig's static wiring facts, including `SourceWiring`, which is the one
that is deliberately *not* a fabric path, because `rig/wiring.inc` declares all
three in one file.

No driver headers: all five now live under `instruments/`, one directory each,
and are still spelled `#include "hal/<model>.hpp"`, in `namespace hal`, at every
call site -- moving a driver out changed its build location and nothing else.

| Driver | Directory |
|---|---|
| `hal::L4411A` | `instruments/l4411a/` |
| `hal::DSO8064A` | `instruments/dso8064a/` |
| `hal::N6701A` | `instruments/n6701a/` |
| `hal::Ac6834B` | `instruments/ac6834b/` |
| `hal::Racal1260` | `instruments/racal1260/` |

A rig's own instrument list, wiring data, and concrete instrument
identities/globals (`Dmm1`/`Dmm2`/`Osc1`/`DcP1`..`DcP4`/`AcP1`/`fabric` in
this repo's case) live in `rig/` at the repo root, not here -- see
`rig/README.md`.

## Static wiring facts, composed at measurement (or sourcing) time

An instrument's matrix/mux channel is fixed regardless of which DUT pin is
being measured (or sourced); a VPC connector pin's channel is fixed
regardless of which instrument is on the other end. `hal::InstrumentWiring`
and `hal::ConnectorWiring` (see `wiring.hpp`) store exactly those two
independent facts -- not one combined table keyed by (instrument, pin),
which would need an entry per *combination* even though the underlying
physical facts are only per instrument and per pin. Both
`core::MeasureEngine` and `core::ConnectEngine`/`core::DisconnectEngine` (see
`framework/core/include/core/verbs/measure.hpp`, `framework/core/include/core/verbs/route.hpp`)
compose the two into one crosspoint command at the moment a measurement or
a routing call is actually made. `Apply`/`Remove` compose nothing: they are
instrument I/O and never touch the fabric at all.

A third table, `hal::SourceWiring`, records which VPC pin each fixed-wired
source instrument's output is *cabled* onto. It is deliberately not a path and
is composed with nothing: its entire content is that the fabric is not
involved. Without it there was no way to say "this pin is not routed", so mux
channels got invented for landing pins purely to satisfy the coverage check --
and a fabricated route to a rail is worse than a missing one. A landing pin may
still carry a mux tap as well (reading a rail where it arrives at the DUT
catches cable drop that the supply's own readback cannot), so the two tables are
not mutually exclusive; what `dut/tests/test_wiring_coverage.cpp` does enforce
is that a pin the rig knows a source is cabled to is declared `SOURCE_POINT`
rather than `POINT`, so the adapter never describes a driven rail as an
ordinary pin.

A rig's own `wiring.inc` (`rig/wiring.inc` in this repo, reached from
`hal/src/verbs/measure.cpp`/`hal/src/verbs/route.cpp` via
`THORIUM_WIRING_TABLE` rather than a hardcoded path -- see this directory's
own `CMakeLists.txt`) holds the actual data, built via
`INSTRUMENT_WIRING`/`WIRE_INSTRUMENT`/`END_INSTRUMENT_WIRING`,
`CONNECTOR_WIRING`/`WIRE_CONNECTOR`/`END_CONNECTOR_WIRING`,
`SOURCE_WIRING`/`WIRE_SOURCE`/`END_SOURCE_WIRING` and
`TAP_WIRING`/`WIRE_TAP`/`END_TAP_WIRING` (see `wiring.hpp`'s own comment).

The first two hold paths; the last two hold pins, and record connections the
fabric is not part of at all — where a supply's output is cabled, and where a
measuring instrument's leads are bolted. `TAP_WIRING` is what makes a rig with
no switching hardware describable rather than merely runnable: the coverage
check accepts a `POINT` covered by a tap row, so a bench of instruments cabled
straight to the DUT keeps the `at(...)` vocabulary instead of falling back to
point-free readings keyed by instrument. `core::MeasureEngine` asks that table
first on every reading and takes the direct branch for a tapped instrument —
which is also why an instrument may not appear in both it and
`INSTRUMENT_WIRING` (`rig/tests/test_wiring_uniqueness.cpp`).

Each rig has exactly one instance of each table, unlike `CRITERIA` (several
groups per file), so these macros build one fixed, namespaced global
(`hal::instrumentWiring`/`hal::connectorWiring`/`hal::sourceWiring`/
`hal::tapWiring`) rather than taking a name. `ADAPTER` is the same one-per-build case and takes
no name either.
Both `hal/src/verbs/measure.cpp` and `hal/src/verbs/route.cpp` `#include` it,
since each is its own translation unit needing its own declaration of the
(inline) tables -- see `wiring.inc`'s own comment.

## Adapter points are compile-time-typed, and `at()` marks a point argument

`ADAPTER`/`POINT`/`END_ADAPTER` (see `adapter.hpp`) mirror
`CRITERIA`/`CRIT`/`END_CRITERIA`: each `POINT` becomes a genuine
`static constexpr` member of the group struct, carrying its location as a
template parameter on `core::AdapterPointTag<Loc>`
(see `framework/core/include/core/topology/adapter.hpp`) rather than as runtime data.
That is what makes a misspelled point name a "no such member" compile
error -- see `dut/README.md` for the concrete examples, since the actual
DUT profile data lives there, not here.

`WireRole` (force/sense) is the wiring table's axis; the matching axis on the
measurement side is `core::SensePath`, a template parameter of `core::Port`. A
four-wire reading is therefore a *different type* from a two-wire one, not the
same type carrying a runtime flag -- `core::MeasureEngine` branches on it with
`if constexpr`, and `hal::isInstrumentWired()` lets a rig-side check confirm at
build time that a rig which wires sense also wires force.

A point deliberately carries no quantity: it names a pin, and what gets
measured there is decided by whichever `core::Port` is aimed at it, so the
same pin can be read for voltage, current or frequency without being
redeclared.

`Measure`/`Apply`/`Remove` all take a point wrapped in `core::at(...)`
(see `framework/core/include/core/verbs/at.hpp`), not an `AdapterPointTag` directly --
`at(...)` exists purely to make a call site read as "at this DUT point" the
same way `_V`/`_A` literals make a bare number read as a `Quantity`. A bare
point with no `at(...)` simply has no matching overload -- a third kind of
compile-time protection alongside the misspelling and quantity-mismatch
cases above.

## Measure

`measure.hpp`/`measure.cpp` assemble the one `Measure` object every test
script measures through (`Measure( Dmm1.voltage(), at( dut::Output5V))`):
`core::MeasureEngine` instantiated with `hal::SwitchFabric`,
`hal::InstrumentWiring`, and `hal::ConnectorWiring`. This has no dependency
on anything under `dut/` at all -- not even a textual `#include` -- since an
`AdapterPointTag` carries everything `Measure` needs to know about a point
in its own type; the DUT profile (`dut`) is only ever
named at each individual `Measure(...)` call site in a script, not baked
into this instantiation.

## Apply / Remove -- the sourcing counterpart to Measure

`source.hpp`/`source.cpp` assemble the `Apply`/`Remove` objects every script
sources through, the same way `measure.hpp`/`measure.cpp` do for `Measure`:
`core::ApplyEngine`/`core::RemoveEngine` (see
`framework/core/include/core/verbs/source.hpp`), which -- unlike `MeasureEngine` --
take no rig types at all, since routing moved out from under them into
`route.hpp`/`route.cpp` (`Connect`/`Disconnect`). Where `Measure` takes a
`core::Port`, `Apply` and `Remove` each take a *builder* --
`N6701ABuilder<Loc>` or `Ac6834BBuilder` -- built up fluently from an
instrument's `.dc(at(...))` or `.ac()` method:

```cpp
Apply(  DcP1.dc().voltage( 24_V).currentLimit( 7_A));
Remove( DcP1.dc());

Apply(  AcP1.ac().phaseVoltage( 115_V).frequency( 400_Hz).currentLimit( 3_A));
```

Neither takes an `at(...)`: a source instrument here is fixed-wired straight to
its VPC pin (or four, for `AcP1`), so there is no point left to choose -- see
`hal::N6701A`'s own comment on why a real power rail is cabled rather than
routed. Where a relay does exist in the path, `Connect` closes it before the
output comes up and `Disconnect` opens it after the output goes down, so the
contacts never move under load -- see `core/verbs/source.hpp`. A sequence that gets
that the wrong way round is not refused, but it is recorded: both verbs ask the
driver whether its output is live at the moment the contact moves, through the
`isEnergised` customization point, and a hot switch lands in the run journal.
See `core/verbs/interlock.hpp`.

Every one of those driver calls is conditional on a bench being attached (see
`core/session/bench.hpp`) -- the stimulus counterpart to what `core::ISession` does for
the reading verbs. A `--replay`, `--inject` or `--skeleton` run instructs
nothing at all, and the machine log says so per instruction rather than
omitting it or claiming otherwise.

Dispatch to the actual instrument (`applyDriver`/`removeDriver`, defined
alongside each builder in `n6701a.hpp`/`ac6834b.hpp`) happens via ADL on
the builder's `.config()` type, the same trick `core::MeasureEngine` uses
for `to_string(instrumentId)` -- `core/verbs/source.hpp` itself has no dependency
on `hal::` at all.

## Instrument identity (DcP1..DcP4/AcP1) vs. instrument class (N6701A/Ac6834B)

`InstrumentId`'s enumerators are rig data, not hal data -- generated from
`THORIUM_INSTRUMENT_TABLE` (`rig/instrument.inc` in this repo's case), not
listed in `instrument.hpp` itself; see this directory's `CMakeLists.txt`.
Two different naming axes, on purpose, for whatever names a rig actually
picks:

- **`InstrumentId`/the global names** (`DcP1`, `DcP2`, ..., `AcP1`) name the
  *role* this rig uses the instrument for ("DC power, channel N"), the same
  way `Dmm1`/`Dmm2` don't encode which literal DMM model is plugged in.
  A script never needs to know or care that `DcP1` happens to be an N6701A
  channel underneath.
- **The C++ class** (`hal::N6701A`, `hal::Ac6834B`) is named after the
  physical instrument model. A real power-supply driver's SCPI dialect and
  channel-addressing scheme is inherently tied to its exact model, so
  naming the class after the model documents that non-portability rather
  than hiding it -- the same reasoning that named `hal::L4411A` (Dmm1/Dmm2's
  concrete type) and `hal::DSO8064A` (Osc1's) after their real models, once
  each was known, retiring the old generic `hal::Dmm`/`hal::Oscilloscope`
  placeholders that stood in for "roughly any DMM/scope" before that.

`DcP1`..`DcP4` are four separate `hal::N6701A` instances, one per module
slot of a single physical N6701A mainframe (it takes up to 4 independent
DC power modules) -- not four different mainframes. Each instance's
constructor takes that module's slot number (`hal::N6701A`'s `mChannel`,
1-4): a fact a real driver will eventually need to build the right SCPI
channel list (e.g. `VOLT 24,(@2)`), kept on the class now even though
nothing reads it yet, so the "one shared box, several independently
addressed channels" pattern exists before the first real driver needs it.
This is a different axis from `InstrumentWiring`'s matrix channel -- that's
which crosspoint a module's output leads land on in the switching fabric;
`mChannel` is which slot the module occupies inside the mainframe. Neither
table knows about the other, and neither is the mainframe's own address on
the bus -- that is a third axis, and the section below is about it.

## How the PC reaches an instrument, and why that is a value

`address.hpp` holds one struct per bus kind -- `Gpib`, `Lan`, `Serial`, `Usb`,
`Simulated` -- and a rig gives every instrument one in the third column of its
`INSTRUMENT()` row:

```
INSTRUMENT( L4411A,       Dmm1, Lan( "bench-dmm1"))
INSTRUMENT( N6701ARelay,  DcP3, Gpib( 0, 14), 3)
```

This is the *control* side, and it is a different fact from everything in
`wiring.inc`, which is entirely about the *signal* side -- what leaves the
instrument's terminals and where the DUT sees it. Re-cabling a supply's output
to another VPC pin does not change its GPIB address; moving the rack to
another subnet does not move a relay.

Three decisions in it are worth stating, because each had a plausible
alternative:

**One struct per bus kind, not a VISA resource string.** `"GPIB0::14::INSTR"`
is a runtime typo the rig can only discover when the session refuses to open,
which on a bench is halfway into an unattended run. This is the same argument
`hal::Parity`/`hal::StopBits` already make against the legacy test language's
`"BaudRate=9600 StopBits=1 WordLength=8 Parity=NONE"`. A driver that wants the
VISA spelling can build one from these fields; it could not get these fields
back out of a string.

Each struct names the **protocol a driver speaks**, not the cabling it reaches
the instrument through -- worth stating because the two diverged years ago. A
bench's GPIB interface is usually a USB-attached controller these days rather
than a card in the PC, and an instrument behind one is still at a `Gpib`
address: it answers to 488 commands at a primary address, and the USB cable is
the controller's business. `Usb` means something narrower, USBTMC -- the
instrument itself is the USB device. The rule of thumb: if unplugging the box
removes one instrument, `Usb`; if it removes a whole bus worth of them, that
bus's kind.

**A constructor value, not a template parameter.** The rule this codebase
follows is: template parameter when it changes what compiles, constructor
argument when it does not. `hal::N6701A`'s `Isolation` is a template parameter
because `DirectWiring` genuinely removes `Connect`/`Disconnect` from the API.
An address removes nothing -- a driver's interface is identical whichever
number a technician set on the rear-panel switch -- and templating on it would
multiply `N6701ADirect`/`N6701ARelay` (aliases that exist purely to keep
`instrument.inc` readable) by one per bus kind.

**The bus *kind* is still checked at compile time.** Each driver's constructor
is constrained by `hal::ReachableOver` naming its own back panel, so
`INSTRUMENT( L4411A, Dmm1, Gpib( 0, 14))` fails with "no matching constructor"
-- an L4411A is an LXI box and has no GPIB connector, so that row is not a
misconfiguration to find on the bench, it is a sentence about hardware that
does not exist. `hal::Simulated` satisfies every driver's constraint
unconditionally, which is what driver tests construct with.

| Driver | Reachable over |
|---|---|
| `hal::L4411A` | `Lan`, `Usb` |
| `hal::DSO8064A` | `Gpib`, `Lan`, `Usb` |
| `hal::N6701A` | `Gpib`, `Lan`, `Usb` |
| `hal::Ac6834B` | `Gpib`, `Serial` |
| `hal::Racal1260` | `Serial`, `Gpib` |

`hal::Racal1260` is the only row with two, and the reason is worth knowing: a
matrix-routed RS232 port is either a port on the PC with its conductors cabled
into the matrix (`Serial`) or a serial module in the switching chassis
commanded over that chassis's bus (`Gpib`), and which one this bench has is not
known -- the model name is a placeholder too, see that driver's README. So the
set is wide because the knowledge is missing, not because the panel has two
connectors, and narrowing it is a one-line change once the bench is known. The
other four each name exactly one panel.

That driver is also where the two directions are easiest to confuse. The
framing it configures (baud rate, parity, stop bits) is what the port speaks
*out* at the DUT; the address is how the PC reaches the port. Under the
`Serial` arrangement those are the same physical port, which is precisely when
keeping them apart matters -- a baud rate is a DUT-facing decision, a device
path is not.

### What still has no address

Nothing reads these yet -- the drivers carry them the way `hal::N6701A`
carried its mainframe slot before any driver needed it, so that the rig table
can state the fact at all. Two gaps are worth knowing about before real-driver
work starts:

- **The run journal.** An address is per-run inventory rather than per-`Apply`,
  so `describeConfig` was deliberately left alone; `hal::to_string(Address)`
  exists for whoever writes the inventory line.

The switching fabric used to be on that list and no longer is: its cards are
declared in `rig/devices.inc` with an address each, reachable through
`hal::addressOf` -- see the section below.

## The switching devices are declared, not named in strings

`switch_device.hpp` generates `hal::SwitchDeviceId` from `THORIUM_DEVICE_TABLE`
the same way `instrument.hpp` generates `hal::InstrumentId` from the instrument
table, and carries three facts per device: which card it is, where the PC
commands it, and -- for a chassis that addresses its cards inside the protocol
rather than on the bus -- which card of that chassis it is.

```cpp
SWITCH_DEVICES
    SWITCH_DEVICE( Racal1260_45,  Matrix1, Gpib( 0, 7),    Card( 1))
    SWITCH_DEVICE( AgilentE1472A, RfMux1,  Gpib( 0, 9, 3), NoCard)
END_SWITCH_DEVICES
```

**The first column is the card, not the kind.** What kind of thing a 1260-45 is
(a matrix) and which channels exist on it (`0000`-`3315`, written
`<group><row><column>`) are datasheet facts, so they come from
`hal::SwitchDeviceModel` rather than from the rig -- which means a rig cannot
say an E1472A is a matrix, and `HOP( Spdt1, 300)` on an 80-channel relay card
is a compile error naming the card and the channels it does have. What the rig
states is what only a bench knows: which cards are in the rack, where they are,
and what to call them.

Three spellings build a hop, all producing the same `hal::SwitchElementId`:
`HOP( Mux1, 3)` for a card numbered flat, `CROSSPOINT( Matrix1, 0, 3, 0)` for a
matrix's `<group><row><column>`, `BANK( RfMux1, 0, 1)` for a banked RF mux's
`<bank><channel>`. Prefer the structured two wherever a card has one: the parts
say at the call site what the digits mean, and the packed form of group 0 row 3
column 00 is `0300`, which C++ reads as octal.

**`Card( n)` is not a GPIB secondary**, and the distinction is on the wire. The
four Racal cards in this rig sit behind one Option 01T smart controller: the PC
opens the controller's single GPIB address and writes `CLOSE 3.0115`, where the
leading `3` is the card's own SW1 address. So they share one `hal::Gpib` and
differ in `Card( n)` -- the shape `DcP1..DcP4` already have with their mainframe
slot. The E1472A is an ordinary VXI servant behind a command module, which does
map its logical address to a real secondary, so it carries one and `NoCard`.

**They are not instruments.** A shared `InstrumentId` was the obvious
alternative and it would cost the guarantee `SwitchElementId`'s own comment has
carried from the start: a switch element "has no quantity type and nothing to
read -- so the type system can never let a script try to `Measure` a mux
channel". `InstrumentId` is what a recorded sample and a wiring entry identify
a *reading* by, and a mux channel produces none. The same line is already drawn
in how the globals are declared -- `Dmm1` unqualified because a script writes
it, `hal::fabric` qualified because nothing but the measure/apply assembly ever
names it.

**Two holes it closed**, beyond giving the cards addresses. `SwitchElementId`
used to be `{ kind, device-name-as-string, channel }`:

- a mistyped `HOP( Matrix, "Matrix22", 14)` was accepted, and the fabric would
  create that element, close it, open it and route nothing -- it had no idea
  which cards existed. `HOP( Matrix22, 14)` is now a compile error.
- `kind` rode on every hop rather than on the card, so
  `HOP( Matrix, "Matrix2", 14)` and `HOP( Mux, "Matrix2", 14)` were **two
  distinct elements for one physical crosspoint**, each with its own use count.
  Connect through one and disconnect through the other and the relay never
  opened, with nothing complaining. `kind` is now stated once per card and
  cannot be contradicted.

One honest consequence: an element can only name a card the rig declares, so on
a rig with no RF selector there is no way to construct an `RfMux` element at
all. Two `switch_fabric` tests demonstrated kind-independence with a phantom
`"RfMux1"` until that stopped being constructible, and moved the kind-level
claim (`RfMux` renders as `"RfMux"`, not as `"Mux"`) onto the enum directly.
This rig now owns an E1472A, so the element-level spelling is back -- but the
enum-level assertion stays, because it is the one that covers a kind no rig
here happens to own.

**Whose relay is this.** Each wiring table also flattens itself into
`hal::instrumentWiringHops`/`hal::connectorWiringHops` — one `hal::WiringHop`
per hop per row, carrying the owner, the row index and whether it is hop zero.
That exists for one check, `rig/tests/test_wiring_uniqueness.cpp`: an element
that identifies an endpoint must not appear in another entry. Not "no element
twice" — sharing a trunk hop is normal and this repo's rig does it ten times
over — which is why hop zero has to be distinguishable, and therefore why a
`Path` being written **endpoint-first** is now a rule rather than a habit.

**Naming.** This codebase keeps three words doing three jobs, and the split is
load-bearing: **driver** is the code that speaks to hardware (`hal::L4411A`),
**instrument** is the hardware a script names (`Dmm1` and `Dmm2` are two
instruments sharing one driver), and **switch device** is the plumbing only
wiring names. Collapsing them all into "drivers" would make "two instruments,
one driver" unsayable. "Devices" as the umbrella is worse still here, since
`dut/` is the Device Under Test. There is no umbrella noun, deliberately -- what
instruments and switch devices actually share is not a category but a
mechanism, `hal::Address`.

One rename is coming, though, and it isn't this one. `SwitchFabric`'s uniform
`close(id)`/`open(id)` won't survive real hardware -- a Racal 1260 matrix card
answers `CLOSE 1.0300` through its chassis controller and an Agilent E1472A
takes SCPI on its own address -- so each card model eventually wants its own
driver package, built exactly the way `instruments/l4411a/` is.
`hal::SwitchDeviceModel` and its spec table are where that starts: the part
number, the kind and the channel space are already stated per card, in one
place, which is what a driver would be constructed from. At that point the
spec rows move out to those packages (and a new rig stops needing an enumerator
here at all), the tree holds drivers for things that aren't instruments, and
`instruments/` -> `drivers/` becomes the honest name. Worth doing when the
first card driver lands, not before.

## Why the AC source's neutral return is part of the model

`AcP1` is fixed-wired on **four** channels, not three: phases A, B and C plus
the neutral/ground return, all closed and opened together by
`Connect`/`Disconnect`. They are registered under one `InstrumentId`, so
`hal::InstrumentWiring::findAll()` hands the driver every entry at once (see
`wiring.hpp`) -- a three-phase source with one phase live and its return open
is not a state worth being able to reach.

This reverses an earlier decision, which is worth recording because the
reasoning behind it stopped applying without looking as though it had. The
neutral was originally left out of the model altogether: hard-wired to ground,
outside the switching fabric, never switched, so nothing to represent. That was
true on its own terms -- and it quietly stopped being the relevant question once
the three *phases* also became fixed, always-together relays rather than
mux-routed ones. If opening this instrument's path is meant to isolate it, which
is the whole safety motivation for fixed-wiring the phases, then the return
conductor is part of that isolation too, not a wire that gets to sit outside the
model.

On the DUT side the same four conductors are `dut::AcInput::A/B/C/N` -- a
`BUNDLE` of four `SOURCE_LINE`s (see `dut/adapter.inc` and `adapter.hpp`),
grouped because they are one interface rather than four unrelated pins, and
declared as source points because `AcP1` is cabled onto all four.

## The driver API is versioned, and a driver says which version it wants

The published driver API is exactly what `hal` exports (see the two-target split
above), and `api_version.hpp` puts a number on it. Each driver under
`instruments/` names the version it was written against:

```cpp
THORIUM_REQUIRE_HAL_API( 1);
```

This exists because a driver directory is meant to be zipped and dropped into
another rig's `instruments/` as it stands, so a driver and the `hal` it is
compiled against are distributed separately and can disagree. Ungated, that
disagreement surfaces from the middle of a template instantiation; gated, it is
one diagnostic naming the driver's own line. `instruments/README.md` has the
driver-side story and a sample of the message.

Two numbers, because one would make the gate a nuisance:
`THORIUM_HAL_API_VERSION` rises on every change to the driver-facing API,
additions included, while `THORIUM_HAL_API_OLDEST_SUPPORTED` rises only when a
change actually breaks drivers written against older versions. The pair spans
the range this `hal` claims to serve; a driver's number either falls in it or
does not. **Changing anything a driver can see is what obliges you to move
them** — the numbers are a promise this directory makes, and nothing derives or
verifies them.

Macros, not only the `hal::kApiVersion`/`hal::kOldestSupportedApiVersion`
constants beside them, because a driver spanning two `hal` versions writes
`#if THORIUM_HAL_API_VERSION >= 3` around the part that differs and an
`inline constexpr int` cannot be asked that. And defined in the header rather
than handed in as a compile definition, unlike the four rig file paths in
`CMakeLists.txt`: those are facts about the linking rig, which only the build
knows, where this is a fact about *these headers* and has to travel with them.

## Dmm's AC mode is stored on the instrument, not the port

`Dmm::acVoltage()`/`acCurrent()` switch a `Dmm` into AC mode;
`voltage()`/`current()` switch it back to DC -- mirroring a real bench DMM's
front-panel mode button. This has one known, accepted sharp edge: two port
handles obtained before/after a mode switch both read whichever mode is
*current* when `rawMeasure()` is eventually called, not the mode active
when each handle was created. This never matters for real usage, since
`Measure(port, at(...))` reads a port immediately and discards it. If it
ever bites in practice, the fix is a `DmmChannel<QuantityT, Mode>` per-mode
port type -- sketched and set aside as overengineering for now.

## What's still a runtime check

`InstrumentWiring::find()`/`ConnectorWiring::find()` are still ordinary
runtime searches, throwing if an instrument or pin isn't wired -- even
though a point's location is now known at compile time, the *wiring table
lookup* itself hasn't been upgraded to a compile-time search yet. See the
`TODO(reflection)` comment in `wiring.hpp` for what that would look like.
