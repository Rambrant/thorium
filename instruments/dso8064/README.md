# hal::DSO8064

Keysight DSO8064 four-channel oscilloscope. Header-only, `namespace hal`,
included as `"hal/dso8064.hpp"`.

Target: `hal_dso8064` / `Thorium::hal_dso8064`. Depends on `Thorium::hal` only.

Reachable over `Gpib`, `Lan` or `Usb` (see `hal/address.hpp`).

## What it measures

Readings are reached through a channel view — `osc1.channel<1>()` — and the
channel number is a **template argument**, bounded by `ValidDso8064Channel`, so
`channel<0>()` and `channel<5>()` are compile errors rather than runtime ones on
a four-channel instrument.

| Family | Members | Quantity |
|---|---|---|
| Amplitude | `vpp()` `vmax()` `vmin()` `vrms()` `vaverage()` | `Voltage` |
| Timing | `frequency()` | `Frequency` |
| Timing | `period()` `riseTime()` `fallTime()` | `Time` |

`riseTime()`/`fallTime()` default to 10/90 % thresholds, overridable at the call
site.

## The two sharp edges

Both measurement mode *and* selected channel are instrument state, not port
state. A port handle obtained before either is switched reads whichever mode and
channel are current when `rawMeasure()` is eventually called:

```cpp
auto ch1 = osc1.channel<1>().vpp();
(void)osc1.channel<3>().vpp();   // switches the instrument to channel 3
ch1.rawMeasure();                // reads channel 3, not channel 1
```

This mirrors the real instrument — one acquisition front end, one selected
channel and mode at a time — so it is documented and tested rather than designed
away. Same shape as `hal::L4411A`'s mode edge. See `tests/test_dso8064.cpp`.

Note also that a channel view is a temporary: the `Port` it returns outlives it
safely, which is tested explicitly.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( DSO8064, Osc1)
rig/active_instruments.hpp  #include "hal/dso8064.hpp"
rig/wiring.inc              which fabric channels its probe inputs land on
```
