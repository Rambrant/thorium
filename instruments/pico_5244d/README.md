# hal::pico_5244d — `Oscilloscope` and `WaveformGenerator`

Pico Technology **PicoScope 5244D** — two channels, 200 MHz, 1 GS/s at 8 bits,
8–16 bit flexible resolution, 512 MS shared memory, USB 3.
`namespace hal::pico_5244d`, included as `"hal/pico_5244d.hpp"`.

**Two faces of one box**, laid out as the DSOX1202G's are: `Oscilloscope` (the
digitiser, a sensor) and `WaveformGenerator` (the built-in function generator, a
source) are siblings, each a row of its own on the same box, sharing one open unit
through `hal::BoxConnection`. The package header includes exactly those two.

Target: `hal_pico_5244d` / `Thorium::hal_pico_5244d`. Depends on `Thorium::hal` only.
Reachable over `Usb` and nothing else.

**Status: written and tested against a fake library; not yet run against a unit.**
Everything in **What has to be confirmed on a bench** below is a statement about
the real `ps5000a` library that no test here can make.

## It is not like the other scopes

The DSOX1202G is an instrument that measures; this is a digitiser that returns
samples. Two consequences shape everything here.

**There is no SCPI and no session.** The only supported way to drive the unit is
Pico's `ps5000a` library, so this is the one driver in the tree that cannot avoid
a vendor binary — and it avoids everything else about one. The library is
**loaded at runtime** (`dlopen` / `LoadLibrary`), exactly as `hal/io/visa_transport`
loads VISA: no `ps5000aApi.h`, no import library, no `find_package`. A machine with
no PicoSDK builds the whole framework and gets, at the first reading:

```
no ps5000a library found (tried libps5000a.so.2, libps5000a.so, libps5000a.dylib, ...).
Install the PicoSDK or PicoScope, or set THORIUM_PS5000A_LIBRARY to the full path
```

`THORIUM_PS5000A_LIBRARY` is the escape hatch, and on macOS the usual answer —
PicoScope 7 keeps its libraries inside its application bundle. The Windows DLL
(`ps5000a.dll`, or `C:\\Program Files\\PicoTechnology\\SDK\\bin\\ps5000a.dll`, or `C:\\Program Files (x86)\\PicoTechnology\\SDK\\bin\\ps5000a.dll`) needs its dependencies (`picoipp.dll` and friends) findable, which
installing the PicoSDK or PicoScope does.

**Every `Measure` is computed here.** There is no `:MEASure`. A block capture is
taken and `measurements.hpp` calculates Vpp, Vbase, a rise time, … from the samples.
Those functions are pure and tested against waveforms with known answers, which the
DSOX1202G's firmware never was. One consequence is better than the scope it
mirrors: `core::UnmeasurableReading` carries a *specific* reason (*no complete edge
between the two thresholds in the record*, *the record is flat*) where the DSOX1202G
can only say "could not".

## The script surface

The same verbs and the same fourteen ports as `keysight_dsox1202g`:

```cpp
Setup( Osc2.timebase().timePerDivision( 10_ms).reference( TimebaseReference::Left));
Setup( Osc2.channel<2>().coupling( Coupling::Dc).range( 5_V).probeAttenuation( 1.0));
Setup( Osc2.trigger().edgeSource<2>().slope( TriggerSlope::Falling).level( 4_V));

Arm( Osc2.single().timeout( 2_s));
Remove( AcP1.ac());
const auto captured = Await( Osc2.single());

const auto dip = Measure( Osc2.channel<2>().vbase(), at( dut::Output5V))
               - Measure( Osc2.channel<2>().vmin(),  at( dut::Output5V));

const auto trace = Fetch( Osc2.channel<2>().waveform());
```

What a DSOX1202G script has to change, each a compile error:

| DSOX1202G | here | why |
|---|---|---|
| `voltsPerDivision( v)` | `range( v)` | a PicoScope has no divisions: its vertical setting is a ±full-scale range (±10 mV … ±20 V). `range( 3_V)` picks the smallest that holds ±3 V; above ±20 V (× probe ratio) is `SettingOutOfRange`, never a silent clamp |
| `trigger().coupling()`, `.reject()`, `.holdoff()` | *(nothing)* | the trigger is a comparator on the digitised signal; a builder offering them would accept settings it ignores |
| `TriggerSlope::Alternating` | *(nothing)* | a running-display feature |
| `acquisition().type()`, `.averagedOver()` | `acquisition().resolution()` | block mode has no averaging; what it has is 8–16 bit resolution, traded against sample rate and channel count |
| *(nothing)* | `timebase().points( n)` | a digitiser has no screen to take the sample count from; default 2000 |

`verticalOffset` means what it does on the DSOX1202G (the level at the centre of the
range, at the probe tip); the SDK's own offset is the opposite sign — a voltage
*added* to the input — and is converted in the driver.

### What a Measure does

* After a completed `Arm`/`Await`, `Measure` and `Fetch` read **that** capture, as
  a stopped scope measures its frozen record: `vbase()` and `vmin()` of one dip
  come from one acquisition. This is what `ac_dropout`-style scripts need.
* With nothing held, each `Measure` takes a **fresh free-running block capture** (no
  trigger) and measures that.
* Any `Setup`, a new `Arm`, and `safe()` drop the held capture, so a changed range
  is never measured against a record taken at the old one.
* A capture whose input **overflowed its range** is not measured: its extremes are the
  range, not the signal. It is an `UnmeasurableReading` saying to use a larger range.
* A channel no `Setup` gave a range is `NotSetUp` — a fault, not a reading. There is
  no front-panel state to inherit; a channel nobody configured is off.

### Arm

The SDK has no "armed and ready" flag (the DSOX1202G's `:AER?`), so `Arm` starts the
block and then waits out the **pre-trigger fill** — the time the unit needs before
any trigger can give a whole record — plus 5 ms. `armTimeout` is the longest it will
accept for that; a longer fill throws rather than return early.

`Await` polls `IsReady`; a timeout answers `false` and stops the unit (a result, not
a fault). The trigger defaults to the first set-up channel, rising, 0 V, **Auto**
(self-triggers after 1 s), which is the DSOX1202G's default too. Say
`sweep( TriggerSweep::Normal)` to wait for a real edge.

### Timebase

`timePerDivision` × 10 is the window; `points` is how many samples it should hold.
The SDK wants a timebase *number*, whose meaning differs by resolution and channel
count, so it is **found by search** through `GetTimebase2` (finest interval not
exceeding window ÷ points) rather than computed from a formula that could be wrong
without hardware to say so. `reference` and `position` place the trigger in the window
as they do on the DSOX1202G; a window that lies wholly after the trigger uses the
SDK's trigger delay. A window the unit's memory cannot hold with the channels now on is
a `SettingOutOfRange` before anything runs.

## Bringing one up

1. **Install the PicoSDK or PicoScope**, so the `ps5000a` library and the USB driver
   exist. Close PicoScope itself — the unit opens once.
2. **Get the serial**: on the back label, or `JO…/…` from PicoScope's Help > About.
3. **Put it in the rig table**:
   ```cpp
   INSTRUMENT( BenchScope2, pico_5244d::Oscilloscope, Osc2, Usb( "JO123/0456"))
   ```
4. **Run something that measures through it.** The unit opens on first use. If it
   answers that it is on USB power or a USB 2.0 port, the driver tells the SDK to
   carry on (`ChangePowerSource`), which a two-channel unit can do; for full
   performance use the supplied DC adapter and a USB 3 port.

| What you see | What it is |
|---|---|
| `no ps5000a library found (tried …)` | no PicoSDK here, or in a place no loader looks — set `THORIUM_PS5000A_LIBRARY` |
| `OpenUnit failed: PICO_NOT_FOUND … units found: …` | not plugged in, already open in PicoScope, or a different serial; the message lists what *is* attached |
| `expected a PicoScope 5244D and found "…"` | the serial belongs to another model — a copied row or a re-cabled bench |
| `SetChannel failed: …` / `PicoFault` | the SDK refused; the status is named where known, hex otherwise (`PicoStatus.h`) |
| `… needs N samples and the unit holds M` | the window is too long for the memory left with these channels on |

**The unit is shared, not a session.** `BoxConnection` was written for an SCPI
session; this unit has a library and a handle instead. Hal API 7 gave the box a
*native* slot beside the session (`native<T>()`, `openNative<T>()`,
`installNative<T>()`), with the session's rules: shared per box and address, opened
lazily by whichever face speaks first, found without being made by safing, released
by `closeSession()` from either face. What it holds is `hal::pico_5244d::Hardware`
(`unit.hpp`): the library and the open `Unit`. Each face keeps its *own* state off to
the side — the scope what each channel was last given, the generator what it last
played — and notices a unit that is not the one it configured by its `Generation`,
so a unit reopened after `closeSession()` gets the scope's setup again.

The identity is `Pico Technology,5244D,<serial>,<driver version>`, shaped like an
`*IDN?` so the preflight banner and pool acquisition need no special case.

## The generator face

`WaveformGenerator` is a source: `Apply( Wfg3.sine().frequency( 1_kHz).amplitude( 1_V))`,
`Remove( Wfg3.sine())`, safed with the rest of the rig. One output (the Gen BNC), no
`channel<N>()`. Shapes are tags, so a builder offers only what its shape has — and
this generator has no duty cycle, no symmetry, no pulse and no load setting, so those
setters do not exist: `sine`, `square` (50 %), `triangle`, `rampUp`, `rampDown`,
`noise`, `dc`.

It differs from the DSOX1202G's generator where the instrument does:

| | DSOX1202G `WaveformGenerator` | `pico_5244d::WaveformGenerator` |
|---|---|---|
| Wire | `:WGEN:...` over SCPI, one command per setting | one SDK call, `ps5000aSetSigGenBuiltInV2`, everything at once |
| Partial Apply | changes what it names | the SDK takes it all, so the driver carries over the last amplitude, frequency and offset; one never set that has no zero is refused |
| Output off | `:WGEN:OUTPut OFF` | none in the SDK — DC at 0 V, 0 Vpp |
| Readback | `outputIsOn()` asks the instrument | none: `outputIsOn()` is what was last sent |
| Load | `.into( Termination)` | none — a fixed 600 Ω source, quoted for a high-impedance load |
| Range | 20 MHz, 5 Vpp open circuit | 20 MHz, **4 Vpp and ±2 V together** |

The last row matters: the SDK *clips* a waveform that leaves the output range and
answers `PICO_OK`. So the driver refuses it first, with the coupled form
`|offset| + amplitude / 2 <= 2 V`.

## What has to be confirmed on a bench

Written from the *PicoScope 5000 Series (A API) Programmer's Guide* and checked
against Pico's own Python wrappers. It has not met a unit. In rough order of risk:

1. **Every hand-declared prototype in `src/library.cpp`.** A typo there is a crash,
   not a compile error. Each is written beside the vendor's own prototype to be
   checked by eye against `ps5000aApi.h`.
2. **The sign and scope of the analog offset** (`ps5000aSetChannel`'s `analogOffset`
   is taken as "added to the input before digitising"), and the **trigger threshold
   conversion** built on it. A 1 V DC source with an offset of 0 and then 1 V settles
   both in one run.
3. **`SetSimpleTrigger`'s `delay`** (samples after the trigger before the first
   sample) and the *Arm* fill-time reasoning.
4. **Monotone timebases** — the search assumes the interval never decreases as the
   number rises and that the low numbers are refused rather than wrong.
5. **The 5244D MSO** is refused at the identity check because its variant string is
   different and its digital inputs change what a channel means to the SDK. If its
   analog half is wanted, accepting it is one line in `Oscilloscope::session()`.
6. **The generator**: the `PS5000A_WAVE_TYPE` numbers (taken from the guide's
   enumeration order), the frequency ceiling of 20 MHz for every shape, the ±2 V /
   4 Vpp limits (from the data sheet), the 0 V DC "off", and that
   `SetSigGenBuiltInV2` with start = stop frequency and no trigger source plays
   continuously. `PicoGenerator` in the dev suite is the check.
7. Statuses beyond the first eighteen in `describeStatus` are hex; add names as they
   are met.

## Not modelled

* The generator's **AWG**, sweeps, shots, triggering of the generator, and the
  shapes beyond the seven above (sinc, Gaussian, half-sine, PRBS).
* Streaming mode, rapid block, ETS, advanced and pulse-width triggers, downsampling,
  the MSO's digital channels, 50 V range.

## Files

```
include/hal/pico_5244d.hpp                   the package header (hal API gate)
include/hal/pico_5244d_oscilloscope.hpp      the oscilloscope face
include/hal/pico_5244d_waveform_generator.hpp the generator face
include/hal/pico_5244d/unit.hpp              Unit and Hardware: what the two faces share
include/hal/pico_5244d/library.hpp           Ps5000a: the vendor API as an interface, + loader
include/hal/pico_5244d/measurements.hpp      the :MEASure subsystem, as pure functions
include/hal/pico_5244d/{vocabulary,trigger,timebase,acquisition,channel,single,fetch,channel_view,customization}.hpp
src/library.cpp                              dlopen/LoadLibrary and the 17 entry points
src/unit.cpp                                 opening the unit, the model check, the identity
src/oscilloscope.cpp                         every capture SDK call, in the order the unit sees it
src/waveform_generator.cpp                   the one generator call
src/measurements.cpp
tests/test_measurements.cpp                  the calculations against known waveforms
tests/test_pico_5244d.cpp                    both faces against a fake Ps5000a
```

## Sources

* *PicoScope 5000 Series (A API) Programmer's Guide* — Pico Technology.
* `picosdk-python-wrappers`, `ps5000a.py` — the argument types and enumerations.


