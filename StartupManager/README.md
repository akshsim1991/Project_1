# Startup Manager

A fast, native tool that shows everything that starts with Windows in one
list, and lets you enable, disable, delete or add entries safely.

© 2026 Akshaya Simha. Developed for faster experience.

A single native executable (about 500 KB). It needs no .NET or other
runtime, starts instantly, and never freezes: reading and changing entries
happens in the background.

## Features

**One list of everything that starts with Windows**

* **Registry Run keys:** for the current user, for all users, and the
  32-bit view used by older programs (`WOW6432Node`).
* **Run once entries:** these run a single time at the next sign-in. They
  can be hidden in Settings.
* **Startup folders:** yours, and the one for all users. Shortcuts are
  resolved to the program they start.
* **Scheduled tasks** that run at sign-in or at start-up, in every
  Task Scheduler folder. Windows' own tasks (`\Microsoft\…`) are hidden by
  default because there are many; Settings can show them.
* **Columns:** Name, Status, Warnings, Publisher, Type, For (current user
  or all users), Command and Location. Program, Description and Runs (the
  task trigger) are available too.
  * Right-click a column header to show or hide columns.
  * Drag headers to reorder them, and drag their edges to resize them.
  * The layout is remembered.
* Colours: enabled entries are green, disabled ones dimmed, warnings red,
  and publishers other than Microsoft purple.
* **Search as you type** (Ctrl+F) across names, commands, paths,
  publishers and descriptions.
* **Filters:** Enabled, Disabled, Needs attention, Not from Microsoft,
  Registry, Startup folders and Scheduled tasks.
* **Sorting** by any column; click again to reverse.
* **Auto-refresh** every 10 seconds by default. Your selection and scroll
  position are kept.

**Warnings ("Needs attention")**

* **File missing:** the program no longer exists. Usually a leftover from
  uninstalled software; safe to delete.
* **Not signed:** the program has no digital signature (programs that are
  not from Microsoft only).
* **Bad signature:** the signature is broken or not trusted.
* **Temp folder:** it runs from a Temp folder, which is typical of malware.
* **Script:** it starts PowerShell, VBScript, mshta or cmd. These are
  sometimes legitimate but are also used by malware.

**Changes**

* **Enable / Disable** use the same "StartupApproved" settings as Task
  Manager and Settings › Apps › Startup, so all three always agree.
  Disabling keeps the entry, and Windows records when it was disabled.
  Scheduled tasks are enabled and disabled in the Task Scheduler itself.
* **Delete (Del)** always asks first, with an extra warning for Microsoft
  entries. A backup is saved before anything is removed:
  * Registry entries: the exact original value.
  * Startup folder shortcuts: the shortcut file is moved into the backup.
  * Scheduled tasks: the full task definition (XML).
* **Restore…** lists every deleted entry with its date. Tick the ones to
  bring back.
* **Undo (Ctrl+Z)** reverses the last 20 changes: enable/disable, delete
  (restores from the backup) and add.
* **Add… (Ctrl+N)** makes a program start with Windows:
  * Choose the program and its arguments.
  * Run it for yourself or for all users (all users needs administrator).
  * Add it as a registry entry or as a Startup folder shortcut.
* Several entries can be changed at once (Ctrl/Shift+click or Ctrl+A). A
  summary lists anything that failed and why, for example "Administrator
  rights are needed for entries that apply to all users".

Backups are kept in `%LOCALAPPDATA%\StartupManager\Deleted` (More › Open
the backup folder).

**Details: double-click an entry** (or press Enter / Alt+Enter)

* Status, and when it was disabled.
* Where it is set (registry key and value, shortcut file, or task) and
  what it runs.
* The program's version, publisher and digital signature (including
  Windows catalog signatures).
* Warnings explained in plain words.
* Buttons:
  * **Open file location.**
  * **Show where it is set:** opens Registry Editor at the key, Explorer at
    the shortcut, or Task Scheduler.
  * **Check on VirusTotal:** sends only the file's SHA-256 hash, never the
    file.
  * **Search online.**
  * **Copy.**

Select several entries and press Enter to get one combined report.

**Other**

* Copy names (Ctrl+C), or full details as tab-separated rows
  (Ctrl+Shift+C) that paste straight into Excel.
* Opens without a UAC prompt. Your own entries can be changed right away.
  Entries for all users and most scheduled tasks need "Restart as
  administrator" (status bar or More menu). Settings can make it always
  start as administrator.
* Light and dark theme, following Windows or set in Settings.
* Crisp on high-DPI screens (per-monitor DPI aware).
* One window: starting it again brings the open window to the front.

## Settings (More › Settings, or Ctrl+,)

| Setting | Default |
|---|---|
| Theme: Same as Windows / Light / Dark | Same as Windows |
| Refresh the list every: never, 5, 10 or 30 seconds, 1 minute | 10 seconds |
| Ask before disabling (deleting always asks) | Off |
| Show Windows' own scheduled tasks | Off |
| Show "run once" entries | On |
| Always start as administrator | Off |
| Reset column layout | — |

Settings, the window position, the column layout, the sort order and the
filter are saved in `HKEY_CURRENT_USER\Software\StartupManager`.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Double-click / Enter / Alt+Enter | Details (several selected: combined report) |
| Del | Delete (asks first, keeps a backup) |
| Ctrl+Z | Undo the last change |
| Ctrl+N | Add an entry |
| F5 | Refresh |
| Ctrl+F | Search (Esc clears it, Enter or ↓ moves to the list) |
| Ctrl+A | Select all |
| Ctrl+C | Copy names |
| Ctrl+Shift+C | Copy details |
| Ctrl+, | Settings |
| F1 | About |
| Shift+F10 / Menu key | Right-click menu |

## Building

Requires CMake 3.20+ and Visual Studio 2022 (C++ desktop workload), or
MinGW-w64.

```bat
cd StartupManager
cmake -S . -B build -A x64
cmake --build build --config Release
:: -> build\Release\StartupManager.exe
```

From Linux:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

GitHub Actions (`.github/workflows/startupmanager.yml`) builds the program
on every push. Download it from the run's **StartupManager-x64** artifact.
The `.exe` is not code-signed, so Windows SmartScreen or Smart App Control
may warn about it.

## How it works

| File | Responsibility |
|---|---|
| `src/Entries.*` | Reading every source (registry, Startup folders, Task Scheduler), enable/disable, delete with backup, restore, add, warnings |
| `src/FileInfo.*` | Program path from a command line, version info, digital signatures (embedded and catalog), SHA-256 |
| `src/Worker.*` | Background thread: refreshes and changes run there, one at a time |
| `src/EntryView.*` | Virtual list view: sorting, filtering, colours, columns, selection kept across refreshes |
| `src/MainWindow.*` | Window, toolbar, status bar, menus, the change flow and undo |
| `src/Dialogs.*` | Settings, Add, Restore and Details windows |
| `src/Settings.*` | Saved preferences |
| `src/Toolbar.*`, `src/Theme.*` | Self-drawn toolbar and status bar, light and dark palette (shared with the other apps) |

## Limitations (planned for Phase 2)

* No boot-time impact yet ("how much does each entry slow down sign-in").
* Only the common autostart places are listed. Services, drivers, Explorer
  add-ons, Winlogon and policy Run keys are not listed yet.
* No "delay this entry" option, no alerts when a program adds itself to
  start-up, and no snapshots or profiles yet.
