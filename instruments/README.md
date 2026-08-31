# instruments/

One directory per instrument driver. Each is self-contained — its header, its
tests, its README, its `CMakeLists.txt` — and nothing outside it names any of
those files. A directory here can be zipped, versioned and handed to another rig
as it stands, and unpacking it into that rig's `instruments/` is the whole
installation procedure.

```
instruments/
    keysight_l4411a/            # 6.5-digit system DMM
        CMakeLists.txt
        README.md
        include/hal/keysight_l4411a.hpp
        tests/test_keysight_l4411a.cpp
    keysight_dso8064a/          # four-channel oscilloscope
    keysight_n6701a/            # one DC supply channel
    keysight_ac6834b/           # three-phase AC source
    racal1260/                  # matrix-routed RS232 port
```

## One name, in four places

A driver's name is the same string in its directory, its header, its CMake
target and its C++ namespace:

| | |
|---|---|
| directory | `instruments/keysight_dso8064a/` |
| header | `include/hal/keysight_dso8064a.hpp`, included as `"hal/keysight_dso8064a.hpp"` |
| target | `hal_keysight_dso8064a`, aliased `Thorium::hal_keysight_dso8064a` |
| namespace | `hal::keysight_dso8064a` |

Two rules produced that shape, and both come from a collision that actually
happened rather than from a preference.

**The manufacturer is part of the name.** A model number is not unique across
vendors — two of them may each ship a "1260" or a "4411" — so a bare model name
is a collision waiting for the second vendor. The token names the company as it
is today rather than the badge on the unit: the DSO8064A, L4411A and N6701A all
shipped as Agilent products and are now Keysight, and one vendor appearing under
two spellings would be worse than a name that is one acquisition out of date.
`racal1260` already reads as Racal's, which is why it is not `racal_1260`.

**Everything a driver declares goes in its own namespace, nested inside `hal`.**
This is the rule to follow when adding one, and the reason is what happens
without it. Every driver used to declare straight into `hal`, and the satellite
types had to invent long unique names to stay out of each other's way —
`DSO8064ATriggerBuilder`, `Racal1260Config`. Worse were the ones that did *not*,
because nothing had claimed the word yet: `Parity`, `StopBits`, `TriggerSlope`,
`Bandwidth`, `ChannelInput`, `ProbeAdapter` and four more were sitting directly
in `hal`, owned by whichever driver happened to declare them first. The second
serial instrument or the second scope collides on all of them.

Inside its own namespace a driver's types say only what they are —
`hal::keysight_dso8064a::TriggerBuilder`, `hal::racal1260::Parity` — and cannot
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
INSTRUMENT( keysight_dso8064a::DSO8064A, Osc1, Lan( "bench-osc1"))
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
| `test_safing.cpp` | names all four, and expands `THORIUM_ACTIVE_INSTRUMENTS` |
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
not a verbatim lift. It held N6701A's and Ac6834B's tests behind a single shared
fixture; each driver now carries its own copy of the fixture, trimmed to the
instruments it actually names, with the test bodies unchanged. Fixture and suite
names were deliberately *not* improved at the same time — `TEST_F` takes its suite
name from the fixture, so renaming would have renamed every test and lost the
before/after comparison that made the move verifiable.

## Adding a driver

Copy `l4411a/` as the template. It is the smallest one, and it exercises every
part of the arrangement: an `INTERFACE` header-only target, the
`include/hal/<model>.hpp` layout that keeps the `"hal/<model>.hpp"` spelling and
`namespace hal` unchanged at every call site, the global-property registration,
the export/install rules, and a test target linking the published API only.

Constrain the constructor to the bus kinds the model's back panel actually has,
the way `l4411a/` does:

```cpp
template<typename AddressT>
    requires ReachableOver<AddressT, Lan, Usb>
L4411A( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}
```

That list is a claim about hardware, so it belongs with the driver rather than
with the rig — a rig row addressing this model over a connector it does not have
then fails to compile instead of failing to open. `hal::Simulated` is in every
driver's set without being listed, and is what a driver's own tests construct
with. See `hal/driver/address.hpp`.

Leave the `THORIUM_REQUIRE_HAL_API` line copied from the template alone unless
the new driver actually needs something newer than the version it names — see
the next section for what the number means.

Then add an `INSTRUMENT()` line to `rig/instrument.inc` and an `#include` to
`rig/active_instruments.hpp` — the driver is available after the copy, and part
of the rig after those two lines.

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
    l4411a.hpp:23 | THORIUM_REQUIRE_HAL_API( 2);
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
pass everywhere. All five drivers here are at 1.

One thing the gate cannot do, worth knowing before trusting it: nothing derives
those two numbers from the API they describe, or checks them against it. They
are a promise `framework/hal` makes, kept by whoever changes a driver-facing header
remembering to bump them. The gate catches a driver and a `hal` that were
honestly labelled and still don't match — the zip-distribution case it exists
for — and not a breaking change that went in without a bump.
