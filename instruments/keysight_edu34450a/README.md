# hal::keysight_edu34450a::EDU34450A

Keysight EDU34450A 5½-digit dual-display bench DMM. Namespace
`hal::keysight_edu34450a`, included as `"hal/keysight_edu34450a.hpp"`.

**This is the driver that talks to real hardware.** Every driver in
`instruments/` answered its readings out of its own simulation hooks; this one
opens a SCPI session over a socket and reads the meter. See
[On the wire](#on-the-wire) for exactly what it sends, and
[Bringing one up](#bringing-one-up) for how to point it at a meter on a desk.

Not header-only any more, and the first here that is not: the template half
stays in the header, because a driver's `Port`-returning members are consumed
as types by the call site, and the SCPI half is an ordinary `.cpp` (see
`src/keysight_edu34450a.cpp`) because none of it depends on which quantity type
the answer gets wrapped in. The target is `STATIC` rather than `INTERFACE`, and
nothing outside its own `CMakeLists.txt` needed to change for that.

Target: `hal_keysight_edu34450a` / `Thorium::hal_keysight_edu34450a`. Depends on
`Thorium::hal` only.

Reachable over `Lan` or `Usb` — gigabit LAN and a rear USBTMC port, and both
work: LAN over a raw SCPI socket needing no vendor software, USB through
whatever VISA is installed (see [Bringing one up](#bringing-one-up)). No GPIB
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

## On the wire

Whether a reading comes off hardware or out of the simulation hooks is decided
by the address column of the rig's instrument table and by nothing else:
`Simulated{}` means the hooks are the instrument, `Lan( ... )` means the meter
at that hostname is. A script cannot tell the difference, and `isSimulated()`
is the one branch that reads it.

The whole conversation for one DC volts reading of a routed rail:

```
->  SYST:ERR?                   once per session, until empty -- whatever the
<-  +0,"No error"                 last user left queued is not this run's
->  *IDN?                       once per session
<-  Keysight Technologies,EDU34450A,MY60012345,01.00-01.00
->  CONF:VOLT:DC 10,1.5E-6      function, range, resolution
->  SYST:ERR?                   did it accept that
<-  +0,"No error"
->  READ?                       trigger, and hand back the reading
<-  +5.02010000E+00
```

Five commands for the first reading, three for each one after. Nothing is
cached between readings: the function is reconfigured before every one, which
costs a round trip (about a millisecond, against the ~20 ms a 5½-digit reading
takes) and buys the property worth having -- a reading does not depend on what
the run did before it, or on whether somebody pressed a front-panel key in
between.

### The function table

`CONFigure` both selects the function and resets that function's measurement
and trigger parameters to their defaults, which is why this driver never sends
a `*RST`: whatever the front panel left set is overwritten before every
reading.

| Port | Command | Discrete resolution? |
|---|---|---|
| `voltage()` | `CONF:VOLT:DC` | yes |
| `acVoltage()` | `CONF:VOLT:AC` | yes |
| `current()` | `CONF:CURR:DC` | yes |
| `acCurrent()` | `CONF:CURR:AC` | yes |
| `resistance()` | `CONF:RES` | yes |
| `fourWireResistance()` | `CONF:FRES` | yes |
| `frequency()` | `CONF:FREQ` | no — its resolution is in hertz |
| `capacitance()` | `CONF:CAP` | no — fixed at 3½ digits |

### What `MeasureSetup` does and does not reach

| Field | On this meter |
|---|---|
| `Range` | sent as `CONFigure`'s `<range>`, in the port's own unit — `.range( 10_V)` becomes `CONF:VOLT:DC 10`. Not calling it leaves the meter autoranging, which is its own reset default. A value that is not one of the meter's discrete ranges is **not** rounded up here: the instrument answers `-222,"Data out of range"` and that arrives as a `hal::io::ScpiFault` naming the command. Rounding would be this driver inventing a range the script did not ask for. |
| `Nplc` | ignored — there is no NPLC command in this model's SCPI at all. `setResolution()` is the speed/precision axis; see [Resolution, not NPLC](#resolution-not-nplc). |
| `Frequency` | ignored — this meter's AC path needs no hint about the signal it is looking at. |
| `LowThreshold`/`HighThreshold` | ignored — edge-timing thresholds for a scope's rise time. |

### The instrument's own rule about resolution

`<resolution>` may only accompany an explicit `<range>`: combined with
autoranging it is refused, because the meter cannot fix an integration time for
a range it has not chosen yet. So there are three forms, and which one is sent
follows from the port rather than from a preference:

| | Sent |
|---|---|
| a range, any resolution | `CONF:VOLT:DC 10,1.5E-6` |
| no range, `Slow` | `CONF:VOLT:DC` — `CONFigure` has just set 5½ digits, which *is* `Slow` |
| no range, `Medium`/`Fast` | `CONF:VOLT:DC` then `VOLT:DC:RES 2.0E-5` |

The three resolution values are the only three the meter accepts. The mapping
onto Slow/Medium/Fast is the one thing on this subject the programmer's
reference does not state outright, so: it gives `1.50E-6` as the default and
labels it 5½ digits, and says `MIN` is the smallest value accepted ("the
highest resolution") and `MAX` the largest ("the least resolution"). A coarser
resolution is a shorter integration, so least resolution is fastest —
`1.5E-6` slow, `3.0E-5` fast, `2.0E-5` the one left in the middle, which agrees
with the data sheet's three reading rates.

### Two traps this model has, both handled

**The secondary display answers too.** `READ?` returns *two* comma-separated
readings when the meter's secondary display is on — a front-panel state a
previous user can leave behind, and one this model's SCPI offers no documented
way to turn off. The primary reading is the first; the second is discarded,
since this driver exposes no port for it.

**Overload is a number.** An input beyond the selected range comes back as
`±9.9E+37` (the front panel shows `OL`) rather than as an error. That becomes
`core::UnmeasurableReading` carrying the function and the range —
`core::MeasureEngine` catches it, records NaN with the reason beside it, fails
whatever criterion was checking it, and carries on to the next check.

### The wrong instrument at the right address is refused

The session's first act after draining the error queue is `*IDN?`, and a model
field that is neither `EDU34450A` nor `34450A` throws. Both are accepted
because this driver is honestly for both — they share one command set, which is
why the 34450A's programmer's reference is the document it was written against.

Without that check, a re-cabled rack, a DHCP lease that moved or a copied row
in the instrument table produces a run full of readings from the wrong box, and
some of them pass.

## Bringing one up

The dev deployment exists for this: one meter, one cable, no rack (see
`dev/README.md`). Put the meter's address in `dev/rig/instrument.inc`, build,
and run:

```bash
cmake --preset windows-dev      # or macos-dev
cmake --build build/dev
./build/dev/bin/run_scripts
```

### Over USB

```cpp
INSTRUMENT( keysight_edu34450a::EDU34450A, Dmm1, Usb( "MY60012345"))
```

The serial number is the one on the label on the back of the meter, and it is
the whole address -- no bus number, no device number, nothing that changes when
somebody moves a cable. It goes through VISA (see `framework/hal/README.md` on
why USB is VISA and not libusb, especially on Windows), which enumerates the USB
instruments and matches on that serial. **You need Keysight IO Libraries Suite
or NI-VISA installed** -- almost certainly true already on a Windows or Linux
bench with a Keysight meter on it, since that is what Connection Expert is part
of. Nothing needs installing to *build*.

If the serial number in the table is not the meter that is plugged in, the error
lists what VISA did see, which is normally enough to fix the table in one edit:

```
no USB instrument with serial number MY60012345 -- VISA enumerated MY60099999
```

### Over LAN

```cpp
INSTRUMENT( keysight_edu34450a::EDU34450A, Dmm1, Lan( "dev-dmm"))
```

A hostname or a dotted quad; port 5025 unless you say otherwise. This path needs
no vendor software at all -- it is a raw SCPI socket -- so it is the one to
prefer on a machine with no VISA, and the one the framework's own tests can
exercise. Prefer a hostname over a literal address on a DHCP bench, since it
survives a lease change.

### GPIB is not this meter

The rack-mount 34450A takes a GPIB option; the EDU is the box that does not have
one, and the constructor says so -- `Gpib( ... )` on this row does not compile
(`hal::ReachableOver`). That is a claim about the back panel, checked at compile
time, and a different thing from whether the *build* can open a bus, which is a
runtime answer (see `hal::io::isSupported`).

### What the failures mean

Each one points somewhere different, which is the whole reason they are
distinct messages:

| | Means |
|---|---|
| `no VISA library could be loaded` | USB or GPIB on a machine with no IO Libraries/NI-VISA. Install one, or set `THORIUM_VISA_LIBRARY` if it is somewhere the loader does not look |
| `no USB instrument with serial number ...` | VISA is working and that meter is not on the bus. The message lists the serials it did find |
| `cannot resolve Lan dev-dmm:5025` | the hostname in the table is not this meter's |
| `cannot reach Lan ...: Connection refused` | something answers at that address but not on 5025 -- check the meter has LAN enabled rather than USB-only |
| `expected an EDU34450A or a 34450A and found ...` | reached an instrument, and it is a different one |
| `rejected "CONF:..."` with the meter's own words | reached the right meter and sent it something it will not accept -- a driver or script bug, and the message names the command |
| `timed out waiting for a reply` | the command was accepted and never answered; on this meter that usually means a reading slower than the 5 s default I/O timeout |

`--inject` and `--skeleton` detach the bench entirely and need no meter and no
VISA at all, which is what keeps a script's own unit tests hardware-free.

### One thing to know before the first USB run

The USB path has never been run against a real instrument. Everything about it
that is ordinary code is tested -- the resource strings, the serial matching,
every refusal -- but VISA itself is not installed on any machine this repository
is developed on, so `viOpen` on an actual meter is unproven. The LAN path *has*
been driven end to end against a real socket. If the first USB run misbehaves,
suspect this file and `framework/hal/src/io/visa_transport.cpp` before suspecting the meter,
and `framework/hal/README.md` says which questions were answered where.

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
                            ... or Simulated{} until a meter actually answers there -- this
                            driver connects, so the column is now an instruction
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
  — the SCPI dialect this model shares, and the document every command in
  `src/keysight_edu34450a.cpp` is taken from: `CONFigure`, `READ?`,
  `…:RESolution`, `…:RANGe[:AUTO]`, `SYSTem:ERRor?`, the `±9.9E+37` overload
  sentinel, and the `PRIMary`/`SECondary` split. Check a command against this
  rather than against another program's source.
