# rig/ -- this rig's data, and nothing else

This directory holds *only* facts specific to this one physical rig -- which
instruments it has, how they're wired to the VPC90 connector array and to
each other, and what to call them -- expressed the same way `dut/`
expresses DUT-specific data: flat, macro-driven `.inc` files (plus one
concrete header) with no library of its own. The mechanism that makes sense
of this data (`hal::InstrumentWiring`, `hal::SwitchFabric`, the driver
classes themselves, the `INSTRUMENTS`/`INSTRUMENT_WIRING`/etc macros) all
lives in `libs/hal/` -- see its README for why that split is possible, and
for the three `THORIUM_*` compile definitions (declared and validated in
`libs/hal/CMakeLists.txt`) this directory's files are handed to hal through.

## Layout

```
rig/
    instrument.inc         # THORIUM_INSTRUMENT_TABLE -- this rig's fixed instrument list, and hal::InstrumentId's enumerators
    wiring.inc              # THORIUM_WIRING_TABLE -- this rig's fixed instrument/connector wiring
    active_instruments.hpp # THORIUM_ACTIVE_INSTRUMENTS -- Dmm1/Dmm2/Osc1/DcP1..DcP4/AcP1/Ser1/fabric
```

## instrument.inc

This rig's fixed, concrete instrument list -- one `INSTRUMENT(type, id,
address, ...)` per instrument, naming its C++ driver type, the global it's
addressed by (id doubles as both the global's name and its
`hal::InstrumentId` -- there is no rig where those differ, so there's no
separate parameter for it), how the PC reaches it, and any constructor
arguments (e.g. `hal::N6701A`'s slot number). Included from two different
places, each with `INSTRUMENT` defined for its own purpose:

- `active_instruments.hpp` below declares each id as an actual global
- `hal/instrument.hpp` keeps only each id, to generate `hal::InstrumentId`'s
  enumerators (see that header's own comment)

Both reads see this exact list, so an instrument added here can never
desync from its own identity enumerator -- there's nothing left to desync
from.

The `address` column is the control side -- `Gpib(0, 14)`,
`Lan("bench-dmm1")`, `Serial("/dev/ttyUSB0")` -- as against the signal side
`wiring.inc` describes below. See `hal/address.hpp` for the types, and note
two things about the column: it is mandatory (a row that omits it fails to
preprocess, in *both* readers), and which bus kinds a given row may use is
fixed by its driver rather than by this file, so `Gpib(...)` on an L4411A --
an LXI box with no GPIB connector -- is a compile error rather than a run
that dies on its first reading.

`DcP1`..`DcP4` all carry the same address on purpose: four modules behind one
mainframe interface is one address and four slots, and the slot stays a
separate constructor argument rather than a field on the address (see
`active_instruments.hpp`'s own comment for that argument in full).

Only one of those two is a macro-redefinition-and-re-`#include` this
codebase couldn't replace with reflection -- see `hal/instrument.hpp`'s own
comment for why generating `hal::InstrumentId` has to work this way
(reflection needs the enum to already exist; generating it is the whole
point of that read, so it can't run after the fact). `hal/src/safing.cpp`
used to read this file a third time for the same *kind* of reason
(`INSTRUMENT` redefined again, to call `.safe()` on each global) but
without the same *necessity* -- `InstrumentId` already existed by the time
it ran, so nothing stopped it from reflecting over `hal::InstrumentTag`-
derived globals instead (see that struct's own comment in
`hal/instrument.hpp`), which is what it does now. Every concrete driver
type inherits `InstrumentTag`, so safing doesn't need this file, or a
hand-maintained second list, at all.

## wiring.inc

This rig's two static wiring facts (see `hal/wiring.hpp`'s own comment):
which matrix/mux channel each instrument is hard-wired to, and which
matrix/mux channel each VPC connector pin is hard-wired to. Read by
`hal/measure.cpp` and `hal/apply.cpp`, each needing its own declaration of
the resulting (inline) `hal::instrumentWiring`/`hal::connectorWiring`
tables since each is its own translation unit.

## active_instruments.hpp

Declares this rig's actual instrument globals (`Dmm1`, `Dmm2`, `Osc1`,
`DcP1`..`DcP4`, `AcP1`, `Ser1` in this repo) plus `hal::fabric`, the switching
fabric's own global state. A script writes `Dmm1`/`DcP1`/etc directly --
no factory function, no lookup -- so this file, together with
`instrument.inc` right alongside it, is this rig's entire contribution to
what would otherwise be a generic `hal::` library with no instruments
plugged into it at all.

## A new rig

A separate rig -- its own repo, its own bench, its own instruments -- reuses
`libs/core/` and `libs/hal/` as-is (via `find_package`/`FetchContent`/
`add_subdirectory`, whichever that repo's own build prefers) and supplies
its own three files in this same shape, pointed at by the same three
`THORIUM_*` variables `libs/hal/CMakeLists.txt` requires. Nothing under
`libs/hal/` needs to change for that to work -- that's the point of the
split this directory draws.
