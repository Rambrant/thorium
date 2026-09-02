# cmake/ — the build's own moving parts

Five files, each solving one problem the top-level `CMakeLists.txt` would
otherwise solve badly inline. Nothing here is generic CMake boilerplate: each
exists because a hand-written alternative had a failure mode worth avoiding, and
that reasoning is the useful part of the file.

| File | Kind | Runs at | Entry point |
|---|---|---|---|
| `CriteriaVariants.cmake` | module, `include()`d | configure | `thorium_generate_criteria_variants()` |
| `InstrumentDrivers.cmake` | module, `include()`d | configure | `thorium_generate_instrument_drivers()` |
| `FetchGTest.cmake` | module, `include()`d | configure | `add_layer_tests()` |
| `GenerateManifest.cmake` | script, `install(SCRIPT)` | **install** | the whole file |
| `ThoriumConfig.cmake.in` | template, `configure_package_config_file()` | configure → install | `find_package(Thorium)` |

The distinction in the *Kind* column matters. Three are modules included into
the project's scope, so their functions are callable from any `CMakeLists.txt`. One
is a standalone script executed in a fresh interpreter at install time, with no
access to project variables except what the surrounding `install(CODE ...)`
calls put in scope. One is never executed here at all — it is installed, to be
read by somebody else's build.

---

## `CriteriaVariants.cmake` — one list of variants, two generated headers

`THORIUM_KNOWN_CRITERIA_VARIANTS` (top-level `CMakeLists.txt`) names the
tolerance variants this deployment knows: `production stress aged`. Every
variant is compiled into the binary and chosen per run with `--criteria=`, so
that list has to reach both the compiler and the runtime selector.
`THORIUM_CRITERIA_MASTER` names which one of them is the master.

```cmake
thorium_generate_criteria_variants(
    "${THORIUM_KNOWN_CRITERIA_VARIANTS}"   # the variant names, in order
    "${THORIUM_CRITERIA_MASTER}"           # which of them is the master
    "${CMAKE_SOURCE_DIR}/dut"              # where criteria_<name>.inc live
    "${CMAKE_BINARY_DIR}/generated/thorium")
```

Sets `THORIUM_CRITERIA_VARIANT_TABLES` and `THORIUM_CRITERIA_VARIANT_NAMES` in
the caller's scope, holding the two generated paths. `framework/core` and
`framework/runner` pass those to the translation units that read them as compile
definitions.

**Two files, not one**, because the consumers want different things and neither
should acquire the other's dependencies:

- `criteria_variant_tables.inc` — every variant's `CRITERIA`/`CRIT` table, each
  in its own namespace, plus a `namespace master = <the master>` alias and a
  `THORIUM_CRITERIA_MASTER_FILE` define holding the master table's path. Read by
  `core/criteria/active_criteria.hpp`, i.e. by every script translation unit,
  and so pulls in the whole predicate/quantity vocabulary.
- `criteria_variant_names.inc` — the names as string literals, nothing else.
  Read only by the runtime `--criteria=` selector, which needs to know what the
  legal names are and not what any of them mean.

**Why generate at all.** The alternatives are an `#ifdef` chain in a header, or
a hand-maintained C++ list beside the CMake one. The second is two lists that
can disagree, and the disagreement is silent.

**The master, and why C++ never learns its name.** The master is the table
`CRIT_FROM_MASTER` borrows from and the one the merged criteria are generated
from, so it has to spell every group and every id out in full. Rather than
pasting the chosen variant's name into a macro, this file emits
`namespace master = <the master>;` next to the variant namespaces, and
`core/criteria/criterion.hpp` pastes the fixed token `master`. No header names a
variant, and `THORIUM_CRITERIA_MASTER` is the only place the choice is made.

The master's table is also emitted **first**, whatever position it holds in the
list, because every other variant's `CRIT_FROM_MASTER` rows read
`master::group::id` at namespace scope and need the master's groups to be
complete types by then. Only the declaration order changes — the X-macro below
still iterates the list as given, since those indices are what `--criteria=`
means.

**Four configure-time checks**, all placed here because here is where the list
actually is:

- `THORIUM_CRITERIA_MASTER` must be one of the known variants. Otherwise the
  alias fails at some unrelated `.inc` file's first use of the macro — an error
  naming neither the list nor the requirement.
- No variant may be called `master`, which is the alias's own name.
- The master's table must not itself use `CRIT_FROM_MASTER`: that asks a
  criterion to initialise itself from itself, which the compiler reports as an
  incomplete type inside a generated file. Matched as an invocation at the start
  of a line, so a table that merely *mentions* the macro in a comment (the dev
  deployment's single table does) is not failed for a sentence.
- Every named variant must have a `dut/criteria_<name>.inc` behind it.
  Otherwise it surfaces as a missing-include error *inside a generated file*,
  pointing at neither the list that named it nor the directory it was expected
  in.

**`_thorium_write_if_changed`** exists because `file(WRITE)` updates the mtime
whether or not the content changed, and these are files every script includes —
an ordinary reconfigure would trigger a full rebuild for nothing.

The variant list also becomes an X-macro,
`THORIUM_FOR_EACH_CRITERIA_VARIANT( X, ...)`. It has to be a macro rather than
anything cleverer because what is repeated is a *token* — a namespace name
pasted next to a group or `CRIT` identifier — and the preprocessor is the only
stage that can still paste tokens.

## `InstrumentDrivers.cmake` — the driver includes, read off the instrument table

`hal/topology/active_instruments.hpp` declares the rig's instrument globals
from `rig/instrument.inc`, so the driver headers behind that table's type
column have to be visible before it expands. This module generates them:
`instrument_drivers.hpp`, one `#include` per distinct driver, in the order the
table first names them.

It works because a driver package, its header and its namespace all carry one
name — `keysight_n6701a`, `hal/keysight_n6701a.hpp`, `hal::keysight_n6701a` —
so the qualifier on a row's type column (`keysight_n6701a::Direct`) already
names the header that row needs. Four `DcP` rows share one driver and collapse
to one include.

The preprocessor could not do this itself, twice over: a macro expansion cannot
emit an `#include` directive at all, and `instrument.inc` is read a second time
by `hal/driver/instrument.hpp` — inside plain `hal`, which every driver depends
on — where those headers must not appear. So it is either this or a
hand-written list beside the table, which is what `rig/` used to hold: six
lines restating what the type column already said.

Two configure-time checks, both for mistakes the compiler would otherwise
report from inside the generated file: a type column with no namespace
qualifier (there is no header to derive and no honest guess), and a row naming
a driver no configured package provides — which is what
`THORIUM_INSTRUMENT_PACKAGES` excluding a driver the rig still names looks
like. The package check goes through `THORIUM_INSTRUMENT_DIRS`, the directory
list the rig supplied, rather than deriving a target name from the namespace:
a driver package names its own target, and this must not start guessing it
back.

`instrument.inc` is a `CMAKE_CONFIGURE_DEPENDS` of `framework/hal/`, so editing
a row regenerates the list on the next build. Written only when the content
changes, for the same reason `CriteriaVariants.cmake` does it.

## `FetchGTest.cmake` — GoogleTest once, plus the per-layer test helper

Fetches GoogleTest from the vendored `third_party/googletest-1.18.0` and
installs nothing system-wide. Included only when `BUILD_TESTING_LAYERS` is on.

`SOURCE_DIR` with no `GIT_REPOSITORY`/`GIT_TAG`, so the "fetch" resolves to a
directory already on disk and configuring never touches the network. **The
directory name is the version**, and the only version — bumping GoogleTest means
unpacking a new `third_party/googletest-<v>` and changing that one path.

```cmake
add_layer_tests(<layer_name> <lib_to_test> <tests_dir>)
```

Creates a `<layer_name>_tests` executable from every `.cpp` in `<tests_dir>`,
links it against that library plus `GTest::gtest_main`, and registers it with
CTest via `gtest_discover_tests`.

**Globbed rather than listed**, and the argument is the failure mode of the
alternative rather than the typing it saves. A source file left out of a library
fails to link — something calls it, nothing defines it, the build stops. A *test*
file left out of its target fails at nothing: it compiles nowhere, runs never,
and reports no absence. The suite stays green precisely because the new test is
not in it. That is the one kind of missing thing this project's
compile-time-checking habit cannot catch for you.

This is the same rule the top-level `CMakeLists.txt` applies to `instruments/`:
glob where the glob settles *availability*, spell it out longhand where the list
*is* the semantic content (`THORIUM_KNOWN_CRITERIA_VARIANTS`,
`rig/instrument.inc`). A `tests/` directory is the first kind — there is no such
thing as a test file that exists but is deliberately not run.

**The directory is a parameter** rather than assumed to be
`${CMAKE_CURRENT_SOURCE_DIR}/tests`, because for one caller it isn't: the
scripts layer's tests are suite content under the deployment's `tests/` while
the target is declared in `framework/runner`. Passing it also stops this helper
globbing a caller's own `tests/` behind its back — `framework/runner/tests/` holds the
hook-ordering fixture, which belongs to a runner target rather than to a test
executable of its own.

An empty glob is a `FATAL_ERROR` naming the directory, because
`add_executable`'s own "no sources" diagnostic names the target instead.

`CONFIGURE_DEPENDS`, so adding or deleting a `tests/*.cpp` is picked up by the
next build with no manual reconfigure.

## `GenerateManifest.cmake` — asking the built binary what it contains

A **script**, not a module: `framework/runner/CMakeLists.txt` runs it via `install(SCRIPT)`
immediately after `install(TARGETS run_scripts ...)`. It executes the
just-installed binary with `--list-tests` and writes the answer out as
`manifest.json` beside it.

The point is that a discovering web server can offer "which suite, which tests,
which criteria variant" without parsing C++ or knowing anything about this build
beyond where `run_scripts` landed. The catalog is not duplicated into the build
system — it is *asked for*, from the one artefact that necessarily has it right.

`criteriaVariants` is a list rather than a single value, which is the visible
half of a real change: every variant is compiled in and chosen per run, so one
installed binary offers what previously needed one install per variant.
`defaultCriteriaVariant` is what a caller gets by passing nothing.

`masterCriteriaVariant` is reported precisely because it is *not* selectable.
The listed variants are not independent tables: each borrows the criteria it
does not change from the master via `CRIT_FROM_MASTER`, so "these tolerances
came from the stress table" is half an answer unless whoever reads the results
can also see which table the unchanged rows were inherited from. Any variant can
be the master, so it cannot be inferred from the list either.

Inputs arrive as plain variables — `THORIUM_RUN_SCRIPTS_EXE`,
`THORIUM_KNOWN_CRITERIA_VARIANTS`, `THORIUM_CRITERIA_VARIANT`,
`THORIUM_CRITERIA_MASTER`, `THORIUM_MANIFEST_OUTPUT` — set by the `install(CODE ...)` calls immediately
before, **not** passed as `-D` arguments. `install(SCRIPT)` shares variable
scope with the surrounding `install(CODE)` calls in `cmake_install.cmake`, which
is the ordinary way to hand values into an install-time script.

Two deliberate details in the parsing: it splits `group|id|description` on only
the first two `|` characters, so a description containing one of its own is not
truncated; and it escapes only backslash and quote, since those three fields are
prose out of `suite/test_catalog.inc` rather than arbitrary input.

## `ThoriumConfig.cmake.in` — the installed package's front door

A template, expanded by `configure_package_config_file()` and installed to
`${CMAKE_INSTALL_LIBDIR}/cmake/Thorium` alongside a generated
`ThoriumConfigVersion.cmake` (`SameMajorVersion` compatibility). Together with
`ThoriumTargets.cmake` — which `core`, `hal`, `hal_rig` and every instrument
driver populate through their own `install(TARGETS ... EXPORT ThoriumTargets)`
calls — this is what makes `find_package(Thorium)` work.

It has no `find_dependency()` calls, because `core` and `hal` have no
third-party dependency in their installed interface. GoogleTest is test-only.

**What the package does not give a consumer:** a `hal` that works on their
bench. `hal`'s `THORIUM_INSTRUMENT_TABLE` /
`THORIUM_WIRING_TABLE` are baked in by `#include` at hal's own compile time, so
an installed `libhal.a` is only ever built for the one rig that supplied those
paths when *it* was configured. A different rig has to build hal from source
against its own `rig/`-equivalent, setting those variables before hal
configures — not link a prebuilt hal from somebody else's install. See
[`framework/hal/README.md`](../framework/hal/README.md).

---

## Two conventions worth knowing before editing

**Glob for availability, list for meaning.** `tests/*.cpp` and
`instruments/*/CMakeLists.txt` are globbed: whether a file is present is the
whole question, and a stale hand-written list would silently under-report.
`THORIUM_KNOWN_CRITERIA_VARIANTS` and `rig/instrument.inc` are written out
longhand: the list itself is the content, and its order is meaningful. Anything
derivable from one of those lists is generated from it rather than written
beside it — which is what `InstrumentDrivers.cmake` does with the driver
includes.

**Fail at configure time, naming the list.** Both of `CriteriaVariants.cmake`'s
checks and both of `InstrumentDrivers.cmake`'s exist because the same mistake,
left to the compiler, produces an error inside a generated file that points at
neither the declaration nor the requirement. That is the CMake-side counterpart to what the C++ does with
`static_assert` — see the root [`README.md`](../README.md) on making things
compile errors where they can be.
