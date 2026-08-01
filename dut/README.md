# dut/ -- DUT-specific data, and nothing else

This directory holds *only* data specific to the actual device under test --
Device X on its standard adapter -- expressed as flat, macro-driven `.inc`
files with no C++ logic of their own. There is deliberately no library here
(no `add_library(dut ...)`): the mechanisms that make sense of this data
(`core::AdapterPointTag`, `core::MeasureEngine`, the `CRITERIA`/`ADAPTER`
macros themselves) all live in `libs/hal`/`libs/core` -- the portable,
DUT-agnostic framework -- not here, since this directory (like `rig/` and
`suite/` alongside it) is this one deployment's content, not part of that
framework; see the top-level `CMakeLists.txt`'s own comment on that split,
and below for why the hal/core split specifically is possible.

## Layout

```
dut/
    tests/
        test_adapter.cpp
    adapter.inc
    criteria_production.inc
    criteria_stress.inc
    criteria_aged.inc
```

## adapter.inc

`ADAPTER`/`POINT`/`END_ADAPTER` (see `libs/hal/include/hal/adapter.hpp`) --
mirroring `CRITERIA`/`CRIT`/`END_CRITERIA` below -- expand into the one
`DeviceX` struct: a fixed set of named points (e.g. `Output5V`),
each carrying its VPC90 location and expected quantity kind baked into its
own *type* (`core::AdapterPointTag<Loc, Kind>`), not stored as runtime data.
That is what makes both of the following genuine compile errors, exactly
like `CRIT`'s protection against a misspelled criterion id:

- **A misspelled point name** -- `DeviceX::Output5Vx` is "no such
  member", the same way `FS_Fuse_6::FS_Fuse_01x` already is.
- **A quantity mismatch** -- `Measure( Dmm1.current(), at( DeviceX::Output5V))`
  fails to find a matching `operator()` overload, since `Output5V` is
  `Voltage`-tagged and `Dmm1.current()` is a `Port<Current, ...>`.
- **A missing `at()`** -- `Measure( Dmm1.voltage(), DeviceX::Output5V)`,
  with the point passed bare, also fails to find a matching overload:
  `MeasureEngine::operator()` takes `core::At<AdapterPointTag<Loc, Kind>>`,
  not an `AdapterPointTag` directly -- see `core/at.hpp`'s own comment for
  why `at(...)` exists as a call-site marker rather than an implicit
  conversion.

Point identifiers can't reuse a spec label verbatim where it starts with a
digit (C++ identifiers can't start with a digit) -- `Output5V`/`Output3V3`
stand in for the spec's "5VOutput"/"3V3Output".

Because there is exactly one adapter per device (the connector on the
device doesn't change), this struct -- not some separate profile type --
*is* the adapter.

Like the criteria files below, this file is deliberately bare: just
`ADAPTER( ... ) ... END_ADAPTER`, nothing else -- no `#pragma once`, no
`#include`s, no namespace. `ADAPTER`/`POINT` expand fully qualified
(`::hal::...`, `::core::...`), unlike `CRITERIA`/`CRIT` which need a
`using namespace` wrapper -- see `hal/adapter.hpp`'s own comment for why.

Each script that measures against this profile `#include`s it directly
(after `hal/adapter.hpp`, for the macros), the same way each script already
`#include`s `core/active_criteria.hpp` individually rather than through
`suite/scripts.hpp` -- see `suite/scripts/supply_rail_script.cpp`.

Reachability -- whether this rig's wiring (see
`libs/hal/include/hal/wiring.hpp`) actually connects a given instrument to
a given point -- is still a runtime check, since `Loc`/`Kind` being
compile-time values doesn't by itself make the *wiring table lookup*
compile-time; see the `TODO(reflection)` in `hal/wiring.hpp` for what a
further upgrade there would look like.

## Criteria variants

The tolerance tables (`CRITERIA`/`CRIT` blocks, see
`libs/core/include/core/criterion.hpp`) for scripts that need more than one
set of numbers: production hardware fresh off the line, a stress-chamber
run, equipment that's been in service for years, etc. They live here, not
in `suite/`, because a tolerance is a property of the DUT being tested --
the same reasoning that puts `adapter.inc` here.

One flat `.inc` file per variant. Each file holds every script's `CRITERIA`
side by side (currently `FS_Fuse_6` for fuse-register checks and
`FS_Supply_1` for supply-rail checks) -- the same `CRITERIA`/`CRIT` names
across all three files, just different tolerance values.

Each `.inc` file is deliberately bare: just `CRITERIA( ... ) ... END_CRITERIA`
blocks, nothing else. No `#pragma once`, no `#include`s, no namespace, and
no `core::`/`using namespace` of its own -- `core/active_criteria.hpp`
supplies all of that, including bringing `EQ`/`MASK`/etc, `Voltage`/etc, and
the `_V`/etc literals into unqualified scope, so a criterion reads
`EQ( 5.0_V).epsilon( 0.05_V)` rather than
`core::EQ( core::Voltage{ 5.0}).epsilon( core::Voltage{ 0.05})`. So every
file here is easy to open and easy to diff against its siblings.

Write a criterion against the `Quantity` itself (`EQ( 5.0_V)`), not the raw
number (`EQ( 5.0)`), whenever the value being checked is one -- that's what
lets `core::Verify` catch a unit mismatch at compile time (`EQ( 5.0_A)`
against a `Voltage` reading fails to compile), rather than silently
comparing bare doubles. See the comment on the `optional<Quantity<Unit>>`
overload in `core/verify.hpp` for how that's picked automatically from the
predicate's own type.

Not everything belongs here: tolerances that never change between variants
(or one-off ad-hoc checks added to catch a specific fault) stay as ad-hoc
`core::Verify(...)` calls directly in the script, the same way they always
have -- see the "Vout" check in `suite/scripts/fuse_register_script.cpp`.
This directory is only for tolerances that genuinely vary by variant.

## When a criterion doesn't actually change between variants

Most variants only change *some* of production's criteria -- a lot of them
stay identical. Rather than retype an unchanged value (and risk it quietly
drifting between files), use `CRIT_FROM_PRODUCTION( group, id)` instead of
`CRIT`:

```cpp
CRITERIA( FS_Fuse_6, "Check of Fuses @ Register CB30")
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_01)
    CRIT_FROM_PRODUCTION( FS_Fuse_6, FS_Fuse_02)
END_CRITERIA
```

`group`/`id` are still stated explicitly -- unlike inheritance, there's
nothing to silently fall back to, so a typo'd id still fails to compile
exactly like an ordinary `CRIT` would. Only the predicate and description
are borrowed from `production`'s matching criterion, so the actual
tolerance value and its prose live in exactly one place. A typo in the
reference itself (wrong group or id) is also a hard compile error. See the
macro's own comment in `core/criterion.hpp` for the full reasoning,
including why it's a companion to `CRIT` rather than a change to `CRITERIA`
itself.

This is why `criteria_production.inc` is always available as `production::...`
regardless of which variant is actually active (see `core/active_criteria.hpp`)
-- every other variant can reference it, even though only one variant's
criteria actually get used by the running scripts.

## Selecting a variant

Which variant actually gets built is a single CMake option,
`THORIUM_CRITERIA_VARIANT` (see the top-level `CMakeLists.txt`), validated
against the list above at configure time -- an unknown value is a hard
configure error, not a silent fallback:

```
cmake -B build -DTHORIUM_CRITERIA_VARIANT=aged
```

Default is `production`. This is a build-wide setting: every script picks
up the same variant through `core/active_criteria.hpp`, so a build always
represents one coherent hardware/test scenario, never a mix.

## Why every variant compiles, always

`suite/tests/test_criteria_variants_compile.cpp` `#include`s every variant
file from here, each in its own namespace, regardless of which one
`THORIUM_CRITERIA_VARIANT` is currently set to. A typo in `aged` is caught
the moment anyone builds the `scripts_tests` target -- not the day someone
finally targets aged equipment for real. It lives under `suite/tests/`
rather than here because it's a test *of* this data, following the same
"content in one place, its tests alongside the rest of the test-script
tests" split this whole directory uses -- `test_adapter.cpp`
above is the same idea, for `adapter.inc`.

That file also `static_assert`s that each variant defines the exact
expected `CRIT` names for both `FS_Fuse_6` and `FS_Supply_1` (not just that
it compiles): a struct with a *renamed* member still compiles fine, so bare
compilation alone doesn't catch a misspelled criterion id. The named check
is what does -- if you add a new `CRITERIA` block here, add a matching
check there too, or a rename in that group can go undetected in whichever
variant isn't currently active.

## Adding a new variant

1. Add a new `criteria_<name>.inc` file here, with the same `CRITERIA`/`CRIT`
   names as its siblings.
2. Add the new variant name to `THORIUM_KNOWN_CRITERIA_VARIANTS` in the
   top-level `CMakeLists.txt`.
3. Add it to the `#include`/`static_assert` block in
   `suite/tests/test_criteria_variants_compile.cpp`.

## Adding a new CRITERIA block (e.g. for a new script)

1. Add the `CRITERIA`/`CRIT` block to every existing `.inc` file here --
   use `CRIT_FROM_PRODUCTION( group, id)` in place of `CRIT` for any
   criterion that's identical to production's.
2. Reference it from the script the same way `supply_rail_script.cpp` and
   `fuse_register_script.cpp` do: `#include "core/active_criteria.hpp"`,
   then use `YourGroupName::YourCritName` directly.
3. Add a `HasYourGroupCriteria` concept + `static_assert`s for it in
   `suite/tests/test_criteria_variants_compile.cpp`, one per variant.

## Why `active_criteria.hpp` is a separate header from `criterion.hpp`

`core/criterion.hpp` is the general, dependency-free `CRITERIA`/`CRIT`
mechanism -- it's used on its own (e.g. by `test_criterion.cpp`) with no
notion of "variants" at all. `core/active_criteria.hpp` is a specific
*consumer* of that mechanism: it resolves `THORIUM_ACTIVE_CRITERIA`, which
requires the `scripts` target's build configuration. Folding the two
together would force that requirement onto every unrelated user of the
general macros -- `core`'s own unit tests would stop compiling.

Note also that `CRITERIA`/`END_CRITERIA` (not `GROUP`/`END_GROUP`) is a
deliberate naming choice: `GROUP` is reserved for grouping test cases, a
separate, unrelated mechanism -- see the comment above `CRITERIA`'s
definition in `criterion.hpp`.
