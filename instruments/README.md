# instruments/

One directory per instrument driver. Each is self-contained — its header, its
tests, its README, its `CMakeLists.txt` — and nothing outside it names any of
those files. A directory here can be zipped, versioned and handed to another rig
as it stands, and unpacking it into that rig's `instruments/` is the whole
installation procedure.

```
instruments/
    l4411a/
        CMakeLists.txt
        README.md
        include/hal/l4411a.hpp
        tests/test_l4411a.cpp
```

## How a directory here gets built

Three steps, each deliberately in a different place:

1. The **top-level `CMakeLists.txt`** globs `instruments/*/CMakeLists.txt` and
   collects the directory paths into `THORIUM_INSTRUMENT_DIRS` — the fourth
   thing a rig supplies `libs/hal`, alongside the three `.inc`/`.hpp` file paths
   it already supplied. `libs/` still reaches nothing outside itself.
2. **`libs/hal/CMakeLists.txt`** `add_subdirectory()`s each of them, at the one
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
API, and the two-target split in `libs/hal/CMakeLists.txt` is what makes it a
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
are rig-level integration tests and belong with the rig, not here.
`libs/hal/tests/test_call_syntax.cpp` and `test_safing.cpp` are both in that
category today and are still in `libs/hal/tests/` pending a `rig/tests/` target.

## Adding a driver

Copy `l4411a/` as the template. It is the smallest one, and it exercises every
part of the arrangement: an `INTERFACE` header-only target, the
`include/hal/<model>.hpp` layout that keeps the `"hal/<model>.hpp"` spelling and
`namespace hal` unchanged at every call site, the global-property registration,
the export/install rules, and a test target linking the published API only.

Then add an `INSTRUMENT()` line to `rig/instrument.inc` and an `#include` to
`rig/active_instruments.hpp` — the driver is available after the copy, and part
of the rig after those two lines.

## Not yet done

- **`n6701a`, `ac6677a`, `dso8064`** still live in `libs/hal/include/hal/`, and
  move out the way `l4411a` already has. `libs/hal/CMakeLists.txt` still lists
  them so they stay visible in an IDE until they go.
- **A hal API version gate.** A driver written against an older `hal` and
  compiled against a newer one currently fails somewhere deep inside a template
  instantiation. A `THORIUM_HAL_API_VERSION` in generic `hal` plus a
  `static_assert` in each driver turns that into one readable line. Worth having
  before any driver is distributed as a zip to anyone.
