# hal::keysight_dsox1202g::DSOX1202G

Keysight InfiniiVision **DSOX1202G** — two channels, 2 GSa/s, 70 MHz as shipped
and licence-upgradable to 200. Header-only, `namespace hal::keysight_dsox1202g`,
included as `"hal/keysight_dsox1202g.hpp"`.

Target: `hal_keysight_dsox1202g` / `Thorium::hal_keysight_dsox1202g`. Depends on
`Thorium::hal` only.

Reachable over `Usb`, and nothing else — see **One connector** below.

Modelled against the *Keysight InfiniiVision 1000 X-Series Oscilloscopes
Programmer's Guide*, version 01.01.0000 (February 2017).

**This is `Osc1`.** It replaced `hal::keysight_dso8064a::DSO8064A`, which is
still in this tree and on no bench: that driver was written against a legacy ATE
script, before anyone knew what scope this rig would actually have. This one was
written against the box on the desk. The two are not interchangeable, and the
section that matters most here is **What a ported script has to re-decide**.

## The four things a script does with it

```cpp
Setup( Osc1.timebase().timePerDivision( 10_ms).reference( TimebaseReference::Left));
Setup( Osc1.channel<2>().coupling( Coupling::Dc).voltsPerDivision( 100_mV));

Arm(   Osc1.single().timeout( 2_s));   // returns once the scope is armed
Remove( AcP1.ac());                    // the event to capture
const auto captured = Await( Osc1.single());

const auto dip = Measure( Osc1.channel<2>().vbase(), at( dut::Output5V))
               - Measure( Osc1.channel<2>().vmin(),  at( dut::Output5V));

const auto trace = Fetch( Osc1.channel<2>().waveform());   // the whole record
```

`suite/scripts/ac_dropout_script.cpp` is the first four written out in full.

## Two channels, and channel 1 is spoken for

`channel<3>()` has no valid instantiation. That is the model: the 1000 X-Series
is `DSOX1202A/G` with two analog inputs and an Ext Trig, and it is the 1200
X-Series `DSOX1204A/G` that has four. A script that names a third channel fails
to build rather than measuring the wrong thing.

On this rig the two are already allocated (`rig/wiring.inc`):

| Channel | What is on it |
|---|---|
| 1 | `RfMux1` bank 0's common — the HF path, for the DUT's config clock probe |
| 2 | `Matrix1` row 2 — the LF measurement bus, which is where a rail is read |

So a script measuring a rail says `channel<2>()`. This is the only reason the
suite's scope calls moved from channel 3 to channel 2 rather than to channel 1.

## One connector

`Lan( "bench-osc1")` does not compile against this driver, and that is the
instrument rather than an omission. The programmer's guide lists, among the
differences from the models around it, *"There is no LAN interface (only USB is
supported)"*, and gives `:HARDcopy:NETWork:ADDRess` as a command the 1000
X-Series does not have. It is the four-channel `DSOX1204A/G` that gained a LAN
interface.

So the constructor is constrained to `hal::Usb` (plus `hal::Simulated`, which
every driver accepts — see `hal/driver/address.hpp`). The row this driver
replaced accepted GPIB, LAN and USB and this rig's table said `Lan`, which is
exactly the sentence about hardware that does not exist that `hal::ReachableOver`
exists to reject.

**The row currently says `Simulated{}`,** because this driver does not open a
session yet. See **Still to come** below.

## Setup — four subsystems, four builders

One builder per SCPI subsystem the instrument actually has, rather than one
flattened bag of scope settings. Each `Setup` call then says which part of the
instrument it is touching, and the split matches how the settings interact.

| Builder | Setting | Values |
|---|---|---|
| `Osc1.trigger()` | `edgeSource<N>()` | channel number, compile-checked |
| | `slope()` | `TriggerSlope::Rising` `Falling` `Either` `Alternating` |
| | `level()` | a `Voltage` |
| | `sweep()` | `TriggerSweep::Auto` `Normal` |
| | `coupling()` | `TriggerCoupling::Dc` `Ac` `LowFrequencyReject` |
| | `reject()` | `TriggerReject::Off` `LowFrequency` `HighFrequency` |
| | `holdoff()` | a `Time`, 60 ns to 10 s |
| `Osc1.timebase()` | `timePerDivision()` | a `Time` |
| | `position()` | a `Time`, relative to the reference below |
| | `reference()` | `TimebaseReference::Left` `Center` `Right` |
| `Osc1.acquisition()` | `type()` | `AcquisitionType::Normal` `Averaged` `HighResolution` `PeakDetect` |
| | `averagedOver()` | a count, 2 to 65536 — also selects `Averaged` |
| `Osc1.channel<N>()` | `coupling()` | `Coupling::Dc` `Ac` |
| | `voltsPerDivision()` | a `Voltage` |
| | `verticalOffset()` | a `Voltage` — the level at screen centre |
| | `bandwidth()` | `Bandwidth::Full` `Limited` (~25 MHz) |
| | `probeAttenuation()` | a ratio: `10.0` for a 10:1 probe |
| | `display()` | `ChannelDisplay::On` `Off` |

An unset field means *leave whatever is already configured*, so a `Setup` naming
only the trigger level does not reset the slope.

## What a ported script has to re-decide

Six differences from the Infiniium driver, each of them a fact about the
instrument. The first four are compile errors, which is the point — a ported
script cannot quietly keep meaning something else.

| Was | Is | Why |
|---|---|---|
| `channel<3>()`, `channel<4>()` | `channel<1>()`, `channel<2>()` | two analog inputs on this model |
| `.input( ChannelInput::Dc1M)` | `.coupling( Coupling::Dc)` | `:CHANnel<n>:IMPedance` takes only `ONEMeg`, so impedance is not a setting and coupling stands alone |
| `.probeAdapter( ProbeAdapter::Div10)` | `.probeAttenuation( 10.0)` | `:CHANnel<n>:PROBe` takes a ratio (0.1 to 10000), not one of four named adapters |
| `.mode( AcquisitionMode::…)` | `.type( AcquisitionType::…)` | the command is `:ACQuire:TYPE`; this instrument's `:ACQuire:MODE` is real-time-against-segmented and means something else |
| `.automaticPoints()`, `.points( n)`, `.sampleRate( … )` | *(nothing)* | `:ACQuire:POINts?` and `:ACQuire:SRATe?` are query-only here: depth follows from the timebase |
| `.unaveraged()` | *(nothing)* | averaging is one of the acquisition *types*, so choosing any other type is already saying it |

And one that is not a compile error, which makes it the one to read twice:

**A measurement this scope cannot make gives no reason.** The Infiniium answers
`9.99999E+37` and, with `:MEASure:SENDvalid` on, one of some thirty result-state
codes: *waveform is clipped high*, *required edge not found*, *top and base are
equal*. There is no `:MEASure:SENDvalid` on the 1000 X-Series. The guide gives
one sentence — a measurement that cannot be made returns `+9.9E+37`, *"typically
because the proper portion of the waveform is not displayed"* — and `+9.9E+37` is
also this instrument's spelling of infinity.

So `core::UnmeasurableReading` always carries the same words here
(`kUnmeasurable`), and a `whenUnmeasurable` handler that branched on the reason
now takes one branch always. `ac_dropout_script.cpp` had such a handler:
*"min not found" means no excursion, which is a dip of zero volts and a pass;
anything else is a bench fault and should fail*. On this instrument that
distinction cannot be made, so the script now fails on any unmeasurable minimum
— the safe direction, because a substitution that can turn a blank screen into a
passing test is the one outcome a test rig must not produce.

`vmiddle()` is also gone: there is no `:MEASure:VMIDdle` on this family, and
computing it from `vtop()` and `vbase()` would put a number in the run journal
that no instrument ever answered.

## What it can do that the Infiniium could not

`TriggerSlope::Either`. The other driver's own comment says why it has no such
enumerator — *"either-edge triggering is an InfiniiVision feature"* — and this is
an InfiniiVision. `Alternating` is there too, and is a running-display feature: a
single-shot capture that asks for it gets whichever edge the scope was on.

## Arm and Await

A single-shot capture is two verbs because the thing being captured happens
between them, and the script is what causes it (see `core/verbs/acquire.hpp`).

`Arm` returns once the scope is **armed and ready**, not once it has been told to
arm. On real hardware that is the guide's own sequence, and the ordering is what
makes it work:

```
:STOP            *OPC?              # let the previous acquisition settle
:SINGle                             # arm
:AER?            → poll until 1     # "oscilloscope is armed and ready, enable DUT here"
:OPERegister:CONDition?  → poll until the RUN bit (0x08) clears
```

`:DIGitize` is deliberately *not* how this is done: it blocks the instrument
against further commands until the acquisition completes, so a single-shot DUT
that has not been enabled yet can never be enabled. Keysight's guide says so
directly.

`Await` watches the RUN bit rather than `:TER?` (the trigger event register):
`:TER?` says a trigger happened and is cleared by being read, but a triggered
scope may still be filling its record. A cleared RUN bit says the capture is
over and the record can be measured.

Awaiting a capture that was never armed answers `false` rather than throwing —
a script that measures a transient it never armed for has produced a wrong
answer, and the check the `Await` gates is exactly where that should surface.

## What it measures

Fourteen readings, each a `core::Port` qualified by which measurement it is, so
two answers about one pin key as `Output5V.Vbase` and `Output5V.Vmin` rather
than colliding in one session slot.

| Family | Ports |
|---|---|
| Amplitude | `vpp()` `vmax()` `vmin()` `vrms()` `vaverage()` `vbase()` `vtop()` `vamplitude()` |
| Timing | `frequency()` `period()` `riseTime()` `fallTime()` `positiveWidth()` `negativeWidth()` |

`vbase()`/`vtop()` are not `vmin()`/`vmax()`, and for transient work that is the
difference that matters: min and max are the extreme samples in the record, so
one spike moves them, where base and top are the settled levels the waveform
spends its time at. `vbase() - vmin()` is therefore the depth of a negative
transient measured against the rail it departed from.

`riseTime()`/`fallTime()` carry 10%/90% thresholds up front, overridable
(`.lowThreshold( 0.2).highThreshold( 0.8)`). That matches the instrument's own
`STANdard` threshold definition, and it is stated rather than left implicit
because a threshold is part of what a rise time *means*.

`vrms()` and `vaverage()` take the instrument's documented defaults for the
parameters this driver does not send: whole-screen (`DISPlay`) interval, and DC
RMS. A script needing cycle-RMS of ripple alone needs a parameter added here.

Not modelled: duty cycle, overshoot and preshoot, all of which the instrument
measures. They are dimensionless ratios and this framework has no dimensionless
quantity to return them as — a `core` question, not a driver one.

## The two sharp edges

Both are inherited from the shape every scope driver here has, and both are
documented rather than fixed because the fix is worse.

**A `Port` reads the channel and mode current when `rawMeasure()` runs**, not
when the handle was taken. `Port` binds to the instrument rather than to the
temporary channel view, because binding to the view was a real dangling
reference — caught by a test, not by the compiler. Harmless for
`Measure( port, at( ...))`, which reads immediately.

**The setting builders do not have that edge**: they carry the channel number
into the config by value. So does `waveform()`.

## Waveform transfer

`Fetch( Osc1.channel<2>().waveform())` hands back a `core::Waveform` — samples
already scaled into volts, plus the timebase that positions them. Each channel
files under its own session key (`Osc1.Channel2`), because two channels hold two
records and one slot for both would let an injected channel-1 trace answer a
channel-2 fetch.

`:WAVeform:POINts` and `:WAVeform:POINts:MODE` — how much of the record to
transfer — are real settings on this instrument and are deliberately not modelled
while there is no transport to spend the transfer time on. They are the first
thing `WaveformBuilder` should grow when there is.

## Still to come

**A transport.** Every reading here comes from the driver's own simulation hooks,
and the address is stored and never opened, exactly as the scope this replaced
did. Replacing the instrument and replacing the transport are two changes, and
doing both at once would make *"the readings changed"* and *"the wiring changed"*
indistinguishable in one diff. `instruments/keysight_edu34450a` is the worked
example of the second step; when it lands here, this driver moves to hal API 3,
grows a `.cpp`, becomes `STATIC` in its `CMakeLists.txt`, and `Osc1`'s row gets
the instrument's real USB serial.

**The waveform generator.** The G in DSOX1202G is a built-in 20 MHz function
generator. It is a *source*: modelling it means `applyDriver`, `removeDriver` and
a place in the rig's safing sequence, not one more `Setup`. This rig does not use
it, so the driver is passive and `Apply( Osc1... )` is a compile error.

**Segmented acquisition**, which is a licensed option (SGM) this instrument may
not even have, and **the trigger kinds beyond edge** (`GLITch`, `PATTern`, `TV`,
and this model's serial-bus triggers), each a large surface with its own
parameters that nothing on this bench exercises.

## One bench consequence worth acting on

This scope's inputs are 1 MOhm and cannot be anything else. `RfMux1` is a 50 ohm
card and its bank 0 common is cabled to channel 1, so the HF path is now
unterminated at the instrument end and a config clock read through it will show
reflections rather than the edge it went looking for. The fix is a 50 ohm
feedthrough terminator at that BNC — a part to buy, recorded as a `TODO(bench)`
in `rig/wiring.inc` where the rest of the wiring facts live.

## Adding it to a rig

One row, and nothing else:

```cpp
// rig/instrument.inc
INSTRUMENT( keysight_dsox1202g::DSOX1202G, Osc1, Usb( "MY12345678"))
```

The namespace qualifier is what generates this driver's `#include`
(`cmake/InstrumentDrivers.cmake`), and the id becomes both the global a script
names and the `hal::InstrumentId` enumerator a reading is filed under.

## Sources

- *Keysight InfiniiVision 1000 X-Series Oscilloscopes Programmer's Guide*,
  version 01.01.0000, February 2017 — the command set modelled here.
- *Keysight InfiniiVision 1200 X-Series and EDUX1052A/G Oscilloscopes
  Programmer's Guide* — the successor family, and the document that states which
  differences (four channels, a LAN interface, the count measurements) belong to
  the `DSOX1204A/G` and not to this model.
