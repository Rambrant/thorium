# Releasing, versioning, and upgrading

A DUT repository pins this framework at a git tag and upgrades when *it* chooses
(see `cmake/README.md`, `thorium_deployment()`). That only works if a tag means
something: what it promises, how a number moves when a promise breaks, and where a
DUT repo reads what changed. This file is that contract. Everything here is
policy, not mechanism -- the mechanisms (`THORIUM_VERSION`, the
`THORIUM_HAL_API_*` gate, the revision in the logs) are named where they
appear, and the rest is for whoever cuts a release to follow.

## What a release is

One annotated git tag, `vMAJOR.MINOR.PATCH`, on this whole repository: `framework/`,
`instruments/`, `cmake/`, `third_party/` and `ref/`. **The framework and every
driver in `instruments/` share that one tag.** A DUT repo does not pick drivers at
versions of their own; it picks a tag, and a driver it needs at a different version
is vendored beside the DUT (`THORIUM_EXTRA_INSTRUMENT_DIRS`). Finer granularity can
be revisited if a real DUT needs it.

The tag, not a branch, is what a DUT's `GIT_TAG` names. Tags are never moved or
deleted once pushed: a log that says `Thorium 0.1.0 (v0.1.1)` has to keep meaning
one commit.

## Version numbers

`THORIUM_VERSION` in `cmake/ThoriumDeployment.cmake` is the only place the number
is written. A release tag is `v` plus it. (`project(Thorium ...)` deliberately
carries no version of its own.)

**Before 1.0** (now), a minor version may break anything and a patch version may
not: `0.2.0` can change the public surface below; `0.1.2` over `0.1.1` fixes bugs
and adds nothing a DUT has to change for. The installed package
(`find_package(Thorium)`) is `SameMinorVersion` until 1.0 for the same reason.

**From 1.0**, ordinary semantic versioning: a major version for anything breaking,
a minor for additions, a patch for fixes; `SameMajorVersion` compatibility.

Three numbers travel with a log and mean different things:

| Where it shows | What it is |
|---|---|
| `Thorium 0.1.0 (v0.1.1)` in the report header | `THORIUM_VERSION` and `THORIUM_FRAMEWORK_REVISION` -- the release line and the commit actually built (a tag, `tag-N-gHASH` past one, `-dirty` with local edits) |
| `Suite/DUT/rig` in the header | the *deployment's* own revision, from its repository |
| `THORIUM_HAL_API_VERSION` | the driver-facing API, versioned apart from the above -- see below |

## The public surface

A version number promises that these keep working. Everything else -- anything in a
`detail` namespace, `framework/*/src/`, generated files, the `THORIUM_*` compile
definitions the build hands `core`/`hal`/the runner, and the structure of
`framework/` itself -- may change in any release.

1. **Building.** `thorium_deployment()` and the cache variables it reads
   (`THORIUM_RIG_DIR`, `THORIUM_DUT_DIR`, `THORIUM_SUITE_DIR`,
   `THORIUM_ACCEPTANCE_DIR`, `THORIUM_DEPLOYMENT_ROOT`,
   `THORIUM_KNOWN_CRITERIA_VARIANTS`, `THORIUM_CRITERIA_VARIANT`,
   `THORIUM_CRITERIA_MASTER`, `THORIUM_INSTRUMENT_PACKAGES`,
   `THORIUM_EXTRA_INSTRUMENT_DIRS`, `THORIUM_DUT_NAME`, `THORIUM_RIG_NAME`,
   `THORIUM_*_VERSION`, `THORIUM_FRAMEWORK_TESTS`, `THORIUM_RUNNER_ACCEPTANCE`,
   `THORIUM_ACCEPTANCE_TESTS`, `BUILD_TESTING_LAYERS`), `add_layer_tests()`, and the
   GCC 16 / C++26 requirement.
2. **A deployment's files.** The table formats and macros a deployment writes:
   `rig/instrument.inc`, `devices.inc`, `wiring.inc` (and the optional `pools.inc`,
   `sites.inc`); `dut/adapter.inc` and the `criteria_<variant>.inc` tables; the
   suite's `test_catalog.inc` and `scripts.hpp`, and the contract that a script
   begins with its deployment prelude, which includes `runner/prelude.hpp`.
3. **What a script is written in.** The verbs (`Measure`, `Apply`, `Connect`,
   `Disconnect`, `Setup`, `Write`, `Read`, `Arm`/`Await`/`Fetch`, `Remove`,
   `Delay`), `Verify`/`Fail`, the predicates and quantities and their literals, and
   `hal::SourceSnapshot`.
4. **The driver API**, below.
5. **Test support.** `runner/testing/acceptance.hpp`, `core/testing/capturing_sink.hpp`,
   `core/testing/verdict.hpp`.
6. **What the binary says.** `run_scripts`' flags and exit status, the keys of the
   machine log (event JSON and SARIF properties), the report header's fields, the
   recording/stimulus file formats, and `manifest.json`.

## The driver API

A driver is written against `hal`, and a DUT can vendor its own beside the framework,
so the driver-facing API has a gate of its own: `THORIUM_HAL_API_VERSION` rises on
every change to it, additions included, and `THORIUM_HAL_API_OLDEST_SUPPORTED`
rises only when a change breaks drivers written against older numbers. Each driver
asserts the version it needs with `THORIUM_REQUIRE_HAL_API( n)` (see
`framework/hal/include/hal/driver/api_version.hpp`, which also keeps the log).

Two rules follow, and the second is the one that is easy to forget because nothing
derives the numbers:

- A release that raises `OLDEST_SUPPORTED` is a breaking release, whatever else it
  holds, and its changelog entry says what a vendored driver must change.
- **A change to anything a driver can see bumps `THORIUM_HAL_API_VERSION` in the same
  commit** -- including the ADL hooks a driver provides (`applyDriver`,
  `connectDriver`, `snapshotDriver`, ...) and the package layout convention
  (`include/hal/<namespace>.hpp`, namespace `hal::<namespace>`, target `hal_<namespace>`).

## What counts as what

| Breaking (minor before 1.0, major after) | Additive (minor from 1.0, patch before) | Fix (patch) |
|---|---|---|
| Renaming or removing anything in the public surface | A new verb, predicate, quantity, macro, option | A bug in behaviour the docs already promised |
| Changing what an existing macro, table column or flag means | A new optional column or flag | Documentation, comments |
| A default that changes a build's result (what gets built, which tests run) | A new driver package | A test that was wrong |
| Raising `THORIUM_HAL_API_OLDEST_SUPPORTED` | A new `THORIUM_HAL_API_VERSION` with `OLDEST_SUPPORTED` unchanged | |
| Removing or renaming a log field | Adding a log field | |

Before 1.0 the "additive" column is still a minor bump if it is large enough to
want a changelog entry; the line that matters is that **a patch never makes a DUT
edit anything**.

## Cutting a release

1. Everything on `main` the release should hold is merged; the tree is clean.
2. Move `CHANGELOG.md`'s *Unreleased* entries under the new version and date, and for
   anything breaking say what a DUT repo has to change.
3. Set `THORIUM_VERSION` in `cmake/ThoriumDeployment.cmake`. If a driver-facing
   header changed, check `THORIUM_HAL_API_VERSION` moved with it.
4. Build and run both of this repository's deployments, from clean:
   ```bash
   cmake --preset macos-debug && cmake --build build/debug && ctest --test-dir build/debug
   cmake --preset macos-dev   && cmake --build build/dev   && ctest --test-dir build/dev
   ```
   and the consumer path, which is the one that matters to a DUT repo -- the template
   in `ref/` configured against this checkout, with only its own tests built:
   ```bash
   cd ref
   cmake --preset macos-debug -DFETCHCONTENT_SOURCE_DIR_THORIUM=..
   cmake --build build/debug && ctest --test-dir build/debug
   ```
5. Commit, then tag the commit: `git tag -a vX.Y.Z -m "Thorium X.Y.Z"`; push the tag.
6. Update the `GIT_TAG` in `ref/CMakeLists.txt` -- it is the template a new DUT repo
   copies -- in a follow-up commit, not the tagged one.

## Upgrading a DUT repository

Change `GIT_TAG` in the repository's `CMakeLists.txt`, read `CHANGELOG.md` for every
version between the old tag and the new, make the edits it lists, reconfigure and
run the DUT's own tests. Nothing about another DUT is involved. The next run's
report header says `Thorium X.Y.Z (vX.Y.Z)`, which is the record that it happened.

## Older lines

No backports by default: a DUT that must stay on `v0.1.x` stays on it, and takes
fixes by moving forward when it can. If a fix has to reach an old line, branch
`release/0.1` from its last tag, cherry-pick, and tag `v0.1.N+1` there. A tag is
immutable; a branch is the only thing that moves.

## Not yet decided

- Where the framework repository lives and how a DUT repo reaches it (the
  placeholder in `ref/CMakeLists.txt` is `https://example.invalid/thorium.git`).
- Whether a driver ever needs its own version independent of the framework's; the
  single shared tag is a starting point and is cheap to revisit.
- Whether `ref/` should be released as its own thing or only travel with the tag.
