# hal::keysight_edu36311a::EDU36311A

One output of a Keysight EDU36311A triple-output DC bench power supply — 90 W
in one chassis as 6 V/5 A, 30 V/1 A and 30 V/1 A, each independently
programmable and independently switchable. `namespace hal::keysight_edu36311a`,
included as `"hal/keysight_edu36311a.hpp"`.

Target: `hal_keysight_edu36311a` / `Thorium::hal_keysight_edu36311a`. Depends on
`Thorium::hal` only. `STATIC`, like the two other drivers that talk.

This driver **opens a real SCPI session** when its rig row names a real address,
and answers from its own state when the row says `hal::Simulated{}`. It is the
first *sourcing* driver here that does either.

## One instance per output, not per chassis

Three outputs behind one LAN hostname or one USB serial is three instances, one
`hal::InstrumentId` each, with the address repeated:

```cpp
hal::keysight_edu36311a::DirectOutput1 dcP5{ hal::InstrumentId::DcP5, hal::Simulated{} };
hal::keysight_edu36311a::RelayOutput2  dcP6{ hal::InstrumentId::DcP6, hal::Simulated{} };
hal::keysight_edu36311a::RelayOutput3  dcP7{ hal::InstrumentId::DcP7, hal::Simulated{} };
```

Same shape `DcP1..DcP4` have behind one N6701A mainframe address — one box,
several endpoints, so the address repeats and the endpoint is elsewhere.

Where this differs from the N6701A is *where* elsewhere. That mainframe has
four slots and any module can be in any of them, so its channel number is a
constructor argument. An EDU36311A has three outputs built into the chassis:
output 1 **is** the 6 V/5 A one and cannot be anything else. So the output is a
type parameter carrying both facts, and a rig cannot write down a combination
the instrument does not have.

| Alias | Channel | Rating | Isolation relay |
|---|---|---|---|
| `DirectOutput1` | 1 | 6 V / 5 A | no |
| `DirectOutput2` | 2 | 30 V / 1 A | no |
| `DirectOutput3` | 3 | 30 V / 1 A | no |
| `RelayOutput1` | 1 | 6 V / 5 A | yes |
| `RelayOutput2` | 2 | 30 V / 1 A | yes |
| `RelayOutput3` | 3 | 30 V / 1 A | yes |

Reachable over `hal::Lan` or `hal::Usb` and nothing else — that is this model's
back panel, the same as its Smart Bench Essentials sibling the EDU34450A. A row
addressing one over GPIB does not compile.

## Isolation is a type parameter

`Connect`/`Disconnect` exist only where there is a relay to move. A
`DirectOutput*` instance has no `connectDriver` overload that compiles at all,
so `Connect( DcP5.dc())` is a compile error rather than a call that quietly
closes nothing — `hal::keysight_edu36311a::SwitchableIsolation` doing the work,
the same mechanism `hal::keysight_edu36311a::EDU36311A` uses.

Because it is a concept rather than a per-tag overload, a new relay-having
isolation tag gets `Connect`/`Disconnect` for free with no change here — which
`tests/test_keysight_edu36311a.cpp` asserts using a tag the driver has never
heard of.

`DirectWiring`/`RelayIsolated` are declared here rather than shared with the
N6701A on purpose: a driver package compiles against `hal` and nothing else, so
that this directory can be zipped and dropped into another rig on its own. Two
drivers is not yet a reason to hoist them into `hal`; a third would be.

### One bench consequence, and it is arithmetic

On this repo's rig the isolation relays are on `Spst1`, a Racal 1260-18, whose
relays are rated **2 A**. Outputs 2 and 3 are 1 A outputs and fit. Output 1 is a
**5 A** output and no relay in the rack will carry it — so `DcP5` is
`DirectOutput1`, cabled straight through, and its lead is not switchable at all.
Same distinction `DcP1`/`DcP2` against `DcP3`/`DcP4` already records, arrived at
from a current rating instead of from history.

## Sourcing

`dc()` returns an immutable builder: `.voltage()`, `.currentLimit()`,
`.overVoltageProtection()`, each returning an updated copy rather than mutating.
Pass the result to `Apply` / `Remove`.

```cpp
Apply( DcP6.dc().voltage( 24_V).currentLimit( 0.5_A).overVoltageProtection( 27_V));

const auto drawn = Measure( DcP6.measuredCurrent());

Remove( DcP6.dc());
```

`Apply` needs no fabric or wiring and can be called before `Connect` ever is:
programming a supply does not require the DUT to be wired up yet. `Remove`
disables the output without disconnecting — that is `Disconnect`'s job, on its
own schedule.

`overVoltageProtection()` sets the level at which the instrument's own hardware
trips the output off, in under 5 ms, whatever the PC is doing. It is **off at
`*RST`**, so a config that does not name it gets no protection beyond the rating
check below — which is a decision about the DUT and belongs in the script, not a
default this driver invents.

OCP (`CURR:PROT`) is not modelled. It is a *trip* rather than the limit
`.currentLimit()` sets — it turns the output off after a delay in
constant-current mode instead of holding the rail there — and that is a
different question about a DUT.

## Reading back

`measuredVoltage()` / `measuredCurrent()` are what the supply reports about its
own output over its own interface — not a routed measurement, so there is no
`at(...)` and the fabric is never touched. A disabled output reads **zero** for
both rather than its last setpoint.

`core::MeasureSetup`'s fields are all ignored, and for once that needs no
argument per field: a supply's readback is not configurable. No range, no
integration time, no trigger, no thresholds.

Reading the rail **at the DUT** is a stricter check than either of these, and
the two are not interchangeable: a supply's own readback cannot see cable drop
or a bad contact. `suite/scripts/rig_power_on.cpp` spells that out.

`outputVoltage()` and `currentLimit()` are the *setpoints* — what the driver was
told, for a journal line to render. On an attached supply they can legitimately
differ from the readings: a current-limited output sits below its voltage
setpoint, which is the whole point of a limit.

## The rating is enforced here, not by the instrument

`Apply` throws `hal::keysight_edu36311a::RatingExceeded` — before anything
reaches the wire — for a voltage, a current limit or a trip level beyond the
output's badge.

This is the one place the driver overrules a script rather than relaying the
instrument's judgement, and the reason is an asymmetry it removes. An attached
EDU36311A refuses 24 V on output 1 by itself (`-222`, arriving as an
`hal::io::ScpiFault` naming the command). A **simulated** one refuses nothing,
because there is nothing there to refuse. So without this the same script passes
in CI and fails on the bench — and every instrument in this repository's CI is
simulated.

The instrument's own few percent of programming headroom is given up
deliberately: 6.18 V is a number an E36300-series supply accepts and no bench
rail is meant to sit at.

`RatingExceeded` is a `std::out_of_range` and is caught nowhere. Unlike
`core::UnmeasurableReading`, it is not a result — a run that continued past it
would be a run driving a rail nobody chose.

## Safing

`safe()` disables the output **and zeroes the setpoint**. Both, because `OUTP
OFF` on the real instrument leaves the setpoint where a test left it, so a
supply safed at 24 V comes back at 24 V the instant anything enables the output
again — a front-panel press, a half-initialised driver, a reconnecting console.

On an attached supply it says both on the wire, and **only down a session that
is already open**. It never opens one: safing runs when a script has already
failed, quite possibly *because* this supply is unreachable, and
`hal::safeRig()` does not catch. The sends are `write()` rather than `checked()`
and a transport error is swallowed, so a supply that cannot be reached does not
abandon the safing of every instrument after it. See `instruments/README.md`,
whose rule this instrument is the most important case of: a rail left on by a
failed run is the one thing on a bench that safing genuinely has to reach.

The current limit and the trip level are deliberately *not* cleared: with the
output off and the setpoint at zero neither has anything to act on, and an
accidental re-enable is safer finding them in place than finding none.

## Two answers to "is this output live"

Both are the electrical interlock's question, asked from two places with two
different budgets — so there are two members, and they can legitimately
disagree.

| | reads | asked by |
|---|---|---|
| `isEnabled() const` | what this driver last commanded | `hal::energisedSourceAt()`, before a low-impedance reading is routed to a pin this supply is cabled onto |
| `outputIsOn()` | the instrument (`OUTP? (@n)`) | `isEnergised()`, on the `Connect`/`Disconnect` path |

`hal::energisedSourceAt()` reflects over every instrument global in the program
and its concept takes a **const** instrument (see
`framework/hal/src/verbs/interlock.cpp`) — deliberately, being an observer on
the measurement path — so `isEnabled()` performs no I/O.

`outputIsOn()` is where a round trip is worth paying for: a relay in this rail's
lead is about to move, and the question is the difference between a contact
moving cold and one arcing. It is right about the cases this process did not
cause — an output left on by a previous run, switched on from the front panel,
or brought up by a coupled channel — which a driver reading back its own memory
would call cold and close a relay into.

So the truthful-but-expensive answer is used exactly where hardware is about to
move. Making the measurement-side check ask the instrument too would be a change
to that concept in `hal` rather than something this driver can decide, and the
case is weaker: routing a reading closes fabric relays, which the `Connect` path
has already been asked about.

`outputIsOn()` is therefore not `const` and not exception-free: a supply that
cannot be reached fails the `Connect` instead of letting it proceed on an
assumption.

## On the wire

Everything this driver sends is in `src/keysight_edu36311a.cpp`, in the order
the instrument sees it, as ordinary code over an `int` channel — no templates.

One `Apply` of a 24 V rail on output 2, in full:

```
CURR 0.5, (@2)        the limit first
SYST:ERR?
VOLT:PROT 27, (@2)    then the trip level, before the setpoint it must be above
SYST:ERR?
VOLT 24, (@2)         then the setpoint
SYST:ERR?
OUTP 1, (@2)          and only then the output on
SYST:ERR?
*OPC?
```

preceded once per session by `SYST:ERR?` (drain whatever the last user left) and
`*IDN?` (and refused if the model is not this one).

### The order is the safety argument

1. **the current limit**, because a limit that arrives after the voltage has
   already been applied was not in force while the rail came up — which is
   exactly the moment a DUT with a short across it draws everything the supply
   has.
2. **the trip level**, because it has to be above the setpoint before the
   setpoint exists. The other way round, a config raising both would set the new
   voltage against the old trip and the instrument would trip its own output off
   inside 5 ms.
3. **the setpoint**.
4. **and only then the output on.** An output enabled before its setpoint is
   programmed delivers whatever the last setpoint was — zero after a `safe()`,
   anything after somebody else's run.

One `checked()` per command rather than a single drain at the end, because that
ordering only holds if each step is known to have landed before the next is
sent. Four round trips per `Apply`, about a millisecond each on a LAN box
against the 80 ms the rail itself takes to settle.

### No `*RST`, ever

It would zero all three outputs, and the other two belong to two other drivers
behind this same address — so a reset on an `Apply` to `DcP6` would drop
`DcP5`'s rail. The meter driver can afford `CONFigure` resetting its function
before every reading precisely because a meter has one input; a shared chassis
cannot.

### Channels by number, never by name

The series offers a modal `INSTrument:SELect P6V` plus channel-less commands,
and a per-command channel list `(@2)`. This driver only ever uses the second,
for two reasons that are both about this model:

- the modal form's output *names* are the E36311A's — `P6V`, `P25V`, `N25V`, the
  last a −25 V rail this box does not have.
- a modal selection is instrument state that survives between commands, so
  three drivers sharing one chassis would be three objects taking turns changing
  one global, and an `Apply` on one output would be wrong whenever another had
  selected itself in between.

The punctuation is not symmetric and that is the instrument's rule, not a slip:
a **command** takes a comma before its channel list (`OUTP 1, (@2)`), a
**query** takes a space (`MEAS:VOLT? (@2)`). Written against the `?` it answers
`-103`, invalid separator.

### `*OPC?` — what it buys and what it does not

This is the call `hal::io::ScpiSession::waitForComplete()` was put there for and
that no driver had yet used. It

- **does** guarantee the instrument has parsed and executed the commands.
  Command processing is under 10 ms on this model, so the next line cannot race
  the enable.
- **does not** guarantee the rail has arrived. Up-programming settles to within
  1% of the excursion in about **80 ms**; down-programming into **no load takes
  about 500 ms**, since nothing but the output capacitor's own leakage is
  discharging it.

A criterion checked immediately after an `Apply` that *lowers* a rail can
legitimately fail on a rail still on its way down. That is hardware, and it
belongs to the script — the only thing that knows what it is waiting for.

## This is not a smaller E36311A

The model number sets a trap, and the driver's identity check is narrower than
the meter's because of it.

The E36300 Series Programming Guide is the right document for the commands: this
instrument runs that series' command set, and every command in the `.cpp` is
straight out of it, unadapted. But that guide's **range table** is the E36311A's:

| | output 1 | output 2 | output 3 |
|---|---|---|---|
| E36311A | 6 V / 5 A | +25 V / 1 A | **−25 V** / 1 A |
| EDU36311A | 6 V / 5 A | 30 V / 1 A | 30 V / 1 A |

So an E36311A answering at this driver's address would accept every command it
sends, while channel 2 is limited to 25 V where this driver believes 30 V and
channel 3 is a *negative* rail. Both mistakes are silent. Hence: commands from
the guide, numbers from the data sheet, and `*IDN?` must say `EDU36311A`
exactly — the other E36300 models are refused for the same reason and not a
worse one.

## What else is deliberately not here

- **series/parallel coupling.** Outputs 2 and 3 can be strapped for 60 V or
  2 A (`OUTP:PAIR`). Two instances would then be one rail, which one
  `InstrumentId` per output cannot describe — and it is a cabling fact besides,
  so a rig doing it wants one instance with a different `Output` tag.
- **output sequencing** (`OUTP:DEL:RISE`/`FALL`). Real and useful on a DUT with
  a rail order, but a script already has that ordering in it explicitly, one
  `Apply` per line, where a reader can see it.
- **data logging, front-panel text, the flash-drive store.** Nothing a test rig
  needs from an instrument it is driving.

`session()` is public, so a bring-up run can send any of these without that
becoming a reason to widen the driver.

## Bringing one up

Over **LAN**, put the hostname in the rig row and the driver opens a raw SCPI
socket on port 5025:

```
INSTRUMENT( keysight_edu36311a::RelayOutput2, DcP6, Lan( "bench-dcp6"))
```

Over **USB**, put the serial number from the instrument's own `*IDN?` or its
`Utility > I/O` screen; `hal::Usb` routes through whatever VISA is installed:

```
INSTRUMENT( keysight_edu36311a::RelayOutput2, DcP6, Usb( "CN61130007"))
```

All three rows of one chassis carry the same address.

### What the failures mean

| Failure | Cause |
|---|---|
| `hal::io::TransportError` on the first `Apply` | nothing answered at that address — wrong hostname, box off, cable out |
| `hal::io::ScpiFault` on `*IDN?` | something answered, and it is not an EDU36311A. Check the row against the rack — and read the message, which will say if it found an E36311A |
| `hal::io::ScpiFault` naming a command | the instrument refused it. `-222` is a value out of range; `-103` is a punctuation bug in this driver |
| `RatingExceeded` | the script asked this output for more than it has. Nothing was sent |
| "no VISA library found" | a `Usb` row on a machine with no VISA installed — a missing dependency, not a missing instrument |

## Adding it to a rig

```
rig/instrument.inc     INSTRUMENT( keysight_edu36311a::DirectOutput1, DcP5, Simulated{})
                       INSTRUMENT( keysight_edu36311a::RelayOutput2,  DcP6, Simulated{})
                       INSTRUMENT( keysight_edu36311a::RelayOutput3,  DcP7, Simulated{})
rig/wiring.inc         the isolation relay each RelayOutput* sits behind, and
                       where each output's lead lands on the DUT
```

Which output a row is, and whether its lead has a relay in it, are both facts
about the bench — so both are chosen in `rig/instrument.inc` by which alias is
named, not here.

## Sources

- Keysight E36300 Series Programming Guide —
  <https://www.batronix.com/files/Keysight/Netzgeraete/E36300-Programming.pdf>
  (every command and query; **not** the range table)
- Keysight EDU36311A Triple Output DC Power Supply Data Sheet —
  <https://www.batronix.com/files/Keysight/Netzgeraete/EDU36311A-Data_Sheet.pdf>
  (the three outputs' ratings, the settling times, the OVP/OCP activation time)
