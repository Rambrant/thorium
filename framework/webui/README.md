# webui/ -- the bench console, as a server

The browser-facing successor to `framework/ui`'s wxWidgets console (deleted;
see "History" below and git log for the original). Like that program, it does
exactly one thing to the rig: launch the installed `run_scripts` as a
subprocess and relay what it says on stdout. It opens nothing itself, decides
no verdict itself, and keeps no state about a run beyond what is needed to
replay it to a browser tab that connects late -- see `run_session.hpp`, and
"What a run means" below on why a UI here "tallies nothing."

It is paired with [`framework/launcher`](../launcher/README.md), which owns
the tray icon and the browser window; this owns the rig.

## Endpoints

| | |
|---|---|
| `GET /`, `GET /console.css`, `GET /js/*.js` | the console page -- `static/`, compiled into the binary by `cmake/EmbedFiles.cmake` (see `src/static_content.hpp`). `/` is `index.html`; the script is ES modules, entered at `js/main.js` |
| `GET /api/options` | `run_scripts --describe-options`, passed through verbatim (it is already JSON) |
| `GET /api/criteria` | `run_scripts --describe-criteria`, passed through verbatim: the criteria variants, the default and the master, under the same keys an install's `manifest.json` uses. What the page's criteria picker is built from; 500 from a `run_scripts` too old to answer, and the page keeps a free-text field. Asked of the binary rather than read from the manifest, so a build-tree `run_scripts` gets a picker too and an installed one's cannot be a stale install behind |
| `GET /api/tests` | `run_scripts --list-tests`, passed through verbatim (`group\|id\|description` lines) |
| `POST /api/run` | body is `{selection, settings, extra}` -- see `ui::RunRequest` -- starts a run; 409 if one is already active |
| `GET /api/events` | Server-Sent Events: every line of the active run's stdout, from wherever this connection joined, plus any stderr line as `{"kind":"stderr","text":...}` |
| `GET /api/log/rtf`, `GET /api/log/sarif` | the last run's report log, as a download. The path is taken from that run's own `runStart` line (`"logs":{"sarif":..,"rtf":..}`, see `core::EventSink::LogFiles`), never from the request; 409 while the run is still going, 404 if it wrote none (`--no-logs`, `--skeleton`). The page sends `HEAD` to both on load, so a reloaded window still offers the last run's logs |
| `GET /api/presence` | held open by every console page for as long as it is open (a comment ping a second, nothing else) -- how many are open is what the next row reports |
| `GET /api/status` | `{"viewers":N,"running":bool}`. `framework/launcher` polls it to quit the console once its last window has closed, but never during a run -- see its README |
| `POST /api/stop` | asks the active run to end the way Ctrl-C does at a terminal: after its current test, with teardown run, the rig safed and both logs written. Writes `stop` to the run's stdin (`run_scripts --stop-on-stdin`, which `buildRunCommand` always passes). 200 once delivered, 409 if there is no run. The run's own stream says when it has ended; `run_scripts` reports a stopped run as **not passed** |
| `POST /api/emergency` | kills the active run at once (`SIGKILL`, `TerminateProcess`) and then runs `--safe`: `{"killed":bool,"safed":bool}`. The run's logs are not written, and it ends with no `runEnd`. With no run active it is `/safe` under another name. Not gated on anything |
| `POST /safe` | `run_scripts --safe`. Never gated on whether a run is active -- see "What a run means" below. Path fixed by `framework/launcher/src/rig_client.hpp`, which already calls it. |

`/api/run` and `/api/events` are deliberately two requests, not one: a
browser's `EventSource` can only issue `GET`, and `POST` is what a run needs
to hand over a JSON body -- see `main.cpp`'s note on the split. A client
starts a run with the first, then opens the second to watch it; `POST
/api/run` only returns once `RunSession::start` has decided whether this is
the run that gets to happen, so by the time a client opens the `EventSource`
there is always something to watch (or the run has already finished, which
`RunSession` still replays correctly -- see its own comment).

## What a run means

Four outcomes, and a client tells them apart the same way the old wxWidgets
console did, because the event stream still carries the same information --
see `protocol/events.hpp`'s `RunEvent::Kind`:

| | |
|---|---|
| `runEnd` arrived, exit 0 | passed |
| `runEnd` arrived, exit non-zero | ran, something failed -- the verdict is in the stream |
| no `runStart` at all | **never started** -- the reason is on stderr, nothing was energised |
| `runStart` but no `runEnd` | **crashed**, and the rig's state is unknown |

The last two look alike from outside -- both exit non-zero with no result --
and only the stream tells them apart. That is why `child_stream.hpp` captures
stderr as its own thing rather than discarding it: a client that only sees
"exit code 1, no events" cannot distinguish a preflight that never touched the
rig from one that did and left it in an unknown state, and the two call for
opposite responses from an operator.

`POST /safe` being ungated is what makes the "crashed" row survivable: nothing
here decides whether it is safe to call, and nothing needs to -- the same
binary that ran, built against this rig's exact instrument list, is what
`--safe` re-invokes, so it cannot be wrong about what there is to safe.

## What it does not do yet

**One run at a time, not one operator.** `RunSession::start` refuses a second
concurrent run, which stops two `run_scripts` processes from opening the same
rig at once. It does not stop two browser tabs from both being allowed to
press "Run" -- the second attempt just gets a 409. Arbitrating *viewers*, as
opposed to *runs*, is a session/auth question for whenever this binds a
routable address rather than only `127.0.0.1` -- see "History" below on why
local-only is the default rather than a limitation to lift casually.

**No manifest-based suite discovery.** `ui::Suite` (from `protocol/suite.hpp`)
carries a `Manifest`, `Tests`, `CriteriaVariants` and so on for exactly this
purpose, but nothing here populates them yet -- `main.cpp` builds a `Suite`
with only `Binary` set, from `--run-scripts=`. `ui::discoverSuites()` is
already there, reused unchanged, for whenever a deployment has more than one
installed suite to pick from.

**No options-dialog UI.** The page in `static/` has the header -- DUT serial,
operator and criteria before a run, and the run's own `runStart` header once
it has started -- the catalog as a collapsible tree with checkboxes, where
ticking a group ticks every test in it, and the old console's colour-coded
results list (with the raw event stream one tab away). A second setup row says where the readings come from -- the bench or a replayed recording,
with a "Record readings" box for the bench (never both: `run_scripts` refuses to record a
replay) -- and how often to run: once, a number of times (`--repeat`), or until a test fails
(`--until-failure`, with an optional "at most" count). The path is typed, because a browser never
reveals a real one, and is resolved from where `run_scripts` runs. With no count, "until a test fails"
is unbounded: it ends when a test fails or the operator presses **Stop**. Run and Stop are one
button, and **Emergency stop** sits in the top corner; see "Stopping a run" below. It does not yet have the generated
form for the rest of the flags; `GET /api/options` already serves what that
would be built from, and see "Generating a form from `--describe-options`"
below for the rules the old console followed, which still apply to whoever
builds it.

## Stopping a run

Two ways, because they are two different promises. **Stop** is the Run button while a run is going; **Emergency stop** is in the top-right corner.

**Stop** asks. The server writes `stop` to the run's stdin; `run_scripts` (which the
window always starts with `--stop-on-stdin`) checks that between tests, groups and passes and, when
it has been told, ends the way a run ends: the current test finishes, the teardown hooks run, the
rig is safed, both logs and any recording are written. It never interrupts a script. The run is
reported as **not passed** whatever its checks said -- the exit status claims every selected test
passed, and a run that ended early did not make that claim -- and the page says `STOPPED`, not
`FAILED`, and reads it as a pass when no check failed. A stop that arrives during the last test cuts
nothing short and leaves a finished run alone.

**Emergency stop** does not ask. It kills the process and runs `--safe`, which is the recovery the
"crashed" row of "What a run means" has always called for, in one step. The run writes no logs and
never reaches `runEnd`. There is no confirmation, on purpose; Stop is the calm way out.

The transport is a pipe rather than a signal because Windows has no way to signal a child that has no
console. At a terminal the same request is Ctrl-C (`SIGINT`/`SIGTERM`): the first asks as above, a
second kills the process, and the rig then needs a `--safe` -- see `framework/runner/src/stop_request.hpp`.
End of input on the pipe is **not** a stop: a console that has gone away is not an instruction, so a
run whose console dies is left running. That is the one open edge here, and it matters most for an
unbounded run.

### Generating a form from `--describe-options`

Every flag `--describe-options` reports, except the ones marked `query` and
the ones a real UI drives specially (`--select`, `--criteria`,
`--dut-serial`, `--operator`, `--safe` in the old console's case), gets a
control chosen by its `kind`: a `switch` is a checkbox (checked to begin with
if it `clears`, since a `--no-...` flag exists to turn something *off*), a
`number` is a spinner that will not go to zero if it is `positive`, a `list`
is a field, and everything else is a field with its `placeholder` as the
hint. The `help` string is the tooltip.

**A control the operator has not touched must contribute no flag to the run.**
Not laziness about defaults: `run_scripts` twice depends on telling "the
caller said nothing" from "the caller named the default" --
`--criteria=` unset means the build's variant rather than a name, and
`--repeat` unset with `--until-failure` means "keep going" rather than
"once". A form that helpfully pre-filled every default and always sent it
would quietly destroy both. This is why `ui::RunRequest::Settings` is a list
of what was set, not a full record of every flag's current value.

## Building

Part of the ordinary framework build, unlike `framework/ui` before it, and
built together with `framework/launcher` -- see the top-level
`CMakeLists.txt`'s note on why both sit beside `framework/core` and
`framework/hal`:

```
cmake --preset windows-dev     # or macos-dev
cmake --build build/dev
```

The same two lines on either platform, with the same compiler (GCC 16) --
which is the point of this target: it has been built and run on macOS with
`macos-dev` exactly as it is on Windows, `webui_protocol_tests` included,
with nothing platform-specific but the `ws2_32` link `CMakeLists.txt` adds on
Windows.

Produces `thorium_webui`, run as:

```
thorium_webui --run-scripts=<path to the installed run_scripts> --port=8420
```

which is exactly what `framework/launcher` is for, on Windows and macOS
alike: `--server=<path to thorium_webui> --server-arg=--run-scripts=<path>
--server-arg=--port=8420`.

`tools/run-webui.sh` does both for a developer: it takes `thorium_webui`,
`run_scripts` and the launcher from one build tree -- the one whose
`run_scripts` was built last, or `--tree=dev|debug|release` -- and starts them
on port 8420, so a rebuilt suite is what the console drives without an
install in between. It prints which tree, and so which rig, before it starts:
the trees are different deployments. `--run-scripts=` drives another one,
an installed one for instance; `--no-launcher` runs the server alone and
opens the page in the default browser; `--help` lists the rest. It starts everything from
`build/`, so runs started from the console write their logs to `build/logs`
(`run_scripts`' `--log-dir` defaults to `logs`, relative to where it runs).

### Tests

`webui_protocol_tests`, registered with `ctest` like every other layer's --
no second build tree, no separate `ctest --test-dir` invocation, because
unlike the old console this one is not a separate CMake project. It points
itself at the `run_scripts` this same build produces
(`$<TARGET_FILE:run_scripts>`), so there is nothing to configure by hand the
way `THORIUM_UI_TEST_BINARY` once had to be.

`webui_js_tests` is the page's JavaScript, and it is testable because the page
is split in two:

- `static/js/model/` is the page's logic with no page in it -- no `document`,
  no `fetch`, plain values in and out: parsing the catalog, what a tick
  selects, which rows an event adds, the header's flags, the four ways a run
  ends. Everything worth testing is here.
- `static/js/*.js` is the view: it reads the page, calls the model, and draws
  what comes back. Kept thin enough that there is nothing in it to test but
  wiring -- anything it starts deciding for itself belongs in a model module.

`tests/js/run.html` imports the model modules straight from the source tree,
so a test edit or a model edit needs no rebuild. `cmake/RunJsTests.cmake` opens
it in headless Chrome and fails unless the report the page writes says every
test passed -- Chrome rather than Node, because Chrome is already on every
machine the console runs on and Node would be one more thing to install. A
build that finds no Chrome still registers the test, which then fails saying
so. A new test module is a `*.test.js` beside the others and one more import in
`tests/js/all.js`.

## cpp-httplib

Vendored into `third_party/cpp-httplib-0.18.5/` the same way GoogleTest is
vendored -- see `cmake/FetchHttplib.cmake` for why (touching the network at
configure time is the thing being avoided here, not an ABI mismatch the way
it is for GoogleTest; cpp-httplib is header-only and has no ABI of its own to
clash with).

## History

`framework/ui` was a wxWidgets desktop console, and it was a *separate* CMake
project built by a *separate* compiler, for one forcing reason: this
framework's build refuses to configure with anything but GCC 16, and GCC
cannot build a GUI toolkit on macOS at all -- wxWidgets' Cocoa port includes
macOS SDK headers written in Clang's block-pointer syntax, which GCC does not
implement. That was never a wxWidgets-specific problem; it is true of any
native toolkit, because they all eventually reach Cocoa on that platform.

A browser front end was the one option that removed the two-compiler split
without giving up a real widget set -- Chrome (or any browser) supplies the
widgets, and this server needs nothing GCC cannot already build everywhere.
`cpp-httplib` was checked, not assumed: it compiles clean under
`-freflection -fcontracts -Wall -Wextra -Wpedantic -Werror`, the framework's
own experimental flags, so this target could join the ordinary build instead
of starting a third CMake project.

It deliberately is not a client-server product by default. Bound to
`127.0.0.1`, opened in Chrome's app mode (see `framework/launcher/README.md`),
it looks like a desktop window and behaves like one -- local or remote is a
bind address, not an architecture. Two things were left unsettled on purpose
before a routable bind address becomes anything but a manual override: there
is no authentication anywhere in this server, and nothing arbitrates two
*operators* (as opposed to two *runs* -- see "What it does not do yet"
above). Both are for whenever a routable address is an actual requirement,
not a default to flip because a lab wanted a dashboard.

An embedded webview (`WebView2`, `WKWebView`) was considered and rejected: it
re-fences a native, per-platform control behind the compiler split this
design exists to remove, or trades it for a Rust toolchain (Tauri). Chrome's
`--app=` mode gets the same "not a browser tab" look with no embedding at
all -- see `framework/launcher/README.md`.

Four small additions to the rest of the framework exist because of this
program, and would not otherwise:

| Addition | Where | Would it exist anyway? |
|---|---|---|
| `core::EventSink` | `framework/core/include/core/journal/event_sink.hpp` | yes -- `journal.hpp` had already argued for a live stream as the example of what its fan-out design buys |
| `cli::Query` + `cli::optionsModel` + `--describe-options` | `framework/runner/src/cli.hpp` | no -- this is the price of a UI not restating the flags `main.cpp` already owns |
| `core::jsonEscape` | `framework/core/include/core/journal/json.hpp` | it is the escaper `SarifSink` already had, moved so a second JSON stream could not grow a second copy |
| `core::EventSink::LogFiles` (`"logs"` on `runStart`) | `framework/core/include/core/journal/event_sink.hpp`, filled in by `framework/runner/src/main.cpp` | no -- a watcher did not choose the log paths and could otherwise only guess them; kept out of `RunInfo` so the logs do not record their own location |

No verb changed and no script changed to add any of the four. The one sink
that changed is `EventSink`, by one optional object on its `runStart` line.
