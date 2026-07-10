# Criteria variants

This directory holds the tolerance tables (`GROUP`/`CRIT` blocks, see
`libs/core/include/core/criterion.hpp`) for scripts that need more than one
set of numbers: production hardware fresh off the line, a stress-chamber
run, equipment that's been in service for years, etc.

## Layout

```
criteria/
    production.inc
    stress.inc
    aged.inc
```

One flat `.inc` file per variant. Each file holds every script's `GROUP`
side by side (currently `FS_Fuse_6` for fuse-register checks and
`FS_Supply_1` for supply-rail checks) -- the same `GROUP`/`CRIT` names
across all three files, just different tolerance values.

Each `.inc` file is deliberately bare: just `GROUP( ... ) ... END_GROUP`
blocks, nothing else. No `#pragma once`, no `#include`s, no namespace. All
of that scaffolding lives in the one place that actually needs it --
`libs/core/include/core/criterions.hpp` -- so every file here is easy to
open and easy to diff against its siblings.

Not everything belongs here: tolerances that never change between variants
(or one-off ad-hoc checks added to catch a specific fault) stay as ad-hoc
`core::Verify(...)` calls directly in the script, the same way they always
have -- see the "Vout" check in `fuse_register_script.cpp`. This directory
is only for tolerances that genuinely vary by variant.

## Selecting a variant

Which variant actually gets built is a single CMake option,
`THORIUM_CRITERIA_VARIANT` (see the top-level `CMakeLists.txt`), validated
against the list above at configure time -- an unknown value is a hard
configure error, not a silent fallback:

```
cmake -B build -DTHORIUM_CRITERIA_VARIANT=aged
```

Default is `production`. This is a build-wide setting: every script picks
up the same variant through `core/criterions.hpp`, so a build always
represents one coherent hardware/test scenario, never a mix.

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
is what does -- if you add a new `GROUP` to the criteria files, add a
matching check here too, or a rename in that group can go undetected in
whichever variant isn't currently active.

## Adding a new variant

1. Add a new `<name>.inc` file here, with the same `GROUP`/`CRIT` names as
   its siblings.
2. Add the new variant name to `THORIUM_KNOWN_CRITERIA_VARIANTS` in the
   top-level `CMakeLists.txt`.
3. Add it to the `#include`/`static_assert` block in
   `test_criteria_variants_compile.cpp`.

## Adding a new GROUP (e.g. for a new script)

1. Add the `GROUP`/`CRIT` block to every existing `.inc` file here.
2. Reference it from the script the same way `supply_rail_script.cpp` and
   `fuse_register_script.cpp` do: `#include "core/criterions.hpp"`, then
   use `YourGroupName::YourCritName` directly.
3. Add a `HasYourGroupCriteria` concept + `static_assert`s for it in
   `test_criteria_variants_compile.cpp`, one per variant.
