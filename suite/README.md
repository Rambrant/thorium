# suite/ -- test scripts and the test catalog

This directory holds the test scripts themselves and the catalog listing
them -- the "what to run" side of the test suite. DUT-specific data (the
adapter wiring, tolerance variants, the Measure singleton) lives under
`dut/` instead -- see `dut/README.md` -- since it's a property of
the device being tested, not of how tests are organized.

`test_catalog.inc` -- the `GROUP`/`TEST`/`END_GROUP` test catalog (see
`framework/core/include/core/catalog/test_catalog.hpp`) -- lists which scripts exist and
what to call them. Unlike the criteria variants, there's only one of these
(no `THORIUM_TEST_CATALOG_VARIANT` -- a build represents one hardware
scenario, but always runs the same set of tests). See
`core/catalog/active_test_catalog.hpp` for how it's consumed.

The same file may declare `RUN_SETUP`/`RUN_TEARDOWN` -- the code bracketing the
selected scripts, typically powering the rig up and back down. Both are
optional and independent, and each takes a description the logs head it with
(every hook is called `setup` or `teardown`, so that prose is what tells one
from another); this catalog declares both -- `RUN_SETUP( rigPowerOn, "...")`
and `RUN_TEARDOWN( rigPowerOff, "...")`, in `scripts/rig_power_on.cpp` and
`scripts/rig_power_off.cpp`. See the README's "Bracket a run with setup and
teardown" for how to add a pair, and `core::RunHook` for what they promise.

A single `GROUP` may bracket itself the same way, with unqualified `SETUP`/
`TEARDOWN` rows in its own table -- the qualifier is on the pair above because
that one sits alone at file scope, where nothing around it says what it
brackets. A group's hooks run only if something in that group was selected, and
once per `--repeat` pass rather than once per run, which is the whole of the
difference and the reason to reach for them: a rig state that only one group's
tests need should not be established for a run that selected none of them. This
catalog declares none; its groups need nothing beyond the run-level pair. See
the README's "Bracket one group".

The pair is worth reading for the distinction it draws against `hal::safeRig()`,
which runs immediately after the teardown on every exit and looks like the same
job: safing is the unconditional, unordered crash path, while these are the
normal one and exist to express the one thing safing deliberately cannot -- a
sequence. Up: relay closed dead, then energised, primary before the alternates.
Down: the exact inverse. Only the setup returns a meaningful verdict -- it reads
each source back, and a `false` stops the run before the first script.

The hooks are also the only functions in this directory that return anything at
all. A *script* returns `void`: its verdict is derived from the checks it
recorded, at the point the runner closes the test (`core::Journal::endTest`), so
there is no flag to fold and no way for a script's answer to disagree with its
own log. A hook cannot work that way because it runs outside any test bracket --
there is no per-test event stream to derive from -- which is exactly why
`core::RunHook` is a distinct type from `core::TestScript` rather than the same
shape under another name.

`scripts.hpp` holds the test scripts' declarations, at global scope
deliberately (see the comment there) -- which is what lets
`core/catalog/active_test_catalog.hpp` name one directly in a `TEST(...)` without
knowing any suite-specific namespace at all. It is declarations *only*: that
header is compiled into `main.cpp` via `THORIUM_TEST_SCRIPTS`, which has no
use for instruments or criteria tables.

`prelude.hpp` is the other half of that split and is what a script actually
includes -- everything a script body is written against, in one line:
`scripts.hpp` itself, the rig's instrument globals, `Measure`/`Apply`/
`Verify`/`Fail`, the merged criteria tables, and the adapter points. A script
therefore begins with exactly

```cpp
#include "../prelude.hpp"
```

and nothing else. Which framework header supplies `Verify` and which supplies
`Measure` is not something a test engineer writing a script should have to
carry; before this split the two scripts here had already drifted to different
answers. Note that `tests/` does *not* use the prelude -- see below.

`scripts/` holds the scripts' definitions, one `.cpp` per script -- not built
via a `CMakeLists.txt` of its own (this directory has none, deliberately), but
discovered by `framework/runner/CMakeLists.txt`.

`tests/` holds the tests *of* this suite's content -- the scripts -- plus
`test_criteria_variants_compile.cpp`, which tests the criteria data that
actually lives in `dut/` (kept here rather than there since it's a
test, following the same "content in one place, tests alongside the rest
of the test-script tests" split `dut/README.md` also describes).

These deliberately include `scripts.hpp`, not `prelude.hpp`: a test of a
script is not a script. It calls one and injects its readings by point
*name* (`Measure.inject( "Output5V", ...)`), so it needs the `Measure` verb
and the quantity types and none of the criteria or adapter tables -- which
is also what lets the `scripts_tests` target build without the criteria
compile definitions, those being `PRIVATE` to the `scripts` library.

They reach a script's verdict through `verdict.hpp`'s `verdictOf( script)`,
which does what the runner does around a script -- open a journal test bracket,
run it, close the bracket and take the answer -- since a script no longer
returns one. Two things follow that are worth knowing when writing one of these:
the "a script that recorded no check cannot pass" rule applies here too (it
lives in `endTest`, not in the runner), and a test that only wants the outcome
needs no recording sink at all -- only one asserting on the individual *rows*
does.

`acceptance/` holds the black-box tests over the *binary*: they run the built
`run_scripts` as a subprocess, with real flags and real log files, and assert on
its stdout, its artifacts and its exit status. Nothing links a Thorium library --
the command line and what comes back out are the entire interface under test.

They live here, in this deployment's suite, rather than beside the runner they
drive, because that is what they actually assert about: this suite's group and
test names, this DUT's name in the report header, this rig's instruments in the
log. None of that is true of a second deployment, which brings its own
`acceptance/` or sets `THORIUM_ACCEPTANCE_TESTS=OFF` and brings none -- see
`dev/README.md`. `framework/runner/CMakeLists.txt` discovers the directory by glob,
the same way it discovers `scripts/` and `tests/`.

What is *not* here is the hook-ordering fixture the acceptance tests use as their
second runner (`run_scripts_hooked`, over a catalog that announces its own
`RUN_SETUP`/`SETUP` and can be made to fail on demand). That fixture names no
instrument and no DUT point, and what it tests is the runner's ordering, so it is
framework: `framework/runner/tests/fixtures/`.

## Layout

```
suite/
    test_catalog.inc
    scripts.hpp
    prelude.hpp
    scripts/
        fuse_register_script.cpp
        supply_rail_script.cpp
        console_script.cpp
        ac_dropout_script.cpp
        rig_power_on.cpp             # the catalog's RUN_SETUP, not a test
        rig_power_off.cpp            # the catalog's RUN_TEARDOWN, not a test
    tests/
        verdict.hpp                  # verdictOf( script) -- a script returns no verdict to assert on
        test_fuse_register_script.cpp
        test_supply_rail_script.cpp
        test_console_script.cpp
        test_ac_dropout_script.cpp
        test_rig_power_on.cpp
        test_rig_power_off.cpp
        test_criteria_variants_compile.cpp
    acceptance/
        test_acceptance.cpp          # the built run_scripts, driven as a subprocess
```

Related: `framework/runner` (not this directory) is the runner -- `main.cpp` plus the
two build targets (`scripts`, `run_scripts`) it needs -- and it is framework,
packaged with `core` and `hal`, holding no DUT-specific content at all. It
reaches this directory the same way `framework/hal` reaches `rig/`: as a path
(`THORIUM_SUITE_DIR`), which is why this directory stays free of build files.
`dut/` holds the DUT-specific data itself -- see `dut/README.md`.
