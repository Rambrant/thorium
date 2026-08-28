# hal::Ac6834B

Agilent/Keysight 6834B AC Power Source/Analyzer — 4500 VA total (1500 VA per
phase), two output ranges (150 V/30 A and 300 V/15 A), three-phase, SCPI over
GPIB/RS-232. Header-only, `namespace hal`, included as `"hal/ac6834b.hpp"`.

Not to be confused with Keysight's newer **AC6800** series — this is the 6834B,
part of the 681xB/6834B/6843A AC Power Solutions family.

Target: `hal_ac6834b` / `Thorium::hal_ac6834b`. Depends on `Thorium::hal` only.

Reachable over `Gpib` or `Serial` — the GPIB/RS-232 pair above, where
`hal::Serial` means a port on the PC (see `hal/driver/address.hpp`).

## Sourcing

`ac()` returns an immutable builder — `.phaseVoltage()`,
`.frequency()`, `.currentLimit()`, each returning an updated copy rather than
mutating — whose result goes to `Apply` / `Remove`. The name is a plain
description, not an axis: a 6834B is a three-phase model with no phase-count
setting. (One- and three-phase are separate models in the 681xB/683xB/6843A
family, not a switch.)

### Balanced and per-phase

A scalar setter drives all three phases alike. Naming the phases individually
switches the builder's type, so balanced and unbalanced configurations are
distinguishable without a second entry point:

```cpp
Apply( AcP1.ac().phaseVoltage( 115_V).frequency( 400_Hz));   // balanced

Apply( AcP1.ac()
            .frequency( 400_Hz)
            .phaseVoltage( phaseA( 115_V), phaseB( 113_V), phaseC( 117_V)));
```

`phaseVoltage()`, `currentLimit()` and `range()` all take both forms — the guide
marks all three *Phase Selectable*. The phase is part of each argument's type, so
the three must be given in A/B/C order; a transposition will not compile.
`frequency()` has no per-phase form: three phases of one source share a frequency
by construction, and `FREQuency` carries no such designation.

`range()` (`VOLT:RANG`) takes the voltage the range must accommodate, not a range
identifier — *"sending a parameter greater than 150 selects the 300 volt range,
otherwise the 150 volt range is selected"* — so `.range( 115_V)` and
`.range( 150_V)` are one instruction, and a readback reports 150 V or 300 V
rather than what was sent.

This is the instrument's own `INSTrument:COUPle` setting — `COUPle ALL` programs
all three phases with one command, and the commands it affects are those the
programming guide marks *Phase Selectable*. `VOLTage` and `CURRent` carry that
designation; `FREQuency` does not, which is why frequency is scalar-only.
`COUPle`'s `*RST` value is `ALL`, which is why a chain starts out balanced.

Per-phase readback is not a stylistic choice either: *"There is no way to query
more than one phase with a single command"* — queries are directed at one phase
via `INSTrument:NSELect`.

### Not modelled

`VOLTage:OFFSet` (DC offset), `PHASe` (per-phase angle — per-phase *only*, it
cannot be broadcast), per-phase `MEASure:POWer?`, the neutral's AC/ACDC and
harmonic queries, harmonic/THD analysis, and arbitrary waveforms.

Also **not** modelled: the coupling between `VOLT:RANG` and `CURR`. The range is
a setpoint here, but nothing validates a current limit against it — the guide
notes the maximum programmable limit depends on the range in force, and that the
order coupled commands are sent in matters. Both are a real driver's job.

`Apply` programs the source without touching the fabric. `Remove` disables it and
requires no prior setup call, so it is safe as an unconditional teardown.

### Reading back

| Call | SCPI | Phase Selectable? |
|---|---|---|
| `measuredVoltage( Phase)` | `MEAS:VOLT?` | yes |
| `measuredCurrent( Phase)` | `MEAS:CURR?` | yes |
| `measuredFrequency()` | `MEAS:FREQ?` | no |
| `measuredTotalPower()` | `MEAS:POW:AC:TOT?` | no |
| `measuredNeutralCurrent()` | `MEAS:CURR:NEUT?` | no |

Which readings take a phase and which do not is the instrument's answer, not a
style choice — the guide marks voltage and current *Phase Selectable* and the
other three not. There is deliberately **no** no-argument `measuredVoltage()`:
*"There is no way to query more than one phase with a single command"*, so
there would be nothing to send. A test asserts its absence.

Each reading carries a port qualifier so it keys its own recording slot —
`AcP1.B.Voltage`, `AcP1.N.Current`, `AcP1.Total.Power`. Total power is
qualified because `MEAS:POW?` is itself phase selectable, so per-phase power
could be added later without colliding.

## Four channels close as one

`Connect` closes **four** fixed channels together: phases A, B and C plus the
neutral/ground return. They are all wired under the same `hal::InstrumentId`, so
`hal::InstrumentWiring::findAll()` returns all four and the driver connects and
disconnects them as a unit — a three-phase source with one phase live and its
return open is not a state worth being able to reach.

This is why `findAll()` exists alongside `find()`: single-channel instruments
like `hal::L4411A` and `hal::DSO8064A` use `find()`, while this driver and
relay-isolated `hal::N6701A` outputs with remote sense need every entry for
their id at once.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( Ac6834B, AcP1)
rig/active_instruments.hpp  #include "hal/ac6834b.hpp"
rig/wiring.inc              WIRE_INSTRUMENT   four channels under one id: phases A/B/C + return
                            WIRE_SOURCE       the VPC pin each phase is cabled onto
dut/adapter.inc             BUNDLE of four SOURCE_LINEs, one per conductor
```

See `framework/hal/README.md` on why the neutral return is part of the model rather
than left outside it as an unswitched wire — a decision about the adapter side
of a three-phase connection rather than about this driver.
