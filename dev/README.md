# dev/ -- the desk bench: a PC, one instrument, and nothing else

This is a second **deployment**, not a second framework. `framework/` and
`instruments/` are shared unchanged with the bench — including `framework/runner`,
which is the whole runner, so this directory brings no `main()` of its own; what
it holds is the same three kinds of content `rig/`, `dut/` and `suite/` hold, for
a rig that is one meter on a desk with a LAN cable to it.

It exists for the work the bench cannot host: developing an instrument driver
against real hardware. Today every driver in this repository answers from its own
simulation hooks — `mSimVoltage` and friends — and nothing reads an
`hal::Address` yet. Making one of them open a real session is the next real piece
of work, and doing it on the rack means booking the rack.

```bash
cmake --preset macos-dev
cmake --build build/dev
ctest --test-dir build/dev
./build/dev/bin/run_scripts
```

## What it is

```
dev/
    rig/     one L4411A, no switching hardware, no wiring
    dut/     an adapter with no points, one criteria table
    suite/   one group, one test, one script
```

Sixty-odd lines of content in total, none of it a special case: every file is the
ordinary form of its table, with the rows a desk bench has.

The selection is three directory paths — `THORIUM_RIG_DIR`, `THORIUM_DUT_DIR`,
`THORIUM_SUITE_DIR` — plus the facts that follow from them
(`THORIUM_KNOWN_CRITERIA_VARIANTS`, `THORIUM_INSTRUMENT_PACKAGES`,
`THORIUM_DUT_NAME`/`THORIUM_RIG_NAME`, `THORIUM_ACCEPTANCE_TESTS`). All of it
lives in the `macos-dev`/`windows-dev` presets, and the top-level
`CMakeLists.txt` has the reasoning. One deployment per build directory.

## The one thing that makes it possible

`core::MeasureEngine` has a point-free overload — a reading that needs no
routing:

```cpp
const auto reference = Measure( Dmm1.voltage());
```

No `at(...)`, no path composed, no relay closed. It was written for a supply
reading back its own output at full load, and it is exactly what a bench with no
fabric needs for every reading it takes. Everything after it is identical to a
bench script: the same `Verify`, the same criteria lookup, the same journal
events, the same RTF and SARIF rows, the same verdict rule.

So the whole chain runs here — catalog, group, test, measure, session, journal,
verify, both sinks — and the only thing absent is the routing.

## What it does not exercise, and what to do about it

**The fabric.** With no cards declared there is no `hal::SwitchDeviceId` to name,
so `HOP(...)` cannot be written, `Measure( port, at( point))` cannot be written,
and path composition, `hal::SwitchFabric` and the electrical interlock never run.
That is not a gap in this deployment — it is what a desk with no relays *is*. If
you want the routed path in the dev loop, put one real mux card on the desk: one
row in `dev/rig/devices.inc`, one `WIRE_INSTRUMENT`, one `WIRE_CONNECTOR`, one
`POINT`, and the entire routed half comes back. The framework needs no change for
it.

**Transport.** Still absent, and still the point. A run with the bench attached
reads `0 V` and fails, correctly — nothing is at the other end of
`Lan( "dev-dmm")` yet. `--inject` and `--skeleton` work fully, because they detach
the bench and take their readings from a file. When a driver grows a real `.cpp`
it becomes `STATIC` in its own `CMakeLists.txt`, which that file already
anticipates.

## What landing this deployment turned up

A second deployment is a test of the portability claim, and it found four places
where framework code had quietly baked in the bench's content. All four are
fixed; they are recorded here because the shape recurs.

| Where | What was assumed | Now |
|---|---|---|
| `hal/fabric/switch_device.hpp` | at least one switching card — `SwitchDeviceInfo switchDevices[]` is a zero-length array otherwise, which is not C++ | `std::array` sized from the enum |
| `core/meta.hpp` | at least one enumerator — the `template for` never reads the parameter over an empty enum, and `-Werror` caught it | `[[maybe_unused]]` on the three parameters |
| `framework/hal/tests/` | this bench's five cards and eight instruments, by name, in the *generic* library's test target | `test_switch_device`/`test_switch_fabric`/`test_wiring` moved to `rig/tests/`; `test_instrument` reflects over whatever the deployment declares |
| `framework/core/tests/` | three criteria variants named `production`/`stress`/`aged` | by index, skipped where the deployment has fewer |

The `framework/` row is the interesting one. `hal_tests` links plain `hal` precisely so
that a test reaching an instrument global or an `Apply` fails to link — and that
check could not see any of this, because an *enumerator* is neither. The link line
catches a test that reaches for the rig's objects; nothing caught a test that
reaches for the rig's names.

### One defect left open

**A driver package's tests name this bench's instrument ids.**
`instruments/dso8064a/tests` constructs its driver with `InstrumentId::Osc1`,
`ac6834b` with `::AcP1`, `racal1260` with `::Ser1`, `n6701a` with `::DcP1..3`. None
of those enumerators exists on a bench that has no scope, no AC source and no
serial port, so those three packages' tests do not compile against this
deployment — which contradicts `instruments/README.md`'s claim that each
directory is independently packageable.

Worked around rather than fixed: `THORIUM_INSTRUMENT_PACKAGES` lets a deployment
build only the packages it has an instrument for, and this one builds `l4411a`
alone. `instruments/l4411a/tests` is fixed properly, as the pattern for the rest —
it takes the ids it needs from `core::meta::values<hal::InstrumentId>` and skips
the two-instrument test where there is only one. The other four want the same
treatment, and that is a separate change.

## Adding to it

**A script.** One `.cpp` in `dev/suite/scripts/`, one declaration in
`dev/suite/scripts.hpp`, one `TEST(...)` row in `dev/suite/test_catalog.inc`. The
glob picks up the file; the catalog name-checks against the header.

**A criterion.** `dev/dut/criteria_production.inc`. One table, so no
`CRIT_FROM_PRODUCTION` and no variant to keep in step.

**A second instrument.** One `INSTRUMENT(...)` row in `dev/rig/instrument.inc`,
its driver header in `dev/rig/active_instruments.hpp`, its package in
`THORIUM_INSTRUMENT_PACKAGES`, and the count in `dev/rig/tests/test_dev_rig.cpp` —
which will fail until you change it, deliberately: what this bench is should not
widen quietly.

**Acceptance tests.** `dev/suite/acceptance/`, plus dropping
`THORIUM_ACCEPTANCE_TESTS: "OFF"` from the `dev-deployment` preset. That option
is off here because acceptance tests assert on a deployment's own facts — group
names, the DUT in the report header, the rig's instruments — and the bench's
(`suite/acceptance/test_acceptance.cpp`) assert on the bench's. The runner they
drive is shared; what is asserted about it is not. Turning the option on with an
empty directory is a configure error rather than a quiet skip.

**A unit test.** `dev/suite/tests/` for a script (inject by key —
`Measure.inject( "Dmm1.Voltage", ...)`, and `--skeleton` prints the keys),
`dev/rig/tests/` for a claim about the bench, `dev/dut/tests/` for one about the
adapter or the criteria. Driver-level work belongs in
`instruments/<model>/tests/`, which needs no deployment at all.
