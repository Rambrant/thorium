# ref/ -- the reference deployment

A complete deployment -- a simulated rig, a DUT with its adapter and criteria
variants, a suite of scripts with their tests, and black-box acceptance tests --
laid out exactly as a DUT repository lays out its own: `rig/`, `dut/`, `suite/`
and `acceptance/` directly under one root. This is what the framework's own tests
and examples run against, and what a new DUT repository starts from.

```
ref/
    CMakeLists.txt  the whole top-level build of a DUT repo: fetch a pinned framework, call thorium_deployment()
    CMakePresets.json  the compiler presets such a repo carries (presets are read before the fetch)
    rig/          the bench: instruments, switching cards, wiring (all Simulated)
    dut/          the device under test: adapter points, criteria variants
    suite/        its test scripts, catalog and the tests of them
    acceptance/   black-box tests over this deployment's own run_scripts
```

It is selected by default (this repo's top-level `CMakeLists.txt` sets
`THORIUM_DEPLOYMENT_ROOT` to this directory), so `cmake --preset macos-debug`
builds it. `dev/`, the desk bench, is the other deployment here and keeps the
repository root as its deployment root; its includes read `dev/rig/wiring.inc`
where this one's read `rig/wiring.inc`.

Why a deployment of its own rather than `dev/`: `dev/` is real hardware over USB,
and an ordinary `run_scripts` preflights its rig, so nothing that drives a
built runner can run there without instruments plugged in. This one is simulated
end to end, and it has what the generic scenarios need that `dev/` does not -- a
switching fabric, DUT points, several criteria variants, run and group hooks.

`CMakeLists.txt` and `CMakePresets.json` here are not part of this repository's
own build -- its top level builds this directory's contents directly -- but the
ones a repository copied from this directory would use. Pointed at this checkout
with `-DFETCHCONTENT_SOURCE_DIR_THORIUM=..`, `cd ref && cmake --preset macos-debug`
builds them as such, which is how the consumer path is exercised without a download.

A repository started from this directory builds against a pinned framework tag, and on
a machine with no network the framework is read from a local mirror instead
(`-DTHORIUM_REPOSITORY=<path>`). The developer-and-tester view of that, step by step, is
in [thorium_device_x's README](https://github.com/Rambrant/thorium_device_x#building-on-an-air-gapped-rig-computer);
the mechanism is in [`cmake/README.md`](../cmake/README.md).

Each directory has its own README: [`rig/`](rig/README.md),
[`dut/`](dut/README.md), [`suite/`](suite/README.md),
[`acceptance/`](acceptance/README.md).

**It is not any real device.** It began as Device X's bench, and Device X now has a
repository of its own ([thorium_device_x](https://github.com/Rambrant/thorium_device_x)),
which started as a copy of this directory and pins the framework at a tag. This
one is the framework's fixture: its DUT is called `ReferenceDut`, its numbers are
invented, and nothing in it should be read as Device X's.
