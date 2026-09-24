# console/ -- the bench console, as a server

The browser-facing half of [`framework/ui/README.md`](../ui/README.md) Sec.5's
deferred design, and [`framework/launcher/README.md`](../launcher/README.md)'s
counterpart: the launcher owns the tray icon and the browser window, this owns
the rig.

Like `framework/ui/src/app/`, it does exactly one thing to the rig: launch the
installed `run_scripts` as a subprocess and relay what it says on stdout. It
opens nothing itself, decides no verdict itself, and keeps no state about a
run beyond what is needed to replay it to a browser tab that connects late --
see `run_session.hpp`, and framework/ui/README.md Sec.2 on why a UI here
"tallies nothing."

## Endpoints

| | |
|---|---|
| `GET /` | the console page (embedded, see `static_content.hpp`) |
| `GET /api/options` | `run_scripts --describe-options`, passed through verbatim (it is already JSON) |
| `GET /api/tests` | `run_scripts --list-tests`, passed through verbatim (`group\|id\|description` lines) |
| `POST /api/run` | body is `{selection, settings, extra}` -- see `ui::RunRequest` -- starts a run; 409 if one is already active |
| `GET /api/events` | Server-Sent Events: every line of the active run's stdout, from wherever this connection joined |
| `POST /safe` | `run_scripts --safe`. Never gated on whether a run is active -- see framework/ui/README.md Sec.4. Path fixed by `framework/launcher/src/rig_client.hpp`, which already calls it. |

`/api/run` and `/api/events` are deliberately two requests, not one: a
browser's `EventSource` can only issue `GET`, and `POST` is what a run needs
to hand over a JSON body -- see `main.cpp`'s note on the split. A client
starts a run with the first, then opens the second to watch it; `POST
/api/run` only returns once `RunSession::start` has decided whether this is
the run that gets to happen, so by the time a client opens the `EventSource`
there is always something to watch (or the run has already finished, which
`RunSession` still replays correctly -- see its own comment).

## What it does not do yet

**One run at a time, not one operator.** `RunSession::start` refuses a second
concurrent run, which stops two `run_scripts` processes from opening the same
rig at once. It does not stop two browser tabs from both being allowed to
press "Run" -- the second attempt just gets a 409. Arbitrating *viewers*, as
opposed to *runs*, is the other half of the gap framework/ui/README.md Sec.5
names and is a session/auth question for whenever this binds a routable
address, not a loopback-only console's problem today.

**No manifest-based suite discovery.** `ui::Suite` (from `framework/ui/src/protocol/suite.hpp`)
carries a `Manifest`, `Tests`, `CriteriaVariants` and so on for exactly this
purpose, but nothing here populates them yet -- `main.cpp` builds a `Suite`
with only `Binary` set, from `--run-scripts=`. `ui::discoverSuites()` is
already there, reused unchanged, for whenever a deployment has more than one
installed suite to pick from.

**No catalog or options-dialog UI.** `static_content.hpp` proves the pipe end
to end -- Run, Safe, and a live log -- not the tree of tests and the generated
form framework/ui/README.md Sec.3 describes. `GET /api/tests` and
`GET /api/options` already serve what that UI would be built from.

## Building

Part of the ordinary framework build -- see the top-level `CMakeLists.txt`'s
note on why this target sits beside `framework/core` and `framework/hal`
rather than beside `framework/ui`:

```
cmake --preset windows-dev
cmake --build build/dev
```

Produces `thorium_console`, run as:

```
thorium_console --run-scripts=<path to the installed run_scripts> --port=8420
```

which is exactly what `framework/launcher` is for: `--server=<path to
thorium_console> --server-arg=--run-scripts=<path> --server-arg=--port=8420`.

## cpp-httplib

Vendored into `third_party/cpp-httplib-0.18.5/`, the same way GoogleTest is
vendored -- see `cmake/FetchHttplib.cmake` for why (touching the network at
configure time is the thing being avoided here, not an ABI mismatch the way
it is for GoogleTest; cpp-httplib is header-only and has no ABI of its own to
clash with).
