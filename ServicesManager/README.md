# Windows Services Manager

A fast, native tool to see every Windows service and start, stop, restart,
pause, resume or kill it, and to change how it starts. It is safe by
default: it asks before risky actions and protects the services Windows
needs to run.

© 2026 Akshaya Simha. Developed for faster experience.

Version 2.0 is a rewrite of the original VB.NET app (`Windows Services.zip`)
as a single native executable: about 400 KB, no .NET or other runtime
needed, starts instantly, and never freezes while a service is slow.

## Features

**The list**

* Every service, with these columns: Name, Service name, Status, Start type,
  Safety, PID, Log on as and Description. Program and Company are available
  too: right-click a column header to show or hide columns, drag headers to
  reorder them, and drag their edges to resize them. The layout is
  remembered.
* **Accurate status:** Running, Stopped, Starting…, Stopping…, Paused,
  Pausing…, Resuming…. Each is colour-coded.
* **Full start types:** Automatic, Automatic (delayed), Manual, Disabled,
  with ", trigger" for services that start on an event.
* **Safety label** for every service:
  * **Critical** (red): Windows needs it to run, start, sign in, network or
    stay secure.
  * **Windows:** part of Windows; a feature stops working without it.
  * **Third-party** (purple): installed by other software. Detected from the
    program's publisher.
* **Search as you type** (Ctrl+F) across names, descriptions, program path
  and company.
* **Quick filters:** Running, Stopped, Automatic but not running (often a
  service that failed), Disabled, Third-party only, Critical to Windows.
* **Sorting:** click any column header to sort; click again to reverse.
* **Auto-refresh** every 5 seconds by default, adjustable in Settings. Your
  selection, focus and scroll position are kept.
* Readable descriptions: `@file.dll,-123` resource references are resolved
  into text.
* Hover a row to see its full description and command line.

**Actions** (toolbar, right-click menu or keyboard)

* **Start, Stop, Restart, Pause, Resume.** These run in the background with
  a timeout, so the window never freezes. Progress is shown in the status
  bar, and Cancel stops a long batch.
* **Kill:** ends a hung service's process immediately. It warns you when
  other services share that process (common with `svchost.exe`) and lists
  the ones that will stop too.
* **Start type:** Automatic, Automatic (delayed start), Manual or Disabled.
  This uses the Windows service API, not raw registry edits, so changes take
  effect properly.
* **Several services at once:** select with Ctrl/Shift+click or Ctrl+A. A
  summary lists anything that failed and why.
* **Helpful follow-ups:**
  * Starting a disabled service offers to set it to Manual and start it.
  * Stopping a service other running services depend on lists them and
    offers to stop them first. Restart brings them back afterwards.
* **Clear errors** instead of crashes, for example "Administrator rights are
  needed", "The service is busy starting or stopping", "The service's
  program file is missing", or "The service did not respond in time (you
  can kill it)".

**Safety**

* **Protect critical services** (on by default): stopping, pausing, killing,
  disabling or setting to Manual a critical service is blocked with an
  explanation. This can be turned off in Settings, after a warning.
* **Confirmations** before stop, restart, pause, kill and start-type
  changes, listing exactly which services are affected. The default button
  is "No". This can be turned off in Settings.

**Other**

* Copy service names (Ctrl+C), or full details as tab-separated rows
  (Ctrl+Shift+C) that paste straight into Excel.
* Open the service's program in Explorer, or search for the service online.
* Opens without a UAC prompt in read-only mode. "Restart as administrator"
  in the status bar or the More menu enables changes. Settings can make it
  always start as administrator.
* Light and dark theme, following Windows or set in Settings.
* Crisp on high-DPI screens (per-monitor DPI aware).
* One window: starting it again brings the open window to the front.

## Settings (More › Settings, or Ctrl+,)

| Setting | Default |
|---|---|
| Theme: Same as Windows / Light / Dark | Same as Windows |
| Refresh the list every: never, 2, 5, 10 or 30 seconds, 1 minute | 5 seconds |
| Wait for a service up to (seconds) | 30 |
| Ask before stopping, restarting, pausing, killing or disabling | On |
| Protect services that are critical to Windows | On |
| Always start as administrator | Off |
| Reset column layout | — |

Settings, the window position, the column layout, the sort order and the
filter are saved in `HKEY_CURRENT_USER\Software\WindowsServicesManager`.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| F5 | Refresh |
| Ctrl+F | Search (Esc clears it, Enter or ↓ moves to the list) |
| Ctrl+A | Select all |
| Ctrl+C | Copy service names |
| Ctrl+Shift+C | Copy details |
| Ctrl+Shift+S | Start |
| Ctrl+Shift+T | Stop |
| Ctrl+Shift+R | Restart |
| Ctrl+Shift+K | Kill |
| Ctrl+, | Settings |
| F1 | About |
| Shift+F10 / Menu key | Right-click menu |

## Building

Requires CMake 3.20+ and Visual Studio 2022 (C++ desktop workload), or
MinGW-w64.

```bat
cd ServicesManager
cmake -S . -B build -A x64
cmake --build build --config Release
:: -> build\Release\ServicesManager.exe
```

From Linux:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

GitHub Actions (`.github/workflows/servicesmanager.yml`) builds the program
on every push. Download it from the run's **WindowsServicesManager-x64**
artifact. The `.exe` is not code-signed, so Windows SmartScreen or Smart App
Control may warn about it.

## How it works

| File | Responsibility |
|---|---|
| `src/Services.*` | Service Control Manager access: listing with configuration, the actions, start types, friendly error text, the critical-service list and safety labels |
| `src/Worker.*` | Background thread: refreshes and actions run there, one at a time, so the window never freezes |
| `src/ServiceList.*` | Virtual list view: only visible rows are formatted; sorting, filtering, colours, columns and selection kept across refreshes |
| `src/MainWindow.*` | Window, toolbar, status bar, menus and the action flow (checks, confirmation, progress, results, follow-ups) |
| `src/Dialogs.*` | Settings dialog |
| `src/Settings.*` | Saved preferences |
| `src/Toolbar.*`, `src/Theme.*` | Self-drawn toolbar and status bar, light and dark palette (shared with Feather PDF) |

* Each refresh reads every service's configuration in the background. A
  program's publisher is read once and cached, because that is the slowest
  part.
* An action waits for the service to reach its new state. It polls at a
  tenth of the service's own estimated time (100 ms to 1 s) and gives up
  after the timeout in Settings.

## Limitations

* Kernel drivers are not listed (planned for a later version, read-only by
  default).
* The "critical" list is built in. It covers the services Windows needs to
  boot, sign in, network and stay secure, but cannot know every PC's
  special setup.
* Remote computers are not supported yet.
