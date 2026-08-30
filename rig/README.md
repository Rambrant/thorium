# rig/ -- this rig's data, and nothing else

This directory holds *only* facts specific to this one physical rig -- which
instruments it has, what switching hardware sits between them, how they're
wired to the VPC90 connector array and to each other, and what to call all
of it -- expressed the same way `dut/`
expresses DUT-specific data: flat, macro-driven `.inc` files (plus one
concrete header) with no library of its own. The mechanism that makes sense
of this data (`hal::InstrumentWiring`, `hal::SwitchFabric`, the driver
classes themselves, the `INSTRUMENTS`/`INSTRUMENT_WIRING`/etc macros) all
lives in `framework/hal/` -- see its README for why that split is possible, and
for the four `THORIUM_*` compile definitions (declared and validated in
`framework/hal/CMakeLists.txt`) this directory's files are handed to hal through.

## Layout

```
rig/
    CMakeLists.txt         # no library -- just rig_tests, over the four files below
    instrument.inc         # THORIUM_INSTRUMENT_TABLE -- this rig's fixed instrument list, and hal::InstrumentId's enumerators
    devices.inc            # THORIUM_DEVICE_TABLE -- this rig's switching cards, and hal::SwitchDeviceId's enumerators
    wiring.inc             # THORIUM_WIRING_TABLE -- this rig's fixed instrument/connector wiring
    active_instruments.hpp # THORIUM_ACTIVE_INSTRUMENTS -- Dmm1/Dmm2/Osc1/DcP1..DcP4/AcP1/Ser1/fabric
    tests/                 # the integration tests that need this rig rather than a mechanism
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
- `hal/driver/instrument.hpp` keeps only each id, to generate `hal::InstrumentId`'s
  enumerators (see that header's own comment)

Both reads see this exact list, so an instrument added here can never
desync from its own identity enumerator -- there's nothing left to desync
from.

The `address` column is the control side -- `Gpib(0, 14)`,
`Lan("bench-dmm1")`, `Serial("/dev/ttyUSB0")` -- as against the signal side
`wiring.inc` describes below. See `hal/driver/address.hpp` for the types, and note
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
codebase couldn't replace with reflection -- see `hal/driver/instrument.hpp`'s own
comment for why generating `hal::InstrumentId` has to work this way
(reflection needs the enum to already exist; generating it is the whole
point of that read, so it can't run after the fact). `hal/src/verbs/safing.cpp`
used to read this file a third time for the same *kind* of reason
(`INSTRUMENT` redefined again, to call `.safe()` on each global) but
without the same *necessity* -- `InstrumentId` already existed by the time
it ran, so nothing stopped it from reflecting over `hal::InstrumentTag`-
derived globals instead (see that struct's own comment in
`hal/driver/instrument.hpp`), which is what it does now. Every concrete driver
type inherits `InstrumentTag`, so safing doesn't need this file, or a
hand-maintained second list, at all.

## devices.inc

The switching hardware between the instruments and the VPC array -- one
`SWITCH_DEVICE(model, id, address, card)` per card, naming which card it is,
where the PC commands it, and which card of its chassis it is:

```cpp
SWITCH_DEVICES
    SWITCH_DEVICE( Racal1260_45,  Matrix1, Gpib( 0, 7),    Card( 1))
    SWITCH_DEVICE( Racal1260_35,  Mux1,    Gpib( 0, 7),    Card( 2))
    SWITCH_DEVICE( AgilentE1472A, RfMux1,  Gpib( 0, 9, 3), NoCard)
END_SWITCH_DEVICES
```

This rig's five cards, and what each is for:

| Id | Card | Kind | Job |
|---|---|---|---|
| `Matrix1` | Racal Instruments 1260-45A | matrix | the instrument crossbar -- quad 4x16, group 0 column 00 is the LF measurement bus |
| `Mux1` | Racal Instruments 1260-35 | mux | the DUT fan-in -- one of 96 channels per routed pin, onto the one common |
| `Spst1` | Racal Instruments 1260-18 | SPST | the power path -- 152 Form A relays, one in each switched supply lead |
| `Spdt1` | Racal Instruments 1260-17 | SPDT | the console changeover -- 80 Form C relays, bench line or parked network |
| `RfMux1` | Agilent E1472A | RF mux | the HF path to the scope -- six 1x4 banks in 50 ohm |

Read twice, exactly as `instrument.inc` is: once by `hal/fabric/switch_device.hpp` to
generate `hal::SwitchDeviceId`'s enumerators, once to carry each device's
model, address and card number. It could not simply be another block inside
`wiring.inc` -- `THORIUM_WIRING_TABLE` is `PRIVATE` to `hal_rig`, and an enum
that `hal::SwitchElementId` is built from has to be visible in a public header.

These are **not** instruments, and that is deliberate rather than an oversight.
A switching device measures nothing and sources nothing; no test script ever
names one, only `wiring.inc` does, and `hal::InstrumentId` is what a *reading*
is identified by. See `hal/fabric/switch_device.hpp` for the argument in full.

Three things this file buys beyond the addresses, all of which were real holes:

- **A card name is checked.** `HOP` takes a `SwitchDeviceId`, so a mistyped
  `Matrix22` is a compile error. It used to be a bare string, and the fabric
  would happily create that element, close it, open it, and route nothing.
- **A card has one kind, and doesn't get it from here.** `kind` used to ride on
  every hop, so `HOP( Matrix, "Matrix2", 14)` and `HOP( Mux, "Matrix2", 14)`
  were two distinct elements for one physical crosspoint, each with its own use
  count in `hal::SwitchFabric`. It then moved to this file, one per card; it now
  comes from the card *model*, because what kind of thing a 1260-45 is was never
  a rig's to state.
- **A channel is one the card has.** The model carries each card's channel
  space, so `HOP( Spdt1, 300)` fails to build against an 80-channel relay card,
  naming the card and its channels. Every channel in `wiring.inc` used to be
  unchecked in exactly the way a card name was.

**`Card( n)` is not a GPIB secondary.** The four Racal cards sit behind one
Option 01T smart controller: the PC opens the controller's single GPIB address
and writes `CLOSE 3.0115`, where the leading `3` is the card's own SW1 address.
One address, four card numbers -- the shape `DcP1..DcP4` already have with their
mainframe slot. The E1472A is a VXI servant behind a command module, which maps
its logical address to a real secondary, so it carries one and `NoCard`.

## wiring.inc

Four blocks, of which two hold this rig's static *path* facts (see
`hal/topology/wiring.hpp`'s own comment): which channel each instrument is
hard-wired to, and which channel each VPC connector pin is hard-wired to. Read
by `hal/src/verbs/measure.cpp` and `hal/src/verbs/route.cpp`, each needing its own
declaration of the resulting (inline) tables since each is its own translation
unit.

The other two hold pins rather than paths, and record connections that bypass
the fabric entirely: `SOURCE_WIRING`, where each fixed-wired supply's output
lands, and `TAP_WIRING`, where a measuring instrument's leads are cabled
straight onto a pin. This rig's `TAP_WIRING` is empty and structurally so — it
has a fabric precisely so its three meters do not need bolting to anything, and
`rig/tests/test_wiring_uniqueness.cpp` makes an instrument named in both that
block and `INSTRUMENT_WIRING` a build error. The table exists for the
deployment with no cards at all; see `hal::TapWiring`.

A route is the composition of those two halves, closed together, which only
works where they meet -- so the topology this file describes is built around
buses. There are two:

- **the LF measurement bus** is column 00 of `Matrix1`'s group 0. `Dmm1`,
  `Dmm2` and `Osc1` sit on rows 0-2, `Mux1`'s common on row 3. An instrument's
  path is its own crosspoint onto that column; a pin's path is its mux channel
  plus the crosspoint that puts the mux common on the same column. One shared
  crosspoint appears in every routed pin's path, which is what
  `hal::SwitchFabric`'s use counting is for.
- **the HF bus** is `RfMux1` bank 0's common, cabled to `Osc1`'s channel 1. A
  tree-switched 1x4 needs no instrument-side hop: the pin's channel is the
  whole path.

Two things stay off the buses on purpose. Supply leads (`DcP3`/`DcP4`,
`AcP1`'s phases) switch on `Spst1`, because it is load current that has to be
kept off signal relays. The console's three conductors switch on `Spdt1`
against `Ser1`'s own three `Spst1` relays, because three wires that must stay
three wires cannot share a mux common -- writing them as mux channels, which
this file used to, describes a rig that shorts its console together the moment
it connects it.

Channels are written the way each card's manual numbers them:
`HOP( Mux1, 3)` flat, `CROSSPOINT( Matrix1, 0, 3, 0)` for the 1260-45's
`<group><row><column>`, `BANK( RfMux1, 0, 1)` for the E1472A's
`<bank><channel>`.

## active_instruments.hpp

Declares this rig's actual instrument globals (`Dmm1`, `Dmm2`, `Osc1`,
`DcP1`..`DcP4`, `AcP1`, `Ser1` in this repo) plus `hal::fabric`, the switching
fabric's own global state. A script writes `Dmm1`/`DcP1`/etc directly --
no factory function, no lookup -- so this file, together with
`instrument.inc` right alongside it, is this rig's entire contribution to
what would otherwise be a generic `hal::` library with no instruments
plugged into it at all.

## tests/

A directory of data with a test target, the same shape `dut/` has. What lands
here is what belongs to neither of the two places a hal-side test would
otherwise go: not to generic `framework/hal/`, because it names concrete
instruments, and not to any one `instruments/<model>/`, because it names more
than one of them (a driver directory that reached for a second driver would
stop being packageable on its own — see `instruments/README.md`).

| File | What makes it rig-level |
|---|---|
| `test_call_syntax.cpp` | source a rail, connect it, measure it with a second instrument, tear down — three drivers and four engines, the shape a script actually writes |
| `test_safing.cpp` | expands `active_instruments.hpp` and calls `hal::safeRig()`, which reflects over this rig's real globals |
| `test_source_instruments.cpp` | `Connect`/`Disconnect` being additive, which takes a DC supply and an AC source in one test |
| `test_describe.cpp` | that the engines actually post `describeConfig`'s output to the journal |
| `test_source_readback.cpp` | a source measuring its own output, and leaving the fabric untouched |
| `test_interlock.cpp` | the electrical interlock end to end — the refusal half has to energise this rig's *real* `DcP3`, since `hal::energisedSourceAt` answers from the global instruments and this rig's `SOURCE_WIRING` |
| `test_wiring_uniqueness.cpp` | that no two endpoints in `wiring.inc` claim one relay — the only file here that expands this rig's real tables |
| `test_switch_device.cpp` | every assertion names one of this bench's five cards, so what it checks is `devices.inc` as much as the mechanism reading it |
| `test_switch_fabric.cpp` | the fabric mechanism is generic, but it cannot be exercised without device ids, and those come from `devices.inc` |
| `test_wiring.cpp` | the same, for path composition across the two wiring tables |

The last three arrived from `framework/hal/tests/` after `dev/` existed — a bench with
no switching hardware has no `Matrix1` for them to name. See `framework/hal/README.md`
on why the `hal`/`hal_rig` link line did not catch them, and `dev/README.md` for
the other three places the same assumption was hiding.

`test_describe.cpp` and `test_source_readback.cpp` are the ones that look
misplaced and aren't: "did `Apply` reach the log" is a claim about the
engine-and-driver pair, which neither side can assert about itself.

`test_wiring_uniqueness.cpp` is the odd one in a different way — it drives no
engine and runs nothing, it is a `static_assert` sweep over `wiring.inc`, the
same shape `dut/tests/test_wiring_coverage.cpp` has. It is here rather than
there because it involves no DUT: it checks this rig's table against itself.
The rule is that **an element that identifies an endpoint may not appear in
another entry** — not that no element appears twice, since this rig's ten
routed pins deliberately share one crosspoint (that is what
`hal::SwitchFabric`'s use counting is for). "Identifies" means hop zero, which
makes the endpoint-first ordering of a `Path` load-bearing rather than merely
conventional; see `hal::WiringHop`. One rule, three mistakes caught: two pins
on one mux channel, a pin and an instrument on one relay, and a trunk hop
written as some other endpoint's own relay.

`rig_tests` is the only test target that links `hal_rig`. `hal_tests` links
generic `hal`, and each driver's tests link that driver alone, so a test written
in either of those places that reaches an instrument global, an `Apply` or
`safeRig()` fails to build — the boundary is checked by the link line rather
than by review. Note what does *not* live here: `dut/tests/`'s
`test_wiring_coverage.cpp` reads `rig/wiring.inc`, but what it checks is that
every DUT point is wired, so it belongs with the DUT profile that names the
points.

## A new rig

A separate rig -- its own repo, its own bench, its own instruments -- reuses
`framework/core/` and `framework/hal/` as-is (via `find_package`/`FetchContent`/
`add_subdirectory`, whichever that repo's own build prefers) and supplies
its own four files in this same shape, pointed at by the same four
`THORIUM_*` variables `framework/hal/CMakeLists.txt` requires. Its own `tests/`
comes with them, for the same reason this one exists: those tests name that
rig's instruments, so they are that rig's content and not the framework's.
Nothing under `framework/hal/` needs to change for any of it to work -- that's the
point of the split this directory draws.
