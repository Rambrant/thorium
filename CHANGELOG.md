# Changelog

What changed between tags, and what a DUT repository has to do about it. See
[`RELEASING.md`](RELEASING.md) for how numbers move and what counts as breaking.
Newest first.

## Unreleased

### Added
- `instruments/pico_5244d`: a driver for the Pico Technology PicoScope 5244D. No SCPI --
  it drives Pico's `ps5000a` library, loaded at runtime (nothing to install to build),
  and computes every `Measure` on the host from a block capture. Tested against a fake
  library; **not yet run against a unit** -- see its README.
- `pico_5244d::WaveformGenerator`: the 5244D's built-in function generator as a second
  face of the scope's box, laid out as the DSOX1202G's is -- a row of its own, sharing
  the one open unit.
- `THORIUM_HAL_API_VERSION` 7: `hal::BoxConnection::native<T>()` / `openNative<T>()` /
  `installNative<T>()`, one shared object per box beside the SCPI session, for a unit
  driven through a vendor library instead of SCPI (additive; `OLDEST_SUPPORTED` stays 1).
  Provided for and used by `pico_5244d`.
- `dev/`: the PicoScope as `Osc2` and its generator as `Wfg3`, both on box `Pico1` (first
  unit found); a `PicoIntoWfg` group of 15 scripts that drive the scope from the 33522B --
  ranges, offset, resolution, coupling, triggers, capture -- and a `PicoGenerator` group
  of 8 that check the Pico's own generator with the Pico's own scope.
- `RELEASING.md` and this file.
- `THORIUM_HAL_API_VERSION` 6: `hal::SourceSnapshot` and the `snapshotDriver` /
  `restoreDriver` / `portDriver` ADL hooks a source driver provides for it
  (additive; `OLDEST_SUPPORTED` stays 1). Provided by `keysight_edu36311a` and
  `keysight_ac6834b`.

### Changed (breaking)
- **`THORIUM_DUT_NAME` has no default name.** It used to be `DeviceX`; unset, it is now
  `unknown`. A DUT repo sets its own before `thorium_deployment()`, in the
  `if(NOT DEFINED THORIUM_DUT_NAME)` form `ref/CMakeLists.txt` shows. (A repo that
  relied on the old default and does nothing will print `unknown` in its logs.)
- The reference deployment in `ref/` is no longer Device X: its DUT is `ReferenceDut`
  and its adapter description "Reference DUT on standard adapter". Device X lives in
  [thorium_device_x](https://github.com/Rambrant/thorium_device_x). The acceptance
  tests take the DUT name from the build (`THORIUM_DUT_NAME`) instead of spelling it.

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
