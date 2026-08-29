# acceptance/ -- black-box tests over this deployment's run_scripts

This directory holds the tests that run the built `run_scripts` as a subprocess
-- real flags, real log files, real exit statuses -- and assert on what comes
back out. Nothing here links a Thorium library: the command line and its
artifacts are the entire interface under test.

That is a different question from the one every other test target asks.
`core_tests` exercises the log sinks with hand-built events and `rig_tests`
exercises the verbs that feed them, which between them settles whether each
piece is correct and says nothing about whether the *binary* wires them
together -- that the flags parse, that log files land where they were asked to,
that `--safe` leaves no log behind, that the exit status agrees with what the
log says happened. Those are properties of a command line, and the only honest
way to test a command line is to run it.

## Why this is a directory of its own

The binary under test is framework -- `framework/runner` builds it, and every
line of it is portable across deployments. What is asserted about it here is
not: this suite's group and test names, this DUT's name in the report header,
this rig's instruments in the log, this deployment's three criteria variants. A
bench with one meter and one script satisfies none of it. So these tests are
deployment content, and they cannot sit beside the runner they drive.

They are not *suite* content either, which is the second half of the placement
and the reason this is a fourth directory rather than a `suite/` subdirectory.
The list above spans all three legs at once: suite names, a DUT name, rig
instruments. `suite/tests/` holds the tests *of* `suite/scripts/`, `rig/tests/`
the tests of the rig data, `dut/tests/` the tests of the DUT profile -- each one
tests the directory it sits in. Nothing here does, so it sits beside them.

There is a mechanical reason too, and it is worth knowing because it forecloses
the obvious alternative rather than merely arguing against it:
`add_layer_tests()` globs a layer's tests directory with `GLOB_RECURSE` (see
`cmake/FetchGTest.cmake` on why, and on the failure mode a flat glob has). So
these sources under `suite/tests/`, at *any* depth, would also be compiled into
`scripts_tests` -- where `THORIUM_RUN_SCRIPTS_EXE` and the other two definitions
below are not defined, because they are `PRIVATE` to `acceptance_tests`. That
helper's own comment states the rule: sources under a `tests/` tree that belong
to another target must live outside every path handed to it.

What is *not* here is the hook-ordering fixture these tests drive as their
second runner (`run_scripts_hooked`, over a catalog that announces its own
`RUN_SETUP`/`SETUP` and can be made to fail on demand). That fixture names no
instrument and no DUT point, and what it tests is the runner's own ordering, so
it is framework: `framework/runner/tests/fixtures/`.

## Build

`framework/runner/CMakeLists.txt` discovers this directory by glob, through
`THORIUM_ACCEPTANCE_DIR` -- the same way it reaches `suite/scripts/` and
`suite/tests/` -- which is why this directory stays free of build files like its
three peers.

`THORIUM_ACCEPTANCE_TESTS` is `ON` by default, so a deployment that brings
acceptance tests gets them with no configuration at all. A deployment that
brings none sets it `OFF` once in its preset; `dev-deployment` does exactly that
(see `dev/README.md`). Turning the option on over an empty directory is a
configure error rather than a quiet skip, for the reason `add_layer_tests` gives
for the same check: a test target that silently declines to exist is the one
kind of missing thing a compile-time-checked codebase cannot catch for you.

Three definitions are baked in, and the header comment in `test_acceptance.cpp`
covers what each is for: the two runner paths (as generator expressions, so this
survives any generator or build layout) and the artifact root.

## Running them

```bash
ctest --test-dir build/debug -L  acceptance -V     # only these, verbose
ctest --test-dir build/debug -LE acceptance        # everything else
```

The label exists because these are the only tests in the tree that shell out and
touch the filesystem, and so the slow half.

They are also meant to be read. Every invocation is printed as it happens,
tagged `[  INVOKE  ]`, which makes `-V` a worked tour of the CLI; the argument
lists in each scenario are written out literally rather than assembled from
shared constants, because the value of a line like
`run_scripts --quiet --log-dir=artifacts/run-1` is that somebody can read it and
then type it.

Each scenario runs in its own directory under
`<build>/framework/runner/acceptance/<suite>.<test>/` and everything it produced
stays there afterwards -- the exact invocation, the console transcript, both log
files, the exit status. That is the point rather than a side effect: a failing
assertion names the file to go and read, and a passing run still leaves real
specimens of both log formats to look at.

## Layout

```
acceptance/
    test_acceptance.cpp          # the built run_scripts, driven as a subprocess
```

Related: `suite/` holds the scripts and the catalog this drives -- see
`suite/README.md`; `dut/` and `rig/` hold the other two legs whose facts these
tests assert on. `framework/runner` builds the binary under test.
