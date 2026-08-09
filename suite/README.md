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
optional, and this catalog declares neither, which is why nothing here
mentions them beyond this paragraph: a catalog without them needs no
placeholder. See the README's "Bracket a run with setup and teardown" for
how to add a pair, and `core::RunHook` for what they promise.

`scripts.hpp` holds the test scripts' declarations, at global scope
deliberately (see the comment there) -- which is what lets
`core/active_test_catalog.hpp` name one directly in a `TEST(...)` without
knowing any suite-specific namespace at all. `scripts/` holds their
definitions, one `.cpp` per script -- not built via a `CMakeLists.txt` of
its own (this directory has none, deliberately), but discovered by
`app/CMakeLists.txt`.

`tests/` holds the tests *of* this suite's content -- the scripts -- plus
`test_criteria_variants_compile.cpp`, which tests the criteria data that
actually lives in `dut/` (kept here rather than there since it's a
test, following the same "content in one place, tests alongside the rest
of the test-script tests" split `dut/README.md` also describes).
There is no runner-specific test; `app/` just has the one executable.

## Layout

```
suite/
    test_catalog.inc
    scripts.hpp
    scripts/
        fuse_register_script.cpp
        supply_rail_script.cpp
    tests/
        test_fuse_register_script.cpp
        test_supply_rail_script.cpp
        test_criteria_variants_compile.cpp
```

Related: `app/` (not this directory) is the runner -- `main.cpp` plus the
two build targets (`scripts`, `run_scripts`) it needs -- kept free of any
DUT-specific content, the same way this directory is kept free of build
files. `dut/` holds the DUT-specific data itself -- see
`dut/README.md`.
