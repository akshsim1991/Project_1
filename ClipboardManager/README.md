# Clipboard Manager 1.0

Keeps **everything you copy**: text, pictures and copied files. There is
no limit on how many items or how large they are. Search, pin and paste any
of it again from any program.

© 2026 Akshaya Simha. Developed for faster experience.

Windows' own clipboard history (Win+V) keeps only the last 25 items, drops
large items, and loses everything unpinned on restart. Clipboard Manager
keeps it all, as **ordinary files in a "Clipboard contents" folder next to
`ClipboardManager.exe`**, so you can open, back up or move them like any
other files.

## Features

**What is kept**

* **Text** of any length, saved as UTF-8 `.txt` files (Notepad shows every
  language correctly).
* **Pictures**: screenshots and images copied from programs and web pages,
  saved as `.png` (transparency is kept).
* **Copied files** (from Explorer): the list of their names and folders,
  saved as `… files.txt`. Pasting the item gives Explorer the files again.
* Each file is named by the time it was copied, for example
  `2026-10-03 10.15.30.123.txt`.
* Copying the same thing again moves it to the top instead of storing it
  twice.
* The program it was copied from is remembered ("Notepad", "Google
  Chrome") and shown.
* Items that password managers mark as private are **never** saved.
* **Pause recording** from the status bar, the More menu or the tray.
* Optional: delete unpinned items older than a number of days. By default
  everything is kept forever. **Pinned** items are never deleted
  automatically.

**Finding and using items**

* **Ctrl+Shift+V** (changeable: Ctrl+Alt+V, Win+Shift+V or none) opens the
  history from any program, with the newest item selected and the search
  box ready.
* **Enter** (or a double-click) **pastes** the item straight into the
  program you were using. The status bar names that program. This can be
  switched off in Settings; Enter then only copies.
* **Search** as you type, across the whole text of every item and the
  program names. Several words must all match. Typing in the list goes to
  the search box; the arrow keys move through the list from the search box.
* **Filter**: everything, text, pictures, files, pinned, or copied today.
* **Preview** of the selected item: the whole text (select part of it to
  copy just that), the picture fitted to the pane on a checkerboard, or the
  list of files. It also shows the size, number of characters and lines,
  when and where it was copied, and its file name.
* The list shows a thumbnail for pictures, the start of each text, and
  when, where from and how big.
* **Copy**, **Pin/Unpin**, **Delete**, also for several selected items at
  once. Copying several items joins their text, one per line.
* Right-click menu: Paste into…, Copy, Copy the file names as text, Pin,
  Open the file, Save as…, Show the file in its folder, Delete.
* **Open folder** opens the "Clipboard contents" folder.
* The tray menu has the **10 most recent items** to copy without opening
  the window.

**Keys**

| Key | Effect |
|---|---|
| Ctrl+Shift+V | Open (or hide) the history from anywhere |
| Enter / double-click | Paste into the program you were using |
| Ctrl+C | Copy the selected items |
| Ctrl+P | Pin or unpin |
| Del | Delete |
| Ctrl+F or Ctrl+E | Search |
| Ctrl+A | Select all |
| Esc | Clear the search, then hide the window |

**General**

* Runs in the notification area. A single click on the icon opens the
  window. Closing the window keeps it recording (can be changed); the first
  time, a notification says so.
* **Start with Windows**: asked once on the first start, and switchable any
  time. It starts quietly in the tray.
* Only one copy runs: starting it again shows the open window.
* Light and dark theme (following Windows or chosen), sharp on high-DPI
  screens. The splitter between the list and the preview can be dragged.

## The "Clipboard contents" folder

```
ClipboardManager.exe
Clipboard contents\
    2026-10-03 10.15.30.123.txt          text
    2026-10-03 10.15.31.456.png          a picture
    2026-10-03 10.15.32.789 files.txt    copied files, one path per line
    index.txt (hidden)                   order, pins, source programs
```

* Files you delete by hand disappear from the history; `.txt` and `.png`
  files you put in the folder are added to it.
* If the index is lost, the history is rebuilt from the files.
* If the program's folder is read-only (for example inside Program Files),
  the folder is created in `%LOCALAPPDATA%\ClipboardManager` instead and
  the status bar says so.
* Settings are kept in `HKEY_CURRENT_USER\Software\ClipboardManager`.

## Command line

| Command | Effect |
|---|---|
| `ClipboardManager.exe` | Open the window |
| `ClipboardManager.exe /tray` | Start in the tray (used when starting with Windows) |

## Building

Requires CMake 3.20+ and Visual Studio 2022 (C++ desktop workload), or
MinGW-w64.

```bat
cd ClipboardManager
cmake -S . -B build -A x64
cmake --build build --config Release
:: -> build\Release\ClipboardManager.exe
```

From Linux:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

GitHub Actions (`.github/workflows/clipboardmanager.yml`) builds it on every
push. Download it from the run's **ClipboardManager-x64** artifact. The
`.exe` is not code-signed, so Windows SmartScreen may warn about it.

## How it works

| File | Responsibility |
|---|---|
| `src/ClipboardIO.*` | Reading the clipboard (text, PNG, bitmaps, file lists; private items; source program) and writing items back; pressing Ctrl+V |
| `src/Store.*` | The "Clipboard contents" folder: saving, the index, repeats, pins, deleting |
| `src/Images.*` | PNG encoding and decoding (GDI+) |
| `src/ItemList.*` | The virtual, owner-drawn history list with thumbnails |
| `src/Preview.*` | The preview pane |
| `src/MainWindow.*` | Window, recording, search and filters, commands, shortcut, paste, tray |
| `src/TrayIcon.*` | Tray icon, tooltip and notifications |
| `src/Dialogs.*`, `src/Settings.*` | Settings window, preferences, start with Windows |

## Limitations

* Only what is copied while Clipboard Manager runs is kept.
* Pasting with Enter sends Ctrl+V to the program you were using. A few
  programs running as administrator do not accept keys from a normal
  program; the item is then on the clipboard, ready for Ctrl+V.
* The preview shows the first 256,000 characters of very long texts. The
  whole text is kept, copied and searched (search covers the first million
  characters); "Open the text file" shows all of it.
* Rich formatting (fonts, colours, HTML) is not kept: text is kept as plain
  text, which is what most people paste.
