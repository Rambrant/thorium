# dev/ -- the desk bench: a PC, a meter, a supply, a scope, a generator, a switch unit

This is a second **deployment**, not a second framework. `framework/` and
`instruments/` are shared unchanged with the bench — including `framework/runner`,
which is the whole runner, so this directory brings no `main()` of its own; what
it holds is the same three kinds of content `rig/`, `dut/` and `suite/` hold, for
a rig that is a meter, a supply, a scope and a waveform generator on a desk
with USB cables to them.

It exists for the work the bench cannot host: developing an instrument driver
against real hardware. Doing that on the rack means booking the rack.

**The first driver has landed.** `hal::keysight_edu34450a::EDU34450A` — this
deployment's one instrument — opens a real SCPI session over a socket and reads
the meter (see `framework/hal/README.md` on `hal/io/`, and that driver's own
README for what it sends). So this directory is no longer waiting for something:
it is the bring-up loop, and running it against a meter on a desk is now a
three-step procedure rather than a plan. See **Transport** below.

```bash
cmake --preset macos-dev
cmake --build build/dev
ctest --test-dir build/dev
./build/dev/bin/run_scripts
```

## What it is

```
dev/
    rig/     one EDU34450A (pooled), one EDU36311A's three outputs, one
             DSOX1202G, one 33522B, one 34980A (not yet in the fabric),
             no wiring
    dut/     an adapter with no points, one criteria table per instrument
    suite/   the meter's sanity check, each of its functions, each of the
             supply's outputs, the scope against its probe-comp output, and
             the generator as the scope measures it, and the switch unit
```

Every file is the ordinary form of its table with the rows a desk bench has. One of them has no counterpart on the
bench: `rig/pools.inc`, the shelf of interchangeable meters this desk draws
Dmm1 from. That is not a special case in the mechanism — it is the fifth rig
table, optional and read the same way as the others — but it is the one table
here the bench deliberately does not have, and its own comment says why.

The selection is three directory paths — `THORIUM_RIG_DIR`, `THORIUM_DUT_DIR`,
`THORIUM_SUITE_DIR` — plus an optional fourth this bench does not use
(`THORIUM_ACCEPTANCE_DIR`, see below), plus the facts that follow from them
(`THORIUM_KNOWN_CRITERIA_VARIANTS`, `THORIUM_INSTRUMENT_PACKAGES`,
`THORIUM_DUT_NAME`/`THORIUM_RIG_NAME`, `THORIUM_ACCEPTANCE_TESTS`). All of it
lives in the `macos-dev`/`windows-dev` presets, and the top-level
`CMakeLists.txt` has the reasoning. One deployment per build directory.

## The one thing that makes it possible

`core::MeasureEngine` has a point-free overload — a reading that needs no
routing:

```cpp
const auto reference = Measure( Dmm1.voltage());
const auto bulk      = Measure( Dmm1.capacitance());
```

No `at(...)`, no path composed, no relay closed. The second line is the one that
could not be written before the meter on this desk became an EDU34450A, and it
is also the one that could not be *routed* on the bench: a capacitance reading
sources into the node it measures, so `core::requiresDeadNode` names it and
`core::MeasureEngine` refuses to route one onto a live rail
(`core/verbs/interlock.hpp`). Point-free, there is nothing to refuse — no
fabric, no source, no pin, just a reference capacitor across the meter's
terminals, which is exactly the shape a driver-development check wants. It was written for a supply
reading back its own output at full load, and it is exactly what a bench with no
fabric and no declared points needs for every reading it takes. (A bench that
declares points reaches them with `at(...)` and no fabric either — see
"Points, but only until someone writes one" below, which is the same story from
the other end.) Everything after it is identical to a
bench script: the same `Verify`, the same criteria lookup, the same journal
events, the same RTF and SARIF rows, the same verdict rule.

So the whole chain runs here — catalog, group, test, measure, session, journal,
verify, both sinks — and the only thing absent is the routing.

## What it does not exercise, and what to do about it

**The fabric -- exercised now, against real relays.** This section used to open
with the routed half being out of reach: no cards, so no `HOP(...)`, so path
composition, `hal::SwitchFabric` and the interlock's fabric side never ran. The
34980A's five cards are `dev/rig/devices.inc` rows now, on box `Swu1`, and the
fabric drives them: one `WIRE_INSTRUMENT` (the internal DMM's bus relay), one
`WIRE_CONNECTOR` (a crosspoint) and one `POINT` (`dut::DeskTerminal`) make
`Measure( Dmm2.voltage(), at( dut::DeskTerminal))` a routed reading that closes
`ROUT:CLOS (@1921)` and `(@1501)` through the chassis, reads, and opens them.
The fabric moves a relay at its first use and its last release, rolls a
half-closed path back, and releases a path even when its reading fails -- see
`framework/hal/include/hal/fabric/switch_fabric.hpp` and `rig_switching.hpp`.
What this desk still does not exercise is a *source* routed through the
switching: the supply's outputs are cabled straight, not through relays.

**Points, but only until someone writes one.** `Measure( port, at( point))` is
no longer out of reach here, and that is the one item on this list the framework
*did* change to fix. A `WIRE_TAP` row names an instrument and a VPC pin with no
path at all (see `hal::TapWiring`), and
`dut/tests/test_wiring_coverage.cpp` accepts a point covered by one — so a
`POINT` in `dev/dut/adapter.inc` plus a `WIRE_TAP` row in `dev/rig/wiring.inc`
is a complete, checked declaration with no card anywhere in it. What that buys
is the `at(...)` half: the coverage check, point-keyed session and journal keys,
and the interlock if the meter ever shares a pin with a supply. Both tables are
empty today because there is no fixture on this desk, not because the shape does
not fit.

Note the rule that comes with it. Once an instrument is tapped, the point-free
spelling is refused for it — `Measure( Dmm1.voltage())` throws and says to write
`at(...)` instead. The two would otherwise key one node two ways, as
`Dmm1.Voltage` and as the pin name, and a suite that mixed them would record
under both. Nothing on this desk is tapped today, so `Measure( Dmm1.voltage())`
above stays exactly as correct as it has always been.

**Transport.** Present, and this is now the item this deployment is *for*
rather than the one it is missing.

Nothing needs an address put anywhere, which is the part worth knowing before
reading the row. This desk draws Dmm1 from a shelf of three interchangeable
meters declared in `dev/rig/pools.inc`, and a pool outranks a row's own address
— so an attached run tries `dev-dmm-1`, `-2`, `-3` in that order and takes the
first that answers `*IDN?`:

```bash
cmake --preset windows-dev      # or macos-dev, or linux
cmake --build build/dev
./build/dev/bin/run_scripts
```

The address column in `dev/rig/instrument.inc` still fixes the row's *kind* at
compile time — `Lan(...)` there is what makes a LAN address legal for this row
at all — but an attached run never opens it. For a meter on none of the three,
say so on the command line rather than editing either file:

```bash
run_scripts --address DeskDmm=lan:dev-dmm-7
```

An override beats the pool as well as the column, so that is the one thing
which always wins. A desk that always has the same meter can set
`THORIUM_ADDRESS_DeskDmm` once instead.

USB needs Keysight IO Libraries Suite or NI-VISA on the machine (almost
certainly already there on a Windows or Linux bench — it is what Connection
Expert belongs to); LAN needs nothing at all. Neither is needed to *build*. See
that driver's README for what each failure message means.

`dmm_self_check.cpp`'s two readings then come off the instrument — a DC volts
and a capacitance, through `Measure`, a session, the journal, a criterion and
both log sinks, with nothing stubbed anywhere in the chain:

```
	measure Dmm1.Voltage                5.001 V     (Dmm1)  instrument readback
	measure Dmm1.Capacitance            468.3 uF    (Dmm1)  instrument readback
	verify  DEV_Dmm_1::DEV_Dmm_Ref      5.001 V     = 5 V +/-50 mV      [PASS]
	verify  DEV_Dmm_1::DEV_Dmm_Bulk     468.3 uF    = 470 uF +/-47 uF   [PASS]
```

What a run does with *no* meter at the other end changed, and changed in the
direction this framework argues for everywhere else. It used to read `0 V` and
fail its criteria; then it failed at the first reading; it now fails before the
first script runs at all, saying what it could not reach:

```
Preflight failed: cannot resolve Lan dev-dmm:5025:
    nodename nor servname provided, or not known
```

That last move is the preflight (see `hal/verbs/preflight.hpp`), which opens
every reachable instrument once at startup so that the whole rig is confirmed
before anything is measured rather than one box at a time as scripts happen to
touch them. On this deployment it is one meter and the difference is half a
second; on a bench it is the difference between a report with one failing test
and no report at all, which is the right way round when the cause is a cable.

Which is the better failure. A rig that reads zero volts off a meter that was
never connected is precisely the outcome this whole design is written against,
and "there is no meter" and "the meter reads zero" must not look the same in a
report. It is also fast — under half a second, because the transport carries a
connect deadline rather than letting the OS retransmit its SYN for a minute.

`--inject` and `--skeleton` still need no meter at all, because they detach the
bench and take their readings from a file — which is what keeps a script's own
unit tests hardware-free. A detached run skips the preflight entirely, for the
same reason it skips safing: a run that must not touch hardware must not open a
socket to find out what is there.

A desk sharing a shelf of meters is the normal case here, and it is the pool
above that answers it rather than anything typed per run: a run needs no flag
while one of the three declared meters is free, and a candidate that is busy or
powered off is simply the wrong one. Only an empty shelf is a failure, and it
arrives at startup naming how many candidates were tried.

`dev/rig/pools.inc` is also where the reason the bench next door must never
have one is written down — a `POOL` row says its candidates are
interchangeable, and a wiring row says an instrument's leads go somewhere
specific, so the two together are a compile error naming the offending row.
That is worth reading before adding a pool anywhere else.

Either way — pool or flag — the run's header records the address and where it
came from, so a log from your desk is still readable by somebody at another
one, and a pooled row also records which candidate of how many it took.

An override is checked against the meter's own back panel, not against the row —
an EDU34450A has LAN and USB, so `--address DeskDmm=usb:<serial>` works here too,
and a bus the instrument has no connector for is refused at startup with the
list of the ones it does have. That is the same list the compiler holds the
table to; see `hal::BackPanel`.

The driver's own tests are hardware-free too, by a different route: they hand it
a fake `hal::io::ITransport` and assert the exact SCPI it would have sent. So
the division of labour for driver work is worth knowing before starting any:

| Question | Answered in | Needs the desk |
|---|---|---|
| does the driver send what its author intended | `instruments/<model>/tests` | no |
| does a reply mean what the driver thinks it means | `framework/hal/tests/io/test_scpi.cpp` | no |
| do the bytes actually leave the process | `framework/hal/tests/io/test_socket_transport.cpp`, against a loopback listener | no |
| does an address become the right VISA resource | `framework/hal/tests/io/test_visa_transport.cpp` | no |
| does VISA itself behave | nowhere — it is closed vendor software, absent from every machine this repo is developed on | **yes** |
| was the author right about the instrument | **here, with the meter plugged in** | yes |

Only the last two rows need hardware, which is the point of the others. The
LAN path has had that confirmation; the USB/VISA path has not, and the next
person to plug a meter in over USB is performing it. A
useful halfway house while writing a driver: a twenty-line script that listens
on a socket and answers `*IDN?`, `SYST:ERR?` and `READ?` will drive the whole
chain above end to end, including the journal and the criteria, without a meter
on the desk at all.

## What landing this deployment turned up

A second deployment is a test of the portability claim, and it found four places
where framework code had quietly baked in the bench's content. All four are
fixed; they are recorded here because the shape recurs.

| Where | What was assumed | Now |
|---|---|---|
| `hal/fabric/switch_device.hpp` | at least one switching card — `SwitchDeviceInfo switchDevices[]` is a zero-length array otherwise, which is not C++ | `std::array` sized from the enum |
| `core/meta.hpp` | at least one enumerator — the `template for` never reads the parameter over an empty enum, and `-Werror` caught it | `[[maybe_unused]]` on the three parameters |
| `framework/hal/tests/` | this bench's five cards and eleven instruments, by name, in the *generic* library's test target | `test_switch_device`/`test_switch_fabric`/`test_wiring` moved to `rig/tests/`; `test_instrument` reflects over whatever the deployment declares |
| `framework/core/tests/` | three criteria variants named `production`/`stress`/`aged` | by index, skipped where the deployment has fewer |

The `framework/` row is the interesting one. `hal_tests` links plain `hal` precisely so
that a test reaching an instrument global or an `Apply` fails to link — and that
check could not see any of this, because an *enumerator* is neither. The link line
catches a test that reaches for the rig's objects; nothing caught a test that
reaches for the rig's names.

It found one more outside `framework/`, which is why the count above is of
*framework* code. A driver package's tests named this bench's instrument ids — `InstrumentId::Osc1` in
`instruments/keysight_dsox1202g/tests`, `::AcP1` in `ac6834b`, `::Ser1` in
`racal1260`, `::Dmm1` in the two DMM packages — and `ac6834b` and `racal1260`
named its switching cards on top of that. None of those enumerators exists on a
desk with one meter and no relays, so those packages' tests did not compile
here at all. Every package now takes the ids it needs from
`core::meta::values<hal::InstrumentId>` and `core::meta::values<hal::SwitchDeviceId>`,
and skips what this deployment is too small to give meaning to: a routed
`Connect` where there is no fabric to route over, a two-instrument test where
there is one instrument. All seven directories build their tests against this
deployment as well as against the bench.

`THORIUM_INSTRUMENT_PACKAGES` stays, because it was never really the fix — what
it does is let a deployment stop paying to compile drivers it has no instrument
for, and this one builds `keysight_edu34450a` alone for that reason rather than
because the others would fail. It is a CMake *cache* variable, which matters
when that line changes: editing the preset does not reach an existing build
directory. Reconfigure it, or the build fails on a driver header the include
path no longer has.

## What the suite checks

Three groups, and they are run differently -- which is the one thing about
them worth knowing before opening the console.

**BenchSanity** is `DmmSelfCheck`: DC volts and capacitance, the chain end to
end.

**DmmFunctions** is every function the EDU34450A has, one test each: DC and AC
volts, DC and AC current, 2- and 4-wire ohms, the frequency counter,
capacitance, plus DC volts on a fixed range and at each of the three
resolutions. Each needs its own reference across the terminals, and the desk
has no switching to present them in turn, so this group is run **a test at a
time**: connect that test's reference, tick that one test. Running the whole
group is expected to fail. The references are in
`dev/dut/criteria_production.inc`'s `DEV_Dmm_Fn` table, and all but the 5 V
cell and the 470 uF capacitor are placeholders (`TODO(desk)`) until the parts
on this desk are written in.

**SupplyOutputs** is each of the EDU36311A's three outputs -- `DcP5`, `DcP6`,
`DcP7`, the bench's names for them -- set to two setpoints, read back, checked
for drawing nothing, removed, and checked for reading zero. The terminals stay
open, so this group needs no fixture and runs whole.

The supply's three outputs are three rows and one **box**, `Psu1` -- the first
column of each row (see `framework/hal/include/hal/topology/boxes.hpp`). So one
flag moves all three:

```bash
run_scripts --address Psu1=usb:<serial>
```

or `THORIUM_ADDRESS_Psu1` in the shell. Preflight contacts the box once and
fans the address out to its three faces, and the driver shares one SCPI session
between them (see `instruments/keysight_edu36311a/README.md`, "One session per
chassis"). It is not pooled, because this desk has one supply -- but it could
be: a pool names a box now, so `POOL( Psu1, ...)` would acquire one supply for
all three outputs.

Every address on this desk names a box the same way: `DeskDmm`, `Psu1`,
`Scope1`, `Wfg1`, `Swu1`. An environment variable still named after an
instrument (`THORIUM_ADDRESS_Osc1`) fails startup naming the variable it
should be, rather than being quietly ignored.

**ScopeProbeComp** is the DSOX1202G against the 1 kHz square wave on its own
probe-compensation terminal: both probes' tips on it, both grounds on its lug,
and the group runs whole. All eight amplitude readings and all six timing
readings on each channel, a single-shot capture with both traces fetched and
checked against the same levels, the four acquisition types, and AC coupling
and the bandwidth limit. The comp output is meant for adjusting probes rather
than calibrating a scope, so its levels in `DEV_Osc_1` are placeholders
(`TODO(desk)`) to be read off this scope once and written in; its timing is the
part worth trusting, and the rows are tighter for it.

The scope's row carries a placeholder serial, `Usb( "CN00000000")`, until the
real one is written in -- and until then **every attached run fails at
startup**, the meter's and the supply's included, because preflight opens every
row. Set `THORIUM_ADDRESS_Scope1=usb:<serial>` or edit the row.

**WfgIntoScope** is the 33522B, checked by the scope: generator CH1 to scope
CH1 and CH2 to CH2 on BNC cables, 1:1, and the group runs whole. Every shape
the driver models (sine, square, ramp, triangle, pulse, noise, DC), frequency
over four decades, amplitude and offset together, duty cycle, ramp symmetry,
Remove, and the two channels at once. Every waveform is told
`.into( Termination::HighImpedance)`, because the scope's input is 1 MOhm; the
termination test is the one that tells it fifty ohms on purpose and checks the
amplitude doubles. The nominals are the settings sent, not placeholders -- the
generator is the reference here -- and the windows are sized to the scope,
which is the less accurate of the two. The generator's row has a placeholder
serial like the scope's did, `Usb( "MY00000000")`, with the same consequence;
set `THORIUM_ADDRESS_Wfg1=usb:<serial>`.

Its unit tests cannot see one thing: whether a setting is inside the 33522B's
limits, which the driver checks in `applyWaveform()` and a detached run never
calls. An attached run against simulated drivers does call it, and needs no
hardware:

```bash
run_scripts --select=WfgSineCh1,... --address=DeskDmm=sim --address=Psu1=sim --address=Psu1=sim --address=Psu1=sim --address=Scope1=sim --address=Wfg1=sim
```

Every script fails its readings there (a simulated scope reads zero) but runs to
its end; a setting out of range would stop it with `SettingOutOfRange`.

**SwitchUnit** is the 34980A with nothing wired to it: every check is a question
the mainframe answers about itself. The rack is four 34932A matrices (34932T
terminal blocks) in slots 1-4 and a 34941A RF multiplexer in slot 5 -- the same
set `rig/devices.inc` records the bench migrating onto. An inventory of all eight
slots, each slot's `SYST:CTYP?` answer also posted as a Note; on each matrix a
crosspoint, the four corners of both matrices in one list, `closeExclusively`,
the four Analog Bus relays (921-924, Matrix 2's) and `openAll`, each checked by
asking `ROUT:CLOS?`; three channels a 34932A does not have, which the mainframe
must refuse; each RF bank selecting 1-of-4 and refusing `ROUT:OPEN`, left on
channel 01; and a crosspoint's life count (`DIAG:REL:CYCL?`) moving when it is
driven. Channel numbers come from hal's own module models, so a numbering
mistake there shows here. The one thing it cannot check is contact: that needs
a short across a crosspoint and the desk meter on the other side.

The group never uses `Dmm2`, so it runs on a mainframe without one -- or with
one left out -- by simulating just that face, which leaves the chassis and its
cards real:

```bash
run_scripts --select=SwuInventory,SwuMatrixSlot1 --address=Dmm2=sim
```

On a unit that has no internal DMM at all, say so in the table instead:
`Dmm2`'s row may read `Simulated{}` while the rest of box `Swu1` keeps its
address (see `hal/topology/boxes.hpp`).

**SwitchUnitDmm** is the mainframe's internal DMM, `Dmm2` -- the same box,
`Swu1`, sharing its one session -- with nothing switched onto it: the meter is
fitted and enabled, an open Analog Bus reads no voltage and an *overload* for
resistance (which `whenUnmeasurable` turns into "beyond 100 MOhm"), and closing
a crosspoint onto an uncabled column (column 2) and a bus relay leaves the bus
open. **SwitchUnitWired** is the whole chain, through the desk's one cable --
`DcP7` onto Matrix 2 column 1 of the 34932A in slot 1, which is
`dut::DeskTerminal`. The cable stays on the desk, and `dev/rig/wiring.inc`
records it as a `WIRE_SOURCE` row: a routed
`Measure( Dmm2.voltage(), at( dut::DeskTerminal))` must read the supply's 5 V,
the fabric closing bus relay 921 and crosspoint 501 for the reading and opening
them after, and the terminal must read zero once the supply is removed. Whether
the meter accepts the bare `MEASure` form it uses is the one thing the first run
confirms; see `instruments/keysight_34980a/README.md`.

Its unit tests hand `Swu1` a fake of this rack built from the same hal models,
because the driver's own simulation knows the slots and not the modules -- it
would accept any channel, and would not select 1-of-4.

The chassis is an **instrument row**, `Swu1`, which it was not until this desk
needed one: preflight checks it, `THORIUM_ADDRESS_Swu1` reaches it, and safing
opens its relays after every source is off (`hal::RelayHoldingInstrument`, and
`DevRig.SafingTurnsTheSupplyOffBeforeItOpensTheSwitchUnitsRelays`, which fails if
the order is ever reversed). Its row carries a placeholder serial,
`Usb( "MY00000000")`, with the same startup consequence as the scope's did.

One thing about the scope's keys worth knowing before replaying a run: a
point-free scope reading keys as `Osc1.<measurement>.<quantity>` --
`Osc1.Vpp.Voltage` -- with no channel in it, so channel 1's and channel 2's
readings of the same measurement share a key. Each script reads one channel,
so nothing collides within a test; a recording of the whole group is a
sequence per key, in run order. A fetched trace does key by channel
(`Osc1.Channel1`).

## Adding to it

**A script.** One `.cpp` in `dev/suite/scripts/`, one declaration in
`dev/suite/scripts.hpp`, one `TEST(...)` row in `dev/suite/test_catalog.inc`. The
glob picks up the file; the catalog name-checks against the header.

**A criterion.** `dev/dut/criteria_production.inc`. One table, which is also
this deployment's master, so no `CRIT_FROM_MASTER` and no variant to keep in
step.

**A second instrument.** One `INSTRUMENT(...)` row in `dev/rig/instrument.inc`,
its package in `THORIUM_INSTRUMENT_PACKAGES` (the row's own type column is what
pulls the driver header in), and the count in `dev/rig/tests/test_dev_rig.cpp` —
which will fail until you change it, deliberately: what this bench is should not
widen quietly.

**A different meter in the same slot** is the same list minus the count: the row,
the header, the package, and the `SafeableInstrument` assertion in
`test_dev_rig.cpp` that names the driver type. Swapping the L4411A for the
EDU34450A was exactly those four edits plus a reconfigure — and one more reading
in `dmm_self_check.cpp`, which is not part of the swap but is the reason for it.

**Acceptance tests.** `dev/acceptance/` — a fourth directory beside the three
above, not a subdirectory of `dev/suite/` — plus `THORIUM_ACCEPTANCE_DIR:
"${sourceDir}/dev/acceptance"` in the `dev-deployment` preset and dropping
`THORIUM_ACCEPTANCE_TESTS: "OFF"` from it. That option
is off here because acceptance tests assert on a deployment's own facts — group
names, the DUT in the report header, the rig's instruments — and the bench's
(`acceptance/test_acceptance.cpp`) assert on the bench's. The runner they
drive is shared; what is asserted about it is not. Turning the option on with an
empty directory is a configure error rather than a quiet skip.

**A unit test.** `dev/suite/tests/` for a script (inject by key —
`Measure.inject( "Dmm1.Voltage", ...)`, and `--skeleton` prints the keys),
`dev/rig/tests/` for a claim about the bench, `dev/dut/tests/` for one about the
adapter or the criteria. Driver-level work belongs in
`instruments/<model>/tests/`, which needs no deployment at all.
