# hal::Ac6677A

Keysight AC6677A three-phase AC source. Header-only, `namespace hal`, included as
`"hal/ac6677a.hpp"`.

Target: `hal_ac6677a` / `Thorium::hal_ac6677a`. Depends on `Thorium::hal` only.

## Sourcing

`threePhaseWye()` returns an immutable builder — `.phaseVoltage()`,
`.frequency()`, `.currentLimit()`, each returning an updated copy rather than
mutating — whose result goes to `Apply` / `Remove`.

`Apply` programs the source without touching the fabric. `Remove` disables it and
requires no prior setup call, so it is safe as an unconditional teardown.

`measuredVoltage()`/`measuredCurrent()` read back what is actually being
delivered.

## Four channels close as one

`Connect` closes **four** fixed channels together: phases A, B and C plus the
neutral/ground return. They are all wired under the same `hal::InstrumentId`, so
`hal::InstrumentWiring::findAll()` returns all four and the driver connects and
disconnects them as a unit — a three-phase source with one phase live and its
return open is not a state worth being able to reach.

This is why `findAll()` exists alongside `find()`: single-channel instruments
like `hal::L4411A` and `hal::DSO8064` use `find()`, while this driver and
relay-isolated `hal::N6701A` outputs with remote sense need every entry for
their id at once.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( Ac6677A, AcP1)
rig/active_instruments.hpp  #include "hal/ac6677a.hpp"
rig/wiring.inc              four channels under one id: phases A/B/C + return
```

See `libs/hal/README.md` on why `ThreePhaseWyePoints` has no neutral point and
isn't templated on its three points — both are decisions about the adapter side
of a three-phase connection rather than about this driver.
