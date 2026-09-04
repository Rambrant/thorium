# hal::keysight_33522b::Wfg33522B

A Keysight (Agilent) 33522B Trueform waveform generator — two independent
30 MHz outputs, 1 µHz frequency resolution, 16 bits of vertical resolution,
1 mVpp to 10 Vpp into 50 Ω. `namespace hal::keysight_33522b`, included as
`"hal/keysight_33522b.hpp"`.

Target: `hal_keysight_33522b` / `Thorium::hal_keysight_33522b`. Depends on
`Thorium::hal` only. `STATIC`, like the three other drivers that talk.

This driver **opens a real SCPI session** when its rig row names a real address,
and answers from its own remembered state when the row says `hal::Simulated{}`.
It is the second *sourcing* driver here, after the EDU36311A supply, and the
first that produces a signal rather than a rail.

## Why the package is named for one model of eight

"33500B" is a **series**, and the eight members of it differ in exactly the
three things a driver has to get right:

| Model | Bandwidth | Channels | Arb |
|---|---|---|---|
| 33509B | 20 MHz | 1 | no |
| 33510B | 20 MHz | 2 | no |
| 33511B | 20 MHz | 1 | yes |
| 33512B | 20 MHz | 2 | yes |
| 33519B | 30 MHz | 1 | no |
| 33520B | 30 MHz | 2 | no |
| 33521B | 30 MHz | 1 | yes |
| **33522B** | **30 MHz** | **2** | **yes** |

They share one command set and one manual, which is what makes a series-wide
driver tempting and wrong: it would have to either believe 30 MHz on a 20 MHz
box or 20 MHz on a 30 MHz one, and would let a rig write `channel<2>()` on a
single-channel model. So the package names the model on the bench — the same
call `keysight_edu36311a` makes about its E36311A sibling, from the same
programming guide, for the same reason.

A second member of the series turning up wants a second package, not a template
parameter here. They are three constants apart, and a package in this tree is
meant to be cheap.

The manufacturer token is `keysight` although the unit very likely says Agilent
on the front: the series launched as an Agilent product and the company is
Keysight now. See `instruments/README.md` for the rule. It is also why the
identity check reads the **model** field of `*IDN?` and not the manufacturer
one — both badges answer with `33522B`, and either is this instrument.

## One instance for the whole box, and why that is the other answer

Two independent outputs behind one address is **one** instance, one
`hal::InstrumentId`, and the channel chosen at the call site:

```cpp
Apply( Wfg1.channel<1>().sine().frequency( 1_kHz).amplitude( 2_V));
Apply( Wfg1.channel<2>().square().frequency( 10_kHz).amplitude( 3.3_V).dutyCycle( 25.0));
```

That is deliberately the opposite of `keysight_edu36311a`, whose three outputs
are three instruments with three ids behind one chassis address. Both shapes
are right, and the question that picks one is the same each time: *is an
endpoint separately wired and separately rated, or is it not?*

- an EDU36311A's outputs **are**. They differ in what they can source, each
  feeds a different DUT rail down its own lead, and each would carry its own
  isolation relay if this rack had any. Three things a rig writes down
  separately are three rows.
- a 33522B's channels are **not**. They are identical, hard-cabled here, and
  routinely used together — a stimulus and its inverse, a clock and the signal
  it gates. So one row, one address, one id, and `channel<N>()`, which is the
  scope's arrangement rather than the supply's.

`channel<0>()` and `channel<3>()` do not compile.

One consequence worth knowing before writing a script: **safing is per box**, so
`hal::safeRig()` takes both channels down together. Per-channel is what
`Remove( Wfg1.channel<1>().sine())` is for.

Reachable over `hal::Lan`, `hal::Usb` or `hal::Gpib`. The first two are standard
on every 33500B Series unit; GPIB is the factory- or user-installed option, so
it is in the list because the back panel *can* have it. There is no RS-232 port
at all, so a `Serial( ...)` row does not compile.

## The shapes are tags, and that is what makes the settings honest

Seven entry points, one per output function, and each returns a builder whose
setters are **only** the settings that shape actually has:

| Entry point | `FUNCtion` | Max frequency | frequency | amplitude | offset | dutyCycle | symmetry |
|---|---|---|---|---|---|---|---|
| `.sine()` | `SIN` | 30 MHz | ✓ | ✓ | ✓ | | |
| `.square()` | `SQU` | 30 MHz | ✓ | ✓ | ✓ | ✓ | |
| `.ramp()` | `RAMP` | 200 kHz | ✓ | ✓ | ✓ | | ✓ |
| `.triangle()` | `TRI` | 200 kHz | ✓ | ✓ | ✓ | | |
| `.pulse()` | `PULS` | 30 MHz | ✓ | ✓ | ✓ | ✓ | |
| `.noise()` | `NOIS` | — | | ✓ | ✓ | | |
| `.dc()` | `DC` | — | | | ✓ | | |

`.dutyCycle()` on a sine is "no matching function", and so is `.frequency()` on
a DC level. That matters more here than the compile-time tidiness suggests: the
instrument **accepts** `FUNC:SQU:DCYC` while a sine is selected and simply
remembers it for next time, so the alternative to a compile error is a setting
that vanishes without a word.

Note the 200 kHz ramp ceiling. It is two decades below the sine ceiling **on the
same box**, and it is the one number in this driver most likely to catch a
script that moved a frequency from one shape to another without re-reading it.

## The termination travels with the amplitude

`.into( Termination::Ohms50 | Termination::HighImpedance)` is `OUTPut:LOAD`, and
it is not a rig constant — it is the frame of reference for every voltage in the
same chain. Changing it re-scales the numbers by two without changing a volt at
the connector:

> If the amplitude is 10 Vpp and you change the output termination setting from
> 50 Ω to high impedance, the displayed amplitude doubles to 20 Vpp.

So it is a setting on the builder, sent **first**, and it decides which limits
apply:

| | into 50 Ω | into high-Z |
|---|---|---|
| amplitude | 1 mVpp – 10 Vpp | 1 mVpp – 20 Vpp |
| peak either polarity | ±5 V | ±10 V |

Two values where the instrument accepts 1 Ω to 10 kΩ, and that is deliberate:
the limits above are stated in the guide for exactly these two cases, and
interpolating them for 75 Ω would be this driver inventing a number.

## The limits are enforced here, not by the instrument

`SettingOutOfRange` is thrown by `Apply` before anything reaches the wire, for
the reason `RatingExceeded` exists next door — a simulated instrument refuses
nothing, and every instrument in this repository's CI is simulated, so without
the check a script passes in CI and misbehaves on the bench.

What makes it sharper here than on the supply is *how* an attached 33522B fails
when the check is absent. It does not refuse and stop. It **clamps and carries
on generating**:

- `FREQ 10e6` while a ramp is selected → "Data out of range", frequency set to
  200 kHz, output still running. Fifty times the wrong frequency into the DUT.
- an offset with no headroom left by the amplitude → the instrument *moves the
  offset* to the largest value the amplitude allows, posts "Data out of range",
  and carries on. The DUT sees a signal centred somewhere nobody chose.

Checked: the frequency against the shape's own ceiling; the amplitude against
the termination; the offset against the peak limit; the coupled
`|offset| ≤ Vmax − Vpp/2`, but only when one config names both (when it names
one, the other is whatever the instrument already holds, which this driver has
not necessarily been told); the duty cycle against 0.01–99.99 %; the symmetry
against 0–100 %.

Not checked, deliberately: the *frequency-dependent* narrowing of the duty cycle
range. This model holds a 16 ns minimum pulse width, so the achievable duty
cycle is 1.6 % – 98.4 % at 1 MHz and 16 % – 84 % at 10 MHz. That is arithmetic on
a frequency a given config may not even carry, so the driver keeps the absolute
bound and the instrument keeps the frequency-dependent one.

## No ports, and that is not an omission

This instrument measures nothing. It has no `core::Port`-returning members, no
`rawMeasure`, and no simulated-reading hooks, because there is no reading to
simulate: a 33522B can be asked what it was *told* (`FREQ?`, `VOLT?`), which is
a readback of this driver's own instruction rather than an observation of the
world. Putting one in a run journal would record a number no instrument ever
measured.

What the generator is actually producing is measured by the scope, through the
fabric, like any other signal.

`function()`, `frequency()`, `amplitude()`, `offset()` and `termination()` are
here, per channel — but they are the **setting**, and they exist so a journal
line and a failure message can be written without a round trip.

## Hard-cabled: no Connect, no Disconnect

There are no relays in this rig at all, so this generator's outputs are cabled
straight through and there is nothing to switch. This driver declares no
`connectDriver`/`disconnectDriver`, so `Connect( Wfg1.channel<1>().sine())` is
"no matching function".

Deliberately **not** done with `keysight_edu36311a`'s `DirectWiring`/
`RelayIsolated` tag pair, even though this is exactly the `DirectWiring` case.
That driver's own comment says a third driver wanting the distinction is the
trigger to hoist the tags into `hal/driver/instrument.hpp` with an API-version
bump — and hoisting them to model an axis this rig has no hardware for would be
adding a template parameter with one legal value. If a relay is ever fitted in
this generator's lead, that is the moment for the hoist, and this class grows an
`Isolation` parameter then.

## On the wire

One `Apply` of a 3.3 Vpp 10 kHz square at 25 % duty cycle on channel 1, into a
high-impedance load:

```
SYST:ERR?                  once per session, until empty
*IDN?                      once per session -- refused if it is not a 33522B
OUTP1:LOAD INF
SYST:ERR?
SOUR1:FUNC SQU
SYST:ERR?
SOUR1:FREQ 10000
SYST:ERR?
SOUR1:FUNC:SQU:DCYC 25
SYST:ERR?
SOUR1:VOLT 3.3 VPP
SYST:ERR?
SOUR1:VOLT:OFFS 0
SYST:ERR?
OUTP1 ON
SYST:ERR?
*OPC?
```

### The order is the whole argument

Every step is where it is because the step after it means something different
otherwise:

1. **the termination**, because it is the frame of reference for every voltage
   below. Sent afterwards, a 3 Vpp amplitude programmed against 50 Ω silently
   becomes 6 Vpp the moment the load is set to `INF`.
2. **the function**, because the frequency and amplitude limits are
   function-dependent. A `FREQ` sent while the previous shape is still selected
   is validated against the *previous* shape's ceiling, then clamped again when
   the function changes under it — twice wrong, both times silently.
3. **the frequency**.
4. **the shape parameter**, because a duty cycle's own limits depend on the
   frequency just set.
5. **the amplitude**.
6. **the offset**, because setting the amplitude can move it: *"Setting
   amplitude from the remote interface can change the offset in order to achieve
   the desired amplitude."* The other way round, an offset the script chose is
   overwritten by one the instrument chose.
7. **the output on**, last. `OUTPut` closes a relay "without zeroing output
   voltage", so whatever is programmed at that instant is what appears at the
   connector.

Each through `checked()`, never `write()`, and one `SYST:ERR?` per command
rather than a single drain at the end — the ordering only holds if each step is
known to have landed before the next is sent.

### Why not `APPLy`

`APPLy:SQUare 10e3,3.3,0` would have replaced most of that in one command, and
is the wrong tool here for three independent reasons:

- **it discards the shape parameters.** "For square waveforms, `APPLy:SQUare`
  replaces the current duty cycle setting with 50%", and `APPLy:RAMP` "overrides
  the current symmetry setting and selects 100%". A driver built on it could not
  honour `.dutyCycle( 25.0)` at all.
- **it enables the output as part of the same command**, which removes the one
  ordering guarantee that matters.
- **it reaches settings nobody asked it to** — it turns off any modulation,
  sweep or burst in force, forces the trigger source to `IMMediate`, and
  overrides the voltage autorange setting.

### No `*RST`

It would reset **both** channels, and a script driving a stimulus on channel 1
and a clock on channel 2 would lose the clock on its next `Apply`.

### Channels in the keyword, not in a channel list

`SOURce1:`/`SOURce2:` prefixes the signal settings, `OUTPut1`/`OUTPut2` is the
connector. There is no `(@1)` here and no `INSTrument:SELect` — which is worth
saying, because the supply driver two directories over spends a long comment
choosing between exactly those two forms. There is no choice to make: this
family has one way to say which channel, it is per-command, and it leaves no
modal state behind.

The `[SOURce[1|2]:]` prefix is optional in the guide's syntax and this driver
always writes it. A file where half the commands carry a channel and half imply
channel 1 is a file where a copied line silently programs the wrong output.

## Safing

`safe()` takes **both** connectors off and then collapses each channel's signal
— `OUTP*n* OFF`, `SOURn:VOLT MIN`, `SOURn:VOLT:OFFS 0` — because `OUTPut OFF`
alone leaves the signal programmed, and a generator safed at 10 Vpp comes back at
10 Vpp the instant anything re-enables the output.

`VOLT MIN` rather than `VOLT 0`: 1 mVpp is this instrument's minimum amplitude
and zero is not legal, so `VOLT 0` would be refused — and on this path it would
be refused *silently*, since `write()` does not read the error queue.

Off first, then the signal, which is deliberately the reverse of the guide's own
advice for ordinary operation (minimise the amplitude *before* switching the
output, to avoid a glitch). That advice is about signal integrity on a working
bench; safing runs after a failure, where a millisecond of glitch on a connector
being switched off is not the hazard and a DUT still being driven is.

Only down a session that is already open, never opening one, and swallowing
`hal::io::TransportError` — see `instruments/README.md`'s rule.

## What is deliberately not here

| | |
|---|---|
| arbitrary waveforms | the B in 33522B is the arb-capable model, and `DATA:ARBitrary` plus `MMEMory` is how a point list gets in. That is a waveform **transfer**, not a setting: it wants the counted binary-block read `hal::io::ITransport` still does not have, and a decision about where a rig's arb files live |
| modulation and sweep | AM/FM/PM/PWM/FSK, `SWEep`, `LIST` — each a whole subsystem with its own carrier-plus-modulator vocabulary |
| burst | `BURSt:NCYCles` — N cycles on a trigger. The nearest thing here to a verb this framework already has (an Arm/Await shape, like the scope's single-shot), and the first thing to add if a script needs a stimulus that stops |
| pulse width and edges | `FUNC:PULS:WIDTh`, `:PERiod`, the two `TRANsition` times. A pulse here is programmed by frequency and duty cycle, which is one of the two ways this instrument allows (`FUNC:PULS:HOLD`); the other is width-based and would be a second, mutually exclusive set of setters |
| `VOLT:UNIT`, `VOLT:HIGH/LOW` | amplitude is Vpp here, always, with the unit on the value rather than in a persistent mode. Quoting an amplitude two ways in one codebase is how a factor of 2.83 gets into a test report |
| `OUTP:POLarity`, `:SYNC` | inversion and the sync connector — neither is part of the signal a DUT sees on this rig |
| channel coupling | `FREQ:COUPle`, `VOLT:COUPle` tie the two channels' settings together, which would make an `Apply` to one channel silently change the other — precisely the property this driver's one-instrument-two-channels shape relies on not being true |

## Bringing one up

Over **LAN**, put the hostname in the rig row and the driver opens a raw SCPI
socket on port 5025:

```
INSTRUMENT( keysight_33522b::Wfg33522B, Wfg1, Lan( "bench-wfg1"))
```

Over **USB**, put the serial number from the instrument's own `*IDN?` or its
`System > I/O Config` screen; `hal::Usb` routes through whatever VISA is
installed:

```
INSTRUMENT( keysight_33522b::Wfg33522B, Wfg1, Usb( "MY59003130"))
```

Over **GPIB**, if the option module is fitted:

```
INSTRUMENT( keysight_33522b::Wfg33522B, Wfg1, Gpib( 0, 10))
```

### What the failures mean

| Failure | Cause |
|---|---|
| `hal::io::TransportError` on the first `Apply` | nothing answered at that address — wrong hostname, box off, cable out |
| `hal::io::ScpiFault` on `*IDN?` | something answered and it is not a 33522B. Read the message: it will name what it found, and the other members of this series are the likely ones |
| `hal::io::ScpiFault` naming a command | the instrument refused it. `-222` is a value out of range; `-113` is a command this model does not have, which would be a bug in this driver |
| `SettingOutOfRange` | the script asked for something this model cannot produce. Nothing was sent |
| "no VISA library found" | a `Usb` or `Gpib` row on a machine with no VISA installed — a missing dependency, not a missing instrument |
| output reads off when this driver turned it on | excessive external voltage applied to the connector disables the output and posts an error. `outputIsOn()` asks the instrument, so it sees this; the driver's remembered state does not |

## Adding it to a rig

```
rig/instrument.inc     INSTRUMENT( keysight_33522b::Wfg33522B, Wfg1, Simulated{})
rig/wiring.inc         nothing -- this generator's outputs are hard-cabled, see above
```

One row for both channels. Which channel a script drives is written in the
script, at compile time, not in the rig table.

## Sources

- Keysight Trueform Series Operating and Service Guide, part number
  33500-90901 — <https://www.ee.columbia.edu/sites/default/files/content/docs/Keysight_33500B_Manual.pdf>
  (its "SCPI Programming Reference" chapter is the command reference for this
  family; there is no separate programmer's manual. Its "Models and Options"
  chapter carries the per-shape maximum-frequency table this driver's shape tags
  are written from)
- Keysight 33500B Series Waveform Generators Data Sheet —
  <https://www.keysight.com/us/en/assets/7018-05928/data-sheets/5992-2572.pdf>
  (the model table at the top of this file)
