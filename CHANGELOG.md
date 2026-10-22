# Changelog

What changed between tags, and what a DUT repository has to do about it. See
[`RELEASING.md`](RELEASING.md) for how numbers move and what counts as breaking.
Newest first.

## Unreleased

### Added
- `RELEASING.md` and this file.
- `THORIUM_HAL_API_VERSION` 6: `hal::SourceSnapshot` and the `snapshotDriver` /
  `restoreDriver` / `portDriver` ADL hooks a source driver provides for it
  (additive; `OLDEST_SUPPORTED` stays 1). Provided by `keysight_edu36311a` and
  `keysight_ac6834b`.

### Changed
- `ThoriumConfigVersion.cmake` is `SameMinorVersion` while the major version is 0.
- `project(Thorium)` carries no version; `THORIUM_VERSION` in
  `cmake/ThoriumDeployment.cmake` is the single source.

## 0.1.1

The first release meant to be pinned from a DUT repository. Breaking for anything
built against the repository as it was before: each item says what to change.

### Added
- **`thorium_deployment()`** (`cmake/ThoriumDeployment.cmake`), the framework's
  entry point for a consumer. A DUT repo's top level is `project()`,
  `FetchContent` of this repository at a tag with `SOURCE_SUBDIR` set to a
  directory that does not exist, `include()` of the module, and the call --
  `ref/CMakeLists.txt` is that file.
- **`ref/`**, a complete simulated deployment (`rig/`, `dut/`, `suite/`,
  `acceptance/`) laid out as a DUT repo is, with its own `CMakeLists.txt` and
  `CMakePresets.json` as the template for one.
- `THORIUM_DEPLOYMENT_ROOT`, `THORIUM_FRAMEWORK_TESTS`, `THORIUM_RUNNER_ACCEPTANCE`,
  `THORIUM_EXTRA_INSTRUMENT_DIRS`.
- `THORIUM_FRAMEWORK_REVISION` and `core::RunInfo::FrameworkRevision`: the report
  header reads `Thorium 0.1.0 (v0.1.1)`, and `frameworkRevision` is in the event
  log and the SARIF properties.
- `runner/prelude.hpp`, the part of a script prelude that names no deployment, and
  `runner/testing/acceptance.hpp`, the black-box test harness.
- `core/testing/verdict.hpp`.

### Changed (breaking)
- **Instrument packages are derived from `rig/instrument.inc`.** An empty
  `THORIUM_INSTRUMENT_PACKAGES` used to mean every package in `instruments/`; it now
  means the packages the table names. A deployment that relied on an unnamed driver
  being built (for its tests, say) lists it in `THORIUM_INSTRUMENT_PACKAGES`.
- **A deployment prelude is two includes.** `suite/prelude.hpp` includes its
  `scripts.hpp`, then `runner/prelude.hpp`, then its DUT's `adapter.inc`; the long
  list of hal/core includes it used to carry is gone.
- **`verdict.hpp` moved** from `suite/tests/` to `core/testing/verdict.hpp`;
  `#include "core/testing/verdict.hpp"`.
- **Framework tests are off for a consumer** by default (`THORIUM_FRAMEWORK_TESTS`).
  This repository's own builds keep them on.
- **The top-level `CMakeLists.txt` is no longer a deployment you can copy.** Use
  `ref/CMakeLists.txt`. The default deployment is `ref/`; `dev/`'s presets set
  `THORIUM_DEPLOYMENT_ROOT` to the repository root.
- The hook-ordering acceptance tests are the framework's
  (`runner_acceptance_tests`), and need a rig of `Simulated{}` instruments.

### Fixed
- The bracket hooks for the Transient and Capacitance groups were two copies of the
  same save-and-restore logic; both are now `hal::SourceSnapshot`, with tests.
