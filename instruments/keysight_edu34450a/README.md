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

This is `Dmm1` on the bench rig (see `rig/instrument.inc`). `Dmm2` is a
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
| capacitance | 1 nF – 10 mF on the instrument, and `core` has no farad unit. Adding one is a change to `core/quantities/`, made for a reading no script takes yet — the wrong order. When a test needs a capacitance, that change and this port arrive together. |
| temperature | `core` does have a `Temperature`, but this is a 2-wire read of a 5 kΩ thermistor in the meter's own front terminals, auto-ranging only. It cannot travel through the matrix to a DUT pin, so the port would be one nothing on this rig could route. |
| continuity, diode test | Threshold answers rather than measurements — a fixed 10 Ω threshold with a beeper, and a forward-voltage check on a fixed 1 V range, both fast-mode only. `resistance()` with an `LT( 10_Ohm)` criterion says the same thing in the vocabulary the report is already written in. |
| secondary display | A whole parallel SCPI tree (`SEC:FUNC`, `CONF:SEC:…`, `MEAS:SEC:…?`) giving two readings from one acquisition — genuinely useful, and a shape `core::Port` does not have. A port is one quantity; two readings keying into one session slot is a design question, not a driver detail. |

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
  those there, and more range and accuracy instead.

Which is the argument this codebase already made when it retired the generic
`hal::Dmm` placeholder: once the real model is known, naming the class after it
documents the non-portability of its measurement-function set rather than
pretending a DMM driver is interchangeable across models.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( keysight_edu34450a::EDU34450A, Dmm1, Lan( "bench-dmm1"))
rig/active_instruments.hpp  #include "hal/keysight_edu34450a.hpp"
rig/wiring.inc              which fabric channels its force and sense leads land on
```

The wiring is rig data and lives nowhere in this directory: which fabric channel
a DMM's leads reach is a fact about the bench, not about the model.

## Sources

- [EDU34450A data sheet](https://www.keysight.com/content/dam/keysight/en/doc/ungate/data-sheets/EDU34450A-5-5-Digit-Dual-Display-Digital-Multimeter.pdf)
  — functions, ranges, accuracies, reading rates, interfaces.
- [Keysight 34450A Programmer's Reference](https://www.batronix.com/files/Keysight/DMM/34450A/34450A-Programming.pdf)
  — the SCPI dialect this model shares: `FUNCtion`, `…:RESolution`,
  `…:RANGe[:AUTO]`, and the `PRIMary`/`SECondary` split.
