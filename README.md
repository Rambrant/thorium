# Thorium

A framework for writing automated hardware test suites in C++26, for a bench of
real instruments testing a real device.

A test script reads like the test specification it came from:

```cpp
const auto rail = Measure( Dmm1.voltage(), at( dut::Output5V));

Verify( FS_Supply_1::FS_Supply_5V0, rail);
```

No instrument address, no relay path, no unit conversion, no logging call. Every
one of those is resolved somewhere else — and, wherever possible, resolved by the
compiler.

---

## 1. The goal: if it can be a compile error, it must be a compile error

A test rig fails in expensive ways. A mistyped test point measures the wrong pin
and passes. A criterion compared against the wrong unit passes. A relay path
nobody wired up throws three hours into a soak test. A tolerance table edited for
one variant silently diverges from another.

Every one of those is a *static* fact — about names, units, wiring and tables that
are all fixed before the rig is switched on. So the design rule for this codebase
is:

> **A mistake that could be caught by the compiler must be caught by the compiler.
> Runtime checks are a last resort, and each one is a documented concession.**

Not a style preference — it is what makes the difference between a build that
fails on a developer's laptop in two seconds and a test report that is quietly
wrong.

### What is a compile error today

Twenty-four classes of mistake, none of which can reach the bench:

| Mistake | What happens |
|---|---|
| Misspelled DUT point — `dut::Output5Vx` | no such member |
| Forgotten `at(...)` — `Measure( Dmm1.voltage(), dut::Output5V)` | no matching overload |
| Misspelled criterion — `FS_Supply_1::FS_Supply_5V0x` | no such member |
| Unit mismatch in a criterion — `EQ( 5.0_A)` against a voltage reading | no viable predicate — in **every** tolerance variant, not just the default one |
| A DUT point with no `rig/wiring.inc` entry | `dut_tests` fails to build |
| A tolerance variant referencing a production value that doesn't exist | no such member |
| A variant missing a criterion production declares | the `scripts` library fails to build, naming the id *and* the variant |
| A criterion present in one variant only | `scripts_tests` fails to build |
| Misspelled or renamed script in `TEST(...)` | undeclared identifier |
| `Connect( DcP1.dc())` on a supply with no isolation relay | no matching `connectDriver` |
| `Osc1.channel<5>()` or `Osc1.trigger().edgeSource<5>()` on a four-channel scope | no valid instantiation |
| An instrument driver with no `safe()` | `static_assert` naming the type |
| A new `QuantityKind` with no matching `Quantity` alias | `static_assert` with the fix |
| A new source instrument with no `describeConfig` | no matching function |
| Sense leads wired without the matching force leads | `dut_tests` fails to build |
| `.epsilon()` on a type a tolerance is meaningless for | constraint not satisfied |
| Misspelled bundle — `at( dut::Consol)` | no such member |
| `Apply( Ser1.rs232())` on a port with no output to energise | no matching `applyDriver` |
| `Apply( Osc1.trigger())` on a scope, which energises nothing | no matching `applyDriver` |
| `Arm( Osc1.trigger())` — arming something that is not a capture | no matching `armDriver` |
| `Setup( Osc1.channel<3>())` naming a channel and no setting | no matching function |
| `BIT_SET<9>()` against a `std::byte` | constraint not satisfied |
| Adding two temperatures — `ambient + hotspot` | no matching operator: a Celsius value is a point, not a magnitude |
| A sensor applied to the wrong reading — `dut::CoolantSensor( current)` | no matching function |

Two of those deserve a second look, because they are the interesting ones.

**Wiring coverage.** `dut/tests/test_wiring_coverage.cpp` has no runtime
assertions at all — the point is that it *compiles*. It reflects over every
`POINT` in the DUT adapter and asserts each one has a matching connector-wiring
entry. Adding a point and forgetting to wire it breaks the build, not a test run
three weeks later. Same trick for tolerance variants in
`suite/tests/test_criteria_variants_compile.cpp`.

**Criteria variants.** Production, stress-chamber and aged-equipment tolerances
are three separate tables, **all compiled into one binary** and chosen per run
with `run_scripts --criteria=stress`. One of them is the *master*
(`THORIUM_CRITERIA_MASTER`, `production` by default but any of them will do), and
a criterion whose value is unchanged from it is written `CRIT_FROM_MASTER`, so
the number lives in exactly one place — and a typo in the reference is a hard
compile error, not a silent fallback.

Nothing was traded away to get the runtime choice. A script still writes
`FS_Supply_1::FS_Supply_5V0` as an ordinary struct member, so a typo is still
"no such member"; what changed is that the criterion now carries one tolerance
per variant and `Verify` picks between them. The compiler in fact checks *more*
than it used to: it now sees every variant next to the script measuring against
it, so an amp criterion in the aged table fails the build today rather than the
day someone first targets aged hardware. See `dut/README.md` for the mechanism.

### What is deliberately still runtime

Being honest about the edges matters more than the table above:

- **Wiring lookup inside a measurement.** `hal::InstrumentWiring::find()` throws
  if an instrument has no entry. The compile-time backstop above covers the
  *connector* side for every declared point; the instrument side is still a
  runtime throw. See the comments in `framework/hal/include/hal/topology/wiring.hpp`.
- **Scripted-session unit mismatch.** Feeding a `Current` to a point declared
  `Voltage` throws, naming the point and both kinds.
- **`--select` with an unknown test id** fails the run before `RUN_SETUP`, naming
  every id the catalog has no test for. One bad id in an otherwise valid list
  refuses the whole list rather than running the good half: a partial selection
  is not a smaller run somebody asked for, it is a list they got wrong, and
  running most of it produces a report that has to be read twice to be trusted.
  `tools/run-tests.sh` validates ids against the catalog before it gets that
  far, but a hand-typed flag has no such help.
- **Instrument I/O.** Real instruments are absent, mis-addressed or lying. That
  is what the runtime is for.
- **A measurement the instrument declines to make.** Not an error and not a
  compile-time question — a real third answer, reported with the instrument's
  own reason and read back as NaN unless the script says otherwise. See
  §3, "When an instrument cannot make the measurement".
- **Two *quantities* measured at one DUT point share a session slot.**
  `Measure( Dmm1.voltage(), at( p))` and `Measure( Dmm1.current(), at( p))` both
  key as `"p"`, so a test injecting both gets them in whichever order the script
  asks. A driver that gives several answers about one pin can key them apart by
  qualifying its ports (`core::Port::qualifiedBy` — the oscilloscope does), but
  the general case would need the `QuantityKind` folded into the key, which
  would rename every key in every existing recording.
- **A script that checks nothing.** Nothing statically says whether a script
  body reaches a `Verify`, so "this test verified something" cannot be a compile
  error. It is a runtime rule instead, and a strict one: the verdict is derived
  from the checks a script recorded, and a test that recorded none fails and says
  so in the log rather than passing vacuously. See `core::Journal::endTest`.
- **Electrical safety of a route.** `SwitchFabric::connect()` is additive and
  reference-counted, and it refuses nothing. Two instruments sharing a pin is
  the intended behaviour — that is how a rail is measured while a supply holds
  it — so connect/disconnect symmetry does *not* mean only one thing is ever on
  a node.

  The two ways that can be electrically wrong get different answers, and
  `core/verbs/interlock.hpp` is where the reasoning lives:

  **An ammeter across a driven rail is refused.** A landing pin is deliberately
  also tapped (see `core::PointKind`), so a current, resistance or capacitance
  reading routed onto a `SOURCE_POINT` is reachable today — and it puts a shunt,
  or a test-current source, across a rail through relays sized for signals. `core::MeasureEngine` throws
  `core::InterlockViolation` before composing the path, naming the pin, the rail
  and the way out. A voltage tap on that same live pin stays permitted, because
  that is the measurement the landing pin is worth declaring for. There is no
  override flag: the escape hatch is `Remove` the rail first, which is the bench
  procedure anyway.

  **A relay moving under load is recorded, not refused.** Hot switching is a
  wear argument rather than a damage one, and `core/verbs/source.hpp` has always said
  the call belongs to whoever writes the sequence — a safety interlock dropping
  a connection must not first wait out a ramp-down. What was missing was never
  the veto but the trace: the rule was stated in eight comments and checked in
  none, so a worn contact tied back to no run and a deliberate hot switch read
  exactly like an accidental one. `Connect`/`Disconnect` now ask the driver
  whether its output is live and say so in their own journal event.

  Recorded is not the same as noticed, so there is one assertion on top of it:
  `AcceptanceMachineLog.NoShippedScriptOrHookMovesARelayUnderLoad` runs the whole
  catalog and fails if any hot-switch notice reaches the log, naming the
  instrument. That is a claim about *this suite's scripts*, not about the
  framework — a rig that wants a hot switch somewhere writes it, gets the note,
  and does not have that test. It covers the paths a run actually takes, which is
  the only thing a log can be checked on.

  What this is not is a safety guarantee, for the same reason `hal::safeRig()`
  is not one — see that file. It also answers from the rig's *global* instrument
  objects, so a supply constructed locally in a test fixture is invisible to it,
  correctly: that supply is not on the bench.

---

## 2. Structure: a portable framework, and one deployment's content

The single most important thing to understand about this tree is that it holds
**two different kinds of thing**:

```
framework/       PORTABLE      -- knows nothing about this rig or this DUT,
                 and depends on nothing outside itself
  core/            quantities/  unit-tagged values and the arithmetic on them
                   criteria/    predicates, the CRITERIA/CRIT tables, Verify
                   verbs/       Measure/Apply/Connect/Arm/Fetch, and the
                                electrical interlock they consult
                   session/     live, replayed or injected readings, and
                                whether the rig is attached at all
                   journal/     the run event stream and its log sinks
                   catalog/     the GROUP/TEST tables a suite declares
                   topology/    the generic half of the DUT-point machinery
                   driver/      what an instrument driver names from core
  hal/             fabric/      the switching hardware -- devices, elements,
                                the path a route closes
                   topology/    this bench: VPC coordinates, the adapter and
                                bundle tables, and the wiring that maps them
                                onto the fabric
                   driver/      the kit a driver is written against
                   verbs/       this rig's instantiation of core's engines
  runner/          main.cpp, its argument parser, and the build targets that
                   turn a deployment's suite into run_scripts

instruments/     THE DRIVERS   -- portable too, but shipped a driver at a time
                 rather than with the framework, which is why it is not under it

rig/             THIS BENCH'S DATA      -- which instruments, wired how,
                 plus the integration tests that need more than one of them
dut/             THIS DEVICE'S DATA     -- named test points, tolerance tables
suite/           THIS SUITE'S CONTENT   -- test scripts and the catalog
acceptance/      THIS DEPLOYMENT'S      -- black-box tests over the built
                 BINARY, TESTED            run_scripts, asserting on facts
                                           from all three of the above

tools/           run-tests.sh (the tester's picker)
cmake/           build helpers -- generated criteria tables, the test-target
                 helper, the install-time manifest, the installed package
docs/            the slide deck, and the generated reference -- which also
                 carries the check that every path named in a comment names
                 a file that is there
```

`core/` and `hal/` share three of those folder names -- `verbs/`, `topology/`
and `driver/` -- wherever the pairing is real rather than thematic:
`hal/verbs/measure.hpp` is the instantiation of `core/verbs/measure.hpp` for
this rig, and the same holds for route, source, acquire, trace and interlock.
The generic half stays in core because it is generic, not because of where it
happens to sit. `core/meta.hpp` is the one header in either library with no
group: it is enum reflection, belonging to no layer and used by several.

`framework/` never depends on anything outside it. A second rig testing a second
device is a *separate repository* that reuses `framework/` unchanged and brings its
own `rig/`, `dut/` and `suite/` — content, not a program. It writes no `main()`:
`framework/runner` owns the runner and reaches a deployment the same way `framework/hal`
reaches a rig. Everything rig-specific gets to the framework through CMake file
paths and a handful of compile definitions — never through an `#include`
pointing outwards.

### Every README in this tree

Each one carries the full rationale for its own directory — including the
alternatives that were rejected and what went wrong with them, which is the half
that cannot be recovered from the code.

**The framework**

| | |
|---|---|
| [`framework/hal`](framework/hal/README.md) | The two-target `hal`/`hal_rig` split, the static wiring facts and how a route is composed, adapter points, and what is still a runtime check |
| [`instruments`](instruments/README.md) | Why each driver is its own packageable directory, and what a driver may assume |
| [`cmake`](cmake/README.md) | The four build helpers: generated criteria tables, the per-layer test target, the install-time manifest, and the installed CMake package |

**The drivers**

| | |
|---|---|
| [`instruments/keysight_edu34450a`](instruments/keysight_edu34450a/README.md) | DMM, 5½-digit — `Dmm1` on both deployments; 4-wire sense, frequency and capacitance; resolution rather than NPLC |
| [`instruments/keysight_l4411a`](instruments/keysight_l4411a/README.md) | DMM, 6½-digit — `Dmm2`, including the 4-wire sense path |
| [`instruments/keysight_dso8064a`](instruments/keysight_dso8064a/README.md) | Oscilloscope — `Osc1` |
| [`instruments/keysight_n6701a`](instruments/keysight_n6701a/README.md) | DC supply — `DcP1`..`DcP4`, and the direct-vs-relay isolation split |
| [`instruments/keysight_ac6834b`](instruments/keysight_ac6834b/README.md) | Three-phase AC source — `AcP1`, balanced vs per-phase |
| [`instruments/racal1260`](instruments/racal1260/README.md) | RS232 port — `Ser1`, routed to a DUT interface through the matrix |

**This deployment's content**

| | |
|---|---|
| [`rig`](rig/README.md) | This bench's instruments, their wiring, the four tables a rig supplies, and the integration tests that need more than one instrument |
| [`dut`](dut/README.md) | This device's test points and criteria variants |
| [`suite`](suite/README.md) | Test scripts, the catalog, and the setup/teardown hooks |

**Other**

| | |
|---|---|
| [`docs`](docs/README.md) | The slide deck, as `.pptx` and a self-contained `.html` |
| [`docs/api`](docs/api/README.md) | The generated reference, and `check_references.py` — which gates it on every cross-reference in the tree still resolving |

`framework/core`, `framework/runner`, `tools/` and `dsl/` have no README of their own — their
rationale lives in the header and `CMakeLists.txt` comments, and for `framework/core`
in this file's §1.

### How it is all connected

Read this as "what a single `Measure(...)` call needs, and where each piece comes
from":

```
  A test script                                     suite/scripts/*.cpp
  Measure( Dmm1.voltage(), at( dut::Output5V))
           │                    │
           │                    └── the POINT: which VPC pin, baked into
           │                        its TYPE                     dut/adapter.inc
           │
           └── the port: which instrument, and which
               quantity to read                          rig/instrument.inc
                                                         (also generates
                                                          hal::InstrumentId)
                            ▼
  core::MeasureEngine composes the route
      instrument → channel            ┐
      connector pin → channel         ┘                 rig/wiring.inc
                            ▼
  hal::SwitchFabric closes the relays, the driver reads, the relays open
                            ▼
  core::ISession decides where the value really came from -- for a reading,
  a serial reply, a capture's yes/no, or a whole trace
      LiveSession  │  ScriptedSession (injected)  │  replay from a recording
                            ▼
  Verify( FS_Supply_1::FS_Supply_5V0, rail)
                            │
                            └── the CRIT: predicate + tolerance
                                                 dut/criteria_<variant>.inc

  Every verb above posts to core::Journal ── one ordered event stream
                            │
      ┌─────────────────────┼─────────────────────┐
      ▼                     ▼                     ▼
  ConsoleSink           RtfSink               SarifSink
  live, colour          human, colour,        machine, EVERY verb,
                        openable mid-run      SARIF 2.1.0

  framework/runner walks suite/test_catalog.inc (GROUP / TEST), bracketing each
  group and test with journal boundaries -- which is how the logs know the
  names that a script, taking no parameters at all, cannot carry itself.
```

Three ideas carry most of the weight:

**Points and criteria are types, not strings.** `POINT` and `CRIT` expand into
`static constexpr` members of a struct, so every reference to one is name-checked
by ordinary C++ lookup. A point's VPC location is a *template parameter* of its
type, which is what makes a misspelled point a "no such member" error rather
than a route that silently goes nowhere.

**A point is a place; the quantity is the port's.** `POINT` says which pin, and
nothing else. What you measure there is whatever instrument port you aim at it,
so voltage, inrush current and ripple frequency at the same pin are three
ordinary calls and not three declarations. Unit safety is unaffected: `Measure`
returns the port's own `Quantity<Unit>`, so a criterion in the wrong unit still
fails to compile at the `Verify` call site.

**A route is composed, not stored.** An instrument's channel and a connector
pin's channel are two independent wiring facts. `Measure` concatenates them at
the moment of reading, so adding a point costs one wiring line — not one line per
(instrument, pin) pair.

**One event stream, many renderings.** Every verb — `Measure`, `Setup`, `Apply`,
`Remove`, `Connect`, `Disconnect`, `Write`, `Read`, `Arm`, `Await`, `Verify`, plus
the safing pass — posts to `core::Journal`. Sinks decide what to show. Neither filtering nor
formatting lives at a call site, which is why a third log format needs no change
to any verb.

---

## 3. How to add things

Each recipe below is complete. Where a step is missable, the build says so.

### Add an instrument to the rig

`rig/instrument.inc` — one line. The id is both the global's name and its
`hal::InstrumentId` enumerator; the third column is how the PC reaches the
instrument; trailing arguments are the driver's constructor arguments.

```cpp
INSTRUMENTS
    INSTRUMENT( keysight_l4411a::L4411A, Dmm3, Lan( "bench-dmm3"))  // a third DMM
    INSTRUMENT( keysight_n6701a::Relay,  DcP5, Gpib( 0, 14), 5)     // slot 5, relay-isolated
END_INSTRUMENTS
```

The address is mandatory, and which bus kinds a row may use is fixed by its
driver — `Gpib(...)` on an L4411A is a compile error, because an LXI box has no
GPIB connector (see `hal/driver/address.hpp`).

The type column's namespace qualifier is not decoration: a driver package, its
header and its namespace all carry one name, so `keysight_l4411a` is what tells
the build to include `hal/keysight_l4411a.hpp`
(`cmake/InstrumentDrivers.cmake`). An unqualified type, or one naming a driver
this build has no package for, fails at configure time saying which row and
why.

Nothing else. `hal::InstrumentId`, the global handle, the driver's `#include`
and the safing sweep are all derived from this list — safing reflects over
`InstrumentTag`-derived globals rather than reading the file again, so a new
instrument is safed because it exists.

### Add a switching device to the rig

`rig/devices.inc` — one line per card, naming which card it is, where the PC
commands it, and (for a chassis whose cards share one address) which card of
that chassis it is:

```cpp
SWITCH_DEVICE( Racal1260_35, Mux2, Gpib( 0, 7), Card( 5))
```

That generates `hal::SwitchDeviceId::Mux2`, which is what wiring rows below name
— so a card nothing declares cannot be routed through. The model column carries
what the datasheet says: the kind (a 1260-35 is a mux, on every rig that owns
one) and the channel space, so a channel that card does not have fails to build.
A card model `hal::SwitchDeviceModel` does not list yet is added there, beside
its channel predicate.

### Wire an instrument to the switching fabric

`rig/wiring.inc`, `INSTRUMENT_WIRING` block. `HOP( device, channel)`, where
`device` is a `SwitchDeviceId` from `devices.inc`:

```cpp
WIRE_INSTRUMENT( Dmm3, CROSSPOINT( Matrix1, 0, 3, 0))
```

`CROSSPOINT( device, group, row, column)` and `BANK( device, bank, channel)` are
`HOP` written the way a card numbers a composite channel — preferred wherever a
card has one, since the parts say what the digits mean and the packed form of
group 0 row 3 column 00 is `0300`, which C++ reads as octal.

Several entries under one id are closed and opened together — that is how a
three-phase source's four conductors move as a unit. For a 4-wire (Kelvin)
measurement, add the sense leads with `WIRE_INSTRUMENT_SENSE`; they are routed
only for readings that ask for them, never for a plain 2-wire reading on the same
instrument.

### Wire a connector pin

Same file, `CONNECTOR_WIRING` block — `rack, connector, pin` then the hop:

```cpp
WIRE_CONNECTOR( A, 1, 7, HOP( Mux1, 7))
```

`WIRE_CONNECTOR_SENSE` for the sense side. **Wire sense without force and
`dut_tests` will not build** — sense leads measure across the connection the
force leads make, so half a route is always a mistake.

Whether a reading uses the sense path is not a wiring question at all: it is a
property of the port's *type* (`core::SensePath::Required`), so
`fourWireResistance()` and `resistance()` are different types and `MeasureEngine`
picks the route with `if constexpr` — a two-wire reading never compiles the sense
lookups at all.

### Add a DUT test point

`dut/adapter.inc` — `POINT( id, rack, connector, pin, description)`:

```cpp
ADAPTER( "Device X on standard adapter")
    POINT( Output12V, A, 1, 7, "12Vdc supply port")
END_ADAPTER
```

A point declares *where*, not *what*. The same pin can then be read for any
quantity, chosen by the port at the call site:

```cpp
Measure( Dmm1.voltage(),                at( dut::Output12V));   // the rail
Measure( Dmm1.current(),                at( dut::Output12V));   // inrush
Measure( Osc1.channel<1>().frequency(), at( dut::Output12V));   // ripple
```

The identifier cannot start with a digit, so a spec label like "12VOutput"
becomes `Output12V`. **Forget to wire the pin and `dut_tests` will not build**
— that is the coverage check, not a runtime surprise. Wiring it means one of
two rows: a `WIRE_CONNECTOR` row on a rig with a switching fabric, or a
`WIRE_TAP` row on one whose instruments are cabled straight to the DUT (see
`hal::TapWiring`). The coverage check accepts either, which is what lets a
bench with no cards declare points at all.

### Add a criterion

`dut/criteria_production.inc` (and the other variants). Group and id become
struct members, so both are compile-checked at every use:

```cpp
CRITERIA( FS_Supply_1, "Supply rail voltage checks")
    CRIT( FS_Supply_12V, EQ( 12.0_V).epsilon( 0.1_V), "12Vdc port within +/-100mV")
END_CRITERIA
```

Available predicates: `EQ`, `NE`, `LT`, `LE`, `GT`, `GE`, `IN`, `MASK`, `ANY`,
`NONE` — each with `.epsilon(...)` where a tolerance is meaningful. Write the
value with a unit literal (`12.0_V`, not `12.0`); that is what makes a unit
mismatch a compile error, and it is what the log prints as the required limit.

`ANY`/`NONE` take a list of values (`ANY( 3.3_V, 5.0_V, 12.0_V)`), one tolerance
covering the whole list. For a criterion that is a disjunction of *conditions*
rather than of values — a range or a relation, which is not a value and so cannot
be an `ANY` option — `ANY_OF` combines whole predicates:

```cpp
CRIT( FS_Supply_Rail, ANY_OF( IN( 3.0_V, 3.6_V), EQ( 5.0_V)), "3V3 band or 5V rail")
```

`ANY_OF` is the one predicate with no `.epsilon(...)` of its own: its members need
not share a value type, so each carries its own tolerance next to the value it
tolerates. Unlike `ANY`, it cannot reject a mixed-unit list when it is built (a
`MASK` has no one value type to compare against), so the mismatch surfaces as a
compile error at the point of use instead — the resulting predicate is callable
with neither unit.

Then add it to `criteria_stress.inc` and `criteria_aged.inc` — same value as the
master table means:

```cpp
CRIT_FROM_MASTER( FS_Supply_1, FS_Supply_12V)
```

**Miss a variant and the build will not complete** — the merged table that every
script names is generated from the master's, so a missing id is a compile error
that says which variant is short of it.

### Add a test script

Three files, in this order:

1. **Declare** in `suite/scripts.hpp`:
   ```cpp
   auto thermalRampScript() -> void;
   ```
2. **Define** in `suite/scripts/thermal_ramp_script.cpp` — no `CMakeLists.txt`
   edit; `suite/scripts/*.cpp` is globbed with `CONFIGURE_DEPENDS`:
   ```cpp
   #include "../prelude.hpp"       // instruments, Measure/Apply/Verify,
                                    // criteria tables, adapter points, literals

   auto thermalRampScript() -> void
   {
       Connect( DcP3.dc());   // relay closes cold, then the output comes up --
       Apply(   DcP3.dc().voltage( 24.0_V).currentLimit( 2.0_A));

       const auto rail = Measure( Dmm1.voltage(), at( dut::Output12V));
       Verify( FS_Supply_1::FS_Supply_12V, rail);

       Remove(     DcP3.dc());   // ...and goes down before the relay opens.
       Disconnect( DcP3.dc());   // A relay moved under load arcs and welds --
                                 // still allowed, but the log says you did it.
   }
   ```
   **No verdict is returned.** The script's outcome is derived from the checks it
   recorded, when the runner closes the test (`core::Journal::endTest`) — so
   there is no `bool allPassed` to fold and no way for a script's answer to
   disagree with its own log. Two consequences worth knowing before writing one:
   a check that cannot be made is recorded with `Fail` rather than by returning
   false, and **a script that records no check at all fails**, because a test
   that verified nothing is not a test that passed.
3. **Register** in `suite/test_catalog.inc` — the script is named as an
   identifier, not a string, so a typo is a compile error:
   ```cpp
   GROUP( Thermal, "Tests validating behaviour over temperature")
       TEST( ThermalRamp, thermalRampScript, "Ramp the 12V rail and check regulation")
   END_GROUP
   ```

A wholly new `GROUP` costs nothing beyond that third step: `core::catalog::Catalog`
is built by reflecting over whatever the catalog file declared, in declaration
order. It used to be a hand-written list, which meant a new group had to be
acknowledged in framework code *and* mirrored into the test-fixture catalog,
so a suite gaining a group could not be a suite-only change.

A script's signature is fixed to `() -> void` — it takes nothing and returns
nothing. No rig or device handle, because routing is resolved statically inside
it; no group or test name either, because the log gets those from the runner's
journal boundaries, and every criterion carries its own group into the log via
`Verify`; and no verdict, because the journal already holds every check the
script made and what each one came to. Each of those would be a second source
for a fact the log already has — and the verdict was the one that could actually
be caught disagreeing: a missing `allPassed &=` used to produce a human log
reading `[PASS]` around a SARIF result reading `fail`, for the same test in the
same run.

A hook — `RUN_SETUP`/`RUN_TEARDOWN` around the run, `SETUP`/`TEARDOWN` around
one group — keeps its `bool`, and that is not an inconsistency: a hook runs
outside any test bracket, so there is no per-test event stream to derive a
verdict from, and what its `bool` means is "did the bracketing work", not "did
the DUT pass".

### Bracket a run with setup and teardown

Powering the rig up before the first script and back down after the last is a
`RUN_SETUP`/`RUN_TEARDOWN` pair in the catalog, named as identifiers exactly the
way `TEST` names a script. They go at the catalog's file scope, outside every
`GROUP` — a group brackets itself with the unqualified pair instead, see
[Bracket one group](#bracket-one-group):

1. **Declare** them alongside the scripts in `suite/scripts.hpp`:
   ```cpp
   [[nodiscard]] auto rigPowerOn()  -> bool;
   [[nodiscard]] auto rigPowerOff() -> bool;
   ```
2. **Define** them in `suite/scripts/` like any other script file.
3. **Register** them in `suite/test_catalog.inc`:
   ```cpp
   RUN_SETUP(    rigPowerOn,  "Bring the AC input and the three DC supplies up, checking each rail as it comes")
   RUN_TEARDOWN( rigPowerOff, "Take the supplies back down in the inverse order and open the fabric")

   GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
       TEST( SupplyRail, supplyRailScript, "Verify supply rail voltages via matrix")
   END_GROUP
   ```

The description is required, exactly as `GROUP`'s and `TEST`'s are, and it is
what the logs head the hook with. Every hook in a catalog is called `setup` or
`teardown` — both levels of them, in every group — so a run bracketing three
groups writes four headings reading `setup`, and this prose is the whole of what
tells them apart. It is also the only place a reader is told what the bracket
was *for*: the tests say what they check, and the state somebody had to
establish before them is otherwise only in the hook's source.

Both are optional and independent — declare one, both, or neither. A catalog
with no `RUN_SETUP` line needs no placeholder for one; absence resolves to
`nullptr` through ordinary name lookup (see `core/catalog/active_test_catalog.hpp`).
The shipped catalog declares both: `suite/scripts/rig_power_on.cpp` brings this rig's
sources up in order and checks each one came up, and
`suite/scripts/rig_power_off.cpp` takes them down in the inverse order.

What they bracket is the **selection**, once — including every `--repeat` pass.
`--repeat=50` powers the rig on once, runs the scripts fifty times, and powers
it off once. A hook that should instead run per repetition would be a different
hook, not a different meaning for this one.

Three things worth knowing:

- **`RUN_TEARDOWN` runs on every way out** — the selection finishing,
  `--until-failure` stopping early, a script throwing straight past everything,
  or **`RUN_SETUP` itself failing**. It is a guard destructor for the same reason
  `hal::RigSafingGuard` is one, and it is constructed *before* `RUN_SETUP` runs,
  so a power-up that energised three rails and failed on the fourth is still
  powered back down. A setup hook should therefore never call the teardown from
  its own failure path — that runs it twice.
- **It runs before the unconditional safing**, so a teardown that expects the
  fabric still wired up gets it. `hal::safeRig()` follows afterwards regardless.
- **A failing hook fails the run.** Setup returning `false` means no test runs at
  all; teardown returning `false` fails a run whose scripts all passed — a rig
  that didn't shut down the way the suite says it should is not a clean run.

### Bracket one group

A `GROUP` may bracket itself, with hooks written as rows of its own table:

```cpp
GROUP( Transient, "Tests validating DUT behaviour while a supply is disturbed")
    SETUP(    armTransient,    "Arm the scope on the 5V rail before the dropout")
    TEST( AcDropout, acDropoutScript, "Verify the 5V rail rides a loss of the primary AC input")
    TEARDOWN( disarmTransient, "Disarm the scope and restore the AC input")
END_GROUP
```

Declared and defined exactly like the run-level pair (`core::RunHook`, a
`[[nodiscard]] auto ... -> bool` in `suite/scripts.hpp`), optional and
independent per group, and at most one of each — a second is a redefinition
error where it is written.

The unqualified spelling belongs to this pair because of where it is written:
the group around it says what it brackets, so the name only has to say what it
is. `RUN_SETUP` carries its qualifier because it sits alone at file scope with
nothing around it to say. The two are not interchangeable and neither silently
does the other's job — a `SETUP` at file scope and a `RUN_SETUP` inside a group
are both compile errors, the first naming the mistake in the identifier it
fails to find.

Two things differ from `RUN_SETUP`/`RUN_TEARDOWN`, and they are the reason the
group-level pair exists at all:

- **They run only if something in their group was selected.**
  `--select=AcDropout` arms the `Transient` group and no other; a `--select`
  that names nothing in a group leaves that group's hooks untouched. This
  matters beyond wasted time: a group's hooks are there to establish a rig state
  *its* tests need, and imposing that state on a run of tests written without it
  is a different run than the one that was asked for.
- **They run once per pass, not once per run.** `--repeat=3` powers the rig up
  once (`RUN_SETUP`) and arms and disarms each selected group three times
  inside it.

Everything else matches the run-level pair, including the two properties that
matter most: the group's `TEARDOWN` is a guard destructor constructed *before*
its `SETUP` runs — so a setup that got half way is still undone, and a script
that throws still hits its group's teardown on the way out — and a failing hook
fails the run.

The one deliberate difference in failure handling: a failing `SETUP` skips
**its own group's** tests and carries on with the rest of the selection, where a
failing `RUN_SETUP` stops everything. A rig state one group could not establish
says nothing about another group's, and discarding the rest of the selection
would report less than the run actually knows. Both messages name what failed,
the group-level one including the group.

The shipped catalog declares no group hooks — `suite/test_catalog.inc` brackets
the whole run with `rigPowerOn`/`rigPowerOff` and its groups need nothing beyond
that — so a group that needs none stays exactly as it reads today.

### Run one script against several criteria groups

Taking no parameters doesn't stop one script body from serving several catalog
entries. Make the script a template and let `TEST` name an instantiation — that
is an ordinary identifier, so the macro needs no change:

```cpp
// suite/scripts.hpp
template<typename Criteria>
auto supplyRailScript() -> void;

// suite/scripts/supply_rail_script.cpp
template<typename Criteria>
auto supplyRailScript() -> void
{
    Verify( Criteria::FS_Supply_5V0, Measure( Dmm1.voltage(), at( dut::Output5V)));
    Verify( Criteria::FS_Supply_3V3, Measure( Dmm1.voltage(), at( dut::Output3V3)));
}

// suite/test_catalog.inc
GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
    TEST( SupplyRailA, supplyRailScript<FS_Supply_1>, "Rails on the A side")
    TEST( SupplyRailB, supplyRailScript<FS_Supply_2>, "Rails on the B side")
END_GROUP
```

What the catalog stores is still an `auto (*)() -> void`, so nothing about the
mechanism changes: the signature stays uniform, a group's assembled test array
stays homogeneous, and a misspelled group is still a compile error rather than a
runtime lookup miss. Each `Verify` logs the group name its own criterion carries, so the
two entries stay distinguishable in the report without the script being told
which one it is.

One preprocessor wrinkle: `TEST` takes three macro arguments, and a template-id
with more than one parameter contains a comma, so `supplyRailScript<A, B>` is
read as four. Give it a name first — `using RailsAB = ...;` or an alias template
— and pass that. A single parameter, as above, has no comma and is fine.

The limit is worth stating: the body still writes `Criteria::FS_Supply_5V0`, so
this reaches only groups that differ in their criteria's **values**, not in which
criteria they hold. That is the case worth having — it's the same code doing the
checking either way. A group of a different shape needs its own script.

This is not how tolerance variants work: swapping production/stress/aged is
`--criteria=`'s job (see **Running**) and changes no script and no catalog
entry. The template is for when one run has to check two groups side by side.

### Measure something that isn't routed

Not every reading travels through the switching fabric. A supply reports the
voltage it is holding and the current it is delivering over its own interface,
and on a rig whose matrix carries signals only, that is the *only* way to read
rail current — a rail at several amps is not a signal, and its output is
hard-wired for exactly that reason.

Such a reading takes no `at(...)`, because there is no pin to route it to:

```cpp
Apply( DcP1.dc().voltage( 24.0_V).currentLimit( 7.0_A));

const auto rail   = Measure( DcP1.measuredVoltage());
const auto inrush = Measure( DcP1.measuredCurrent());
```

It touches the fabric not at all, so it cannot disturb — or be disturbed by —
whatever else is currently routed. It still goes through the session seam, so it
injects and replays like any other reading; the key is
`"<instrument>.<quantity>"`:

```cpp
Measure.inject( "DcP1.Current", core::quantities::Current{ 6.4 });
```

`measuredVoltage()`/`measuredCurrent()` rather than `voltage()`/`current()`
deliberately: a supply has both a setpoint and a reading for each of those, and
`dc().voltage( 24_V)` sets while `measuredVoltage()` reads.

### Measure a current the matrix cannot carry

Two ways, depending on the bench:

**The source's own readback**, as above — no wiring, no routing.

**A shunt**, which is a voltage measurement at a pin plus Ohm's law. Declare the
sense pin as an ordinary `POINT` and convert in units, so the result is a
`Current` a criterion in amps can check:

```cpp
const auto drop = Measure( Dmm1.voltage(), at( dut::ShuntSense));
const auto load = drop / 10.0_mOhm;          // a Current, not a double

Verify( FS_Supply_1::FS_Load_Max, load);
```

What you should *not* do is route a DMM's current terminals through the matrix.
Signal relays are not rated for load current, and an ammeter closed onto a pin a
supply is driving is a short across that supply — see §1 on what the fabric does
and does not protect you from.

### Read a sensor that answers in the wrong unit

A thermistor or an LM35 on the DUT turns a temperature into a voltage precisely
so a voltmeter can read it. The bench measures volts; the spec is written in
degrees; the mapping between them belongs to that one part number and to nothing
else on the bench.

So it is neither a port nor an operator. There is no `Dmm1.temperature()` for a
meter that is in volts — `.range( 2_V)` would stop making sense, and the
recording would claim the bench read degrees when it read millivolts. And there
is no `Voltage → Temperature` conversion in the algebra: Ohm's law is there
because it holds for every resistor, while 10 mV/degC holds for one sensor, and
an operator would make the *wrong* curve a silent success instead of a compile
error.

It is a **transducer**: a named value, declared beside the point it belongs to.

1. **Declare the point** in `dut/adapter.inc`, as an ordinary `POINT` — a sensor
   fitted to a pin is not a claim that the pin has no other uses, so nothing
   here says "temperature":
   ```cpp
   POINT( CoolantSense, A, 2, 4, "LM35 coolant sensor output")
   ```

2. **Declare the sensor** next to it, in a DUT header:
   ```cpp
   namespace dut
   {
       constexpr auto CoolantSensor = core::quantities::Transducer<Voltage, Temperature>{
           "CoolantSensor", "LM35 on TP4 -- 10 mV/degC, 0 V at 0 degC",
           +[]( const Voltage sensed) -> Temperature { return Temperature{ sensed.value() * 100.0 }; } };
   }
   ```
   A plain function pointer, so the sensor is a `constexpr` value rather than
   something built at startup, and so a non-linear curve — Steinhart–Hart, a
   thermocouple polynomial — is simply a longer body.

3. **Read it, then derive**, in the script:
   ```cpp
   // Dmm2, not Dmm1: NPLC is the 6.5-digit L4411A's precision axis. Dmm1 is an
   // EDU34450A, which has no NPLC command -- see that driver's own README.
   const auto sense   = Measure( Dmm2.voltage().range( 2_V).nplc( 10), at( dut::CoolantSense));
   const auto coolant = dut::CoolantSensor( sense);        // a Temperature, not a double

   Verify( FS_Thermal_1::FS_Coolant_Max, coolant);
   ```

Two statements rather than one, deliberately: both units stay visible, so the
script says the bench read 0.612 V and the device is at 61.2 degC — which is
what actually happened. The criterion is written in the unit the datasheet uses:

```cpp
CRITERIA( FS_Thermal_1, "Thermal limits at full load")
    CRIT( FS_Coolant_Max, LT( 85_degC), "Coolant below 85 degC at full load")
    CRIT( FS_Coolant_Rise, LE( 40_K), "Rise over ambient within 40 K")
END_CRITERIA
```

**The conversion happens after the session, never before it.** `Measure` records
what the *instrument* returned, so `CoolantSense` stays keyed as a `Voltage` in
the recording and a `--replay` run derives the temperature from those same
volts. Recording the derived value instead would bake one calibration
permanently into the archive; recalibrate the sensor and yesterday's runs would
be uncorrectable. Injection keys the same way:

```cpp
Measure.inject( "CoolantSense", core::quantities::Voltage{ 0.612 });
```

The safety is in the signature and nowhere else. A `Transducer<Voltage,
Temperature>` takes a `Voltage` — hand it a `Current`, or the `Temperature` it
just produced, and it does not compile. Two sensors on one adapter are two named
values a reader can tell apart, which two lambdas at two call sites are not.

### Temperatures, and differences between them

A temperature is not a magnitude. There is no meaningful zero on the Celsius
scale — its zero is a convention about water — so a Celsius value is a *point*,
and the gap between two of them is a different kind of thing. Thorium says so in
the types:

```cpp
const auto ambient = dut::IntakeSensor(  Measure( Dmm1.voltage(), at( dut::IntakeSense)));
const auto hotspot = dut::CoolantSensor( Measure( Dmm1.voltage(), at( dut::CoolantSense)));

const TemperatureDelta rise    = hotspot - ambient;    // a span in K, not a degC
const Temperature      ceiling = ambient + 40.0_K;     // and back to a point

Verify( FS_Thermal_1::FS_Coolant_Rise, rise);
```

| Written | Result |
|---|---|
| `hotspot - ambient` | `TemperatureDelta` — a span in K |
| `ambient + 40.0_K`, `40.0_K + ambient`, `ambient - 40.0_K` | `Temperature` — a point again |
| `12.0_K + 8.0_K`, `20.0_K / 2.0` | `TemperatureDelta` — an ordinary magnitude |
| `ambient + hotspot` | **compile error** — two points have no sum |
| `ambient * 2.0` | **compile error** — twice 20 degC is 40 degC only by accident |

Nothing else in the framework changed shape for this. A unit declares its
difference unit on its own tag (`using DifferenceType = K_Type;`) and every unit
that declares none — every other unit here — keeps exactly the arithmetic it
always had: a `Voltage` minus a `Voltage` is still a `Voltage`.

What *did* change is that the predicates no longer assume arithmetic closes over
the type. `EQ`/`IN`/`LT`/… compare raw magnitudes internally (see
`detail::asDouble` in `core/criteria/predicates.hpp`), so a tolerance works
whatever a difference turns out to be. That is what leaves the algebra free to
produce new types — the same freedom `drop / 10.0_mOhm` uses to hand back a
`Current`. The unit safety is untouched, because it never lived in the
arithmetic: a predicate's `operator()` takes exactly its own type, so a `Voltage`
still cannot be checked against a criterion in degrees, nor be given a tolerance
in amps.

### Talk to the DUT over serial

A console dialogue is four verbs, and a route that stays open across all of them.

1. **Declare the interface** in `dut/adapter.inc` as a `BUNDLE` — one `LINE` per
   wire, the return included:
   ```cpp
   BUNDLE( Console, "RS232 debug console")
       LINE( Tx,   A, 2, 1, "console transmit, DUT to bench")
       LINE( Rx,   A, 2, 2, "console receive, bench to DUT")
       LINE( Gnd,  A, 2, 5, "console signal ground")
   END_BUNDLE
   ```
2. **Wire both halves** in `rig/wiring.inc` — `WIRE_INSTRUMENT` rows for the
   port's own channels, `WIRE_CONNECTOR` rows for the interface's pins. A routed
   instrument needs both, where a cabled supply needs `WIRE_SOURCE` instead.
3. **Write the dialogue**:
   ```cpp
   Connect( Ser1.rs232(), at( dut::Console));
   Setup(   Ser1.rs232().baudRate( 9600).wordLength( 8)
                        .parity( Parity::None).stopBits( StopBits::One));

   Write( Ser1.rs232(), "RD 30\r");

   const auto reply = Read( Ser1.rs232().terminator( "\r").timeout( 500ms));

   Verify( FS_Console_1::FS_Console_Ack,   reply.before( "\r"));
   Verify( FS_Console_1::FS_Console_Ready, reply.at( 4));

   Disconnect( Ser1.rs232(), at( dut::Console));
   ```

Five things in that are worth knowing.

**`Connect` takes the interface, not a pin.** All three lines close as one path.
An RS232 console is not usable a wire at a time, so making the bundle the unit
means a script cannot express the half-connected case; adding a line to the
bundle changes what `Connect` closes without touching a call site.

**The route is held open for the whole dialogue.** `Setup`, `Write` and `Read`
never touch the fabric. This is the one real difference from `Measure`, which
connects, reads and disconnects inside the single call: a reading is
instantaneous and independent, where dropping the path between a command and its
answer would break the exchange.

**`Setup` is not `Apply`.** Configuring a UART changes what a later `Write` means
and changes nothing at the DUT's pins, so it is a verb of its own — and the one the
oscilloscope uses for all of its trigger, timebase, acquisition and channel
settings. `Apply` on an instrument that has no output to energise is a compile
error rather than a call that silently does nothing, on a serial port and on a
scope alike.

**A reply too short to check is `Fail`, not a contrived `Verify`.** `reply.at( 4)`
is guarded in the real script — a silent DUT is a failed check, not a crash out of
`core::Bytes::at` — and what the guard's other branch records is *both* status
criteria, by name, as unchecked:

```cpp
Fail( FS_Console_1::FS_Console_Ready, reason);
Fail( FS_Console_1::FS_Console_Fault, reason);
```

```
verify  FS_Console_1::FS_Console_Ready  <unchecked>  bit 3 set  [FAIL]  console reply is 0 bytes, too short to hold a status byte
```

Recorded rather than skipped, because a report in which a check simply does not
appear reads as a run that did not need it. Recorded as *unchecked* rather than
failed, because a truncated reply is not evidence that the DUT is unready — it is
evidence of nothing, and the row says so while still stating what was required.
`Fail` is not a `Verb` of its own: it posts as a `Verify` with no value, so both
logs and every sink already handle it. See `core/criteria/verify.hpp`, which also records
why this is a verb rather than an always-false `FAIL` predicate.

**A reply is `core::Bytes`, not a `std::string`.** Length is its own fact, so an
embedded NUL is an ordinary byte; elements are `std::byte`, so a locale-aware
compare is a compile error; and the logs render a text payload as `"ACK\r"` and a
binary one as `<41 43 4B 0D 08>`, chosen per payload so a reader never has to work
out which half of a line is an escape.

Criteria hold an expected payload as a `bytes( "ACK")` pattern rather than a
`Bytes` — a `CRIT` entry is `static constexpr` and `Bytes` allocates — and bit
patterns as `MASK`, `BITS_SET`/`BITS_CLEAR`, or `BIT_SET<N>()`/`BIT_CLEAR<N>()`:

```cpp
CRITERIA( FS_Console_1, "Debug console dialogue over RS232")
    CRIT( FS_Console_Ack,    EQ( bytes( "ACK")),  "Console must acknowledge the status command")
    CRIT( FS_Console_Ready,  BIT_SET<3>(),        "Status bit 3 (READY) must be set once the rails are up")
    CRIT( FS_Console_Fault,  BIT_CLEAR<7>(),      "Status bit 7 (FAULT) must be clear")
END_CRITERIA
```

The bit number is a template parameter so `BIT_SET<9>()` against a `std::byte` is
a compile error; written as an argument it could only ever have been a criterion
that quietly never matched.

**Testing it without a bench** works exactly as it does for a reading, because
`Read` and `Measure` share one session bank:

```cpp
Read.inject( "Ser1.Data", { "ACK\r", "0xF5\r" });

EXPECT_TRUE( consoleScript());
```

`--record` and `--replay` cover both in one file, in one ordered stream — a
payload row carries `<bytes>` where a reading carries its unit, and its value is
unspaced hex so a reply containing a tab or a newline still round-trips.

### Capture a single-shot event

Every other reading in this framework is self-contained: `Measure` connects,
reads and disconnects inside one call. A triggered capture is not, because the
thing being captured happens *between* the halves and the script is what causes
it. That is the whole reason `Arm` and `Await` exist as two verbs.

```cpp
Setup( Osc1.trigger().edgeSource<3>().slope( TriggerSlope::Falling)
                     .level( 4.8_V).sweep( TriggerSweep::Auto));
Setup( Osc1.timebase().timePerDivision( 10_ms).reference( TimebaseReference::Left));
Setup( Osc1.acquisition().mode( AcquisitionMode::HighResolution).automaticPoints());
Setup( Osc1.channel<3>().input( ChannelInput::Dc1M).probeAdapter( ProbeAdapter::Div10)
                        .voltsPerDivision( 100_mV).verticalOffset( 5_V));

const auto baseline = Measure( Osc1.channel<3>().vbase(), at( dut::Output5V));

Arm( Osc1.single().timeout( 2_s));       // returns once the scope is ARMED

Remove(     AcP1.ac());                  // the event to capture
Disconnect( AcP1.ac());

const auto captured = Await( Osc1.single());

Verify( FS_Transient_1::FS_Transient_Captured, captured);
```

`suite/scripts/ac_dropout_script.cpp` is this written out in full.

**`Arm` blocks until the instrument is armed, not until it has been told to
arm.** Those are different moments, and the gap between them is where a
single-shot test silently fails — the event fires while the scope is still
setting itself up, nothing triggers, and the run reports a missing transient
that was in fact there. Keysight's own Infiniium programmer's reference gives
the sequence (`:SINGLE`, then poll `:AER?`) with the comment *"oscilloscope is
armed and ready, enable DUT here"* on exactly the line `Remove` sits on above.
So the ordering rule is **Arm, then cause the event**, and nothing enforces it.
Unlike Connect-before-Apply, breaking it is not even recorded: whether a scope
was armed in time is a fact about the hardware's own state machine, and the
framework has nothing to ask about it the way it can ask a supply whether its
output is on.

**`Await` returns `bool` rather than throwing on timeout.** "The transient never
arrived" is a finding about the DUT, not an error in the bench — very possibly
the thing the test was written to check — so it comes back as a value a criterion
is pointed at. It is `[[nodiscard]]`: an `Await` whose answer is dropped is a
script that waited and then measured whatever was in the acquisition buffer from
before.

**Check it before reading anything out of the capture.** A measurement taken
after a capture that never completed is a measurement of the previous one. It
will be a number, it will very likely be in tolerance, and it will mean nothing.

**Both verbs are generic.** They live in `core/verbs/acquire.hpp` and are named for the
operation, not the instrument: a transient recorder, a digitizer or a counter
with an armed gate is the same shape. `hal::keysight_dso8064a::DSO8064A` is simply the first driver
to answer to `armDriver`/`awaitDriver`, exactly as `hal::racal1260::Racal1260` was the first
to answer to `writeDriver`/`readDriver`.

**Testing it without a bench** works as it does for a reading and a reply — an
`Await` is an observation, so it goes through the same session bank:

```cpp
Measure.inject( "Output5V.Vbase", Voltage{ 5.00 });
Await.inject(   "Osc1.Acquisition", true);
Measure.inject( "Output5V.Vmin",  Voltage{ 4.88 });

EXPECT_TRUE( acDropoutScript());
```

`Arm` is deliberately absent from that list: it is stimulus, like `Setup` and
`Write`, so there is nothing for a replay to reproduce about it. A flag row in a
recording carries `<flag>` where a reading carries its unit.

Note the two measurement keys. An oscilloscope gives fifteen different answers
about one pin, so its ports name which one they are (`core::Port::qualifiedBy`)
and key as `"Output5V.Vbase"` rather than sharing one `"Output5V"` slot. An
ordinary DMM reading needs no such thing and is unaffected.

### Take the whole captured trace

`Measure` asks the instrument for one number about a record. `Fetch` takes the
record itself:

```cpp
const auto captured = Await( Osc1.single());

Verify( FS_Transient_1::FS_Transient_Captured, captured);

const auto trace = Fetch( Osc1.channel<3>().waveform());

Verify( FS_Transient_1::FS_Dip_Depth, trace.minimum<Voltage>());
```

What comes back is a `core::Waveform`: samples **already in a unit**, plus the
timebase that gives them their positions. Not raw ADC counts with a preamble to
undo them — the scaling is the driver's job, and a recording holding raw counts
would be unreadable without the instrument that wrote it. That is the same
choice `Measure` already makes one number at a time.

**There is no criterion that takes a trace.** A criterion is a stated limit a
test specification carries, and "the waveform is correct" is not one. So a
script reduces first — `minimum<Voltage>()`, `maximum`, `peakToPeak`, `mean`,
all of them explicit about the unit and checked against the trace's own — and
checks the number. For anything a reduction does not cover, the samples are
there:

```cpp
const auto settled = std::ranges::all_of(
    trace.samples() | std::views::drop( 500),
    []( const auto v) { return v > 4.9; });
```

**Check the `Await` before fetching.** A trace taken after a capture that never
completed is the *previous* acquisition — it will look like a perfectly good
trace. Nothing enforces the order, exactly as nothing enforces Arm-before-event.

**A trace never appears in a log.** Both logs get a summary —
`4096 pts @ 1 us, 0.4 V pk-pk` — and never the samples. There is no length at
which spelling out four thousand numbers is what a reader wanted; the trace
itself is in the recording, at full precision, which is what `--replay` reads.

**Testing it without a bench** is the same as for the other three, and each
channel has its own key, since a four-channel scope holds four records at once:

```cpp
Fetch.inject( "Osc1.Channel3", capturedEarlier);
```

`Fetch` is generic and lives in `core/verbs/trace.hpp` — a transient recorder, a
digitizer or a logger with a memory behind it hands back the same shape.
`hal::keysight_dso8064a::DSO8064A` is simply the first driver to answer to `fetchDriver`.

### When an instrument cannot make the measurement

A real instrument has a third answer besides a number and a fault: *"I could not
measure that."* An Infiniium returns `9.99999E+37`, and with `:MEASure:SENDvalid`
on it also returns which of some thirty specific things went wrong — no edge on
the trace, the waveform clipped, top and base equal.

A driver reports this by throwing `core::UnmeasurableReading` carrying the
instrument's own words. `Measure` catches it, and the default is **NaN**:

```cpp
const auto rise = Measure( Osc1.channel<3>().riseTime(), at( dut::ClkProbe));
// nan, if there was no edge -- and the log says why
```

NaN is chosen rather than defaulted to. It compares false against every
predicate, so the criterion beneath it fails, the instrument's reason is
recorded beside the point name, and the run carries on to the next check.
Throwing out of `Measure` would abandon every later check in the script over one
reading the instrument declined to make.

Where a script has a *specific* meaning for the absence, it says so on the line
that asks the question:

```cpp
Osc1.channel<3>().vmin().whenUnmeasurable( []{ return 0_V; })

Osc1.channel<3>().vmin().whenUnmeasurable( []( auto reason)
{
    // No excursion is a dip of zero. A clipped trace is a bench fault, and
    // should still fail.
    return reason.contains( "min not found") ? 0_V : Voltage{ NAN };
})
```

A callable and not a value, deliberately: the substitution is a decision, and a
decision deserves somewhere to put its reasoning. `.whenUnmeasurable( 0_V)` would
read as "this measured zero", which is the one thing it must never be mistaken
for.

This replaces the legacy ATE's `ISINVALID()` predicate, which answered only
*whether* a reading failed and threw away *why* — and did it in an `if` block
several lines below the measurement it applied to, where it silently applied to
whatever else was in the variable by then.

### Add a unit of measurement

Three edits, all genuine vocabulary — here is `Decibel`, had it not existed:

```cpp
// framework/core/include/core/quantities/quantity.hpp
struct dB_Type { static constexpr std::string_view Symbol = "dB"; };
using Decibel = Quantity< dB_Type>;
constexpr Decibel operator""_dB( long double v) { /* ... */ }   // optional

// framework/core/include/core/quantities/quantity_kind.hpp -- the enumerator, named to match
enum class QuantityKind { /* ... */ Decibel };
```

The variant, both kind↔type mappings, the symbol lookup, `to_string` and the
recording round-trip are all generated from those. Add the enumerator without the
alias and a `static_assert` tells you exactly what is missing.

A unit whose *differences* are in a different unit says so on its tag, and needs
nothing else — see `degC_Type`, and the two sections above on what that buys:

```cpp
struct degC_Type { static constexpr std::string_view Symbol = "degC"; using DifferenceType = K_Type; };
```

Two optional lines are worth knowing about, because both are easy to leave out
and neither fails the build. A tag may declare an `SiPrefixRange`, which is what
makes a value render as `470 uF` rather than `0.00047 F` — see `F_Type`, whose
span is the widest here. And `core::requiresDeadNode` in
`core/verbs/interlock.hpp` decides whether a reading of the new kind may be
routed onto a live rail: it is a question about *sourcing into the node*, not
about the instrument, and a kind that charges or drives what it measures belongs
in it. `framework/core/tests/verbs/test_interlock.cpp` iterates every enumerator,
so a new kind fails there until somebody decides which side it is on.

### Add a script unit test (no hardware)

`suite/tests/` — inject readings and run the script. No rig required:

```cpp
Measure.inject( "Output12V", core::quantities::Voltage{ 12.05});

EXPECT_TRUE( thermalRampScript());
```

A single injected value is *sticky*: the point reads 12.05 V however many times
the script measures it. To make a point read differently on each measurement,
inject a sequence instead — which is what makes a repeated run testable without
hardware:

```cpp
using namespace core::literals;

Measure.inject( "Output12V", { 12.05_V, 12.06_V, 11.80_V });   // a braced list
Measure.inject( "Output12V", std::move( capturedRail));        // any owned range
Measure.inject( "Output12V", rampingRail( 12.0_V, 0.01_V));    // a std::generator
```

All three arrive as one `core::ValueSource` (see `core/session/session.hpp`), so the
caller picks the algorithm rather than the framework offering a menu of them.
A coroutine is the general case:

```cpp
auto rampingRail( Voltage from, Voltage step) -> std::generator<Voltage>
{
    for( auto v = from; ; v = Voltage{ v.value() + step.value() })
        co_yield v;
}
```

That also settles what happens when the values run out, without the framework
deciding it: a finite sequence **throws** once exhausted — measuring a point
more times than the test authored values for it means the script diverged from
what was expected, which is worth failing on — while a generator that never
ends never runs out. Neither is privileged.

`Measure.load( path)` replays a recording captured from a real run instead — the
file `run_scripts --record=` writes, which is the same thing `--replay=` reads.
`core::injectStimulusFromFile( Measure.sessions(), path)` arms the same bank
from an authored stimulus file, so a test whose scenario is long enough to be
worth reviewing on its own can live beside the test rather than inside it.
`Measure.useLive()` goes back to hardware.

For one trace rather than a whole scenario, `core::traceFromFile( kind, timing,
path)` reads a plain file of numbers into a `Waveform` — the same file, read by
the same reader, that a stimulus line's `trace( V, ..., "dip.samples")` names:

```cpp
Fetch.inject( "Osc1.Channel3",
              core::traceFromFile( core::quantityKindOf<Voltage>(),
                                   { -1e-3_s, 1e-06_s }, "dip.samples"));
```

`core::readSamples( path)` is the numbers alone. Both name the file in every
failure they report — including an empty one, which is a truncated export
rather than a trace of nothing.

---

## 4. Running a suite

```bash
cmake --preset macos-debug          # or windows-debug
cmake --build build/debug
build/debug/bin/run_scripts --dut-serial=SN-000123
```

Presets are a platform × build-type matrix — `macos-debug`, `macos-release`,
`windows-debug`, `windows-release` — composed from two hidden halves so that a
compiler path is written once rather than once per build type. The platform half
picks the compiler -- the only place in the tree one is named -- and pins the
preset to its host; the build-type half picks
`Debug`/`Release` and the directory, `build/debug` or `build/release`. Both
platforms build with the same generator (Ninja) into the same directory, so
every command in this README works unchanged on either.

`cmake --list-presets` shows only the pair matching the host: each platform half
carries a `condition` on `${hostSystemName}`, so the Windows presets are
invisible on macOS and vice versa, and naming the wrong host's preset explicitly
is an error rather than an attempt to run MinGW paths through Homebrew's GCC.

**Ninja is a prerequisite on both hosts**, and has to be on `PATH` when the
preset is configured. CMake resolves a generator's build program before it reads
a line of this project, so a missing ninja fails with `unable to find a build
program corresponding to "Ninja"` and nothing more helpful — no compiler check,
no version banner, no mention of a preset:

```bash
brew install ninja                  # macOS
winget install Ninja-build.Ninja    # Windows -- some MinGW distributions
                                    # already ship one in C:/mingw64/bin
```

An IDE is the usual reason this bites on one host and not the other: CLion (like
Visual Studio) configures with a ninja it bundles, so a build tree the IDE
maintains happily can be unbuildable from a terminal on that same machine. To
borrow the IDE's copy instead of installing one, name it once — it lands in that
tree's cache and later configures of the tree reuse it:

```bash
cmake --preset macos-debug -DCMAKE_MAKE_PROGRAM=/path/to/bundled/ninja
```

Note that `-Werror` applies to Release as well, so an optimiser-only warning
fails that build rather than being reported.

| Flag | Effect |
|---|---|
| *(none)* | run every test in the catalog |
| `--list-tests` | print `group\|id\|description` per test, run nothing |
| `--select=a,b` | run only these test ids, in catalog order |
| `--criteria=NAME` | apply that tolerance variant — `production`, `stress`, `aged` |
| `--repeat=N` | run the selection N times over |
| `--until-failure` | stop as soon as a pass fails |
| `--safe` | drop the rig to idle and exit — no test, no log |
| `--record=PATH` | write every reading the run took, in order (heavy payloads go to `PATH.d/`) |
| `--replay=PATH` | take every reading from that file instead of the rig (honours `--select`) |
| `--skeleton=PATH` | write out every reading the scripts ask for, against placeholders |
| `--inject=PATH` | take readings from a stimulus file — authored, not captured |
| `--dut-serial=`, `--operator=` | traceability, into both logs |
| `--log-dir=`, `--sarif=`, `--rtf=` | where the logs go |
| `--quiet`, `--no-logs`, `--no-color` | suppress the console, the files, the colour |

Exit code is 0 only if every selected test passed.

**Choosing the tolerances.** Every variant is compiled in, so one binary serves
all of them:

```bash
build/debug/bin/run_scripts                       # the build's default variant
build/debug/bin/run_scripts --criteria=stress     # stress-chamber tolerances
```

An unknown name is fatal and lists the ones that would have worked — a runner
that quietly fell back to the default would apply the wrong tolerances to real
hardware and hand back a log that looks entirely normal. The choice is frozen
when the journal opens, so the variant named in both logs' header is provably
the one every check in them was made against.

**Repeating a run.** What repeats is the *selection*, not each script:
`--select=A,B --repeat=3` runs `A B A B A B`, never `A A A B B B`. That is the
unit a soak run cares about — "the tests, again" — and it is also the unit
`RUN_SETUP`/`RUN_TEARDOWN` bracket.

```bash
build/debug/bin/run_scripts --repeat=50                    # fifty passes
build/debug/bin/run_scripts --until-failure                # until something breaks
build/debug/bin/run_scripts --repeat=50 --until-failure    # at most fifty, stop on failure
```

`--until-failure` on its own has no bound, deliberately: how many passes a DUT
survives is what such a run is trying to find out, so requiring the number up
front would be requiring the answer. A repeated run is still **one** run — one
report, one exit status, passes marked in the machine log, and any failing pass
fails the whole thing.

**Recording and replaying the readings.** A third artifact, and the only one
that is an input as well as an output:

```bash
build/debug/bin/run_scripts --record=readings.tsv      # capture what the rig read
build/debug/bin/run_scripts --replay=readings.tsv      # run again off the file, no rig
```

Where the two logs describe a run, this is the readings themselves, in order
(see `core/session/recording.hpp` for the format — flat TSV, one row per reading, each
carrying the test that took it). It is
what reproduces a bench failure at a desk: the replayed run takes its verdict
from the file, so it passes or fails exactly as the recorded one did, with no
instrument attached.

All four kinds of observation go into that one file, in the order they happened
— a rail voltage, a serial reply, whether a capture landed, a whole trace —
because a replay that armed some seams and not others would take its voltages
from the file and ask absent hardware for the rest.

**A recording is one file plus, sometimes, a directory beside it.** Anything too
large to keep in a row — a multi-kilobyte frame, a captured trace — is written
to `PATH.d/` and referred to from the row by a name derived from its own
contents:

```
12  1787491956557  Ser1.Data       Ser1  <bytes>  41434B0D
13  1787491956558  Osc1.Channel3   Osc1  <trace>  Voltage  -0.001  1e-06  @3F0A19C4D2E7B815.wfm
```

Both halves move together: `--replay=PATH` finds the directory from the file's
own name, so copying a recording off the bench means copying both, and a file
whose directory was left behind fails loudly rather than replaying half a run.

Naming the blobs after their contents rather than after the row that wrote one
is what keeps the file diffable, which is the whole reason it is flat text: a
sequence-numbered reference would be byte-identical between a run whose DUT
answered correctly and one whose DUT did not. It also means a run that reads the
same frame fifty times stores it once. A trace keeps its unit and timebase *in*
the row for the same reason — two runs whose captures differ only in sample rate
should differ visibly, without opening anything.

**The rows are written as the run takes them,** not accumulated and dumped at the
end, so a run that is killed part-way leaves the readings it had already taken.

**Every recording says which tests its run was asked for,** on the first line:

```
# select=SupplyRail,StatusRegister
```

or `# select=<all>` for a run given no `--select`. Provenance for a person — a
recording is as long as the run is, so *whole run or somebody's narrow capture?*
is the first question about a file that arrived from a bench, and the rows do not
answer it without reading all of them.

It is a comment, so `--replay` skips it like any other, and nothing reads it
back. That line is deliberate: the refusal below keys on the **rows**, because
the rows are what a replay consumes. A header can be edited, copied between
files, or outlive what it described — it says what the run was *told* to do,
while the rows say what it *did*.

**One test out of a whole run.** Every row says which test took the reading, so
`--replay` combines with `--select` — capture broadly on the bench, then debug
one script at a desk:

```bash
build/debug/bin/run_scripts --record=readings.tsv                      # the whole run
build/debug/bin/run_scripts --replay=readings.tsv --select=SupplyRail  # one script of it
```

The selected test gets *its own* rows. That is the whole point of the column:
readings are queued per DUT point, so without it a replayed test dequeues from
the front of each point's queue and takes whatever the first test to touch that
point recorded. Two tests measuring one rail is all it takes, and the result is
not an error — it is a green verdict about the wrong numbers.

Readings taken outside any test carry `<run>` in that column instead of a test
id, and no selection filters them out: `RUN_SETUP` and `RUN_TEARDOWN` bracket
every selection, so a replay of one test still needs the readings that powered
the rig up. A group's own hooks read the same way — they too run outside any
test bracket — and a replay of one test brings its group's hooks with it, since
selection decides which groups are bracketed at all. A recording with no rows for the selected tests is refused up front, naming
the tests it does hold — the alternative is a first `Measure` complaining about a
point name, which reports the symptom and leaves the cause to be guessed.

Opt-in, unlike the logs, and for the opposite reason: a log is evidence that a
run happened and every run should leave one, while a recording is a tool for a
particular investigation and is as long as the run is — a fifty-pass soak
records fifty passes, and replaying it needs `--repeat=50` to match.

The two are exclusive. Recording a replay would faithfully write out the values
it had just been fed, handing back a file that looks like a fresh capture and is
a copy of its input. Both are also fatal if the path cannot be opened, checked
before anything is measured — a caller who asked to record a run and silently
got none, or asked to replay one and silently got live hardware, has been told
the run did something it did not.

### Readings that were never on a bench

`--record`/`--replay` capture and reproduce a run that happened. Two more flags
are for the run that has not: a DUT that does not exist yet, a fault that is
hard to provoke on the bench, a rig in another building.

```bash
build/debug/bin/run_scripts --skeleton=skeleton.tsv    # what do the scripts read?
build/debug/bin/run_scripts --inject=healthy.stim      # here is what they read
```

**`--skeleton` answers the question that blocks authoring anything.** A script's
session keys — `Output5V.Vbase`, `Osc1.Acquisition`, `Ser1.Data`,
`AcP1.A.Voltage` — come from `core::Port::qualifiedBy`, from each engine's
`"<instrument>.<what>"` rule and from the DUT adapter, and are written down
nowhere a person can read them. This runs the selection against a session that
touches nothing and answers everything, and writes what the scripts *asked for*:

```
# select=<all>
# Skeleton written by run_scripts --skeleton. This is a valid --replay file.
# Every value below is a PLACEHOLDER -- no instrument was touched. ...
0	1787494495696	<run>	AcP1.A.Voltage	AcP1	Voltage	0
...
10	1787494495696	AcDropout	Osc1.Acquisition	Osc1	<flag>	1
12	1787494495696	StatusRegister	Ser1.Data	Ser1	<bytes>
```

Edit the value column and it replays. It writes **no logs** and its exit status
is about the file rather than the checks: a run that read placeholders tested
nothing, and must not leave an RTF with a DUT serial in its header saying it
did. One honest limit — a script whose control flow depends on what it read has
more than one path, and a skeleton is the one the placeholders produce.

**`--inject` takes the readings from a stimulus file** ([`core/session/stimulus.hpp`](framework/core/include/core/session/stimulus.hpp)),
which is a different format from a recording on purpose:

```
# The DUT as it should behave. Keys from --skeleton.
Output5V          = 5.01 V                  # sticky: however often it is read
Output5V.Vmin     = 4.85 V, 4.70 V          # a list: one per read, in order
Osc1.Acquisition  = true
Ser1.Data         = <41 43 4B 0D 08>
Osc1.Channel3     = trace( V, -0.001 s, 1e-06 s, "dip.samples")
```

Two rules carry the whole format. **One value is sticky; a list is a sequence**
— the distinction `Measure.inject( "Output5V", 5.01_V)` and
`Read.inject( "Ser1.Data", { ... })` already draw, spelled the same way. This is
the thing a recording cannot express: every value in one is consumed once, so
`--repeat=50` needs fifty passes' worth of rows, where the sticky line above
answers a run of any length. And **a value is spelled the way a log spells it**
— the payload forms are `core::describeValue`'s output character for character,
so a reply can be read off a log line and pasted straight in.

The unit symbols are exactly `core::quantities`' own — `V A W VA Ohm s dB Hz
var`, and nothing at all for the dimensionless one. No prefixed spellings: the
file's vocabulary *is* that list, so there is no second table to keep in step,
and `1e-06 s` says what `1us` would have to be translated into anyway.

Traces are **described, not encoded** — samples inline for a short one, or a
plain file of numbers beside the stimulus file for a real one. There is no
`ramp()`/`pulse()`/`sine()` vocabulary, deliberately: those would be a
specification language invented here covering whichever shapes came up first,
where a list of numbers covers every shape and is what a scope export, a
spreadsheet or a model already produces.

`--inject` layers over `--replay`, which is why both exist — it is how a
captured failure gets re-run with one reading changed, to find out whether that
reading was the cause. It is exclusive with the two modes that *write* a file,
since injected readings written into one would be indistinguishable from
observed ones.

A bad line is fatal before anything is measured, and says where it is:

```
Could not read the stimulus file: stimulus line 2: 'Volts' is not a unit symbol
  -- in 'Output3V3 = 3.3 Volts'
```

### None of these three touch the rig

`--replay`, `--inject` and `--skeleton` all **detach the bench**: no `Apply`,
`Connect`, `Setup`, `Write` or `Arm` reaches an instrument, and neither does the
safing pass on the way out. One sentence behind all three — *a run whose
readings are fiction must not be driving hardware.*

That was half true before and stated as though it were whole. The observing
verbs have always honoured it, because a driver call lives inside a callback a
scripted session never invokes. The instructing verbs had no such seam and
called their drivers unconditionally, so a replayed run took its readings from a
file and then energised rails for real — survivable only because every driver in
this repo is still simulated.

**The report says so in its header**, rather than leaving a reader to notice
`--replay=` on the command-line row two lines below:

```
DeviceX -- Sun 23 Aug 2026 18:53:45 CEST
DUT serial        SN-000123
Operator          thomas
Criteria          production
Criteria master   production
Bench             DETACHED -- no instrument was touched
Framework         Thorium 0.1.0
```

Present on every run, because the header is a form and both answers matter — an
ordinary run's row reads `Bench  attached`, which is the reassurance half. It is
also the one metadata row that raises its voice: every other is deliberately
quiet, and a detached bench is exactly "something a reader must notice that is
not a failed check". A detached run's checks can all pass, and what they passed
about is a file. The SARIF header carries it as a boolean, `"benchAttached":
false`, so a consumer can filter those runs out without parsing a command line.

The instruction is still **logged** too, and says it went nowhere:

```
Apply DcP1 = voltage=28 V, currentLimit=7 A -- not performed -- no bench attached
```

Posted rather than suppressed, because a routing step omitted for brevity is
exactly the one that explains a failed reading — and posted *marked*, because an
unmarked one would state that 28 V went onto a rail that was never energised. It
costs the human report nothing: that report shows only the verbs that observed
something, so the marker appears solely in the machine log, which is where
per-event precision is the point.

**Safing is skipped too, and that is the sharp end of it.** Safing an unattached
bench would be the single instruction in the run that *did* reach real hardware
— a `--replay` at a desk opening the relays of whatever rig the runner happened
to be pointed at. A detached run never closed a relay or turned an output on, so
there is nothing in a state to return to idle. `--safe` is unaffected: it runs
before any of this and is invoked precisely because the caller wants it to reach
the rig.

There is no `--offline` flag, because there is no fourth combination worth
having. A detached run with live readings has nowhere to get them
(`core::LiveSession` refuses rather than reaching for an instrument), and an
attached run with described readings would put a number no instrument produced
into a log beside a rail that really is energised.

Detachment is deliberately **not** the same fact as injecting readings. A script
unit test injects its values *and* wants `Apply` and `Connect` to reach their
drivers — the simulated instrument state and the fabric are what such a test
then asserts on. Injecting is about where a value comes from; attachment is
about whether an instruction goes anywhere, and only something that knows the
whole run is in a position to say the second one.

`tools/run-tests.sh [build-dir]` is the tester-facing picker: choose a group, then
one, several or all of its tests. It offers the catalog and nothing else — unit
tests are the build system's business, deliberately not reachable from there.

### The two logs

Both are written by default, named for the run's start time, and both carry the
same traceability header — DUT, serial, operator, criteria variant, the master
that variant inherited its unchanged criteria from, whether a bench was
attached, framework version, content revision, UTC instant, command line.

The master is there because the variants are not independent tables: a `stress`
run was held to stress's tolerances for the rows stress changes and to the
master's for every row it does not, so the applied name alone does not say which
numbers were in force. It is a row of its own even when it repeats the applied
variant — the header is a form, and a field that appears only sometimes is one
nobody comparing two reports can rely on finding. The machine log carries it as
`"criteriaMaster"`, beside `"criteriaVariant"`.

**`*.rtf` — for people.** Colour-coded, grouped the way the catalog is,
descriptions in grey, each check stating what was measured *and what was
required*. It carries what the run **observed** — `Measure`, `Read` and `Verify`
— and not what the bench was told to do:

```
DeviceX -- Sat 01 Aug 2026 11:13:16 CEST
DUT serial        SN-000123
Criteria          production
Criteria master   production
Bench             attached
Suite/DUT/rig     5a4052f
--------------------------------------------------------------------
setup Bring the AC input and the three DC supplies up, checking each rail as it comes
	measure DcP1.Voltage                28 V        (DcP1)  instrument readback
	verify                              28 V        = 28 V +/-0.1 V   [PASS]  ...

OutputVoltage Tests validating DUT output voltage rails
	SupplyRail Verify supply rail voltages via matrix
	measure Output5V                    5.021 V     (Dmm1)  5Vdc supply port
	verify  FS_Supply_1::FS_Supply_5V0  5.021 V     = 5 V +/-0.05 V   [PASS]  ...
	RESULT  SupplyRail                                                [PASS]

teardown Take the supplies back down in the inverse order and open the fabric
```

A hook gets a heading of its own — its id, then the description its catalog line
gave it, exactly the way a group and a test are headed. It sits at the level of
what it brackets: a `GROUP`'s own `SETUP`/`TEARDOWN` is indented with that
group's tests, the catalog's `RUN_SETUP`/`RUN_TEARDOWN` unindented with the
groups. Without the heading, the rails `RUN_SETUP` checked read as belonging to
no test at all; without the description, every one of them would read `setup`.

It is a **valid, openable RTF document after every logged event** — the closing
brace is rewritten and seeked back over on each flush — so it can be read while a
long run is still going, and a killed run leaves a readable log.

**`*.sarif` — for tools.** SARIF 2.1.0, *every* verb including the routing steps
and the safing pass, because a routing step omitted for brevity is exactly the one
that explains a failed reading. Each criterion becomes a SARIF rule keyed
`FS_Supply_1/FS_Supply_5V0` — its own group and id, the pair a test spec traces
to — so "results for rule X" means "every time this requirement was checked",
across runs and DUTs. Values carry both formatted text and a bare number.

It also carries the **catalog it walked**. Entering a group, a test, or a
`SETUP`/`TEARDOWN` bracket is itself a result — informational, level `none`,
exactly like the `pass 2 of 3` note a `--repeat` run leaves — under the rules
`Thorium/Group`, `Thorium/Test` and `Thorium/Phase`:

```json
{ "ruleId": "Thorium/Test",  "kind": "informational",
  "message": { "text": "Test SupplyRail -- Verify supply rail voltages via matrix" },
  "properties": { "boundary": "Test", "group": "OutputVoltage",
                  "test": "SupplyRail", "title": "Verify supply rail voltages via matrix" } }
```

A hook's id is `setup` or `teardown`, its `title` is the description its catalog
line gave it, and which level it is is the group that qualifies it:
`OutputVoltage/setup` is that group's own `SETUP`, plain `setup` is the
catalog's `RUN_SETUP`. Every event a hook posted carries `"phase":
"setup"` too — without it, the readings a group's setup takes are
indistinguishable here from its first test's, since a hook's events deliberately
carry no test id (that is what keeps them in a `--replay` of one test; see
`core/session/recording.hpp`).

That is the only place the titles from `GROUP`/`TEST` appear at all, and it is
what lets a group whose every test was deselected still say it was reached.

The live console view renders the same content as the RTF from the same events, so
what an operator watched and what the report says cannot disagree.

**`--record=` writes a third file, and it is not a log.** Flat TSV, one row per
reading — `sequence, wallClockMillis, test, point, instrument, kind, value` — carrying
nothing a person wants and nothing a report needs, because its only reader is
`--replay=`. Neither log can serve that purpose: they describe a run, in formats
built for a person and for a SARIF server, while a replay needs the values
themselves in the order they were taken and nothing else.

That split is also why the logs are allowed to be lossy about a large value and
the recording is not. A payload longer than a log line renders abridged with its
true length beside it (`"BOOT v2.1 OK ..." (413 bytes)`), a trace renders as a
summary and never as its samples, and every value column in both logs is bounded
regardless of which verb produced it. None of that touches the recording, which
holds every octet and every sample.

---

## 5. Build options

```bash
cmake --preset macos-debug -DTHORIUM_CRITERIA_VARIANT=stress
```

| Variable | Default | Meaning |
|---|---|---|
| `THORIUM_KNOWN_CRITERIA_VARIANTS` | `production stress aged` | which tolerance tables are compiled in. Each needs a matching `dut/criteria_<name>.inc`, checked at *configure* time |
| `THORIUM_CRITERIA_VARIANT` | `production` | which of them applies when `--criteria=` is not given. A typo fails the *configure* step |
| `THORIUM_CRITERIA_MASTER` | `production` | which of them is the master: the table `CRIT_FROM_MASTER` borrows from and the merged criteria are generated from. Any variant will do, provided it spells every criterion out with `CRIT` (checked at *configure* time) |
| `THORIUM_DUT_NAME` | `DeviceX` | DUT name in both logs |
| `THORIUM_RIG_NAME` | `thorium-rig-1` | bench name in the machine log |
| `THORIUM_SUITE_VERSION`, `THORIUM_DUT_VERSION`, `THORIUM_RIG_VERSION` | `git describe --always --dirty` | content revision in both logs |
| `BUILD_TESTING_LAYERS` | `ON` | build each layer's tests |
| `THORIUM_INSTRUMENT_TABLE`, `THORIUM_WIRING_TABLE`, `THORIUM_DEVICE_TABLE` | this repo's `rig/` | the three paths a *separate* rig repo would point at its own data |

The last three are how `framework/hal` stays generic: it is compiled against whichever
rig supplied those paths, so an installed `libhal.a` is only ever valid for that
one rig — a different bench builds hal from source against its own tables.

**Requires GCC 16+** for C++26 reflection (`-freflection`) and contracts
(`-fcontracts`). Reflection is not decoration: it is what removes the
hand-maintained parallel lists — enum names, criteria iteration, the safing sweep,
the quantity tables — that would otherwise each be a place to forget something.

The requirement is checked at configure time, right after the compiler banner,
because the alternative is an `unrecognized command line option '-freflection'`
from the middle of a translation unit. The usual way to trip it is configuring
without a preset — a bare `cmake -B`, or an IDE profile that manages its own
build directory and its own toolchain — since the presets are what name GCC in
the first place.

---

## 6. Tests

```bash
ctest --test-dir build/debug --output-on-failure     # everything
ctest --test-dir build/debug -LE acceptance          # unit tests only
ctest --test-dir build/debug -L  acceptance -V       # the CLI tour
ctest --test-dir build/debug -N                      # count and name them, run nothing
```

| Target | Covers |
|---|---|
| `core_tests` | units, predicates, criteria, sessions, journal, all three log sinks |
| `hal_tests` | hal's own generic mechanism — switching fabric, wiring, VPC locations, adapter macros, addresses |
| `<model>_tests` | one per driver directory, each linking its own driver alone (`l4411a_tests`, `dso8064a_tests`, …) |
| `rig_tests` | the integration tests that need this rig — several instruments at once, safing, `describeConfig` reaching the journal |
| `dut_tests` | the DUT profile — including the wiring-coverage build check |
| `scripts_tests` | the test scripts, injected; plus the variant parity build check |
| `acceptance_tests` | the `run_scripts` binary as a subprocess — flags, log files, exit codes |

No test counts in this section, deliberately. `-N` prints the current ones —
`Total Tests:`, and it honours `-L`/`-LE`, so the split costs one more command
rather than a table that has to be trusted. A count written down here is a claim
about a number that changes every time anyone adds a test, and it went stale
exactly that way: it advertised 317 for long enough that the real figure passed
490.

The acceptance tests are worth knowing about separately: they run the real binary
with real flags and keep everything under `<build>/framework/runner/acceptance/<suite>.<test>/` —
the exact invocation, the console transcript, and both log files. A failing
assertion names the file to open, and a passing run still leaves specimens of both
log formats to look at. Every invocation is printed as it runs, so
`ctest -L acceptance -V` doubles as a worked tour of the CLI.

---

## 7. Conventions worth knowing before editing

- **Comments say *why*, not what.** Most non-obvious decision in this codebase
  carries the reasoning that produced it, including the alternatives rejected and
  what went wrong with them. That is deliberate: the *why* is the part that cannot
  be recovered from the code, and it is what stops a later reader "tidying" a
  load-bearing choice away.
- **Content is data, mechanism is code.** `.inc` files are flat, macro-driven
  tables meant to be readable by a test engineer — close to what a spec
  spreadsheet looks like. The machinery that interprets them lives in `framework/`.
- **Guards are not simplicity.** Where a parallel list can be *derived* instead of
  asserted, derive it. A `static_assert` that catches a forgotten edit is strictly
  worse than a design with nothing to forget.
