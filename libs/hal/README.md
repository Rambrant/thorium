# hal/ -- generic instrument drivers, switching fabric, Measure, and Apply/Remove

This directory holds the mechanism, not any one rig's facts: the VPC90
connector array's coordinate system, the matrix/mux switching fabric, the
`InstrumentWiring`/`ConnectorWiring` machinery, the API instrument drivers are
written against, and the `Measure` and `Apply`/`Remove` objects every test
script calls through. Meant to be
linked by many rigs testing many DUTs, not just this repo's -- so nothing
here knows what "Device X" is (DUT-specific data lives under `dut/`,
see its README) or what instruments a given rig actually has, how they're
wired, or what to call them (that's `rig/`, see its own README). A rig
supplies those as three file paths at configure time -- see
`THORIUM_ACTIVE_INSTRUMENTS`/`THORIUM_INSTRUMENT_TABLE`/
`THORIUM_WIRING_TABLE` in this directory's `CMakeLists.txt` -- the same
compile-definition-swap mechanism `core/active_criteria.hpp` already uses
for `THORIUM_CRITERIA_VARIANT_TABLES`. This repo's own top-level `CMakeLists.txt`
sets those three to point at `rig/`, since this repo is (for now) both the
library and its one rig; a separate rig repo pulling this library in later
would set the same three variables pointing at its own `rig/`-equivalent
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

## Layout

```
libs/hal/
    include/hal/
        address.hpp        # Gpib/Lan/Serial/Usb/Simulated -- how the PC reaches an instrument
        vpc_location.hpp   # VpcLocation/VpcRack -- the VPC90 coordinate system
        switch_fabric.hpp  # SwitchElementId, SwitchFabric (matrix/mux relay state)
        instrument.hpp     # InstrumentId -- enumerators generated from THORIUM_INSTRUMENT_TABLE
        wiring.hpp         # InstrumentWiring/ConnectorWiring + WIRE macros
        adapter.hpp        # ADAPTER/POINT/END_ADAPTER macros
        describe.hpp       # the describe customization point drivers hook into
        measure.hpp        # MeasureEngine alias + extern Measure
        apply.hpp           # ApplyEngine/RemoveEngine aliases + extern Apply/Remove
        safing.hpp         # safeRig()
```

No driver headers: all four now live under `instruments/`, one directory each,
and are still spelled `#include "hal/<model>.hpp"`, in `namespace hal`, at every
call site -- moving a driver out changed its build location and nothing else.

| Driver | Directory |
|---|---|
| `hal::L4411A` | `instruments/l4411a/` |
| `hal::DSO8064` | `instruments/dso8064/` |
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
`core::MeasureEngine` and `core::ApplyEngine`/`core::RemoveEngine` (see
`libs/core/include/core/measure.hpp`, `libs/core/include/core/apply.hpp`)
compose the two into one crosspoint command at the moment a measurement or
a sourcing call is actually made.

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
`hal/measure.cpp`/`hal/apply.cpp` via `THORIUM_WIRING_TABLE` rather than a
hardcoded path -- see this directory's own `CMakeLists.txt`) holds the
actual data, built via
`INSTRUMENT_WIRING`/`WIRE_INSTRUMENT`/`END_INSTRUMENT_WIRING` and
`CONNECTOR_WIRING`/`WIRE_CONNECTOR`/`END_CONNECTOR_WIRING` (see
`wiring.hpp`'s own comment). Each rig has exactly one instance of each
table, unlike `CRITERIA` (several groups per file), so these macros build
one fixed, namespaced global (`hal::instrumentWiring`/`hal::connectorWiring`)
rather than taking a name. `ADAPTER` is the same one-per-build case and takes
no name either.
Both `hal/measure.cpp` and `hal/apply.cpp` `#include` it, since each is its
own translation unit needing its own declaration of the (inline) tables --
see `wiring.inc`'s own comment.

## Adapter points are compile-time-typed, and `at()` marks a point argument

`ADAPTER`/`POINT`/`END_ADAPTER` (see `adapter.hpp`) mirror
`CRITERIA`/`CRIT`/`END_CRITERIA`: each `POINT` becomes a genuine
`static constexpr` member of the group struct, carrying its location as a
template parameter on `core::AdapterPointTag<Loc>`
(see `libs/core/include/core/adapter.hpp`) rather than as runtime data.
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
(see `libs/core/include/core/at.hpp`), not an `AdapterPointTag` directly --
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

`apply.hpp`/`apply.cpp` assemble the `Apply`/`Remove` objects every script
sources through, the same way `measure.hpp`/`measure.cpp` do for `Measure`:
`core::ApplyEngine`/`core::RemoveEngine` (see
`libs/core/include/core/apply.hpp`) instantiated with the same three rig
types `MeasureEngine` uses. Where `Measure` takes a `core::Port`, `Apply`
and `Remove` each take a *builder* -- `N6701ABuilder<Loc>` or
`Ac6834BBuilder` -- built up fluently from an instrument's `.dc(at(...))`
or `.ac()` method:

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
contacts never move under load -- see `core/apply.hpp`.

Dispatch to the actual instrument (`applyDriver`/`removeDriver`, defined
alongside each builder in `n6701a.hpp`/`ac6834b.hpp`) happens via ADL on
the builder's `.config()` type, the same trick `core::MeasureEngine` uses
for `to_string(instrumentId)` -- `core/apply.hpp` itself has no dependency
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
  concrete type) and `hal::DSO8064` (Osc1's) after their real models, once
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
| `hal::DSO8064` | `Gpib`, `Lan`, `Usb` |
| `hal::N6701A` | `Gpib`, `Lan`, `Usb` |
| `hal::Ac6834B` | `Gpib`, `Serial` |
| `hal::Racal1260` | `Gpib` |

Note `hal::Racal1260`: it is an RS232 port and it is *not* reached over one.
`hal::Serial` is a cable from the PC; the framing that driver configures is
what it speaks at the DUT through the matrix. Its own constraint is what stops
a rig row from confusing the two directions.

### What still has no address

Nothing reads these yet -- the drivers carry them the way `hal::N6701A`
carried its mainframe slot before any driver needed it, so that the rig table
can state the fact at all. Two gaps are worth knowing about before real-driver
work starts:

- **The switching fabric.** `SwitchFabric`'s cards are named by bare string
  (`"Matrix2"`, `"Mux1"`), and closing a relay on real hardware is a GPIB/VXI
  write to a card that needs addressing exactly as an instrument does. It is
  declared in `rig/active_instruments.hpp` outside the `INSTRUMENT()` table, so
  it did not get a column. `hal::Address` lives in generic `hal` rather than in
  a driver package partly so the fabric can use it when that is done.
- **The run journal.** An address is per-run inventory rather than per-`Apply`,
  so `describeConfig` was deliberately left alone; `hal::to_string(Address)`
  exists for whoever writes the inventory line.

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
