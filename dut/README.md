# dut/ -- DUT-specific data, and nothing else

This directory holds *only* data specific to the actual device under test --
Device X on its standard adapter -- expressed as flat, macro-driven `.inc`
files with no C++ logic of their own. There is deliberately no library here
(no `add_library(dut ...)`): the mechanisms that make sense of this data
(`core::AdapterPointTag`, `core::MeasureEngine`, the `CRITERIA`/`ADAPTER`
macros themselves) all live in `framework/hal`/`framework/core` -- the portable,
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

`ADAPTER`/`POINT`/`END_ADAPTER` (see `framework/hal/include/hal/adapter.hpp`) --
mirroring `CRITERIA`/`CRIT`/`END_CRITERIA` below -- expand into the one
`dut` struct: a fixed set of named points (e.g. `Output5V`),
each carrying its VPC90 location baked into its own *type*
(`core::AdapterPointTag<Loc>`), not stored as runtime data. That is what makes
both of the following genuine compile errors, exactly like `CRIT`'s protection
against a misspelled criterion id:

- **A misspelled point name** -- `dut::Output5Vx` is "no such
  member", the same way `FS_Fuse_6::FS_Fuse_01x` already is.
- **A missing `at()`** -- `Measure( Dmm1.voltage(), dut::Output5V)`,
  with the point passed bare, also fails to find a matching overload:
  `MeasureEngine::operator()` takes `core::At<AdapterPointTag<Loc>>`,
  not an `AdapterPointTag` directly -- see `core/at.hpp`'s own comment for
  why `at(...)` exists as a call-site marker rather than an implicit
  conversion.

Point identifiers can't reuse a spec label verbatim where it starts with a
digit (C++ identifiers can't start with a digit) -- `Output5V`/`Output3V3`
stand in for the spec's "5VOutput"/"3V3Output".

Because there is exactly one adapter per device (the connector on the
device doesn't change), this struct -- not some separate profile type --
*is* the adapter. That is also why `ADAPTER` takes only a description and
not a name: a build targets one DUT on one adapter, so the struct is always
`dut`, and no call site has to repeat a name the build already fixed. The
DUT's identity for the logs is `THORIUM_DUT_NAME` (see
`framework/core/CMakeLists.txt`) -- a display string, not a C++ identifier.

Like the criteria files below, this file is deliberately bare: just
`ADAPTER( ... ) ... END_ADAPTER`, nothing else -- no `#pragma once`, no
`#include`s, no namespace. `ADAPTER`/`POINT` expand fully qualified
(`::hal::...`, `::core::...`), unlike `CRITERIA`/`CRIT` which need a
`using namespace` wrapper -- see `hal/adapter.hpp`'s own comment for why.

A script does not `#include` this file itself: `suite/prelude.hpp` does it
(after `hal/adapter.hpp`, for the macros -- that ordering rule is exactly
what is worth stating once there rather than at the top of every script),
alongside `core/active_criteria.hpp` and the rig's instruments. A script
includes the prelude and nothing else. `dut/tests/` does include this file
directly, since those tests exercise the data itself.

Reachability -- whether this rig's wiring (see
`framework/hal/include/hal/wiring.hpp`) actually connects a given instrument to
a given point -- is still a runtime check, since `Loc`/`Kind` being
compile-time values doesn't by itself make the *wiring table lookup*
compile-time; see the `TODO(reflection)` in `hal/wiring.hpp` for what a
further upgrade there would look like.

## Criteria variants

The tolerance tables (`CRITERIA`/`CRIT` blocks, see
`framework/core/include/core/criterion.hpp`) for scripts that need more than one
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
have -- see the "Supply voltage at Vout" check in
`suite/scripts/fuse_register_script.cpp`. This directory is only for
tolerances that genuinely vary by variant.

That choice has one consequence worth knowing before making it. An ad-hoc
check takes prose and nothing else -- `Verify( "Supply voltage at Vout",
EQ( 12.0_V).epsilon( 0.05_V), reading)` -- and its result reports in the
machine log under the shared `Thorium/Verify` rule rather than under a rule
of its own. Its text is still in the file (the result's logical location and
message), but a SARIF consumer cannot group, trend, suppress or baseline it
as a distinct requirement. A `CRIT` entry can be, because it has a stable
compile-time id. So: if the check is a requirement you want to follow across
runs, declare it here; if it is an assertion that a script needs in order to
be honest about what it did, leave it inline.

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
from inside any other variant (see `core/active_criteria.hpp`) -- every other
variant can reference it. It is also the *reference table* the merged criteria
are generated from, for the same reason: it is the one file that cannot use
`CRIT_FROM_PRODUCTION` itself, so it is the one guaranteed to spell out every
group and every id in full.

## Selecting a variant

**Every** variant listed above is compiled into the binary. Which one a run
applies is chosen on the command line:

```
run_scripts --criteria=aged
```

Without the flag, the variant the build was configured for applies -- a single
CMake option, `THORIUM_CRITERIA_VARIANT` (see the top-level `CMakeLists.txt`),
validated against `THORIUM_KNOWN_CRITERIA_VARIANTS` at configure time, so an
unknown value is a hard configure error rather than a silent fallback:

```
cmake -B build -DTHORIUM_CRITERIA_VARIANT=aged
```

Default is `production`. An unknown `--criteria=` is likewise fatal, and prints
the names that would have worked -- a runner that quietly fell back to the
default would apply the wrong tolerances to real hardware and hand back a log
that looks entirely normal.

The choice is still coherent per run, never a mix: it is frozen the moment the
journal opens, so the variant named in both logs' traceability header is
provably the one every check in them was made against.

This used to be a build-wide setting -- one variant baked in per build, three
variants meaning three build directories and three binaries. It is not any
more, and nothing was given up in compile-time checking to get there; see
"How all three fit in one binary" below.

## How all three fit in one binary

`core/active_criteria.hpp` reads the variant files twice.

**Pass 1** pulls in every variant, each into its own namespace
(`thorium::criteria::production`, `::stress`, `::aged`) -- generated from
`THORIUM_KNOWN_CRITERIA_VARIANTS` by `cmake/CriteriaVariants.cmake`. Their
being siblings is what makes `CRIT_FROM_PRODUCTION`'s unqualified
`production::group::id` resolve from inside any of them.

**Pass 2** re-reads the reference table with `CRITERIA`/`CRIT` redefined, so
each `CRIT` emits not one `Criterion` but a `core::MultiCriterion` holding
every variant's same-named criterion. That is the same
redefine-the-macro-and-re-`#include` trick `CRIT_FROM_PRODUCTION` and
`hal::safeRig()` use, and it is doing something reflection cannot: building a
*new struct type* whose members are named by another file.

A script is unaffected and needs no change. `FS_Supply_1::FS_Supply_5V0` is
still an ordinary `static constexpr` member of an ordinary struct -- it just
carries three tolerances instead of one, and `core::Verify` picks between them
by the selected variant's index.

What this costs in compile-time checking is nothing. What it adds:

| | before | now |
|---|---|---|
| Typo'd `CRIT` id in a script | no such member | unchanged |
| A `CRIT` production declares that a variant is missing | caught by `scripts_tests` | compile error naming the id **and** the variant |
| A variant whose predicate doesn't fit the reading (an amp criterion on a volt rail) | **shipped silently** until someone targeted that variant | compile error, on every build |

That last row is the one worth noticing: the compiler previously never saw the
inactive variants next to the script measuring against them, so it could not
have caught it.

## Why every variant compiles, always

Given the above, a real build now enforces most of this on its own. What it
cannot see is a group or `CRIT` that exists **only** in a non-reference variant:
the merge only ever looks up the ids production declares, so a stray id in
`criteria_stress.inc` is never merged, never run, and never complained about.

`suite/tests/test_criteria_variants_compile.cpp` is what catches that. It
`#include`s every variant file from here, each in its own namespace, and uses
reflection to `static_assert` group/id parity **both ways** between every pair
-- no hand-written concept or check per `CRITERIA` group, which matters with
dozens of them. It lives under `suite/tests/` rather than here because it's a
test *of* this data, following the same "content in one place, its tests
alongside the rest of the test-script tests" split this whole directory uses --
`test_adapter.cpp` above is the same idea, for `adapter.inc`.

## Adding a new variant

1. Add a new `criteria_<name>.inc` file here, with the same `CRITERIA`/`CRIT`
   names as its siblings.
2. Add the new variant name to `THORIUM_KNOWN_CRITERIA_VARIANTS` in the
   top-level `CMakeLists.txt`.
3. Add a namespace for it, and a `checkParity` pair against `production`, in
   `suite/tests/test_criteria_variants_compile.cpp`.

Steps 1 and 2 are what a run needs; the tables, the `--criteria=` name list and
the manifest all follow from that one list. Step 3 is the surplus-id check the
build cannot do for itself.

## Adding a new CRITERIA block (e.g. for a new script)

1. Add the `CRITERIA`/`CRIT` block to every existing `.inc` file here --
   use `CRIT_FROM_PRODUCTION( group, id)` in place of `CRIT` for any
   criterion that's identical to production's.
2. Reference it from the script the same way `supply_rail_script.cpp` and
   `fuse_register_script.cpp` do: `#include "core/active_criteria.hpp"`,
   then use `YourGroupName::YourCritName` directly.

Nothing to add to the tests: the parity check in
`suite/tests/test_criteria_variants_compile.cpp` reflects over whatever groups
each variant declares, so a new one is covered the moment it exists. Miss it in
one variant and the build says so.

## Why `active_criteria.hpp` is a separate header from `criterion.hpp`

`core/criterion.hpp` is the general, dependency-free `CRITERIA`/`CRIT`
mechanism -- it's used on its own (e.g. by `test_criterion.cpp`) with no
notion of "variants" at all. `core/active_criteria.hpp` is a specific
*consumer* of that mechanism: it resolves `THORIUM_CRITERIA_VARIANT_TABLES`
and `THORIUM_PRODUCTION_CRITERIA`, which require the `scripts` target's build
configuration. Folding the two
together would force that requirement onto every unrelated user of the
general macros -- `core`'s own unit tests would stop compiling.

Note also that `CRITERIA`/`END_CRITERIA` (not `GROUP`/`END_GROUP`) is a
deliberate naming choice: `GROUP` is reserved for grouping test cases, a
separate, unrelated mechanism -- see the comment above `CRITERIA`'s
definition in `criterion.hpp`.
