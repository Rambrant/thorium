# dev/ -- the desk bench: a PC, one instrument, and nothing else

This is a second **deployment**, not a second framework. `framework/` and
`instruments/` are shared unchanged with the bench — including `framework/runner`,
which is the whole runner, so this directory brings no `main()` of its own; what
it holds is the same three kinds of content `rig/`, `dut/` and `suite/` hold, for
a rig that is one meter on a desk with a LAN cable to it.

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
    rig/     one EDU34450A, no switching hardware, no wiring
    dut/     an adapter with no points, one criteria table
    suite/   one group, one test, one script
```

Sixty-odd lines of content in total, none of it a special case: every file is the
ordinary form of its table, with the rows a desk bench has.

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

**The fabric.** With no cards declared there is no `hal::SwitchDeviceId` to name,
so `HOP(...)` cannot be written, and path composition, `hal::SwitchFabric` and
the electrical interlock's fabric side never run. That is not a gap in this
deployment — it is what a desk with no relays *is*. If you want the routed path
in the dev loop, put one real mux card on the desk: one row in
`dev/rig/devices.inc`, one `WIRE_INSTRUMENT`, one `WIRE_CONNECTOR`, one `POINT`,
and the entire routed half comes back. The framework needs no change for it.

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

Put the meter's address in `dev/rig/instrument.inc` — a serial number for USB,
a hostname for LAN — build, and run:

```cpp
INSTRUMENT( keysight_edu34450a::EDU34450A, Dmm1, Usb( "MY60012345"))   // rear USBTMC port, via VISA
INSTRUMENT( keysight_edu34450a::EDU34450A, Dmm1, Lan( "dev-dmm"))      // LXI port 5025, no vendor software
```

```bash
cmake --preset windows-dev      # or macos-dev, or linux
cmake --build build/dev
./build/dev/bin/run_scripts
```

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
fail its criteria; it now fails at the reading, immediately, saying what it
could not reach:

```
Uncaught exception during test run: cannot resolve Lan dev-dmm:5025:
    nodename nor servname provided, or not known
```

Which is the better failure. A rig that reads zero volts off a meter that was
never connected is precisely the outcome this whole design is written against,
and "there is no meter" and "the meter reads zero" must not look the same in a
report. It is also fast — under half a second, because the transport carries a
connect deadline rather than letting the OS retransmit its SYN for a minute.

`--inject` and `--skeleton` still need no meter at all, because they detach the
bench and take their readings from a file — which is what keeps a script's own
unit tests hardware-free.

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

### One defect left open

**A driver package's tests name this bench's instrument ids.**
`instruments/keysight_dsox1202g/tests` constructs its driver with `InstrumentId::Osc1`,
`ac6834b` with `::AcP1`, `racal1260` with `::Ser1`, `n6701a` with `::DcP1..3`. None
of those enumerators exists on a bench that has no scope, no AC source and no
serial port, so those three packages' tests do not compile against this
deployment — which contradicts `instruments/README.md`'s claim that each
directory is independently packageable.

Worked around rather than fixed: `THORIUM_INSTRUMENT_PACKAGES` lets a deployment
build only the packages it has an instrument for, and this one builds
`keysight_edu34450a` alone. That package and `keysight_l4411a` are both fixed
properly, as the pattern for the rest — each takes the ids it needs from
`core::meta::values<hal::InstrumentId>` and skips the two-instrument test where
there is only one. The other four want the same treatment, and that is a
separate change.

It is a CMake *cache* variable, which matters when this line changes: editing
the preset does not reach an existing build directory. Reconfigure it, or the
build fails on a driver header the include path no longer has.

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
