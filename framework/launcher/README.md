# launcher/ -- the thing with the tray icon

The native half of the browser-based bench console -- see
[`framework/webui/README.md`](../webui/README.md)'s "History" for why a
browser front end removes the two-compiler split the old wxWidgets console
needed (GCC cannot build a GUI toolkit against macOS's Cocoa headers at all)
and why the console should still not be a client-server product by default --
bound to `127.0.0.1`, one operator at a time. This program is the "not a
browser tab you have to go find" part of that answer.

There are two of it: `src/` is the Windows launcher and `macos/` the macOS
one. They take the same flags and offer the same three menu actions, and
share no code -- see "Why one launcher per platform" below.

It does four things, in order, every time it starts, and a fifth while it runs:

1. **Refuses to be a second instance.** On Windows, a named mutex
   (`src/single_instance.hpp`), scoped `Local\` rather than `Global` -- one
   console per interactive session, which is what "two operators" means on a
   bench PC with one logged-in user. On macOS, an exclusive `flock()` on
   `~/Library/Application Support/Thorium/launcher.lock`
   (`macos/single_instance.hpp`) -- a lock the kernel drops when the process
   dies, crash included, which is the property the mutex was chosen for.
2. **Starts the console server as a supervised child** and waits for it to
   bind its port (`health_check.hpp`) before doing anything else. Every child
   this program spawns -- the server, each browser window -- is torn down
   when the launcher goes, *including if the launcher itself crashes*:
   - Windows: one Job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`
     (`src/process_job.hpp`). The kernel tears the job down when the last
     handle to it closes.
   - macOS has no Job object, so `macos/process_group.hpp` builds the same
     promise from a process group and a watchdog: every child is spawned into
     one group, and a small forked process leading that group holds the read
     end of a pipe whose only write end is the launcher's. The launcher
     exiting for any reason -- Quit, a crash, `kill -9` -- closes that write
     end, the watchdog's `read()` returns 0, and it signals the whole group:
     `SIGTERM`, two seconds' grace, then `SIGKILL`. `run_scripts` is covered
     too, since `thorium_webui` starts it with a plain `fork()` that keeps
     the group.
3. **Opens Chrome in app mode** (`browser_launch.hpp`) against
   `http://127.0.0.1:<port>` -- `--app=`, not `--kiosk` and not an embedded
   `WebView2`/`WKWebView` control. No tab strip, no address bar, but a real,
   resizable, closable window, against a profile of its own
   (`%LOCALAPPDATA%\Thorium\chrome-profile`, or
   `~/Library/Application Support/Thorium/chrome-profile`). An embedded
   webview is the trap `framework/webui/README.md`'s "History" names
   explicitly: it re-fences a per-platform native control behind the thing
   this redesign exists to get out from behind. `--app=` needs none of that,
   on any platform that has Chrome.

   On macOS, Chrome's own output -- its updater, crash reporter and
   push-message registration, none of it about the console -- goes to
   `~/Library/Application Support/Thorium/chrome.log` (truncated per launch)
   rather than to the terminal the console was started from. And a
   `SingletonLock` left in the profile by the last session's Chrome is
   removed before the first window opens (`clearStaleProfileLock` in
   `macos/browser_launch.hpp`): the watchdog's `SIGKILL` leaves one every
   time, and Chrome only clears a dead lock recorded under the Mac's
   *current* hostname -- which follows the network -- so after moving
   networks it reported the profile as "in use on another computer".
4. **Puts an icon in the tray** -- the notification area on Windows
   (`src/tray.hpp`), the menu bar on macOS (`macos/status_item.hpp`, with no
   Dock icon of its own) -- with three actions: *Show console* (opens another
   app-mode window -- see the note in `src/main.cpp` about why this does not
   yet refocus an existing one), *Safe the rig* (posts to the server
   directly, over HTTP, so it works even if the browser window has been
   closed by mistake -- this is the one action
   `framework/webui/README.md`'s "What a run means" says must never be
   unavailable), and *Quit*.

   If *Safe the rig* does not get a 2xx answer, both launchers say so --
   an alert on macOS, a message box on Windows -- since a safe that silently
   did not happen is the worst way this action can fail. `thorium_webui`
   answers 500 when `run_scripts --safe` could not even start, and no
   answer at all counts as a failure too.

   On Windows 11 a new tray icon starts out in the notification area's
   overflow (the `^` beside the clock), not on the taskbar itself, and there
   is no API to promote it. The menu is on a right-click of the icon there;
   drag it onto the taskbar to keep it in sight.

5. **Quits when the console is closed** (`watchForClosedConsole` in
   `src/main.cpp` and `macos/main.cpp`, the same logic on both). Closing the last console
   window ends everything, the same as *Quit*, about three seconds later --
   but never while a run is in flight: a window closed during a run leaves
   the run, the server and *Safe the rig* going, and the console quits once
   the run ends (unless *Show console* has brought a window back by then).
   It cannot watch Chrome for this, because Chrome on macOS keeps running
   when its last window closes. It asks the server instead: every open
   console page holds `/api/presence` open, and `/api/status` reports how
   many do and whether a run is active (see `framework/webui/README.md`).
   The three seconds are what let a page reload -- which drops and reopens
   that connection -- pass without being taken for a close. Windows asks
   the server too, although watching the Chrome process would be enough
   there, so that the two launchers cannot disagree about when to quit.

   The Windows launcher starts its children with `CREATE_NO_WINDOW`
   (`src/child_process.cpp`). It is a GUI program with no console, so
   without that flag `thorium_webui` -- a console program -- would open a
   console window of its own beside the real one.

## What this is not yet

The server now exists -- `framework/webui`'s `thorium_webui` -- and both
launchers have been run against it end to end: the Windows one including a
real `run_scripts` on a real deployment, the macOS one against the dev
deployment (preflight refuses without the bench's meter attached, which is
the "never started" outcome, and *Safe the rig*, *Show console*, Quit and a
`kill -9` of the launcher all behave as described above). What is still
missing:

There is no config file. Command-line flags
(`--server=`, `--server-arg=`, `--port=`, `--title=`) are the whole surface --
see `config.hpp`, one per platform. A real deployment will want these to come
from somewhere more permanent than a shortcut's target field, and
`config.hpp` is the one place that changes when it does.

The macOS launcher is a bare executable, not an `.app` bundle. It hides its
own Dock icon at runtime, and runs fine from a terminal or a login item, but
there is nothing to double-click in Finder -- and without a config file
there would be nothing for a double-click to pass it anyway. The two arrive
together.

"Show console" always opens a fresh Chrome window rather than refocusing an
existing one -- see the note in `src/main.cpp`'s `showConsole` lambda. Fine
for a first cut since closing the console and clicking the tray icon again is
the uncommon path; refocusing a specific app-mode window needs an
`EnumWindows` search by title/class on Windows, and the Accessibility API on
macOS.

Single *operator* session (only one browser talking to the server at a time)
is a server-side concern, not this program's -- see
`framework/webui/README.md`'s "What it does not do yet". The launcher's
lock only stops two *launchers*; a remote browser reaching the same server
later would not go through this program at all.

## Why one launcher per platform

framework/ui was one wxWidgets program built by whichever compiler each of the
three platforms had (deleted; see `framework/webui/README.md`'s "History").
This is not that: everything this program does is platform-specific by
nature. On Windows it is `CreateMutexW`, a Job object, `Shell_NotifyIcon` and
the `App Paths` registry key; on macOS it is `flock()`, a watched process
group, `NSStatusItem` and Launch Services. Those do not line up one-to-one --
the Job object alone became a fork, a pipe and a signal sequence -- and what
the two programs genuinely share is four command-line flags and three menu
items, less than any abstraction over them would cost. So each platform gets
its own few hundred lines, and a Linux bench gets a third when one is
needed, not a portability layer grown over these. `--app=` mode itself is
identical everywhere; only the handful of lines that spawn it and put an icon
next to the clock are not.

**Both are built by GCC -- on macOS too, even though the menu bar item is
AppKit.** AppKit's headers are the Clang-only block syntax that ended
framework/ui, so the macOS launcher never includes them. It reaches AppKit
through the Objective-C runtime's plain C API instead (`objc_msgSend`, cast
to each method's real signature -- see `macos/objc.hpp`), and GCC 16 builds
it with the same `-Wall -Wextra -Wpedantic -Werror` as everything else. That
keeps Thorium one compiler everywhere, the thing the browser front end was
chosen for in the first place.

The price is that the compiler no longer checks any AppKit call: a
misspelt class is nil and every message to it silently does nothing, a
misspelt selector aborts, a wrong cast is undefined behaviour. So every
message the launcher sends is one row in `macos/appkit.hpp`, and the tests
below check each row against the running system -- see "Testing the macOS
launcher".

## Building

Windows -- MinGW-w64 GCC, C++20, ordinary Win32 SDK headers:

```
cmake --preset windows-launcher
cmake --build build/launcher
```

macOS -- Homebrew GCC 16, C++20, AppKit through the Objective-C runtime:

```
cmake --preset macos-launcher
cmake --build build/launcher
```

Both from this directory, and neither needs a third-party dependency. Like
everything else GCC builds on macOS, the result links Homebrew's
`libstdc++`, so the bench Mac needs Homebrew's GCC installed to run it. On
macOS the result is `build/launcher/macos/thorium_launcher`, run with the same
flags as on Windows:

```
thorium_launcher --server=<path to thorium_webui> \
    --server-arg=--run-scripts=<path to run_scripts> --server-arg=--port=8420
```

## Testing the macOS launcher

```
ctest --test-dir build/launcher
```

or `tools/run-ctest.sh`, which includes `build/launcher` with the
framework's own trees. Four tests, split by what they need:

| Test | Needs | Catches |
|---|---|---|
| `launcher_macos.signatures` | nothing -- headless | For every row of `appkit.hpp`: the class exists, the method exists on it as a class or instance method as the row says, and its runtime type encoding is the one the row's C++ signature implies. The same for the launcher's own runtime-built menu-target class, against the `v@:@` its callers send. First, it proves it *can* fail: a deliberately misspelt class, a misspelt selector, a wrong cast and a wrong method kind must each be rejected. |
| `launcher_macos.no_raw_runtime_calls` | nothing | Any `objc_msgSend`, `sel_registerName` or `objc_getClass` outside `objc.hpp` -- a message that skipped the table would skip the signature test too -- and any use of `<objc/objc.h>`'s `BOOL`/`YES`/`NO`, which are the wrong type under GCC (below). |
| `launcher_macos.menu` | a logged-in desktop | The menu bar item built for real: the items and their order, each item reaching its own handler through its own target and action, the icon set as a template, and `postToMain()` + `stop()` waking and ending the event loop from another thread. What a signature check cannot see: the right method called on the wrong item, or not at all. |
| `launcher_macos.teardown` | a desktop and Chrome | The real binary against a stand-in server: the console lands in the watchdog's process group (Chrome included), a second launcher refuses, and a `kill -9` of the launcher leaves nothing of it running. Opens one Chrome window for a few seconds, under a throwaway `$HOME`. |

The last two report themselves skipped, not failed, without a desktop
session, so a headless runner still gets the two that need none.

Not covered by any of them: a clean Quit clicked from the real menu bar
(that needs Accessibility permission a test cannot grant itself --
`launcher_macos.menu` covers the handler and `stop()`, and the teardown test
covers what closing the watchdog's pipe does), and anything visual -- that
the icon *looks* right is still a look at the menu bar.

The signature test earned its keep on its first run. `<objc/objc.h>` defines
`BOOL` as `bool` only when a Clang-predefined macro says so; under GCC it is
`signed char` on every architecture, where the arm64 ABI says `bool`. Seven
messages were being called with the wrong type -- working by accident, and
undefined behaviour. `objc.hpp`'s `Bool` is the ABI's type, and the raw-call
test now bans the other one.
