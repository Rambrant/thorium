# suite/ -- criteria variants and test catalog

This directory holds the tolerance tables (`CRITERIA`/`CRIT` blocks, see
`libs/core/include/core/criterion.hpp`) for scripts that need more than one
set of numbers: production hardware fresh off the line, a stress-chamber
run, equipment that's been in service for years, etc.

It also holds `catalog.inc` -- the `GROUP`/`TEST`/`END_GROUP` test catalog
(see `libs/core/include/core/test_catalog.hpp`), listing which scripts exist
and what to call them. Unlike the criteria variants, there's only one of
these (no `THORIUM_TEST_CATALOG_VARIANT` -- a build represents one hardware
scenario, but always runs the same set of tests). It lives here rather than
under `scripts/` because, like the criteria variants, it's data specific to
this DUT's test plan, not framework code -- see
`scripts/include/scripts/catalog.hpp` for how it's consumed.

## Layout

```
suite/
    production.inc
    stress.inc
    aged.inc
    catalog.inc
```

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
have -- see the "Vout" check in `fuse_register_script.cpp`. This directory
is only for tolerances that genuinely vary by variant.

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

This is why `production.inc` is always available as `production::...`
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

## Why every variant compiles, always

`scripts/tests/test_criteria_variants_compile.cpp` `#include`s every
variant file, each in its own namespace, regardless of which one
`THORIUM_CRITERIA_VARIANT` is currently set to. A typo in `aged` is caught
the moment anyone builds the `scripts_tests` target -- not the day someone
finally targets aged equipment for real.

That file also `static_assert`s that each variant defines the exact
expected `CRIT` names for both `FS_Fuse_6` and `FS_Supply_1` (not just that
it compiles): a struct with a *renamed* member still compiles fine, so bare
compilation alone doesn't catch a misspelled criterion id. The named check
is what does -- if you add a new `CRITERIA` block to the criteria files, add
a matching check here too, or a rename in that group can go undetected in
whichever variant isn't currently active.

## Adding a new variant

1. Add a new `<name>.inc` file here, with the same `CRITERIA`/`CRIT` names
   as its siblings.
2. Add the new variant name to `THORIUM_KNOWN_CRITERIA_VARIANTS` in the
   top-level `CMakeLists.txt`.
3. Add it to the `#include`/`static_assert` block in
   `test_criteria_variants_compile.cpp`.

## Adding a new CRITERIA block (e.g. for a new script)

1. Add the `CRITERIA`/`CRIT` block to every existing `.inc` file here --
   use `CRIT_FROM_PRODUCTION( group, id)` in place of `CRIT` for any
   criterion that's identical to production's.
2. Reference it from the script the same way `supply_rail_script.cpp` and
   `fuse_register_script.cpp` do: `#include "core/active_criteria.hpp"`,
   then use `YourGroupName::YourCritName` directly.
3. Add a `HasYourGroupCriteria` concept + `static_assert`s for it in
   `test_criteria_variants_compile.cpp`, one per variant.
