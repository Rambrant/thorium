# Thorium

A framework for writing automated hardware test suites in C++26, for a bench of
real instruments testing a real device.

A test script reads like the test specification it came from:

```cpp
const auto rail = Measure( Dmm1.voltage(), at( dut::Output5V));

allPassed &= Verify( FS_Supply_1::FS_Supply_5V0, rail);
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

Eighteen classes of mistake, none of which can reach the bench:

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
| `Osc1.channel<5>()` on a four-channel scope | no valid instantiation |
| An instrument driver with no `safe()` | `static_assert` naming the type |
| A new `QuantityKind` with no matching `Quantity` alias | `static_assert` with the fix |
| A new source instrument with no `describeConfig` | no matching function |
| Sense leads wired without the matching force leads | `dut_tests` fails to build |
| `.epsilon()` on a type a tolerance is meaningless for | constraint not satisfied |
| Misspelled bundle — `at( dut::Consol)` | no such member |
| `Apply( Ser1.rs232())` on a port with no output to energise | no matching `applyDriver` |
| `BIT_SET<9>()` against a `std::byte` | constraint not satisfied |

Two of those deserve a second look, because they are the interesting ones.

**Wiring coverage.** `dut/tests/test_wiring_coverage.cpp` has no runtime
assertions at all — the point is that it *compiles*. It reflects over every
`POINT` in the DUT adapter and asserts each one has a matching connector-wiring
entry. Adding a point and forgetting to wire it breaks the build, not a test run
three weeks later. Same trick for tolerance variants in
`suite/tests/test_criteria_variants_compile.cpp`.

**Criteria variants.** Production, stress-chamber and aged-equipment tolerances
are three separate tables, **all compiled into one binary** and chosen per run
with `run_scripts --criteria=stress`. A criterion whose value is unchanged from
production is written `CRIT_FROM_PRODUCTION`, so the number lives in exactly one
place — and a typo in the reference is a hard compile error, not a silent
fallback.

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
  runtime throw. See the comments in `libs/hal/include/hal/wiring.hpp`.
- **Scripted-session unit mismatch.** Feeding a `Current` to a point declared
  `Voltage` throws, naming the point and both kinds.
- **`--select` with an unknown test id** does not run that test rather than
  failing. `tools/run-tests.sh` validates ids against the catalog before it gets
  that far, but a hand-typed flag has no such help.
- **Instrument I/O.** Real instruments are absent, mis-addressed or lying. That
  is what the runtime is for.
- **Electrical safety of a route.** `SwitchFabric::connect()` is additive and
  reference-counted, and it refuses nothing. Two instruments sharing a pin is
  the intended behaviour — that is how a rail is measured while a supply holds
  it — so connect/disconnect symmetry does *not* mean only one thing is ever on
  a node. Nothing today prevents closing a path that would be electrically
  wrong (an ammeter across a driven rail, or opening a relay under load). That
  is a runtime interlock the fabric does not yet have.

---

## 2. Structure: a portable framework, and one deployment's content

The single most important thing to understand about this tree is that it holds
**two different kinds of thing**:

```
libs/            THE FRAMEWORK -- portable, knows nothing about this rig or DUT
  core/            units, criteria, predicates, the Measure/Verify/Apply verbs,
                   sessions, the run journal and its log sinks
  hal/             switching fabric, wiring, and the API drivers are written
                   against -- the mechanism, not any one instrument

instruments/     THE DRIVERS   -- one directory per instrument, each
                 independently packageable, each with its own tests

rig/             THIS BENCH'S DATA      -- which instruments, wired how
dut/             THIS DEVICE'S DATA     -- named test points, tolerance tables
suite/           THIS SUITE'S CONTENT   -- test scripts and the catalog
app/             THE RUNNER             -- main.cpp, build targets

tools/           run-tests.sh (the tester's picker)
cmake/           build helpers -- generated criteria tables, the test-target
                 helper, the install-time manifest, the installed package
docs/            the slide deck
```

`libs/` never depends on anything outside it. A second rig testing a second
device is a *separate repository* that reuses `libs/core` and `libs/hal`
unchanged and brings its own `rig/`, `dut/`, `suite/` and `app/`. Everything
rig-specific reaches the framework through three CMake file paths and a handful
of compile definitions — never through an `#include` pointing outwards.

### Every README in this tree

Each one carries the full rationale for its own directory — including the
alternatives that were rejected and what went wrong with them, which is the half
that cannot be recovered from the code.

**The framework**

| | |
|---|---|
| [`libs/hal`](libs/hal/README.md) | The two-target `hal`/`hal_rig` split, the static wiring facts and how a route is composed, adapter points, and what is still a runtime check |
| [`instruments`](instruments/README.md) | Why each driver is its own packageable directory, and what a driver may assume |
| [`cmake`](cmake/README.md) | The four build helpers: generated criteria tables, the per-layer test target, the install-time manifest, and the installed CMake package |

**The drivers**

| | |
|---|---|
| [`instruments/l4411a`](instruments/l4411a/README.md) | DMM — `Dmm1`/`Dmm2`, including the 4-wire sense path |
| [`instruments/dso8064`](instruments/dso8064/README.md) | Oscilloscope — `Osc1` |
| [`instruments/n6701a`](instruments/n6701a/README.md) | DC supply — `DcP1`..`DcP4`, and the direct-vs-relay isolation split |
| [`instruments/ac6834b`](instruments/ac6834b/README.md) | Three-phase AC source — `AcP1`, balanced vs per-phase |
| [`instruments/racal1260`](instruments/racal1260/README.md) | RS232 port — `Ser1`, routed to a DUT interface through the matrix |

**This deployment's content**

| | |
|---|---|
| [`rig`](rig/README.md) | This bench's instruments, their wiring, and the three tables a rig supplies |
| [`dut`](dut/README.md) | This device's test points and criteria variants |
| [`suite`](suite/README.md) | Test scripts, the catalog, and the setup/teardown hooks |

**Other**

| | |
|---|---|
| [`docs`](docs/README.md) | The slide deck, as `.pptx` and a self-contained `.html` |

`libs/core`, `app/`, `tools/` and `dsl/` have no README of their own — their
rationale lives in the header and `CMakeLists.txt` comments, and for `libs/core`
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
  core::ISession decides where the value really came from
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

  app/run_scripts walks suite/test_catalog.inc (GROUP / TEST), bracketing each
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
`Remove`, `Connect`, `Disconnect`, `Write`, `Read`, `Verify`, plus the safing
pass — posts to `core::Journal`. Sinks decide what to show. Neither filtering nor
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
    INSTRUMENT( L4411A,       Dmm3, Lan( "bench-dmm3"))     // a third DMM
    INSTRUMENT( N6701ARelay,  DcP5, Gpib( 0, 14), 5)        // mainframe slot 5, relay-isolated
END_INSTRUMENTS
```

The address is mandatory, and which bus kinds a row may use is fixed by its
driver — `Gpib(...)` on an L4411A is a compile error, because an LXI box has no
GPIB connector (see `hal/address.hpp`).

Nothing else. `hal::InstrumentId`, the global handle, and the safing sweep are
all derived from this list — safing reflects over `InstrumentTag`-derived globals
rather than reading the file again, so a new instrument is safed because it
exists.

### Add a switching device to the rig

`rig/devices.inc` — one line per matrix card, mux or RF selector, naming what
kind of hardware it is and where the PC commands it:

```cpp
SWITCH_DEVICE( Mux, Mux3, Gpib( 0, 7, 4))
```

That generates `hal::SwitchDeviceId::Mux3`, which is what wiring rows below name
— so a card nothing declares cannot be routed through, and a card's kind is
stated once rather than repeated (and possibly contradicted) at every hop.

### Wire an instrument to the switching fabric

`rig/wiring.inc`, `INSTRUMENT_WIRING` block. `HOP( device, channel)`, where
`device` is a `SwitchDeviceId` from `devices.inc`:

```cpp
WIRE_INSTRUMENT( Dmm3, HOP( Matrix2, 18))
```

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
becomes `Output12V`. **Forget the matching `WIRE_CONNECTOR` and `dut_tests` will
not build** — that is the coverage check, not a runtime surprise.

### Add a criterion

`dut/criteria_production.inc` (and the other variants). Group and id become
struct members, so both are compile-checked at every use:

```cpp
CRITERIA( FS_Supply_1, "Supply rail voltage checks")
    CRIT( FS_Supply_12V, EQ( 12.0_V).epsilon( 0.1_V), "12Vdc port within +/-100mV")
END_CRITERIA
```

Available predicates: `EQ`, `NE`, `LT`, `LE`, `GT`, `GE`, `IN`, `MASK`, `ANY` —
each with `.epsilon(...)` where a tolerance is meaningful. Write the value with a
unit literal (`12.0_V`, not `12.0`); that is what makes a unit mismatch a compile
error, and it is what the log prints as the required limit.

Then add it to `criteria_stress.inc` and `criteria_aged.inc` — same value as
production means:

```cpp
CRIT_FROM_PRODUCTION( FS_Supply_1, FS_Supply_12V)
```

**Miss a variant and the build will not complete** — the merged table that every
script names is generated from production's, so a missing id is a compile error
that says which variant is short of it.

### Add a test script

Three files, in this order:

1. **Declare** in `suite/scripts.hpp`:
   ```cpp
   [[nodiscard]]
   auto thermalRampScript() -> bool;
   ```
2. **Define** in `suite/scripts/thermal_ramp_script.cpp` — no `CMakeLists.txt`
   edit; `suite/scripts/*.cpp` is globbed with `CONFIGURE_DEPENDS`:
   ```cpp
   #include "../prelude.hpp"       // instruments, Measure/Apply/Verify,
                                    // criteria tables, adapter points, literals

   auto thermalRampScript() -> bool
   {
       bool allPassed = true;

       Connect( DcP3.dc());   // relay closes cold, then the output comes up --
       Apply(   DcP3.dc().voltage( 24.0_V).currentLimit( 2.0_A));

       const auto rail = Measure( Dmm1.voltage(), at( dut::Output12V));
       allPassed &= Verify( FS_Supply_1::FS_Supply_12V, rail);

       Remove(     DcP3.dc());   // ...and goes down before the relay opens.
       Disconnect( DcP3.dc());   // A relay moved under load arcs and welds.

       return allPassed;
   }
   ```
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
acknowledged in framework code *and* mirrored into both test-fixture catalogs,
so a suite gaining a group could not be a suite-only change.

A script's signature is fixed to `() -> bool` — it takes nothing at all. No rig
or device handle, because routing is resolved statically inside it; and no group
or test name either, because the log gets those from the runner's journal
boundaries, and every criterion carries its own group into the log via `Verify`.
Handing a script names it doesn't need would only create a second source for a
fact the log already has.

### Bracket a run with setup and teardown

Powering the rig up before the first script and back down after the last is a
`SETUP`/`TEARDOWN` pair in the catalog, named as identifiers exactly the way
`TEST` names a script:

1. **Declare** them alongside the scripts in `suite/scripts.hpp`:
   ```cpp
   [[nodiscard]] auto rigPowerOn()  -> bool;
   [[nodiscard]] auto rigPowerOff() -> bool;
   ```
2. **Define** them in `suite/scripts/` like any other script file.
3. **Register** them in `suite/test_catalog.inc`:
   ```cpp
   SETUP(    rigPowerOn)
   TEARDOWN( rigPowerOff)

   GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
       TEST( SupplyRail, supplyRailScript, "Verify supply rail voltages via matrix")
   END_GROUP
   ```

Both are optional and independent — declare one, both, or neither. A catalog
with no `SETUP` line needs no placeholder for one; absence resolves to `nullptr`
through ordinary name lookup (see `core/active_test_catalog.hpp`). The shipped
catalog declares both: `suite/scripts/rig_power_on.cpp` brings this rig's
sources up in order and checks each one came up, and
`suite/scripts/rig_power_off.cpp` takes them down in the inverse order.

What they bracket is the **selection**, once — including every `--repeat` pass.
`--repeat=50` powers the rig on once, runs the scripts fifty times, and powers
it off once. A hook that should instead run per repetition would be a different
hook, not a different meaning for this one.

Three things worth knowing:

- **`TEARDOWN` runs on every way out** — the selection finishing, `--until-failure`
  stopping early, a script throwing straight past everything, or **`SETUP` itself
  failing**. It is a guard destructor for the same reason `hal::RigSafingGuard` is
  one, and it is constructed *before* `SETUP` runs, so a power-up that energised
  three rails and failed on the fourth is still powered back down. A setup hook
  should therefore never call the teardown from its own failure path — that runs
  it twice.
- **It runs before the unconditional safing**, so a teardown that expects the
  fabric still wired up gets it. `hal::safeRig()` follows afterwards regardless.
- **A failing hook fails the run.** Setup returning `false` means no test runs at
  all; teardown returning `false` fails a run whose scripts all passed — a rig
  that didn't shut down the way the suite says it should is not a clean run.

### Run one script against several criteria groups

Taking no parameters doesn't stop one script body from serving several catalog
entries. Make the script a template and let `TEST` name an instantiation — that
is an ordinary identifier, so the macro needs no change:

```cpp
// suite/scripts.hpp
template<typename Criteria>
auto supplyRailScript() -> bool;

// suite/scripts/supply_rail_script.cpp
template<typename Criteria>
auto supplyRailScript() -> bool
{
    bool allPassed = true;

    allPassed &= Verify( Criteria::FS_Supply_5V0, Measure( Dmm1.voltage(), at( dut::Output5V)));
    allPassed &= Verify( Criteria::FS_Supply_3V3, Measure( Dmm1.voltage(), at( dut::Output3V3)));

    return allPassed;
}

// suite/test_catalog.inc
GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
    TEST( SupplyRailA, supplyRailScript<FS_Supply_1>, "Rails on the A side")
    TEST( SupplyRailB, supplyRailScript<FS_Supply_2>, "Rails on the B side")
END_GROUP
```

What the catalog stores is still an `auto (*)() -> bool`, so nothing about the
mechanism changes: the signature stays uniform, `Tests` stays a homogeneous
array, and a misspelled group is still a compile error rather than a runtime
lookup miss. Each `Verify` logs the group name its own criterion carries, so the
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

allPassed &= Verify( FS_Supply_1::FS_Load_Max, load);
```

What you should *not* do is route a DMM's current terminals through the matrix.
Signal relays are not rated for load current, and an ammeter closed onto a pin a
supply is driving is a short across that supply — see §1 on what the fabric does
and does not protect you from.

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

   allPassed &= Verify( FS_Console_1::FS_Console_Ack,   reply.before( "\r"));
   allPassed &= Verify( FS_Console_1::FS_Console_Ready, reply.at( 4));

   Disconnect( Ser1.rs232(), at( dut::Console));
   ```

Four things in that are worth knowing.

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
and changes nothing at the DUT's pins, so it is a verb of its own — and one the
oscilloscope will want too. `Apply` on a port that has no output to energise is a
compile error rather than a call that silently does nothing.

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

### Add a unit of measurement

Three edits, all genuine vocabulary:

```cpp
// libs/core/include/core/quantity.hpp
struct degC_Type { static constexpr std::string_view Symbol = "degC"; };
using Temperature = Quantity< degC_Type>;
constexpr Temperature operator""_degC( long double v) { /* ... */ }   // optional

// libs/core/include/core/quantity_kind.hpp -- the enumerator, named to match
enum class QuantityKind { /* ... */ Temperature };
```

The variant, both kind↔type mappings, the symbol lookup, `to_string` and the
recording round-trip are all generated from those. Add the enumerator without the
alias and a `static_assert` tells you exactly what is missing.

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

All three arrive as one `core::ValueSource` (see `core/session.hpp`), so the
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
`Measure.useLive()` goes back to hardware.

---

## 4. Running a suite

```bash
cmake --preset macos-debug          # or windows-debug
cmake --build build/debug
build/debug/app/run_scripts --dut-serial=SN-000123
```

Presets are a platform × build-type matrix — `macos-debug`, `macos-release`,
`windows-debug`, `windows-release` — composed from two hidden halves so that a
compiler path is written once rather than once per build type. The platform half
picks the compiler and generator (Ninja on macOS, MinGW Makefiles on Windows);
the build-type half picks `Debug`/`Release` and the directory, `build/debug` or
`build/release`. Both platforms land in the same directory, so every command in
this README works unchanged on either.

`cmake --list-presets` shows only the ones whose generator exists on the host, so
the Windows pair is invisible on macOS and vice versa. Note that `-Werror`
applies to Release as well, so an optimiser-only warning fails that build rather
than being reported.

| Flag | Effect |
|---|---|
| *(none)* | run every test in the catalog |
| `--list-tests` | print `group\|id\|description` per test, run nothing |
| `--select=a,b` | run only these test ids, in catalog order |
| `--criteria=NAME` | apply that tolerance variant — `production`, `stress`, `aged` |
| `--repeat=N` | run the selection N times over |
| `--until-failure` | stop as soon as a pass fails |
| `--safe` | drop the rig to idle and exit — no test, no log |
| `--record=PATH` | write every reading the run took, in order |
| `--replay=PATH` | take every reading from that file instead of the rig |
| `--dut-serial=`, `--operator=` | traceability, into both logs |
| `--log-dir=`, `--sarif=`, `--rtf=` | where the logs go |
| `--quiet`, `--no-logs`, `--no-color` | suppress the console, the files, the colour |

Exit code is 0 only if every selected test passed.

**Choosing the tolerances.** Every variant is compiled in, so one binary serves
all of them:

```bash
build/debug/app/run_scripts                       # the build's default variant
build/debug/app/run_scripts --criteria=stress     # stress-chamber tolerances
```

An unknown name is fatal and lists the ones that would have worked — a runner
that quietly fell back to the default would apply the wrong tolerances to real
hardware and hand back a log that looks entirely normal. The choice is frozen
when the journal opens, so the variant named in both logs' header is provably
the one every check in them was made against.

**Repeating a run.** What repeats is the *selection*, not each script:
`--select=A,B --repeat=3` runs `A B A B A B`, never `A A A B B B`. That is the
unit a soak run cares about — "the tests, again" — and it is also the unit
`SETUP`/`TEARDOWN` bracket.

```bash
build/debug/app/run_scripts --repeat=50                    # fifty passes
build/debug/app/run_scripts --until-failure                # until something breaks
build/debug/app/run_scripts --repeat=50 --until-failure    # at most fifty, stop on failure
```

`--until-failure` on its own has no bound, deliberately: how many passes a DUT
survives is what such a run is trying to find out, so requiring the number up
front would be requiring the answer. A repeated run is still **one** run — one
report, one exit status, passes marked in the machine log, and any failing pass
fails the whole thing.

**Recording and replaying the readings.** A third artifact, and the only one
that is an input as well as an output:

```bash
build/debug/app/run_scripts --record=readings.tsv      # capture what the rig read
build/debug/app/run_scripts --replay=readings.tsv      # run again off the file, no rig
```

Where the two logs describe a run, this is the readings themselves, in order
(see `core/recording.hpp` for the format — flat TSV, one row per reading). It is
what reproduces a bench failure at a desk: the replayed run takes its verdict
from the file, so it passes or fails exactly as the recorded one did, with no
instrument attached.

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

`tools/run-tests.sh [build-dir]` is the tester-facing picker: choose a group, then
one, several or all of its tests. It offers the catalog and nothing else — unit
tests are the build system's business, deliberately not reachable from there.

### The two logs

Both are written by default, named for the run's start time, and both carry the
same traceability header — DUT, serial, operator, criteria variant, framework
version, content revision, UTC instant, command line.

**`*.rtf` — for people.** Colour-coded, grouped the way the catalog is,
descriptions in grey, each check stating what was measured *and what was
required*. It carries what the run **observed** — `Measure`, `Read` and `Verify`
— and not what the bench was told to do:

```
DeviceX -- Sat 01 Aug 2026 11:13:16 CEST
DUT serial        SN-000123
Criteria          production
Suite/DUT/rig     5a4052f
--------------------------------------------------------------------
OutputVoltage Tests validating DUT output voltage rails
	SupplyRail Verify supply rail voltages via matrix
	measure Output5V                    5.021 V     (Dmm1)  5Vdc supply port
	verify  FS_Supply_1::FS_Supply_5V0  5.021 V     = 5 V +/-0.05 V   [PASS]  ...
	RESULT  SupplyRail                                                [PASS]
```

It is a **valid, openable RTF document after every logged event** — the closing
brace is rewritten and seeked back over on each flush — so it can be read while a
long run is still going, and a killed run leaves a readable log.

**`*.sarif` — for tools.** SARIF 2.1.0, *every* verb including the routing steps
and the safing pass, because a routing step omitted for brevity is exactly the one
that explains a failed reading. Each criterion becomes a SARIF rule keyed
`FS_Supply_1/FS_Supply_5V0` — its own group and id, the pair a test spec traces
to — so "results for rule X" means "every time this requirement was checked",
across runs and DUTs. Values carry both formatted text and a bare number.

The live console view renders the same content as the RTF from the same events, so
what an operator watched and what the report says cannot disagree.

**`--record=` writes a third file, and it is not a log.** Flat TSV, one row per
reading — `sequence, wallClockMillis, point, instrument, kind, value` — carrying
nothing a person wants and nothing a report needs, because its only reader is
`--replay=`. Neither log can serve that purpose: they describe a run, in formats
built for a person and for a SARIF server, while a replay needs the values
themselves in the order they were taken and nothing else.

---

## 5. Build options

```bash
cmake --preset macos-debug -DTHORIUM_CRITERIA_VARIANT=stress
```

| Variable | Default | Meaning |
|---|---|---|
| `THORIUM_KNOWN_CRITERIA_VARIANTS` | `production stress aged` | which tolerance tables are compiled in. Each needs a matching `dut/criteria_<name>.inc`, checked at *configure* time |
| `THORIUM_CRITERIA_VARIANT` | `production` | which of them applies when `--criteria=` is not given. A typo fails the *configure* step |
| `THORIUM_DUT_NAME` | `DeviceX` | DUT name in both logs |
| `THORIUM_RIG_NAME` | `thorium-rig-1` | bench name in the machine log |
| `THORIUM_SUITE_VERSION`, `THORIUM_DUT_VERSION`, `THORIUM_RIG_VERSION` | `git describe --always --dirty` | content revision in both logs |
| `BUILD_TESTING_LAYERS` | `ON` | build each layer's tests |
| `THORIUM_ACTIVE_INSTRUMENTS`, `THORIUM_INSTRUMENT_TABLE`, `THORIUM_WIRING_TABLE` | this repo's `rig/` | the three paths a *separate* rig repo would point at its own data |

The last four are how `libs/hal` stays generic: it is compiled against whichever
rig supplied those paths, so an installed `libhal.a` is only ever valid for that
one rig — a different bench builds hal from source against its own tables.

**Requires GCC 16+** for C++26 reflection (`-freflection`) and contracts
(`-fcontracts`). Reflection is not decoration: it is what removes the
hand-maintained parallel lists — enum names, criteria iteration, the safing sweep,
the quantity tables — that would otherwise each be a place to forget something.

---

## 6. Tests

```bash
ctest --test-dir build/debug --output-on-failure     # all 317 tests
ctest --test-dir build/debug -LE acceptance          # unit tests only (280)
ctest --test-dir build/debug -L  acceptance -V       # the CLI tour (37)
```

| Target | Tests | Covers |
|---|---|---|
| `core_tests` | 172 | units, predicates, criteria, sessions, journal, all three log sinks |
| `hal_tests` | 98 | drivers, switching fabric, wiring, safing, `describeConfig` |
| `dut_tests` | 4 | the DUT profile — including the wiring-coverage build check |
| `scripts_tests` | 6 | the test scripts, injected; plus the variant parity build check |
| `acceptance_tests` | 37 | the `run_scripts` binary as a subprocess — flags, log files, exit codes |

The acceptance tests are worth knowing about separately: they run the real binary
with real flags and keep everything under `<build>/app/acceptance/<suite>.<test>/` —
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
  spreadsheet looks like. The machinery that interprets them lives in `libs/`.
- **Guards are not simplicity.** Where a parallel list can be *derived* instead of
  asserted, derive it. A `static_assert` that catches a forgotten edit is strictly
  worse than a design with nothing to forget.
