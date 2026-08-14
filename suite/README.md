# suite/ -- test scripts and the test catalog

This directory holds the test scripts themselves and the catalog listing
them -- the "what to run" side of the test suite. DUT-specific data (the
adapter wiring, tolerance variants, the Measure singleton) lives under
`dut/` instead -- see `dut/README.md` -- since it's a property of
the device being tested, not of how tests are organized.

`test_catalog.inc` -- the `GROUP`/`TEST`/`END_GROUP` test catalog (see
`libs/core/include/core/test_catalog.hpp`) -- lists which scripts exist and
what to call them. Unlike the criteria variants, there's only one of these
(no `THORIUM_TEST_CATALOG_VARIANT` -- a build represents one hardware
scenario, but always runs the same set of tests). See
`core/active_test_catalog.hpp` for how it's consumed.

The same file may declare `SETUP`/`TEARDOWN` -- the code bracketing the
selected scripts, typically powering the rig up and back down. Both are
optional and independent: this catalog declares `TEARDOWN( rigPowerOff)`
(`scripts/rig_power_off.cpp`) and no `SETUP` at all, since nothing here powers
the rig up yet -- and an absent hook needs no placeholder. See the README's
"Bracket a run with setup and teardown" for how to add one, and
`core::RunHook` for what they promise.

`rigPowerOff` is worth reading for the distinction it draws against
`hal::safeRig()`, which runs immediately after it on every exit and looks like
the same job: safing is the unconditional, unordered crash path, while the
teardown is the normal one and exists to express the one thing safing
deliberately cannot -- a sequence (alternate sources down before the primary,
outputs off before relays open).

`scripts.hpp` holds the test scripts' declarations, at global scope
deliberately (see the comment there) -- which is what lets
`core/active_test_catalog.hpp` name one directly in a `TEST(...)` without
knowing any suite-specific namespace at all. It is declarations *only*: that
header is compiled into `main.cpp` via `THORIUM_TEST_SCRIPTS`, which has no
use for instruments or criteria tables.

`prelude.hpp` is the other half of that split and is what a script actually
includes -- everything a script body is written against, in one line:
`scripts.hpp` itself, the rig's instrument globals, `Measure`/`Apply`/
`Verify`, the merged criteria tables, and the adapter points. A script
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
discovered by `app/CMakeLists.txt`.

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
There is no runner-specific test; `app/` just has the one executable.

## Layout

```
suite/
    test_catalog.inc
    scripts.hpp
    prelude.hpp
    scripts/
        fuse_register_script.cpp
        supply_rail_script.cpp
        rig_power_off.cpp            # the catalog's TEARDOWN, not a test
    tests/
        test_fuse_register_script.cpp
        test_supply_rail_script.cpp
        test_rig_power_off.cpp
        test_criteria_variants_compile.cpp
```

Related: `app/` (not this directory) is the runner -- `main.cpp` plus the
two build targets (`scripts`, `run_scripts`) it needs -- kept free of any
DUT-specific content, the same way this directory is kept free of build
files. `dut/` holds the DUT-specific data itself -- see
`dut/README.md`.
