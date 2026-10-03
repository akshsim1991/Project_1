# Battery Status 2.0

Keeps an eye on your laptop battery: level, time left, **real battery
health**, charge cycles, a history graph, and alerts when the battery is
low or charged. A live icon sits in the notification area.

© 2026 Akshaya Simha. Developed for faster experience.

Version 2 is a rewrite of the VB.NET Battery Status 1.2
(`BATTERY_STATUS.zip`) as a single native executable (about 450 KB, no
.NET needed). Everything version 1.2 did is still here, and these problems
are fixed:

* Battery health was a random number. It is now read from the battery
  itself.
* The tray icon was loaded from a file on one particular PC, so the app
  crashed on any other PC.
* Updates stopped while charging.
* The low-battery alert only appeared at the moment of unplugging.
* The "fully charged" sound only played when unplugging at exactly 100%.
* The time left showed nonsense while charging.
* Start-with-Windows could not be turned off again.

## Features

**At a glance**

* A large battery gauge with the percentage. It is green, amber or red by
  level, with a lightning bolt while charging.
* The state: *Charging*, *On battery*, *Fully charged*, *Plugged in, not
  charging* (battery care or a charge limit), or *battery saver on*.
* **Time left** on battery, from the battery's real power use, falling back
  to Windows' estimate and then to how fast the level has been dropping.
* **Time until full** while charging, and until your charged-alert level
  (for example "99% in 35 min").
* Advice: *Plug in the charger*, *You can unplug the charger*.
* The percentage is also shown in the title bar.

**Battery** card (read from the battery driver)

* **Health**: how much of its original capacity the battery still holds,
  in full words, for example "73% of its original capacity". **Condition**
  then says what was lost: "Worn — lost 27% of its capacity since new". It
  is *Good* from 80%, *Worn* from 60%, and *Replace soon* below that.
* A full charge holds, for example "49.8 Wh (new: 56.0 Wh)", plus the
  energy in the battery now.
* **Charge cycles**.
* **Power**: charging at, or using, so many watts. Some batteries (many HP
  laptops, for example) do not report this; it is then **measured** from the
  change in stored energy over a few minutes, and marked "(measured)".
* Voltage and temperature, when the battery reports them.
* Model, manufacturer, chemistry and manufacturing date.
* PCs with two batteries show totals.

**This session** card

* Plugged in / unplugged since when, and for how long. This also works
  after a restart, from the history.
* Charged since plugged in, or used since unplugged (in %).
* **Battery use** or **Charging speed** in plain words, for example "About 6%
  of the battery per hour (8.1 W)", and Windows' own estimate.
* **A full battery lasts about…**, based on your own usage history.
* Battery saver on or off.

**History graph**

* The battery level over the last 1 hour, 6 hours, 24 hours or 7 days.
* Charger periods are shaded. The low and charged alert levels are shown as
  dashed lines.
* Hover the graph to see the time, level, state and power.
* The history is saved once a minute (and whenever something changes) to
  `%LOCALAPPDATA%\BatteryStatus\history.csv`, and kept for 30 days by
  default.
* **Export…** saves it as a CSV file for Excel.

**Alerts** (all adjustable in Settings)

* **Low battery** at 20% and **critical** at 10%, also when the level drops
  while you work, not only when unplugging.
* **Charged, unplug** at 99%. Setting it to 80% helps the battery last
  longer.
* Optional: charger connected and disconnected.
* **Repeat** every 5 minutes until dealt with (plugged in or unplugged), or
  only once.
* Shown as Windows notifications. Optionally also as a message box for low
  and critical, like version 1.2.
* Sound: none, Windows notification, Windows alarm, or your own WAV file,
  with a Test button.
* **Pause alerts for 1 hour** from the tray or the More menu.

**Tray**

* A live icon: a battery picture filled to the level (green, amber or red,
  with a bolt when charging), or the percentage as a number. It adapts to a
  light or dark taskbar.
* The tooltip shows the level, state and time left or time to full.
* A single click opens the window, even when it is minimised. Right-click
  for: Open or Hide, Pause alerts, Start with Windows, Settings, About and
  Exit.
* Minimising or closing the window keeps it running in the tray (the close
  behaviour can be changed). The first time, a notification says so.

**More**

* **Battery report**: creates and opens Windows' detailed battery report
  (`powercfg /batteryreport`), with capacity history and usage.
* **Copy a summary** of everything shown, and a shortcut to Windows' power
  and battery settings.
* **Start with Windows**:
  * The first launch asks once, like version 1.2.
  * It can be switched on or off any time (tray menu, More menu or Settings).
  * It starts quietly in the tray.
  * It replaces version 1.2's start-up entry, so the two never run
    together.
* Only one copy runs: starting it again shows the open window.
* Light and dark theme (following Windows or chosen), sharp on high-DPI
  screens.
* Settings: theme, update interval (1–30 s), tray icon style, start with
  Windows, start in the tray, close to the tray, and how long to keep the
  history.

## Command line

| Command | Effect |
|---|---|
| `BatteryStatus.exe` | Open the window |
| `BatteryStatus.exe /tray` | Start in the tray (used when starting with Windows) |
| `BatteryStatus.exe /demo` | A simulated battery, to try the program on a desktop PC (nothing is saved) |

## Building

Requires CMake 3.20+ and Visual Studio 2022 (C++ desktop workload), or
MinGW-w64.

```bat
cd BatteryStatus
cmake -S . -B build -A x64
cmake --build build --config Release
:: -> build\Release\BatteryStatus.exe
```

From Linux:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

GitHub Actions (`.github/workflows/batterystatus.yml`) builds it on every
push. Download it from the run's **BatteryStatus-x64** artifact. The `.exe`
is not code-signed, so Windows SmartScreen may warn about it.

## How it works

| File | Responsibility |
|---|---|
| `src/Battery.*` | `GetSystemPowerStatus`, plus the battery class driver (IOCTL) for capacities, cycles, voltage, rate, chemistry, temperature; demo simulation |
| `src/History.*` | Recording, loading, pruning and exporting the history; drain/charge speeds; last plug/unplug times |
| `src/Summary.*` | Estimates and wording shared by the window, tooltip and copied summary |
| `src/Alerts.*` | Alert rules: once, then repeat; reset when the situation changes; pause |
| `src/TrayIcon.*` | The drawn tray icon, tooltip and notifications |
| `src/Dashboard.*` | Gauge, cards and the history graph (GDI+) |
| `src/MainWindow.*` | Window, polling, tray behaviour, commands, battery report, export |
| `src/Dialogs.*`, `src/Settings.*` | Settings window, sounds, preferences, start with Windows |

## Limitations

* Health, cycles, voltage, power and temperature come from the battery's
  own driver. Some batteries do not report all of them: missing values are
  left out, and health then shows "Not reported".
* Estimates need a few minutes of history after starting or plugging
  in or out.
* The history only covers the time Battery Status was running.
