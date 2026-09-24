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
| `GET /` | the console page (embedded, see `static_content.hpp`) |
| `GET /api/options` | `run_scripts --describe-options`, passed through verbatim (it is already JSON) |
| `GET /api/tests` | `run_scripts --list-tests`, passed through verbatim (`group\|id\|description` lines) |
| `POST /api/run` | body is `{selection, settings, extra}` -- see `ui::RunRequest` -- starts a run; 409 if one is already active |
| `GET /api/events` | Server-Sent Events: every line of the active run's stdout, from wherever this connection joined, plus any stderr line as `{"kind":"stderr","text":...}` |
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

**No catalog or options-dialog UI.** `static_content.hpp` proves the pipe end
to end -- Run, Safe, and a live log -- not the tree of tests and a generated
form. `GET /api/tests` and `GET /api/options` already serve what that UI would
be built from; see "Generating a form from `--describe-options`" below for the
rules the old console followed, which still apply to whoever builds this one.

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

Part of the ordinary framework build, unlike `framework/ui` before it and
unlike `framework/launcher` today -- see the top-level `CMakeLists.txt`'s note
on why this target sits beside `framework/core` and `framework/hal`:

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

### Tests

`webui_protocol_tests`, registered with `ctest` like every other layer's --
no second build tree, no separate `ctest --test-dir` invocation, because
unlike the old console this one is not a separate CMake project. It points
itself at the `run_scripts` this same build produces
(`$<TARGET_FILE:run_scripts>`), so there is nothing to configure by hand the
way `THORIUM_UI_TEST_BINARY` once had to be.

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

Three small additions to the rest of the framework exist because of this
program, and would not otherwise:

| Addition | Where | Would it exist anyway? |
|---|---|---|
| `core::EventSink` | `framework/core/include/core/journal/event_sink.hpp` | yes -- `journal.hpp` had already argued for a live stream as the example of what its fan-out design buys |
| `cli::Query` + `cli::optionsModel` + `--describe-options` | `framework/runner/src/cli.hpp` | no -- this is the price of a UI not restating the flags `main.cpp` already owns |
| `core::jsonEscape` | `framework/core/include/core/journal/json.hpp` | it is the escaper `SarifSink` already had, moved so a second JSON stream could not grow a second copy |

No verb changed, no script changed, no sink changed to add any of the three.
