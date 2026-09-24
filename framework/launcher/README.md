# launcher/ -- the thing with the tray icon

The native half of the browser-based bench console -- see
[`framework/console/README.md`](../console/README.md)'s "History" for why a
browser front end removes the two-compiler split the old wxWidgets console
needed (GCC cannot build a GUI toolkit against macOS's Cocoa headers at all)
and why the console should still not be a client-server product by default --
bound to `127.0.0.1`, one operator at a time. This program is the "not a
browser tab you have to go find" part of that answer.

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
   closable window. An embedded webview is the trap
   `framework/console/README.md`'s "History" names explicitly: it re-fences a
   per-platform native control behind the thing this redesign exists to get
   out from behind. `--app=` needs none of that, on any platform that has
   Chrome.
4. **Puts an icon in the tray** (`tray.hpp`) with three actions: *Show
   console* (opens another app-mode window -- see the note in `main.cpp` about
   why this does not yet refocus an existing one), *Safe the rig* (posts to
   the server directly, over HTTP, so it works even if the browser window has
   been closed by mistake -- this is the one action
   `framework/console/README.md`'s "What a run means" says must never be
   unavailable), and *Quit*.

## What this is not yet

The server now exists -- `framework/console`'s `thorium_console` -- and this
has been run against it end to end, including a real `run_scripts` on a real
deployment: `--server=<path to thorium_console> --server-arg=--run-scripts=<path>
--server-arg=--port=8420`. What is still missing:

There is no config file. Command-line flags
(`--server=`, `--server-arg=`, `--port=`, `--title=`) are the whole surface --
see `config.hpp`. A real deployment will want these to come from somewhere
more permanent than a shortcut's target field, and `config.hpp` is the one
place that changes when it does.

"Show console" always opens a fresh Chrome window rather than refocusing an
existing one -- see the note in `main.cpp`'s `showConsole` lambda. Fine for a
first cut since closing the console and clicking the tray icon again is the
uncommon path; refocusing a specific app-mode window needs an `EnumWindows`
search by title/class.

Single *operator* session (only one browser talking to the server at a time)
is a server-side concern, not this program's -- see
`framework/console/README.md`'s "What it does not do yet". The launcher's
mutex only stops two *launchers*; a remote browser reaching the same server
later would not go through this program at all.

## Why Windows-only, unlike framework/ui before it

framework/ui was one wxWidgets program built by whichever compiler each of the
three platforms had (deleted; see `framework/console/README.md`'s "History").
This is not that: everything this program does is Win32-specific by nature --
`CreateMutexW`, a Job object, `Shell_NotifyIcon`, the `App Paths` registry
key. A macOS or Linux bench gets its own small launcher against that
platform's own tray/process APIs when one is needed, not a portability layer
grown over this one. `--app=` mode itself is identical on every platform;
only the handful of lines that spawn it and put an icon next to the clock are
not.

## Building

```
cmake --preset windows-launcher
cmake --build build/launcher
```

MinGW-w64 GCC, C++20, ordinary Win32 SDK headers -- no vcpkg, no third-party
dependency at all.
