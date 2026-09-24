# launcher/ -- the thing with the tray icon

The native half of the browser-based bench console described in
[`framework/ui/README.md`](../ui/README.md) Sec.5. That document works out why a
browser front end removes the two-compiler split wxWidgets needs (GCC cannot
build a GUI toolkit against macOS's Cocoa headers at all) and why the console
should still not be a client-server product by default -- bound to
`127.0.0.1`, one operator at a time. This program is the "not a browser tab
you have to go find" part of that answer.

It does four things, in order, every time it starts:

1. **Refuses to be a second instance.** A named mutex
   (`single_instance.hpp`), scoped `Local\` rather than `Global` -- one
   console per interactive session, which is what "two operators" means on a
   bench PC with one logged-in user.
2. **Starts the console server as a supervised child**
   (`child_process.hpp`, `process_job.hpp`) and waits for it to bind its port
   (`health_check.hpp`) before doing anything else. Every child this program
   spawns -- the server, each browser window -- lives in one Windows Job
   object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, so "the launcher is gone"
   always means "nothing of the console is still running", including if the
   launcher itself crashes: the kernel tears the job down when the last
   handle to it closes, which happens even then.
3. **Opens Chrome in app mode** (`browser_launch.hpp`) against
   `http://127.0.0.1:<port>` -- `--app=`, not `--kiosk` and not an embedded
   `WebView2` control. No tab strip, no address bar, but a real, resizable,
   closable window. An embedded webview is the trap framework/ui/README.md
   names explicitly: it re-fences a per-platform native control behind the
   thing this redesign exists to get out from behind. `--app=` needs none of
   that, on any platform that has Chrome.
4. **Puts an icon in the tray** (`tray.hpp`) with three actions: *Show
   console* (opens another app-mode window -- see the note in `main.cpp` about
   why this does not yet refocus an existing one), *Safe the rig* (posts to
   the server directly, over HTTP, so it works even if the browser window has
   been closed by mistake -- this is the one action framework/ui/README.md
   Sec.4 says must never be unavailable), and *Quit*.

## What this is not yet

There is no server for it to start. `--server=` takes any executable that
binds the given `--port=` and answers HTTP, which for now means testing this
against a stand-in (`python -m http.server <port>` binds the port; it will not
answer `POST /safe`, so only the show/quit half of the tray is exercisable
until the real server exists).

There is no config file. Command-line flags
(`--server=`, `--server-arg=`, `--port=`, `--title=`) are the whole surface --
see `config.hpp`. A real deployment will want these to come from somewhere
more permanent than a shortcut's target field, and `config.hpp` is the one
place that changes when it does.

Single *operator* session (only one browser talking to the server at a time)
is a server-side concern, not this program's -- see framework/ui/README.md
Sec.5's "nothing arbitrates two operators" gap. The launcher's mutex only
stops two *launchers*; a remote browser reaching the same server later would
not go through this program at all.

## Why Windows-only, unlike framework/ui

framework/ui is one wxWidgets program built by whichever compiler each of the
three platforms has. This is not that: everything it does is Win32-specific by
nature -- `CreateMutexW`, a Job object, `Shell_NotifyIcon`, the `App Paths`
registry key. A macOS or Linux bench gets its own small launcher against that
platform's own tray/process APIs when one is needed, not a portability layer
grown over this one. `--app=` mode itself is identical on every platform;
only the handful of lines that spawn it and put an icon next to the clock are
not.

## Building

```
cmake --preset windows-launcher
cmake --build build/launcher
```

MinGW-w64 GCC (matching `framework/ui`'s preset), C++20, ordinary Win32 SDK
headers -- no vcpkg, no third-party dependency at all.
