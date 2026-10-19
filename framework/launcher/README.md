# launcher/ -- the thing that makes the console a window

The native half of the browser-based bench console -- see
[`framework/webui/README.md`](../webui/README.md)'s "History" for why a
browser front end removes the two-compiler split the old wxWidgets console
needed, and why the console should still not be a client-server product by
default -- bound to `127.0.0.1`, one operator at a time. This program is the
"not a browser tab you have to go find" part of that answer.

It is one program, the same on Windows and macOS: no tray icon, no menu bar
item, nothing of the platform's GUI. Every platform difference is behind one
header, `src/platform.hpp`, implemented once per *family*, not per product:
`src/windows/platform.cpp`, `src/posix/platform.cpp` (the lock, the process
group and watchdog, spawning -- what macOS and Linux share) and
`src/posix/system_macos.cpp` (the few things a Mac does its own way: where the
support directory is, where Chrome is, how an error is shown), with
`src/posix/system_linux.cpp` its Linux counterpart. Everything else
is ordinary C++ and the HTTP client the server itself is built on.

It takes four flags, and has no config file:

| Flag | |
|---|---|
| `--server=<path>` | the console server to start (required) |
| `--server-arg=<arg>` | one more argument for it; repeatable |
| `--browser-arg=<arg>` | one more flag for every Chrome window; repeatable -- today only the teardown test uses it |
| `--port=<n>` | the port the server listens on, default 8420 |

```
thorium_launcher --server=<path to thorium_webui> \
    --server-arg=--run-scripts=<path to run_scripts> --server-arg=--port=8420
```

## What it does

In order, every time it starts, and a last thing while it runs:

1. **Refuses to be a second console.** One launcher per interactive session --
   a named mutex scoped `Local\` on Windows, an exclusive `flock()` on
   `~/Library/Application Support/Thorium/launcher.lock` on macOS -- released
   by the kernel however the holder dies, crash included. A second launcher
   does not fail, though: it asks Chrome for another console window and
   exits, since there is no menu to ask from. That is how an operator who
   closed the window mid-run gets it back (see step 4), and it starts no
   second server.
2. **Starts the console server as a supervised child** and waits for it to
   answer `GET /api/status`. Every child this program spawns -- the server,
   each Chrome window -- is torn down when the launcher goes, *including if
   the launcher itself crashes*:
   - Windows: one Job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. The
     kernel tears the job down when the last handle to it closes.
   - macOS has no Job object, so the same promise is built from a process
     group and a watchdog: every child is spawned into one group, and a small
     forked process leading that group holds the read end of a pipe whose
     only write end is the launcher's. The launcher exiting for any reason --
     a crash, `kill -9` -- closes that write end, the watchdog's `read()`
     returns 0, and it signals the whole group: `SIGTERM`, two seconds'
     grace, then `SIGKILL`. `run_scripts` is covered too, since
     `thorium_webui` starts it with a plain `fork()` that keeps the group.
3. **Opens Chrome in app mode** against `http://127.0.0.1:<port>` -- `--app=`,
   not `--kiosk` and not an embedded `WebView2`/`WKWebView` control. No tab
   strip, no address bar, but a real, resizable, closable window, against a
   profile of its own (`%LOCALAPPDATA%\Thorium\chrome-profile`, or
   `~/Library/Application Support/Thorium/chrome-profile`). An embedded
   webview is the trap `framework/webui/README.md`'s "History" names: it
   re-fences a per-platform native control behind the thing this redesign
   exists to get out from behind.

   On macOS, Chrome's own output -- its updater, crash reporter and
   push-message registration, none of it about the console -- goes to
   `~/Library/Application Support/Thorium/chrome.log` (truncated per launch)
   rather than to the terminal the console was started from. And a
   `SingletonLock` left in the profile by the last session's Chrome is
   removed before the first window opens: the watchdog's `SIGKILL` leaves one
   every time, and Chrome only clears a dead lock recorded under the Mac's
   *current* hostname -- which follows the network -- so after moving networks
   it reported the profile as "in use on another computer".
4. **Quits when the console is closed.** Closing the last console window
   ends everything about three seconds later -- but never while a run is in
   flight: a window closed during a run leaves the run and the server going,
   and the console quits once the run ends (unless the launcher has been run
   again by then, which brings a window back). It cannot watch Chrome for
   this, because Chrome on macOS keeps running when its last window closes. It
   asks the server instead: every open console page holds `/api/presence`
   open, and `/api/status` reports how many do and whether a run is active
   (see `framework/webui/README.md`). The three seconds are what let a page
   reload -- which drops and reopens that connection -- pass without being
   taken for a close.

   It also quits if the server dies: nothing listening on the port for five
   seconds. *Refused*, specifically -- a server that accepts the connection
   and does not answer is busy, and a busy server in the middle of a run is
   never taken for a dead one.

A failure to start is reported on stderr, and also as a dialog when stderr is
not a terminal -- a `MessageBox` on Windows, an `osascript` alert on macOS --
since a launcher started from a shortcut that fails silently is just a console
that never appears.

## What is not here

There is no tray icon or menu bar item. It used to have one, with *Show
console*, *Safe the rig* and *Quit*; nobody was going to use them, and they
were the reason the program had to be two. *Show console* is now running the
launcher again, *Quit* is closing the window, and *Safe the rig* is the
console page's own control -- the server's `POST /safe` is unchanged, and
still never gated on whether a run is active.

The launcher is also not a double-clickable macOS `.app` bundle, and has no
config file: the flags above are the whole surface. A real deployment will
want them from somewhere more permanent than a shortcut's target field, and
`config.hpp` is the one place that changes when it does.

The Linux half, `src/posix/system_linux.cpp`, is an XDG support directory
(`$XDG_DATA_HOME/thorium`), Chrome or Chromium from `PATH`, `/proc/<pid>/exe`
for the stale-lock check, and a `zenity` or `notify-send` dialog. It was
written on speculation, without a Linux machine: see "Testing".

Single *operator* session (only one browser talking to the server at a time)
is a server-side concern, not this program's -- see
`framework/webui/README.md`'s "What it does not do yet". The launcher's lock
only stops two *launchers*; a remote browser reaching the same server later
would not go through this program at all.

## Why a seam rather than a program per platform

The first version was two programs sharing five flags and nothing else,
because most of what each did was its own platform's GUI: `Shell_NotifyIcon`
on one side, `NSStatusItem` on the other -- driven, under GCC, through the
Objective-C runtime's C API with every message checked against the running
system by a test of its own, since GCC cannot read AppKit's headers. Dropping
the menu dropped all of that, and what is left that is genuinely different
fits in `platform.hpp`: where the support directory is, the lock, the process
group, where Chrome is, how to say an error. Windows and POSIX genuinely
disagree about the lock and the process group -- the Job object alone becomes
a fork, a pipe and a signal sequence -- and macOS differs from Linux only in
the small system_macos.cpp; no abstraction over any of it would be smaller than the
files it replaced.

Both are built by GCC, with the same `-Wall -Wextra -Wpedantic -Werror` as
everything else, and the build's `-static-libstdc++`, so the macOS launcher
does not need Homebrew's GCC on the machine that runs it.

## Building

Part of the ordinary build, beside `framework/webui` -- the server it starts
and the launcher are built together, by the same preset:

```
cmake --preset windows-dev     # or macos-dev, or any other preset
cmake --build build/dev
```

The result is `<tree>/framework/launcher/thorium_launcher` (`.exe` on
Windows) -- or let `tools/run-webui.sh` find it beside the server it picks.

## Testing

With the rest of a tree's tests, under `BUILD_TESTING_LAYERS` -- so
`tools/run-ctest.sh` runs them in every tree, or just these with:

```
ctest --test-dir build/dev -R launcher
```

`launcher.profile_lock` runs on any POSIX host; `launcher.teardown` is macOS
only so far (it drives `launchctl`, `mdfind` and BSD `ps`):

| Test | Needs | Catches |
|---|---|---|
| `launcher.profile_lock` | nothing -- headless | `clearStaleProfileLock` against a real directory: the dead-Chrome-under-another-hostname case that put "profile in use on another computer" in the console's terminal. |
| `launcher.teardown` | a desktop and Chrome | The real binary against a stand-in server (`tests/stand_in_server.py`, which answers only `/api/status`): the console lands in the watchdog's process group, Chrome included; a second launcher opens a window and exits without disturbing the server; and a `kill -9` of the launcher leaves nothing of it running. Opens one Chrome window for a few seconds, under a throwaway `$HOME`. Skipped, not failed, without a desktop session. |

Not covered: the Linux half. `system_linux.cpp` has been neither compiled nor
run. WSL2 is a real Linux kernel, so the lock, the process group and the
watchdog behave there as on a bench, and `launcher.profile_lock` plus the build
itself can be checked in a WSL shell with GCC 16. Chrome and the dialogs need a
display: Windows 11's WSLg provides one, and Chrome has to be installed inside
the distribution (not Windows' own). The teardown test still needs a Linux port
of its macOS-specific shell.

Not covered: the Windows program. It is the same `main.cpp`, `config.cpp` and
`console_client.cpp`, but `src/windows/platform.cpp` was written without a
Windows build in reach and has been neither compiled nor run.

Not covered anywhere: the operator closing the window (that needs a real
console page holding `/api/presence`), and what a dialog looks like.
