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
    CMakeLists.txt         # no library -- just rig_tests, over the three files below
    instrument.inc         # THORIUM_INSTRUMENT_TABLE -- this rig's fixed instrument list, and hal::InstrumentId's enumerators
    devices.inc            # THORIUM_DEVICE_TABLE -- this rig's switching cards, and hal::SwitchDeviceId's enumerators
    wiring.inc             # THORIUM_WIRING_TABLE -- this rig's fixed instrument/connector wiring
    tests/                 # the integration tests that need this rig rather than a mechanism
```

## instrument.inc

This rig's fixed, concrete instrument list -- one `INSTRUMENT(type, id,
address, ...)` per instrument, naming its C++ driver type, the global it's
addressed by (id doubles as both the global's name and its
`hal::InstrumentId` -- there is no rig where those differ, so there's no
separate parameter for it), how the PC reaches it, and any constructor
arguments (a driver may declare one; none on this bench does any more). Included from two different
places, each with `INSTRUMENT` defined for its own purpose:

- `hal/topology/active_instruments.hpp` declares each id as an actual global
- `hal/driver/instrument.hpp` keeps only each id, to generate `hal::InstrumentId`'s
  enumerators (see that header's own comment)

Both reads see this exact list, so an instrument added here can never
desync from its own identity enumerator -- there's nothing left to desync
from.

CMake reads it a third time, at configure time rather than through the
preprocessor: `cmake/InstrumentDrivers.cmake` takes the namespace qualifier off
each row's type column (`keysight_edu36311a` in `keysight_edu36311a::RelayOutput2`) and
generates the `#include` list `active_instruments.hpp` opens before it expands
the table. A driver package, its header and its namespace all carry one name,
so the qualifier that tells a *reader* which driver a row is also tells the
build. That is why adding an instrument here is one row and nothing else --
there is no include list in this directory to keep in step, and a row naming a
driver this build has no package for fails at configure time saying so.

The `address` column is the control side -- `Gpib(0, 14)`,
`Lan("bench-dmm1")`, `Serial("/dev/ttyUSB0")` -- as against the signal side
`wiring.inc` describes below. See `hal/driver/address.hpp` for the types, and note
two things about the column: it is mandatory (a row that omits it fails to
preprocess, in *both* readers), and which bus kinds a given row may use is
fixed by its driver rather than by this file, so `Gpib(...)` on an EDU34450A
-- a LAN-and-USB box with no GPIB connector -- is a compile error rather than a
run that dies on its first reading.

`DcP1`..`DcP4` all carry the same address on purpose: four modules behind one
mainframe interface is one address and four slots, and the slot stays a
separate constructor argument rather than a field on the address (see
`hal/topology/active_instruments.hpp`'s own comment for that argument in full).

`DcP5`..`DcP7` are a second box of that shape and the comparison is instructive
rather than repetitive: three outputs of one `hal::keysight_edu36311a::EDU36311A`
triple-output supply, 6 V/5 A and two of 30 V/1 A. One address again, but the
endpoint is in the *type* column (`DirectOutput1`, `RelayOutput2`,
`RelayOutput3`) rather than a trailing argument, because these three outputs are
built into the chassis and a channel number handed in separately would let this
table write down an output the instrument does not have. The mainframe's slot is
rightly an argument for the opposite reason -- any module can be in any slot.

Which of the two isolation aliases each of those rows takes is arithmetic, and
it is the one thing about them a reader might think arbitrary: `Spst1`'s relays
are rated 2 A, so `DcP6` and `DcP7` (1 A each) get one and `DcP5` (5 A) cannot
have one at all and is cabled straight through. None of the three has a
`SOURCE_WIRING` row yet -- this DUT has no point for a 6 V or a 30 V rail, and
inventing one would be inventing a DUT fact.

`Dmm1` and `Dmm2` are two *different* models, which is the easiest thing in this
table to skim past. They were two L4411A instances --
two wiring facts sharing one C++ type, the way `DcP1`..`DcP4` still are -- and
`Dmm1` is now a 5.5-digit `hal::keysight_edu34450a::EDU34450A` against the
L4411A's 6.5. A script measuring a rail cannot tell them apart; a criterion
tightened past 5.5 digits can, and so can anything reaching for `.nplc()`, which
the EDU34450A has no command for. See
`instruments/keysight_edu34450a/README.md`.

`Osc1` is the other row whose model has changed, and it changed further than the
meter did. It was a four-channel Infiniium DSO8064A reconstructed
from a legacy ATE script; it is now the `hal::keysight_dsox1202g::DSOX1202G`
actually on the bench, which has two channels, 1 MOhm inputs and no LAN
connector — so the row's address column could not stay `Lan("bench-osc1")` even
in principle, and does not compile if it tries. See
`instruments/keysight_dsox1202g/README.md` for what a script has to re-decide,
and `instrument.inc`'s own comment for why this row now says `Simulated{}` while
the instrument it names is the one thing on this rig that physically exists.

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

**Where this rack is going.** The five card rows above are staying, and they are
also on their way out: the decision taken 2026-09-02 is that this rig's
switching becomes a single Keysight 34980A -- four **34932A** dual 4x16 armature
matrices in slots 1-4 and one **34941A** quad 1x4 50 Ω RF multiplexer in slot 5.
Both models and their channel spaces are in generic `hal` already, and the
mainframe is `instruments/keysight_34980a/`.

The rows have not moved because that module set is not a five-for-five swap, and
`rig/devices.inc` sets out what it does and does not replace. In short: the
crossbar, the DUT fan-in and the HF path all land better than they sit today
(matrix columns are a real crossbar, so the one-shared-mux-common limitation
below simply goes away). What has no home is the **power path** -- every 34932A
channel is rated 1 A against the 1260-18's 2 A, so `AcP1`'s 3 A phases cannot be
switched at all and `DcP6`/`DcP7` sit exactly on the limit -- and the **console
changeover**, since a matrix crosspoint is make-only and there is no Form C.
Both are decisions about hardware rather than code, so the wiring re-plan waits
on them.

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
  `Dmm2` and `Osc1`'s channel 2 sit on rows 0-2, `Mux1`'s common on row 3. An instrument's
  path is its own crosspoint onto that column; a pin's path is its mux channel
  plus the crosspoint that puts the mux common on the same column. One shared
  crosspoint appears in every routed pin's path, which is what
  `hal::SwitchFabric`'s use counting is for.
- **the HF bus** is `RfMux1` bank 0's common, cabled to `Osc1`'s channel 1. A
  tree-switched 1x4 needs no instrument-side hop: the pin's channel is the
  whole path. It is 50 ohm cable into a scope with 1 MOhm-only inputs, so it
  wants a feedthrough terminator at that BNC — a part, not a line of code; see
  `wiring.inc`'s own `TODO(bench)`.

Two things stay off the buses on purpose. Supply leads (`DcP3`/`DcP4`,
`DcP6`/`DcP7`, `AcP1`'s phases) switch on `Spst1`, because it is load current
that has to be kept off signal relays -- and `DcP5`'s 5 A lead switches nowhere
at all, because 2 A is what that card's relays are rated for. The console's three conductors switch on `Spdt1`
against `Ser1`'s own three `Spst1` relays, because three wires that must stay
three wires cannot share a mux common -- writing them as mux channels, which
this file used to, describes a rig that shorts its console together the moment
it connects it.

Channels are written the way each card's manual numbers them:
`HOP( Mux1, 3)` flat, `CROSSPOINT( Matrix1, 0, 3, 0)` for the 1260-45's
`<group><row><column>`, `BANK( RfMux1, 0, 1)` for the E1472A's
`<bank><channel>`, and `ROW_COLUMN( device, 3, 15)` for a 34932A's plain
row/column.

## What is *not* here

The mechanism that turns these three tables into this rig's actual instrument
globals (`Dmm1`, `Dmm2`, `Osc1`, `DcP1`..`DcP7`, `AcP1`, `Ser1`) plus
`hal::fabric` is `hal/topology/active_instruments.hpp`, in the framework
alongside the `wiring.hpp`/`adapter.hpp` mechanisms behind the other
declarative tables. It used to be an `active_instruments.hpp` in this
directory, which meant every rig carrying its own copy of a macro definition
that never varies.

What did vary was that file's driver `#include`s, and those are now generated
from `instrument.inc` (see above). So what is left here is content: three
tables of this bench's facts, and no C++ scaffolding at all. That is this
rig's entire contribution to what would otherwise be a generic `hal::` library
with no instruments plugged into it at all.

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
| `test_safing.cpp` | includes `hal/topology/active_instruments.hpp` and calls `hal::safeRig()`, which reflects over this rig's real globals |
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
