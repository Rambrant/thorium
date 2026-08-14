# hal::N6701A

One channel of a Keysight N6701A DC power supply mainframe. Header-only,
`namespace hal`, included as `"hal/n6701a.hpp"`.

Target: `hal_n6701a` / `Thorium::hal_n6701a`. Depends on `Thorium::hal` only.

## One instance per channel, not per mainframe

`hal::N6701A` models a *channel*. A four-channel mainframe is four instances with
four `hal::InstrumentId`s and no shared state at this layer — the same way `Dmm1`
and `Dmm2` don't share state. The channel number is a constructor argument:

```cpp
hal::N6701ADirect dcP1{ hal::InstrumentId::DcP1, 1 };
hal::N6701ADirect dcP2{ hal::InstrumentId::DcP2, 2 };
```

## Isolation is a type parameter, and that's the interesting part

`N6701A<Isolation>` is templated on how the channel reaches the DUT, with two
instantiations in use:

| Alias | `HasRelay` | Connect/Disconnect |
|---|---|---|
| `N6701ADirect` (`DirectWiring`) | `false` | **does not compile** |
| `N6701ARelay` (`RelayIsolated`) | `true` | closes/opens the fixed channel |

A hard-wired supply has no relay for `Connect` to close, so rather than have
`Connect` fail at runtime or silently no-op, there is no `connect(dcP1.dc())`
overload that compiles at all. That's `hal::SwitchableIsolation` doing the work.

Because it is a concept rather than a per-tag overload, a new relay-having
isolation tag gets Connect/Disconnect for free with no driver change — which is
asserted directly in `tests/test_n6701a.cpp` using a tag the driver has never
heard of.

## Sourcing and reading back

`dc()` returns an immutable builder: `.voltage()`, `.currentLimit()`, each
returning an updated copy rather than mutating. Pass the result to `Apply` /
`Remove`. `measuredVoltage()`/`measuredCurrent()` read back what the supply is
actually delivering — a disabled supply reads zero rather than its setpoint.

`Apply` needs no fabric or wiring at all and can be called before `Connect` ever
is: programming the supply doesn't require the DUT to be wired up yet. `Remove`
disables the output without disconnecting — that is `Disconnect`'s job, on its own
schedule.

Remote-sense leads, where a rail has them, come along automatically: `Connect`
uses `findAll()` and so closes a `WIRE_INSTRUMENT_SENSE` entry together with the
force channel, no driver change needed.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( N6701ADirect, DcP1, 1)
                            INSTRUMENT( N6701ARelay,  DcP3, 3)
rig/active_instruments.hpp  #include "hal/n6701a.hpp"
rig/wiring.inc              the fixed channel each relay-isolated output sits on
```

Whether a given channel is direct-wired or relay-isolated is a fact about the
bench, so it is chosen in `rig/instrument.inc` by which alias is named — not
here.
