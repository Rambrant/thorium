# hal::DSO8064A

Agilent/Keysight Infiniium **DSO8064A** — four channels, 600 MHz. Header-only,
`namespace hal`, included as `"hal/dso8064a.hpp"`.

Target: `hal_dso8064a` / `Thorium::hal_dso8064a`. Depends on `Thorium::hal` only.

Reachable over `Gpib`, `Lan` or `Usb` (see `hal/address.hpp`).

Modelled against the *Infiniium 54830-series Programmer's Reference*
(54830-97014), which is the same command set the 8000 Series ships.

## The four things a script does with it

```cpp
Setup( Osc1.timebase().timePerDivision( 10_ms).reference( TimebaseReference::Left));
Setup( Osc1.channel<3>().input( ChannelInput::Dc1M).voltsPerDivision( 100_mV));

Arm(   Osc1.single().timeout( 2_s));   // returns once the scope is armed
Remove( AcP1.ac());                    // the event to capture
const auto captured = Await( Osc1.single());

const auto dip = Measure( Osc1.channel<3>().vbase(), at( dut::Output5V))
               - Measure( Osc1.channel<3>().vmin(),  at( dut::Output5V));

const auto trace = Fetch( Osc1.channel<3>().waveform());   // the whole record
```

`suite/scripts/ac_dropout_script.cpp` is the first four written out in full.

## Setup — four subsystems, four builders

One builder per SCPI subsystem the instrument actually has, rather than one
flattened bag of scope settings. Each `Setup` call then says which part of the
instrument it is touching, and the split matches how the settings interact.

| Builder | Setting | Values |
|---|---|---|
| `Osc1.trigger()` | `edgeSource<N>()` | channel number, compile-checked |
| | `slope()` | `TriggerSlope::Rising` `Falling` |
| | `level()` | `Voltage` |
| | `sweep()` | `TriggerSweep::Auto` `Triggered` `Single` |
| | `coupling()` | `TriggerCoupling::Dc` `Ac` `LowFrequencyReject` `HighFrequencyReject` |
| | `holdoff()` | `Time` |
| `Osc1.timebase()` | `timePerDivision()` | `Time` |
| | `position()` | `Time` — trigger to reference point |
| | `reference()` | `TimebaseReference::Left` `Center` `Right` |
| `Osc1.acquisition()` | `mode()` | `AcquisitionMode::RealTime` `PeakDetect` `HighResolution` `Segmented` |
| | `points()` / `automaticPoints()` | memory depth |
| | `averagedOver()` / `unaveraged()` | across successive triggers |
| | `sampleRate()` / `automaticSampleRate()` | `Frequency` |
| `Osc1.channel<N>()` | `input()` | `ChannelInput::Dc1M` `Dc50` `Ac1M` `LfReject1` `LfReject2` |
| | `voltsPerDivision()` | `Voltage` |
| | `verticalOffset()` | `Voltage` at screen centre |
| | `bandwidth()` | `Bandwidth::Full` `Limited` |
| | `probeAdapter()` | `ProbeAdapter::None` `Div10` `Div20` `Div100` |
| | `display()` | `ChannelDisplay::On` `Off` |

Every field is optional and unset means **leave whatever is already
configured** — so a `Setup` naming only the trigger level does not reset the
slope. Same convention as `core::MeasureSetup` and `hal::Racal1260Config`.

Note that `input()` is one setting, not two: this instrument offers DC at 1 MΩ,
DC at 50 Ω and AC at 1 MΩ, and there is no AC-coupled 50 Ω input. Separate
coupling and impedance knobs would make a fourth combination expressible and
wrong.

## Arm and Await

`Arm` returns only once the scope is **armed and ready to trigger**, not once it
has been told to arm. Those are different moments, and a single-shot test that
causes its event in the gap between them silently records nothing. Keysight's
own "Single Shot Device Under Test" sequence is what this implements:

```
:STOP; *OPC?      :ADER?      :SINGLE      while (AER? == 0) wait
                                           // armed and ready, enable DUT here
```

So the script's rule is: **Arm, then cause the event.** Nothing enforces it, for
the same reason nothing enforces Connect-before-Apply.

`Await` polls the done register against a timeout and returns `bool` — a
transient that never arrived is a finding about the DUT, not an error in the
bench, so it comes back as a value a criterion can be pointed at. Awaiting
without arming answers `false` rather than throwing: a script that measures a
transient without arming has measured the stale acquisition buffer, and the
check this gates is exactly where that should surface.

Both verbs are **generic** and live in `core/acquire.hpp` — a transient
recorder, a digitizer or a counter with an armed gate is the same shape. This is
simply the first driver to answer to `armDriver`/`awaitDriver`.

Timeouts default to 5 s for the capture and 1 s for arming (`kDefaultCaptureTimeout`,
`kDefaultArmTimeout`), and are always written to the log even when the script
did not name them — a report of a run that timed out has to say what it was
waiting for.

## What it measures

Readings are reached through a channel view — `osc1.channel<1>()` — and the
channel number is a **template argument**, bounded by `ValidDso8064aChannel`, so
`channel<0>()` and `channel<5>()` are compile errors rather than runtime ones on
a four-channel instrument. `trigger().edgeSource<N>()` is bounded the same way.

| Family | Members | Quantity |
|---|---|---|
| Levels | `vpp()` `vmax()` `vmin()` `vrms()` `vaverage()` | `Voltage` |
| Levels | `vbase()` `vtop()` `vamplitude()` `vmiddle()` | `Voltage` |
| Timing | `frequency()` | `Frequency` |
| Timing | `period()` `riseTime()` `fallTime()` `positiveWidth()` `negativeWidth()` | `Time` |

`riseTime()`/`fallTime()` default to 10/90 % thresholds, overridable at the call
site.

**`vbase()` is not `vmin()`**, and the difference is what transient work turns
on. Min and max are the extreme samples in the record, so a single spike moves
them. Base and top are the settled levels the waveform spends its time at, so
they are not moved by the spike at all — which makes `vbase() - vmin()` the depth
of a negative transient measured against the rail it departed from.

Each reading keys its session slot by which measurement it is —
`"Output5V.Vbase"` rather than a second `"Output5V"` — via
`core::Port::qualifiedBy`. Without that, two of the fifteen measurements taken at
one pin would share one injection slot.

## Readings the instrument refuses to make

An Infiniium answers a measurement it could not make with `9.99999E+37`, and with
`:MEASure:SENDvalid` on it returns a numeric result state saying *which* of some
thirty specific things went wrong. `hal::MeasurementFault` carries the subset
this bench can reach, with the instrument's own code numbers.

Such a reading throws `core::UnmeasurableReading` out of `rawMeasure()`, and
`core::MeasureEngine` catches it:

- by default the reading is **NaN**, which fails every criterion, reports the
  instrument's reason in the log beside the point name, and lets the run carry
  on to the next check
- `.whenUnmeasurable( callable)` substitutes the script's own meaning for the
  absence, and the callable may take the reason:

```cpp
Osc1.channel<3>().vmin().whenUnmeasurable( []{ return 0_V; })
Osc1.channel<3>().vmin().whenUnmeasurable( []( auto reason)
{
    return reason.contains( "min not found") ? 0_V : Voltage{ NAN };
})
```

This replaces the legacy ATE's `if( ISINVALID( dVOLTMIN)) dNEGTRANSIENT = 0;` —
the same decision, made on the line that asks the question rather than several
lines below it, and with the diagnosis kept rather than thrown away.

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
away. Same shape as `hal::L4411A`'s mode edge. See `tests/test_dso8064a.cpp`.

The *setting* methods have no such edge: they carry the channel number into the
config by value. Both the channel view and the builder it returns are
temporaries that whatever they produce safely outlives, which is tested
explicitly.

## Waveform transfer

`Fetch` takes the whole captured record off one channel, where the fifteen
measurements above each take one number out of it:

```cpp
const auto captured = Await( Osc1.single());

Verify( FS_Transient_1::FS_Transient_Captured, captured);

const auto trace = Fetch( Osc1.channel<3>().waveform());
const auto dip   = trace.minimum<Voltage>();
```

What comes back is a `core::Waveform` — samples **already scaled into volts**,
plus the timebase from `:WAVeform:PREamble?`. The scaling happens here and not
one layer up, because a raw level is a fact about this digitiser at this
vertical setting and this driver is the only thing that knows the encoding; a
recording holding raw levels would be unreadable without the instrument that
wrote it.

Each channel files under its own session key — `"Osc1.Channel3"`, via
`traceQualifier` — for the same reason the measurements carry `qualifiedBy`:
four channels hold four records at once, and one slot would let an injected
channel-1 trace answer a channel-3 `Fetch`.

`waveform()` does **not** switch the instrument's selected channel, unlike the
fifteen measurement methods — the channel travels in the config by value, so
the sharp edge below does not apply to it.

**Check the `Await` first.** A trace fetched after a capture that never
completed is the previous acquisition, and it will look like a perfectly good
trace.

This was deferred until the recording format could carry one, which was the
right order: an observation that could not be replayed would have been the one
hole in `--replay`. A trace row now carries its unit and timebase and refers its
samples to a file beside the recording (see `core/recording.hpp`).

## What it deliberately does not model

- **Segmented acquisition** — the enumerator exists because the instrument has
  it; nothing else supports it, because it changes what a "capture" is and the
  acquisition verbs are written for the single-record case.
- **Duty cycle, overshoot, preshoot** — dimensionless ratios, and this framework
  has no dimensionless quantity to return them as. A core question, not a driver
  one.
- **Trigger kinds beyond edge** (`GLITch`, `ADVanced` and the pattern, state,
  delay, TV and violation modes under it) — a large surface, each with its own
  parameter set, that nothing on this rig can exercise.
- **`:TRIGger:EDGE:SLOPe EITHER`** — because this instrument does not have it.
  The legacy script carries that line; either-edge triggering is an
  InfiniiVision feature, and modelling it would produce a driver that compiled a
  setting the scope cannot perform.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( DSO8064A, Osc1, Lan( "bench-osc1"))
rig/active_instruments.hpp  #include "hal/dso8064a.hpp"
rig/wiring.inc              which fabric channel its probe input lands on
```

One `WIRE_INSTRUMENT` row, not four. `hal::InstrumentWiring` keys on
`InstrumentId`, so all four channels reach the DUT through the same fabric path
— which matches this bench, where the four channels carry different probes onto
the same point. A rig whose scope channels land on *different* points would need
a per-channel wiring key, which does not exist today.
