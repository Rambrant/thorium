# cmake/ — the build's own moving parts

Four files, each solving one problem the top-level `CMakeLists.txt` would
otherwise solve badly inline. Nothing here is generic CMake boilerplate: each
exists because a hand-written alternative had a failure mode worth avoiding, and
that reasoning is the useful part of the file.

| File | Kind | Runs at | Entry point |
|---|---|---|---|
| `CriteriaVariants.cmake` | module, `include()`d | configure | `thorium_generate_criteria_variants()` |
| `FetchGTest.cmake` | module, `include()`d | configure | `add_layer_tests()` |
| `GenerateManifest.cmake` | script, `install(SCRIPT)` | **install** | the whole file |
| `ThoriumConfig.cmake.in` | template, `configure_package_config_file()` | configure → install | `find_package(Thorium)` |

The distinction in the *Kind* column matters. Two are modules included into the
project's scope, so their functions are callable from any `CMakeLists.txt`. One
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

```cmake
thorium_generate_criteria_variants(
    "${THORIUM_KNOWN_CRITERIA_VARIANTS}"   # the variant names, in order
    "${CMAKE_SOURCE_DIR}/dut"              # where criteria_<name>.inc live
    "${CMAKE_BINARY_DIR}/generated/thorium")
```

Sets `THORIUM_CRITERIA_VARIANT_TABLES` and `THORIUM_CRITERIA_VARIANT_NAMES` in
the caller's scope, holding the two generated paths. `libs/core` and `app` pass
those to the translation units that read them as compile definitions.

**Two files, not one**, because the consumers want different things and neither
should acquire the other's dependencies:

- `criteria_variant_tables.inc` — every variant's `CRITERIA`/`CRIT` table, each
  in its own namespace. Read by `core/active_criteria.hpp`, i.e. by every script
  translation unit, and so pulls in the whole predicate/quantity vocabulary.
- `criteria_variant_names.inc` — the names as string literals, nothing else.
  Read only by the runtime `--criteria=` selector, which needs to know what the
  legal names are and not what any of them mean.

**Why generate at all.** The alternatives are an `#ifdef` chain in a header, or
a hand-maintained C++ list beside the CMake one. The second is two lists that
can disagree, and the disagreement is silent.

**Two configure-time checks**, both placed here because here is where the list
actually is:

- The variant set must contain `production`. `CRIT_FROM_PRODUCTION` resolves an
  unqualified `production::group::id`, so a set without one fails at some
  unrelated `.inc` file's first use of the macro — an error naming neither the
  list nor the requirement.
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
scripts layer's tests are suite content under `suite/tests/` while the target is
declared in `app/`. Passing it also stops this helper globbing a caller's own
`tests/` behind its back — `app/tests/` holds `test_acceptance.cpp`, which is
deliberately a target of its own.

An empty glob is a `FATAL_ERROR` naming the directory, because
`add_executable`'s own "no sources" diagnostic names the target instead.

`CONFIGURE_DEPENDS`, so adding or deleting a `tests/*.cpp` is picked up by the
next build with no manual reconfigure.

## `GenerateManifest.cmake` — asking the built binary what it contains

A **script**, not a module: `app/CMakeLists.txt` runs it via `install(SCRIPT)`
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

Inputs arrive as plain variables — `THORIUM_RUN_SCRIPTS_EXE`,
`THORIUM_KNOWN_CRITERIA_VARIANTS`, `THORIUM_CRITERIA_VARIANT`,
`THORIUM_MANIFEST_OUTPUT` — set by the `install(CODE ...)` calls immediately
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
bench. `hal`'s `THORIUM_ACTIVE_INSTRUMENTS` / `THORIUM_INSTRUMENT_TABLE` /
`THORIUM_WIRING_TABLE` are baked in by `#include` at hal's own compile time, so
an installed `libhal.a` is only ever built for the one rig that supplied those
paths when *it* was configured. A different rig has to build hal from source
against its own `rig/`-equivalent, setting those variables before hal
configures — not link a prebuilt hal from somebody else's install. See
[`libs/hal/README.md`](../libs/hal/README.md).

---

## Two conventions worth knowing before editing

**Glob for availability, list for meaning.** `tests/*.cpp` and
`instruments/*/CMakeLists.txt` are globbed: whether a file is present is the
whole question, and a stale hand-written list would silently under-report.
`THORIUM_KNOWN_CRITERIA_VARIANTS` and `rig/instrument.inc` are written out
longhand: the list itself is the content, and its order is meaningful.

**Fail at configure time, naming the list.** Both of `CriteriaVariants.cmake`'s
checks exist because the same mistake, left to the compiler, produces an error
inside a generated file that points at neither the declaration nor the
requirement. That is the CMake-side counterpart to what the C++ does with
`static_assert` — see the root [`README.md`](../README.md) on making things
compile errors where they can be.
