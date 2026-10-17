# Windows Services Manager

A fast, native tool to see every Windows service and start, stop, restart,
pause, resume or kill it, and to change how it starts. It is safe by
default: it asks before risky actions and protects the services Windows
needs to run.

© 2026 Akshaya Simha. Developed for faster experience.

Version 2 is a rewrite of the original VB.NET app (`Windows Services.zip`)
as a single native executable: about 600 KB, no .NET or other runtime
needed, starts instantly, and never freezes while a service is slow.
Version 2.1 adds the details window, online "can it be disabled?" advice,
start-up impact, security warnings, profiles, snapshots and undo.

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

**Details: double-click a service** (or press Enter / Alt+Enter)

* **General:** a full report on the service, which you can copy:
  * **Can it be disabled?** Online advice (see below).
  * Status, process ID, how long it has been running, memory and CPU time.
  * Which other services share its process.
  * Start type, and how much it slowed down Windows start-up.
  * Command line, program file, version, company and digital signature
    (including Windows catalog signatures).
  * The account it runs as, warnings, recovery settings and dependencies.
  * Buttons: Open file location, Check on VirusTotal (sends only the file's
    SHA-256 hash, never the file), and Search online.
* **Configuration (editable):** display name, description, start type, and
  the log-on account: Local System, Local Service, Network Service, or a
  user account with password. Choosing a user account grants it "Log on as
  a service", as Windows' own console does.
* **Recovery (editable):** what Windows does on the first, second and later
  failures (take no action, restart the service, run a program, restart
  the computer), the wait time, when the failure count resets, and whether
  to act on error stops too.
* **Dependencies:** a tree of the services it needs (all levels) and the
  services that need it.
* **History:** the most recent System event-log entries about the
  service, such as starts, stops, crashes and failed starts.

Select several services and double-click (or press Enter) to get **one
summary** of all of them, with the advice for each.

**"Can it be disabled?" advice (online)**

The advice comes from `data/service-advice.tsv` in this repository and
covers about 200 common Windows services. Each has a verdict and a
one-line reason:

* **Do not disable:** Windows or security needs it.
* **Disable only if you don't need it:** names the feature that stops
  working (printing, Bluetooth, Xbox games, Remote Desktop…).
* **Usually safe to disable:** for example telemetry, retail demo, fax.

The list is downloaded the first time you open details in a session. It
is saved, so without internet the saved copy is shown (with its date).
Without internet and with no saved copy, the window says so. Turn it off in
Settings ("Look up online whether services can be disabled"). To improve the
advice, edit the `.tsv` file on GitHub; everyone gets the update without a
new version of the program.

**Start-up impact and warnings**

* **Boot delay** column and "Slowed down start-up" filter: Windows records
  services that delayed start-up (Diagnostics-Performance log, event 103).
  This needs administrator rights.
* **Warnings** column and "Needs attention" filter:
  * Program file missing.
  * Third-party program with no digital signature, or a broken or untrusted
    signature.
  * Running from a user, AppData or Temp folder.
  * Unquoted path with spaces (a known security hole).

**Profiles, snapshots and undo** (the **Profiles** button)

* **Built-in profiles:**
  * Privacy: reduce telemetry.
  * Lighter: fewer background services.
  * No Xbox (not a gamer).
  * No printer or scanner.
  * Security hardening.
* **Review before applying:** every profile opens a review window listing
  each change (now → new) with a tick box. Critical services are protected.
  Services not on this PC are skipped.
* **Your own profiles:** select services and choose "Save as a profile" to
  save their start types and re-apply them later, or on another PC (copy the
  `.wsm` file).
* **Snapshots:** save the start types of every service, and restore them
  later through the same review window.
* **Automatic snapshot before every change:** before any start-type change
  (toolbar, Properties, profile, restore or undo), all start types are saved
  to `Snapshots\Automatic`. The newest 30 are kept.
* **Undo** (Ctrl+Z) reverts the last start-type changes, up to 20 steps.

Profiles and snapshots are plain text files in
`%LOCALAPPDATA%\WindowsServicesManager\Profiles` and `...\Snapshots`.

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
| Look up online whether services can be disabled | On |
| Protect services that are critical to Windows | On |
| Always start as administrator | Off |
| Reset column layout | — |

Settings, the window position, the column layout, the sort order and the
filter are saved in `HKEY_CURRENT_USER\Software\WindowsServicesManager`.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Double-click / Enter / Alt+Enter | Details (several selected: summary) |
| Ctrl+Z | Undo the last start-type change |
| F5 | Refresh (also re-checks signatures and boot delays) |
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
| `src/Details.*` | Details window (General, Configuration, Recovery, Dependencies, History) and the multi-service summary |
| `src/Inspect.*` | Signatures (embedded and catalog), SHA-256, process memory/CPU, recovery settings, configuration changes, event log, boot delays |
| `src/Advice.*` | Downloads, caches and looks up the "can it be disabled?" list (WinHTTP) |
| `src/Profiles.*` | Profiles, snapshots, automatic snapshots |
| `data/service-advice.tsv` | The advice list (downloaded by the app from GitHub) |
| `src/Dialogs.*` | Settings dialog and the review-changes window |
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
* The advice is general guidance for home PCs. Company-managed PCs may need
  services the list calls safe to disable.
* Start-up impact is only known for services Windows itself reported as
  slowing down start-up.
