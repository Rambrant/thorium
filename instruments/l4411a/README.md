# hal::L4411A

Keysight L4411A 6.5-digit system DMM. Header-only, `namespace hal`, included as
`"hal/l4411a.hpp"`.

Target: `hal_l4411a` / `Thorium::hal_l4411a`. Depends on `Thorium::hal` only.

Reachable over `Lan` or `Usb` — LXI packaging, no GPIB connector, and the
constructor is constrained to say so (see `hal/address.hpp`).

## What it measures

| Member | Reading | Notes |
|---|---|---|
| `voltage()` | DC volts | |
| `acVoltage()` | AC volts | |
| `current()` | DC amps | |
| `acCurrent()` | AC amps | |
| `resistance()` | ohms, 2-wire | `SensePath::NotUsed` |
| `fourWireResistance()` | ohms, 4-wire | `SensePath::Required` — routes force and sense together |

Each returns a `core::Port` carrying the quantity *and* the sense requirement in
its type, so a four-wire reading and a two-wire one are different types and
`core::MeasureEngine` branches on that at compile time. The chained setup
builders (`.range()`, `.nplc()`, `.frequency()`) preserve it.

Several `L4411A` instances can coexist on one rig, distinguished by the
`hal::InstrumentId` each is constructed with — this rig has `Dmm1` and `Dmm2`
(see `rig/instrument.inc`).

## The one sharp edge

Measurement mode is instrument state, not port state. A port handle obtained
before a mode switch reads whichever mode is current when `rawMeasure()` is
eventually called, not the mode that was active when the handle was created:

```cpp
auto dcPort = dmm1.voltage();
(void)dmm1.acVoltage();     // switches the instrument to AC
dcPort.rawMeasure();        // reads the AC value, not the DC one
```

This mirrors the real instrument — there is one measurement front end and it has
one mode — so it is documented and tested rather than designed away. See
`tests/test_l4411a.cpp`.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( L4411A, Dmm1)
rig/active_instruments.hpp  #include "hal/l4411a.hpp"
rig/wiring.inc              which fabric channels its force and sense leads land on
```

The wiring is rig data and lives nowhere in this directory: which matrix channel
a DMM's leads reach is a fact about the bench, not about the model.
