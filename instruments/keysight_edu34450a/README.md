# hal::keysight_edu34450a::EDU34450A

Keysight EDU34450A 5½-digit dual-display bench DMM. Header-only, namespace
`hal::keysight_edu34450a`, included as `"hal/keysight_edu34450a.hpp"`.

Target: `hal_keysight_edu34450a` / `Thorium::hal_keysight_edu34450a`. Depends on
`Thorium::hal` only.

Reachable over `Lan` or `Usb` — gigabit LAN and a rear USBTMC port, no GPIB
connector and no option that adds one (the 34450A whose command set this shares
does offer GPIB; this is the box that does not), and the constructor is
constrained to say so (see `hal/driver/address.hpp`). The front-panel USB *host*
port takes a flash drive holding saved setups; it is not a way for the PC to
reach the instrument.

This is `Dmm1` on the bench rig (see `rig/instrument.inc`) and the one
instrument on the dev bench (`dev/rig/instrument.inc`). `Dmm2` is a
`hal::keysight_l4411a::L4411A`, and the two being different C++ types is the
point — see [Why not just another L4411A](#why-not-just-another-l4411a).

## What it measures

| Member | Reading | Notes |
|---|---|---|
| `voltage()` | DC volts | 100 mV – 1000 V |
| `acVoltage()` | AC volts | true RMS, AC-coupled, 100 mV – 750 V, 20 Hz – 100 kHz |
| `current()` | DC amps | 10 mA – 3 A |
| `acCurrent()` | AC amps | true RMS, 10 mA – 3 A |
| `resistance()` | ohms, 2-wire | 100 Ω – 100 MΩ, `SensePath::NotUsed` |
| `fourWireResistance()` | ohms, 4-wire | `SensePath::Required` — routes force and sense together |
| `frequency()` | hertz | 20 Hz – 300 kHz off the voltage input, 20 Hz – 10 kHz off the current one |
| `capacitance()` | farads | 1 nF – 10 mF, 2-wire; requires a dead node — see below |

Each returns a `core::Port` carrying the quantity *and* the sense requirement in
its type, so a four-wire reading and a two-wire one are different types and
`core::MeasureEngine` branches on that at compile time. The chained setup
builders (`.range()`, `.nplc()`, `.frequency()`) preserve it.

### `frequency()` and `.frequency()` are different things

`Dmm1.frequency()` asks the meter how fast the signal is — SCPI `FUNC "FREQ"`, a
counter reading. `Dmm1.acVoltage().frequency( 50_Hz)` *tells* the meter what to
expect, which is a `core::MeasureSetup` field for meters that need telling. This
one does not, and ignores it. The names are a dot apart and both are right; the
tests assert they stay distinct.

### Not exposed, and why

| Function | Why not |
|---|---|
| temperature | `core` does have a `Temperature`, but this is a 2-wire read of a 5 kΩ thermistor in the meter's own front terminals, auto-ranging only. It cannot travel through the matrix to a DUT pin, so the port would be one nothing on this rig could route. |
| continuity, diode test | Threshold answers rather than measurements — a fixed 10 Ω threshold with a beeper, and a forward-voltage check on a fixed 1 V range, both fast-mode only. `resistance()` with an `LT( 10_Ohm)` criterion says the same thing in the vocabulary the report is already written in. |
| secondary display | A whole parallel SCPI tree (`SEC:FUNC`, `CONF:SEC:…`, `MEAS:SEC:…?`) giving two readings from one acquisition — genuinely useful, and a shape `core::Port` does not have: a port is one quantity, and this is two out of one round trip. The *session* half of that has since stopped being a problem — a slot is a name and a quantity, so a voltage and a current at one pin no longer collide (see the root README) — which leaves the port shape as the whole of it, plus a qualifier for the same-quantity pairings this meter allows (VDC alongside VAC). Still a design question rather than a driver detail. |

## Capacitance needs a dead node

This is the one function neither the `L4411A` nor anything else on this bench
has, and it is why `core` grew a farad (`core::quantities::Capacitance`,
`_pF`/`_nF`/`_uF`/`_mF`/`_F`).

It is measured the way a DMM measures a capacitance and not the way an LCR
bridge does: a known current into the node — 100 nA on the 1 nF range, 1 mA on
the 10 mF one — and the slope of the ramp that current produces. Two
consequences:

**It sources into what it measures.** `core::requiresDeadNode` names
`Capacitance` alongside `Current` and `Resistance`
(`core/verbs/interlock.hpp`), so `core::MeasureEngine` refuses a routed
capacitance reading at a pin an energised supply is cabled onto, before
anything closes. A rail driving that node does not merely spoil the reading —
it holds the node at its own voltage, and the meter measures the rail's
regulation loop. `Remove` the source first, or read the meter's own terminals
with the point-free `Measure` overload.

The rule did not change to accommodate this; it is what the predicate already
said, asked about one more kind. That is the property of being named for what a
reading requires of the *node* rather than for what an instrument does — and
`rig/tests/test_interlock.cpp` asserts the refusal against this rig's real
`SOURCE_WIRING`.

**It reads the whole node.** Two terminals, DC: anything in parallel with the
part is in the answer, and a leaky capacitor reads high because the leakage adds
to the charging current. The data sheet's accuracies assume a NULL of the open
test leads first — on the 1 nF range the leads are a meaningful fraction of full
scale.

No 4-wire variant and no sense path: the instrument has one capacitance function
and it is 2-wire. Kelvin sensing answers a question about lead resistance, which
is not what limits this measurement.

## Resolution, not NPLC

`core::MeasureSetup::Nplc` means nothing on this model, and the driver does not
pretend otherwise. An L4411A integrates over a number of power-line cycles and
will take any number; this family has no NPLC command at all. `VOLT:DC:RES`
accepts exactly three values, which the front panel and the data sheet name
Slow, Medium and Fast:

| | Digits | Readings/s (DCV) | Normal-mode rejection |
|---|---|---|---|
| `Resolution::Slow` | 5½ | ~1.3 | 60 dB |
| `Resolution::Medium` | 4½ | ~49 | 60 dB |
| `Resolution::Fast` | 4½ | up to 110 | 0 dB |

So it is instrument state, set with `setResolution()` and left where it was put,
the way `hal::keysight_dso8064a::DSO8064A::setMode()` already is. Fast mode has
no line-frequency rejection at all — right for sequencing many pins, wrong for a
rail tolerance check. The driver defaults to `Slow`, which is both what the
instrument resets to and what its accuracy specifications are quoted at.

## The one sharp edge

Measurement mode is instrument state, not port state. A port handle obtained
before a mode switch reads whichever mode is current when `rawMeasure()` is
eventually called, not the mode that was active when the handle was created:

```cpp
auto dcPort = Dmm1.voltage();
(void)Dmm1.acVoltage();     // switches the instrument to AC
dcPort.rawMeasure();        // reads the AC value, not the DC one
```

This mirrors the real instrument — one measurement front end, one SCPI
`FUNCtion` setting — so it is documented and tested rather than designed away.
See `tests/test_keysight_edu34450a.cpp`.

## Why not just another L4411A

`Dmm1` and `Dmm2` used to be two `L4411A` instances. A script measuring a rail
still cannot tell the two meters apart, and one C++ type would have had to lie
about one of them:

- **Digits.** 5½ here, 6½ on the L4411A. A criterion tightened to the L4411A's
  resolution is not a criterion this meter can answer.
- **Integration.** NPLC there, three discrete resolutions here.
- **Functions.** Capacitance, temperature and a secondary display here; none of
  those there, and more range and accuracy instead. Capacitance is the one a
  script can actually see: `Dmm1.capacitance()` compiles and `Dmm2.capacitance()`
  does not, which is the difference between the two meters made checkable rather
  than documented.

Which is the argument this codebase already made when it retired the generic
`hal::Dmm` placeholder: once the real model is known, naming the class after it
documents the non-portability of its measurement-function set rather than
pretending a DMM driver is interchangeable across models.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( keysight_edu34450a::EDU34450A, Dmm1, Lan( "bench-dmm1"))
rig/wiring.inc              which fabric channels its force and sense leads land on
```

A deployment that names its packages explicitly needs one more line —
`THORIUM_INSTRUMENT_PACKAGES: "keysight_edu34450a"` in its preset. The bench
deployment globs and needs none; the dev one names it (see `CMakePresets.json`).
Note that it is a CMake *cache* variable, so changing it in a preset does not
reach an existing build directory: reconfigure, or the build fails on a missing
`hal/keysight_edu34450a.hpp`.

The wiring is rig data and lives nowhere in this directory: which fabric channel
a DMM's leads reach is a fact about the bench, not about the model.

## Sources

- [EDU34450A data sheet](https://www.keysight.com/content/dam/keysight/en/doc/ungate/data-sheets/EDU34450A-5-5-Digit-Dual-Display-Digital-Multimeter.pdf)
  — functions, ranges, accuracies, reading rates, interfaces.
- [Keysight 34450A Programmer's Reference](https://www.batronix.com/files/Keysight/DMM/34450A/34450A-Programming.pdf)
  — the SCPI dialect this model shares: `FUNCtion`, `…:RESolution`,
  `…:RANGe[:AUTO]`, and the `PRIMary`/`SECondary` split.
