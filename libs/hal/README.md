# hal/ -- generic instrument drivers, switching fabric, Measure, and Apply/Remove

This directory holds the mechanism, not any one rig's facts: the instrument
driver classes (`N6701A`, `Ac6677A`, `L4411A`, `DSO8064`), the VPC90
connector array's coordinate system, the matrix/mux switching fabric, the
`InstrumentWiring`/`ConnectorWiring` machinery, and the `Measure` and
`Apply`/`Remove` objects every test script calls through. Meant to be
linked by many rigs testing many DUTs, not just this repo's -- so nothing
here knows what "Device X" is (DUT-specific data lives under `dut/`,
see its README) or what instruments a given rig actually has, how they're
wired, or what to call them (that's `rig/`, see its own README). A rig
supplies those as three file paths at configure time -- see
`THORIUM_ACTIVE_INSTRUMENTS`/`THORIUM_INSTRUMENT_TABLE`/
`THORIUM_WIRING_TABLE` in this directory's `CMakeLists.txt` -- the same
compile-definition-swap mechanism `core/active_criteria.hpp` already uses
for `THORIUM_ACTIVE_CRITERIA`. This repo's own top-level `CMakeLists.txt`
sets those three to point at `rig/`, since this repo is (for now) both the
library and its one rig; a separate rig repo pulling this library in later
would set the same three variables pointing at its own `rig/`-equivalent
instead.

## Layout

```
libs/hal/
    include/hal/
        vpc_location.hpp   # VpcLocation/VpcRack -- the VPC90 coordinate system
        switch_fabric.hpp  # SwitchElementId, SwitchFabric (matrix/mux relay state)
        instrument.hpp     # InstrumentId -- enumerators generated from THORIUM_INSTRUMENT_TABLE
        l4411a.hpp          # hal::L4411A -- a generic DMM driver
        dso8064.hpp         # hal::DSO8064 -- a generic scope driver
        n6701a.hpp          # hal::N6701A/N6701ABuilder -- one N6701A channel
        ac6677a.hpp          # hal::Ac6677A/Ac6677ABuilder, phase()/ThreePhaseWyePoints
        wiring.hpp         # InstrumentWiring/ConnectorWiring + WIRE macros
        adapter.hpp        # ADAPTER/POINT/END_ADAPTER macros
        measure.hpp        # MeasureEngine alias + extern Measure
        apply.hpp           # ApplyEngine/RemoveEngine aliases + extern Apply/Remove
```

A rig's own instrument list, wiring data, and concrete instrument
identities/globals (`Dmm1`/`Dmm2`/`Osc1`/`DcP1`..`DcP4`/`AcP1`/`fabric` in
this repo's case) live in `rig/` at the repo root, not here -- see
`rig/README.md`.

## Two static wiring facts, composed at measurement (or sourcing) time

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

A rig's own `wiring.inc` (`rig/wiring.inc` in this repo, reached from
`hal/measure.cpp`/`hal/apply.cpp` via `THORIUM_WIRING_TABLE` rather than a
hardcoded path -- see this directory's own `CMakeLists.txt`) holds the
actual data, built via
`INSTRUMENT_WIRING`/`WIRE_INSTRUMENT`/`END_INSTRUMENT_WIRING` and
`CONNECTOR_WIRING`/`WIRE_CONNECTOR`/`END_CONNECTOR_WIRING` (see
`wiring.hpp`'s own comment). Each rig has exactly one instance of each
table, unlike `ADAPTER` (named per DUT profile) or `CRITERIA` (several
groups per file), so these macros build one fixed, namespaced global
(`hal::instrumentWiring`/`hal::connectorWiring`) rather than taking a name.
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
script measures through (`Measure( Dmm1.voltage(), at( DeviceX::Output5V))`):
`core::MeasureEngine` instantiated with `hal::SwitchFabric`,
`hal::InstrumentWiring`, and `hal::ConnectorWiring`. This has no dependency
on anything under `dut/` at all -- not even a textual `#include` -- since an
`AdapterPointTag` carries everything `Measure` needs to know about a point
in its own type; the DUT profile (`dut::DeviceX`) is only ever
named at each individual `Measure(...)` call site in a script, not baked
into this instantiation.

## Apply / Remove -- the sourcing counterpart to Measure

`apply.hpp`/`apply.cpp` assemble the `Apply`/`Remove` objects every script
sources through, the same way `measure.hpp`/`measure.cpp` do for `Measure`:
`core::ApplyEngine`/`core::RemoveEngine` (see
`libs/core/include/core/apply.hpp`) instantiated with the same three rig
types `MeasureEngine` uses. Where `Measure` takes a `core::Port`, `Apply`
and `Remove` each take a *builder* -- `N6701ABuilder<Loc>` or
`Ac6677ABuilder` -- built up fluently from an instrument's `.dc(at(...))`
or `.threePhaseWye(...)` method:

```cpp
Apply(  DcP1.dc( at( Input24V)).voltage( 24_V).currentLimit( 7_A));
Remove( DcP1.dc( at( Input24V)));

Apply(  AcP1.threePhaseWye( { .a=phase( at( AcInput_A)), .b=phase( at( AcInput_B)),
                              .c=phase( at( AcInput_C)) })
            .phaseVoltage( 115_V).frequency( 400_Hz).currentLimit( 3_A));
```

Dispatch to the actual instrument (`applyDriver`/`removeDriver`, defined
alongside each builder in `n6701a.hpp`/`ac6677a.hpp`) happens via ADL on
the builder's `.config()` type, the same trick `core::MeasureEngine` uses
for `to_string(instrumentId)` -- `core/apply.hpp` itself has no dependency
on `hal::` at all.

## Instrument identity (DcP1..DcP4/AcP1) vs. instrument class (N6701A/Ac6677A)

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
- **The C++ class** (`hal::N6701A`, `hal::Ac6677A`) is named after the
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
table knows about the other, and nothing here yet models how a script
would address the mainframe itself (GPIB address, VISA resource string,
etc.) -- that's deferred for every instrument, not specific to this one,
until real-driver work begins.

## Why `ThreePhaseWyePoints` has no neutral point

This rig's AC neutral is hard-wired to ground rather than routed through
the switching fabric, so `ThreePhaseWyePoints` is just `{ a, b, c }` --
`Apply`/`Remove` never route or source a neutral connection. `AcInput_N`
still exists as an ordinary DUT adapter point (see
`dut/adapter.inc`) for diagnostic `Measure(...)` calls (e.g.
verifying it actually reads as ground) -- it's simply never part of a
`ThreePhaseWyePoints`, and `hal::ac6677a.hpp` has no `.n` field to put it in.

## Why `ThreePhaseWyePoints` isn't templated on its three points

The natural first design -- `template<auto A, auto B, auto C>
struct ThreePhaseWyePoints` -- doesn't work: `AcP1.threePhaseWye({ .a=...,
.b=..., .c=... })` builds the argument via designated-initializer aggregate
initialization, which is not a deduction context for a class template's
non-type parameters, so none of the three points would ever be deduced.
`ac6677a.hpp` instead erases each point's `Loc` to a runtime `VpcLocation`
(`ErasedPhasePoint`) the moment it's wrapped by `phase(at(...))` -- the one
place the compile-time `Kind == Voltage` check happens, since `phase()`'s
parameter type only accepts a `Voltage`-tagged point. Losing `Loc` at that
point costs nothing: `applyDriver`/`removeDriver` only ever need it as a
runtime value for the connector-wiring lookup anyway, the same way
`InstrumentWiring::find(InstrumentId)` already works.

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
