# instruments/

One directory per instrument driver. Each is self-contained — its header, its
sources if it has any, its tests, its README, its `CMakeLists.txt` — and
nothing outside it names any of those files. A directory here can be zipped, versioned and handed to another rig
as it stands, and unpacking it into that rig's `instruments/` is the whole
installation procedure.

```
instruments/
    keysight_edu34450a/         # 5.5-digit dual-display bench DMM -- talks to real hardware
        CMakeLists.txt          #   STATIC, not INTERFACE: it has a .cpp
        README.md
        include/hal/keysight_edu34450a.hpp
        src/keysight_edu34450a.cpp   #   the SCPI: command table, ranges, overload
        tests/test_keysight_edu34450a.cpp
    keysight_dsox1202g/         # two-channel oscilloscope -- Osc1, talks to real hardware
        CMakeLists.txt          #   STATIC, not INTERFACE: it has a .cpp
        README.md
        include/hal/keysight_dsox1202g.hpp
        src/keysight_dsox1202g.cpp   #   the SCPI: setup, arm/await, waveform transfer
        tests/test_keysight_dsox1202g.cpp
    keysight_edu36311a/         # triple-output DC supply -- DcP5..DcP7, talks to real hardware
        CMakeLists.txt          #   STATIC, not INTERFACE: it has a .cpp
        README.md
        include/hal/keysight_edu36311a.hpp
        src/keysight_edu36311a.cpp   #   the SCPI: the Apply ordering, readback, safing
        tests/test_keysight_edu36311a.cpp
    keysight_33522b/            # two-channel 30 MHz waveform generator -- Wfg1, talks to real hardware
        CMakeLists.txt          #   STATIC, not INTERFACE: it has a .cpp
        README.md
        include/hal/keysight_33522b.hpp
        src/keysight_33522b.cpp      #   the SCPI: the Apply ordering, why not APPLy, safing
        tests/test_keysight_33522b.cpp
    keysight_34980a/            # switch/measure MAINFRAME -- not an instrument at all
        CMakeLists.txt          #   STATIC, not INTERFACE: it has a .cpp
        README.md
        include/hal/keysight_34980a.hpp
        src/keysight_34980a.cpp      #   the SCPI: ROUT:CLOS/OPEN, slot inspection
        tests/test_keysight_34980a.cpp
    keysight_ac6834b/           # three-phase AC source
    racal1260/                  # matrix-routed RS232 port
```

**One of these names a model where the badge names a series, and that is the
rule rather than an exception.** `keysight_33522b` drives a 33522B and refuses
the other seven members of the 33500B Series on `*IDN?`, even though all eight
speak the same commands from the same manual — they differ in channel count
(one or two), bandwidth (20 or 30 MHz) and arb capability, which are exactly the
three things a driver states as facts. A `keysight_33500b` would have had to
believe one member's numbers about all of them. That directory's own README has
the table.

**One of these is not an instrument, and that is not a filing mistake.**
`keysight_34980a` is a switch/measure *mainframe* -- eight slots of switching
modules and an optional internal DMM. It measures nothing and sources nothing on
its own, carries no `hal::InstrumentId`, and is not an `hal::InstrumentTag`, so
`hal::safeRig()` never sees it. It is here because
`hal/fabric/switch_device.hpp` named this directory as the destination for
switch-card drivers before there were any ("each model becomes its own directory
with its own header"), and because the box is a Janus: when its internal DMM
gets a face, that face is an instrument driver in the *same* package, sharing
the one session. See that directory's README, and `rig/devices.inc`, which
records this rack's migration onto one.

**Three directories left this tree on 2026-09-02**, and the reason is worth
recording because it is the counterweight to **Availability is not use** below.
`keysight_l4411a` (a 6.5-digit DMM, `Dmm2`), `keysight_n6701a` (four channels of
a modular DC mainframe, `DcP1`..`DcP4`) and `keysight_dso8064a` (the four-channel
Infiniium this rig was written against before the scope that turned up) were all
deleted. None of the three is on the bench and none had grown a transport. A
driver kept for a model nobody has is a driver nobody checks against anything —
and the two that were still *named* by `rig/instrument.inc` were worse than
unused, because the rig table was describing hardware that does not exist.

What that cost, stated once so it is not rediscovered: `Dmm2` was a *different
resolution* from `Dmm1` on purpose (6.5 digits against 5.5, NPLC against three
discrete resolutions), and this bench now has one meter. `keysight_edu34450a`'s
README keeps the argument for why one class covering both would have had to lie
about one of them, which is the thing to read before adding a second meter back.

**Two of the seven are simulated; five talk.** `EDU34450A`, `DSOX1202G`,
`EDU36311A`, `Wfg33522B` and the `34980A` mainframe open a SCPI session over
their address and drive or read the hardware (`hal/io/`, see
`framework/hal/README.md`); `Ac6834B` and `Racal1260` answer from their own
simulation hooks whatever their rig row says, exactly as every driver here used
to. Both are kept deliberately without hardware behind them, because a shipped
test group depends on each -- see `rig/instrument.inc`. That asymmetry is the
current state of the work rather than a design; what doing it again involves is
in **A driver with a transport** below.

The five that talk are worth reading in that order. The meter is the smallest
case -- configure, read one number, check the queue. The scope adds the three
things a DMM never needs: a sequence that has to be *waited on* (arm, then poll
two registers), a record transferred rather than a reading, and a measurement
whose answer may be "I could not". The supply is the first one that *drives*
rather than observes, and what that adds is a third kind of care again: the
order of four commands is the safety argument, `*OPC?` earns its place because
a source's commands complete after they return, and `safe()` finally has
something it genuinely has to say on the wire. The waveform generator is the
second source and sharpens exactly that point, because it is the first
instrument here that answers a bad instruction by *carrying on* -- an
out-of-range frequency is clamped rather than refused and the output keeps
running at the wrong one -- so its ordering argument and its up-front limit
checks are both aimed at a failure nothing downstream would notice. It is also
the one driver here with no `core::Port` at all, which is what a box that
sources and measures nothing looks like. The mainframe is the odd one out and
the most instructive for it: it has no readings and no outputs at all, so what
it demonstrates is the *other* half of a bench -- that a wrong command to a
matrix is not a wrong reading but a reading of a different node, which nothing
downstream can notice.

## One name, in four places

A driver's name is the same string in its directory, its header, its CMake
target and its C++ namespace:

| | |
|---|---|
| directory | `instruments/keysight_dsox1202g/` |
| header | `include/hal/keysight_dsox1202g.hpp`, included as `"hal/keysight_dsox1202g.hpp"` |
| target | `hal_keysight_dsox1202g`, aliased `Thorium::hal_keysight_dsox1202g` |
| namespace | `hal::keysight_dsox1202g` |

Two rules produced that shape, and both come from a collision that actually
happened rather than from a preference.

**The manufacturer is part of the name.** A model number is not unique across
vendors — two of them may each ship a "1260" or a "4411" — so a bare model name
is a collision waiting for the second vendor. The token names the company as it
is today rather than the badge on the unit: the 34980A and the Ac6834B both
shipped as Agilent products and are now Keysight -- the 34980A's own
programmer's reference still says Agilent on every page -- and one vendor
appearing under two spellings would be worse than a name that is one
acquisition out of date.
`racal1260` already reads as Racal's, which is why it is not `racal_1260`.

`keysight_edu36311a` is the case where this rule stops being about vendors and
starts being about one vendor's own catalogue: Keysight also ships an `E36311A`,
which speaks the same SCPI and has different ranges and a negative rail. The
model number is not enough there either, which is why that driver refuses an
E36311A on `*IDN?` rather than tolerating it.

`keysight_33522b` is the same rule one step further, and the step is worth
noticing because it changes what the name has to *narrow*. There the ambiguity
is not another model with a confusable number -- it is that the number on the
badge, "33500B", names a **series of eight**, all speaking one command set out
of one manual and differing in channel count, bandwidth and arb capability. So
the token names the member on the bench rather than the family it belongs to,
and that driver refuses the other seven on `*IDN?` for the reason the EDU36311A
refuses its sibling: a driver that accepted them would be stating facts about a
box it is not talking to.

**Everything a driver declares goes in its own namespace, nested inside `hal`.**
This is the rule to follow when adding one, and the reason is what happens
without it. Every driver used to declare straight into `hal`, and the satellite
types had to invent long unique names to stay out of each other's way —
`DSOX1202GTriggerBuilder`, `Racal1260Config`. Worse were the ones that did *not*,
because nothing had claimed the word yet: `Parity`, `StopBits`, `TriggerSlope`,
`Bandwidth`, `ChannelInput`, `ProbeAdapter` and four more were sitting directly
in `hal`, owned by whichever driver happened to declare them first. The second
serial instrument or the second scope collides on all of them — and this tree
now *has* a second scope, so that is no longer hypothetical: `keysight_dsox1202g`
and `keysight_dsox1202g` both declare a `Bandwidth`, a `TriggerSlope`, a
`TimebaseReference` and six more, and they mean different sets of values.

Inside its own namespace a driver's types say only what they are —
`hal::keysight_dsox1202g::TriggerBuilder`, `hal::racal1260::Parity` — and cannot
collide with another driver at all.

Nested inside `hal` rather than beside it, which is what keeps a driver's bodies
short: unqualified lookup still reaches `hal::describeSetting`,
`hal::InstrumentId`, `hal::Simulated` and the rest of the driver kit from inside
the nested namespace. And the ADL customization points — `applyDriver`,
`setupDriver`, `describeConfig` and the rest — belong *in* the driver's
namespace, where ADL finds them on that driver's own config type. Call them
unqualified; a hand-written `hal::describeConfig( ... )` both defeats the
mechanism and, since every driver declares that same name, can name the wrong
driver's overload.

The rig names the type in full, once, where it declares the instrument:

```cpp
// rig/instrument.inc
INSTRUMENT( keysight_dsox1202g::DSOX1202G, Osc1, Simulated{})
```

## How a directory here gets built

Three steps, each deliberately in a different place:

1. The **top-level `CMakeLists.txt`** globs `instruments/*/CMakeLists.txt` and
   collects the directory paths into `THORIUM_INSTRUMENT_DIRS` — the fifth
   thing a rig supplies `framework/hal`, alongside the four `.inc`/`.hpp` file paths
   it already supplied. `framework/` still reaches nothing outside itself.
2. **`framework/hal/CMakeLists.txt`** `add_subdirectory()`s each of them, at the one
   point in the configure where it can: after `hal` (which every driver compiles
   against) and before `hal_rig` (which compiles against the drivers).
3. **This directory's `CMakeLists.txt`** declares its own target and registers
   it on the `THORIUM_INSTRUMENT_TARGETS` global property. Nothing derives a
   target name from a directory name; a driver package says what it provides.

## Availability is not use

A driver sitting in this tree is *available* to the rig. What the rig actually
*has* is still declared one explicit line at a time in `rig/instrument.inc`, and
that has not changed. A driver here that no `INSTRUMENT()` line names contributes
no global, no `hal::InstrumentId` enumerator and no object code — it is compiled
against nothing and linked into nothing.

So the glob decides what *could* be plugged in, and the rig's own single
reviewable list decides what *is*. Dropping a zip into this directory cannot
quietly change what a rig measures.

## What a driver may depend on

`Thorium::hal` and, through it, `Thorium::core`. That is the published driver
API, and the two-target split in `framework/hal/CMakeLists.txt` is what makes it a
real boundary rather than a naming convention.

It is checked explicitly there, and worth knowing why: the natural assumption is
that the target graph enforces it for free, since `hal_rig` links the drivers and
so a driver linking `hal_rig` closes a cycle. It doesn't. CMake tolerates cycles
among static libraries and accepts that one silently — configure succeeds, and
the driver has acquired the entire rig as a dependency without anything
complaining. Hence the `FATAL_ERROR` check.

A driver may not depend on another driver, on `rig/`, or on this rig's wiring.

## Where tests go

A driver's own tests live in its directory and link its target alone — which
transitively means `hal` and `core` and nothing else. That constraint is the
point: a test here that needs the switching fabric, an `Apply`, or a second
instrument is, by construction, not testing this driver in isolation, and the
directory stops being packageable the moment one does.

Tests that genuinely need several instruments together, or this rig's wiring,
are rig-level integration tests and belong with the rig, not here. They live in
`rig/tests/` — five files, which passed through `framework/hal/tests/` on their way
out of this tree:

| File | Why it can't live in a driver directory |
|---|---|
| `test_call_syntax.cpp` | names three drivers at once |
| `test_safing.cpp` | names all four, and includes `hal/topology/active_instruments.hpp` |
| `test_source_instruments.cpp` | one test needing both a DC supply and an AC source |
| `test_describe.cpp`, `test_source_readback.cpp` | assert that the *engines* post to the journal — a claim about the engine-and-driver pair, which neither side can make about itself |

That last row is worth reading closely before assuming a file is misplaced: both
files said as much in their own header comments long before any of this moved.

Which of the three places a test belongs is now a link error rather than a
judgement call. A driver's test target links its own driver alone, `hal_tests`
links generic `hal`, and only `rig_tests` links `hal_rig` — so a test reaching
for a second instrument, an `Apply` or `safeRig()` from either of the first two
fails to build where it sits. See `rig/README.md`.

The split of `test_source_instruments.cpp` is the one place where moving tests was
not a verbatim lift. It held the N6701A's and Ac6834B's tests behind a single shared
fixture; each driver now carries its own copy of the fixture, trimmed to the
instruments it actually names, with the test bodies unchanged. Fixture and suite
names were deliberately *not* improved at the same time — `TEST_F` takes its suite
name from the fixture, so renaming would have renamed every test and lost the
before/after comparison that made the move verifiable.

## Adding a driver

Copy `keysight_ac6834b/` as the template for a simulated driver, or
`keysight_edu34450a/` for one that talks. The first is the smallest one and
exercises every part of the arrangement: an `INTERFACE` header-only target, the
`include/hal/<model>.hpp` layout that keeps the `"hal/<model>.hpp"` spelling and
`namespace hal` unchanged at every call site, the global-property registration,
the export/install rules, and a test target linking the published API only.

Constrain the constructor to the bus kinds the model's back panel actually has,
the way `ac6834b/` does:

```cpp
template<typename AddressT>
    requires ReachableOver<AddressT, Lan, Usb>
Ac6834B( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}
```

That list is a claim about hardware, so it belongs with the driver rather than
with the rig — a rig row addressing this model over a connector it does not have
then fails to compile instead of failing to open. `hal::Simulated` is in every
driver's set without being listed, and is what a driver's own tests construct
with. See `hal/driver/address.hpp`.

Leave the `THORIUM_REQUIRE_HAL_API` line copied from the template alone unless
the new driver actually needs something newer than the version it names — see
the next section for what the number means.

Then add an `INSTRUMENT()` line to `rig/instrument.inc`. That is the whole
second step: the driver is available after the copy and part of the rig after
that one line. Its header does not have to be included anywhere by hand — the
row's type column names the driver's namespace, and
`cmake/InstrumentDrivers.cmake` turns that into the `#include` at configure
time.

## A driver with a transport

`hal/io/` is part of `Thorium::hal`, so a driver may use it under the rule
above. There are two worked examples now — `keysight_edu34450a/` for a meter and
`keysight_dsox1202g/` for a scope — and the first four points below were learned
from the meter, the last two from the scope.

**The address decides, and nothing else.** A driver checks for `hal::Simulated`
and answers from its own hooks; anything else it opens with
`hal::io::openTransport( address)`, which picks the transport — a socket for
`Lan`, VISA for `Usb`/`Gpib`/`Serial` — so a driver never names one. No mode flag, no build option, no
environment variable — a rig states what it has once, in its instrument table,
and a script measuring a rail is identical either way. The corollary is that
the address column becomes an *instruction* the moment a driver reads it: a
hostname that no box answers to is now a failing run rather than a note to a
reviewer, which is why `rig/instrument.inc`'s `Dmm1` row says `Simulated{}`.

**The session opens lazily, and it has to.** A rig's instruments are globals
constructed before `main()`, so a constructor that opened a socket would make
every binary that links the rig — every unit test, `--replay`, `--help` — reach
for the bench at static-initialisation time, and would throw from a constructor
with nowhere to catch it. Open on the first reading that needs one, which is
also exactly when a detached run does not need one.

**Take the transport as an interface, and offer a way to hand one in.**
`useTransport()` on the EDU34450A is what its tests use to assert the exact SCPI
it would have sent, with no socket in sight — and is also how a rig with a VISA
installation, or one on a bus `openTransport` does not implement, gets the
driver working with no change to it. A driver written against a concrete socket
class can only be tested against hardware, which means never in CI.

**Configure through `checked()`, not `write()`.** A SCPI instrument does not
answer a bad command: it queues an error and carries on measuring whatever it
was measuring before. The reading after a silently-refused `CONFigure` is
plausible, often in tolerance, and completely untraceable.
`hal::io::ScpiSession::checked()` costs one round trip and turns that into an
exception naming the command.

**An instrument you have to *wait* for needs two different failures.** The meter
answers `READ?` when it has a reading and there is nothing to decide. A scope is
armed, then waited on, and the two ends of that are not the same kind of event:
`Arm` throwing when the instrument never armed is right, because the script is
one line from causing an event with nothing listening and then measuring the
*previous* acquisition; `Await` returning `false` when nothing triggered is also
right, because "no transient" is a result a criterion checks. Getting these the
same way round would either abandon runs over ordinary DUT behaviour or record
readings of a capture that never happened. Timeouts belong to the call rather
than to the driver, for the same reason — see `DSOX1202G::armSingle`.

**`safe()` may use a session; it may not open one.** Safing runs when a script
has already failed, quite possibly *because* an instrument is unreachable, and
`hal::safeRig()` does not catch. So a driver with something worth saying on the
wire (`:STOP`, for a scope left armed) says it only down a session that is
already open, and swallows the transport error if that fails — otherwise the
cleanup path replaces the run's real failure with its own, and abandons the
safing of every instrument after it. Closing the session is not safing's job
either: its error queue is the best evidence of what went wrong.

## Which hal a driver was written against

A driver and the `hal` it compiles against travel separately — that is the
entire point of a directory here being zippable — so the two can disagree.
Each driver says which API version it was written for, one line after its
includes:

```cpp
#include "hal/driver/address.hpp"
#include "hal/driver/api_version.hpp"
#include "hal/driver/instrument.hpp"

THORIUM_REQUIRE_HAL_API( 1);
```

Without it, the disagreement surfaces as a failure deep inside a template
instantiation — the driver names a member of a `hal` type that has since changed
shape, and the error arrives from the middle of `core::MeasureEngine` with a
page of substituted template arguments in front of it and nothing in it saying
*wrong hal version*. With it, both directions are one diagnostic pointing at the
driver's own line:

```
api_version.hpp:150: error: static assertion failed: This instrument driver was
written against a NEWER hal API than the one it is being compiled against:
update framework/hal, or install the driver package matching this hal. [...]
    note: in expansion of macro 'THORIUM_REQUIRE_HAL_API'
    ac6834b.hpp:23 | THORIUM_REQUIRE_HAL_API( 2);
```

Two numbers live in `hal/driver/api_version.hpp`, and the second is what keeps the gate
from becoming a nuisance:

| | |
|---|---|
| `THORIUM_HAL_API_VERSION` | rises on every change to the driver-facing API, additions included |
| `THORIUM_HAL_API_OLDEST_SUPPORTED` | rises only when a change actually breaks drivers written against older versions |

So a driver asking for 1 against a `hal` at 5 that still supports 1 compiles,
silently and correctly. A gate that failed on every additive bump would be
switched off within a week.

The number is written as a **literal**, never as `THORIUM_HAL_API_VERSION`
itself — which would assert that this `hal` is compatible with this `hal`, and
pass everywhere. Two are at 2 (they use `hal::ConfigBuilder`), and the five that
open a session -- `keysight_edu34450a`, `keysight_dsox1202g`,
`keysight_edu36311a`, `keysight_33522b` and `keysight_34980a` -- are at 3 (they
use `hal/io/`).

One thing the gate cannot do, worth knowing before trusting it: nothing derives
those two numbers from the API they describe, or checks them against it. They
are a promise `framework/hal` makes, kept by whoever changes a driver-facing header
remembering to bump them. The gate catches a driver and a `hal` that were
honestly labelled and still don't match — the zip-distribution case it exists
for — and not a breaking change that went in without a bump.
