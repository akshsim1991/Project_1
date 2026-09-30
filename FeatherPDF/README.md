# Feather PDF

A small, fast, native PDF **viewer and page editor** for Windows 10 and 11.
It opens PDFs in tabs and lets you scroll, jump to pages, zoom, search,
follow links, browse bookmarks and thumbnails, read in two-page, rotated or
night mode, select and copy text or images, and print. You can also delete,
reorder, rotate, insert, merge, split and extract pages, and highlight,
underline or strike through text, with undo/redo and crash-safe saving. It
has no accounts, cloud features, telemetry or background services.

© 2026 Akshaya Simha. Developed for faster experience.

```
┌──────────────────────────────────────────────────────────────────────┐
│ manual.pdf  ✕ │ report.pdf  ✕ │ +                                    │  ← tabs
├──────────────────────────────────────────────────────────────────────┤
│ Open │ ‹ › [ 12 ] / 250 │ −  125%  + │                    Search │ ⋯ │
├──────────────────────────────────────────────────────────────────────┤
│ [find in document      ] ˄ ˅  Aa  3 of 17                        ✕ │  ← Ctrl+F only
├──────────────────────────────────────────────────────────────────────┤
│                         ┌──────────────┐                             │
│                         │   PDF PAGE   │                             │
│                         └──────────────┘                             │
└──────────────────────────────────────────────────────────────────────┘
```

* Executable: about 450 KB, plus `pdfium.dll` (about 7 MB). No runtime installation needed.
* Opens a window right away; documents load on a background thread.
* The number of rendered pixels in memory depends on the window size, not on
  the page count or the zoom level.

---

## 1. Technology stack and why

| Choice | Reason |
|---|---|
| **C++17 + raw Win32 API** | Starts fastest and uses the least memory on Windows. There is no runtime to load (no .NET, JVM, Chromium or Qt). With a static CRT the program is one executable plus the PDF engine DLL. Idle memory is a few MB above what the engine uses. |
| **GDI** for drawing | Rendered tiles are 32-bit DIB buffers, blitted with `StretchDIBits`. There is no GPU context, so no idle GPU use and no driver problems. It is fast enough for 2D page blitting. |
| **Segoe MDL2 / Fluent icon font** for toolbar icons | Ships with Windows 10/11, stays sharp at any DPI, and needs no image resources. |
| **CMake** | Builds with Visual Studio 2022 (recommended), with MinGW-w64 on Windows, or cross-compiles from Linux. |

Alternatives considered: WinUI/WPF/.NET (slower cold start and 30–80 MB
baseline), Electron/WebView (hundreds of MB, and it is effectively a
browser), Qt (heavy DLLs, and LGPL obligations). None of them fits "a small
native Windows utility".

## 2. PDF engine: PDFium

| | **PDFium** (selected) | MuPDF | Windows.Data.Pdf (built in) |
|---|---|---|---|
| Rendering quality / compatibility | Excellent (Chrome's PDF engine) | Excellent | Good |
| Speed / memory | Very good, supports partial (clipped) rendering | Very good | Good |
| Text search API | Yes (`fpdf_text`) | Yes | **No text API** |
| Robustness on malformed files | Continuously fuzzed by Google | Good | Good |
| License | **BSD-3 / Apache-2.0 (permissive)** | **AGPL-3** or paid commercial | OS component |
| Integration | Prebuilt DLL + C API | Build from source | WinRT/COM |

PDFium has permissive licensing, is battle-tested in Chrome, and has a clean
C API with page rendering to caller-provided buffers and text search. MuPDF
is equally good technically, but AGPL would force the whole app to be AGPL.
The built-in Windows PDF API cannot search text. Feather PDF uses the
prebuilt binaries from
[bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries),
pinned to release `chromium/8066`, without V8/JavaScript or XFA.

## 3. Architecture

```
PDF files ─► DocEditor × N (one per tab)                          ┐
               │  PdfEngine (PDFium, lazy file reads)              │
               │  + edit history (undo/redo, safe save)            │ render worker
               ▲                                                   │ thread
               │ commands / wanted-tile list                       │
            RenderWorker ── renders a tile, extracts a page's      ┘
               │           text layer, copies text, applies an
               │           edit, saves, or searches one page per step
               │ PostMessage(result)
               ▼
MainWindow ─► Tab × N ─► PdfView ── layout, visible pages, zoom, navigation,
   │                        │        text selection
   │                        └─► PageCache (bounded LRU of tiles) ─► GDI paint
   ├─ TabBar (self-drawn tab strip)
   ├─ Sidebar ─ bookmarks TreeView / ThumbView (lazy thumbnails, 16 MB LRU)
   ├─ Toolbar (self-drawn, hosts page and search edits)
   ├─ SearchState (merged results, next/previous)
   ├─ Settings (HKCU\Software\FeatherPDF)
   └─ FileAssoc (per-user .pdf registration)
```

| Source file | Responsibility |
|---|---|
| `src/main.cpp` | Entry point, command line, DPI awareness, message loop |
| `src/MainWindow.*` | Top-level window, tabs, commands, menus, open/password/error flow, search UI |
| `src/TabBar.*` | Themed tab strip (select, close, middle-click close, "+") |
| `src/Sidebar.*` | Optional left panel: bookmarks tree or thumbnails, with a draggable splitter |
| `src/ThumbView.*` | Virtualised page-thumbnail list, rendered lazily on a low-priority worker queue |
| `src/PdfView.*` | Page layout (single / continuous / two-page), rotation, zoom, navigation, tile requests, painting, text selection, links, image copy |
| `src/PageCache.*` | Bounded LRU cache of rendered tiles (memory policy) |
| `src/RenderWorker.*` | Background thread that owns PDFium (all tabs); job priorities |
| `src/PdfEngine.*` | PDFium wrapper: open, page sizes, tile rendering (rotation, night/dim colours), text and link layer, bookmarks, metadata, text copy, search, printing, page edits, text markup, writing files |
| `src/DocEditor.*` | One open document on the worker: edit history, undo/redo by replay, safe saving |
| `src/Search.*` | UI-side search state (sorted matches, current match, status) |
| `src/Toolbar.*` | Flat themed toolbar with icon-font buttons and hosted edits |
| `src/Theme.*` | Light/dark palette (System/Light/Dark), dark title bar/scrollbars/menus |
| `src/Settings.*` | Persisted window placement, zoom mode, view mode, theme, open tabs and pages |
| `src/FileAssoc.*` | `.pdf` "Open with" / Default-apps registration (HKCU) |

### Threading

PDFium is not thread-safe, not even across documents, so **exactly one
worker thread** calls into it for every tab. Each tab's document has its own
`PdfEngine`, keyed by document id. The UI thread never blocks on PDF work:

* Opening a document runs on the worker. The window is already visible and
  shows "Opening…". If opening fails, the previously open document stays open.
* Every repaint **replaces** the worker's list of wanted tiles. Stale requests
  (pages you scrolled past) disappear instead of piling up.
* Priorities are commands (open, text layer, copy, search start/cancel)
  first, then visible tiles nearest the viewport centre, then prefetch tiles,
  then sidebar thumbnails, then printing (one page per step), then search
  (one page per step). A print job or a running search never delays
  rendering.
* When there is no work, the worker sleeps on a condition variable (0% CPU).
  The UI has no polling timers.

### Editing, undo and saving

PDFium can change a document in memory but has no undo. `DocEditor`
therefore records every edit (delete, move, rotate, insert, merge, text
markup) as a small description. The edit is applied to the live document
straight away. **Undo** re-opens the file as it was last loaded and replays
every edit except the last. That takes milliseconds because nothing is
rendered while replaying. **Redo** applies the next recorded edit again.
Inserted and merged files are first copied to a private temporary folder
(`%TEMP%\FeatherPDF`), so undo and redo keep working even if the originals
change or their tabs are closed.

Every edit gives the document a new internal id. Tiles, text layers and
search results that were still queued for the old state are then ignored
instead of briefly showing the wrong page. Until the new tiles arrive, the
previous rendering stays on screen as a placeholder, so there is no flash.

**Saving is crash-safe:**

1. The document is written to a new file in the same folder and flushed to
   disk.
2. That file is opened again to check that it is a valid PDF with every page.
3. Only then does it replace the original, using `ReplaceFileW`, which keeps
   the original's attributes and permissions.

A crash, power cut or full disk at any step leaves the original file
untouched. If the original is also what undo replays from, a private copy
of it is kept first, so changes made before saving can still be undone.
Encrypted PDFs stay encrypted with the same password.

### Rendering and caching

1. **Page virtualisation.** Layout is an array of page offsets computed with
   integer math from page sizes only. No page content is parsed until the
   page is on screen. Finding the visible pages is a binary search, so it
   costs the same for 10 or 10,000 pages.
2. **Tiles, not pages.** Each visible page is split into tiles: full-width
   rows up to 2048 px wide and 512 px tall, or 1024 px columns on wider
   pages. PDFium renders a tile straight into its buffer using an offset and a
   clip, so there are no intermediate copies. Only visible tiles plus a
   prefetch band are requested: one screen below (the reading direction) and
   half a screen above.
3. **Bounded cache.** Tiles live in an LRU cache whose byte budget follows
   the viewport: `clamp(5 × screen + 16 MB, 64 MB, 256 MB)`. That is about
   64 MB on a 1080p window. Prefetch stops at 90% of the budget, and tiles
   drawn in the current frame are pinned, so a tight budget cannot cause
   render/evict loops.
4. **Smooth zoom without extra rendering.** After a zoom change, tiles from
   the old zoom level are stretched as placeholders straight away. During
   Ctrl+wheel or pinch zooming, rendering waits until the wheel stops for
   150 ms, so intermediate zoom steps are not rendered. Once the new level is
   sharp, the old tiles are freed.
5. **Parsed-page cache.** The worker keeps only the four most recently
   parsed pages (`FPDF_PAGE`). Tiles of the same page arrive in bursts, and
   this keeps content-stream parsing to about once per page.

## 4. How RAM usage is controlled

* **Tile buffers use `VirtualAlloc`/`VirtualFree`.** Evicting a tile returns
  its memory to Windows immediately instead of leaving it in a fragmented heap.
* **Memory depends on the window size, not the document.** A 5,000-page
  PDF at 1600% zoom holds only the tiles on screen plus the budgeted prefetch.
  A single A4 page at 1600% as one bitmap would be about 900 MB.
* PDFium renders with `FPDF_RENDER_LIMITEDIMAGECACHE`, which keeps its
  decoded-image cache small.
* The file is **read lazily** through `FPDF_LoadCustomDocument`. It is not
  loaded into memory; Windows' file cache handles the reads.
* When the window is **minimised**, all tiles, the back buffer and the parsed
  pages are released.
* **Tabs in the background cost almost nothing.** Only the active tab's view
  is visible and renders. When you switch away from a tab, its tiles and text
  layers are freed; switching back re-renders the visible area in a few
  milliseconds. A background tab keeps only its parsed document structure.
* **Thumbnails** are rendered only for the part of the sidebar you can see
  (plus a few below), on the lowest-priority worker queue, and kept in a
  16 MB LRU cache. Nothing is rendered while the panel is hidden, and the
  cache is freed when the panel closes or the window is minimised.
* **Bookmarks** are read once when a document opens and stored as a flat
  list (at most 20,000 entries); the tree control holds only titles.
* **Text selection** keeps the character boxes of at most 32 pages (a few
  hundred KB). They are fetched only when the mouse moves over a page.
  Copying runs on the worker, so selecting 500 pages does not load 500 text
  layers into the UI.
* Search results store only rectangles (a few dozen bytes per match). Text
  pages are closed as soon as each page has been searched.

In a Wine test with a 600-page document: about 56 MB when opened, a plateau
around 116 MB after rapid paging through 150 pages (including Wine's own
overhead), about 75 MB at 1600% zoom, and back to about 58 MB at fit-page.
Native Windows figures should be lower.

## 5. How large PDFs are handled

* Opening reads only the xref and page tree. The page sizes of all pages are
  collected without parsing any content, which takes milliseconds even for
  thousands of pages.
* Layout, visible-page lookup and scrollbar mapping all work for any page
  count. Offsets are 64-bit, and scrollbar units are scaled if a document
  exceeds the 32-bit scrollbar range.
* Search runs page by page on the worker, starting at the current page and
  wrapping around. Results stream in, the first match is shown immediately,
  and the status shows progress ("Searching… 40%", then "3 of 17+").
* Unusual page sizes are clamped to sane bounds. Mixed portrait and
  landscape documents are laid out centred, and fit modes re-fit per page in
  single-page mode.

---

## Features

**Tabs:** every document opens in its own tab. Opening a file that is
already open switches to its tab. Close a tab with its ✕, a middle-click or
Ctrl+W, and switch with Ctrl+Tab or the mouse wheel over the tab strip.
Double-clicking a PDF in Explorer while Feather PDF is running opens it as a
new tab in the existing window (use `/newwindow` for a separate window).
The open tabs and the page of each are restored on the next start.

**Opening:** Open (Ctrl+O or Ctrl+T, with multi-select), drag and drop one or
more files onto the window, double-click in Explorer (after registering),
and the command line.

**Text selection:** where the PDF contains real text, the cursor becomes an
I-beam over it. Drag to select (the view scrolls when you drag past the
edge), double-click to select a word, Shift+click to extend, Ctrl+A to
select all, and Ctrl+C or right-click › Copy to copy. Dragging on blank
areas or on scanned (image-only) pages pans the page instead.

**Viewing:** continuous scrolling or single-page mode, fit width, fit page,
actual size (100% equals the printed size on screen), 5%–1600% zoom,
Ctrl+wheel zoom anchored at the cursor, touchpad pinch, drag-to-pan, and
full screen (F11).

**Search:** Ctrl+F, incremental search as you type, Enter/F3 for next,
Shift+Enter/Shift+F3 for previous, a match-case toggle, a match count, and
highlighted matches with the current one in orange. Esc closes the bar.

**Links:** internal links jump to their page, and web links (`http`,
`https`, `mailto`) open in your browser. Addresses written as plain text
(for example `www.example.com`) work too. Hovering a web link shows its
real address first. Links that would launch files or programs are ignored.

**Bookmarks and thumbnails:** ⋯ › Bookmarks panel (Ctrl+B) shows the PDF's
table of contents, and ⋯ › Thumbnails panel (Ctrl+Shift+B) shows page
previews that are rendered only as you scroll. Click an entry to jump
there, and drag the panel edge to resize it. The choice is remembered.

**Page layout and rotation:** ⋯ › Page layout › Single page / Continuous /
Two pages, with "Show cover page separately" for books and magazines.
Rotate left or right (Ctrl+L / Ctrl+R) turns sideways scans upright.

**Page colours:** ⋯ › Page colours › Normal / Dark (night mode: light text
on a dark page) / Dimmed. This applies to all tabs and is remembered.
Printing and copying always use the original colours.

**Printing:** ⋯ › Print (Ctrl+P) opens the standard Windows print dialog,
with all pages, the current page or page ranges (such as `1-3, 7`), and
copies. Each page is fitted to the paper and turned automatically when
page and paper orientation differ. Pages are printed in the background
while you keep reading, with progress in the title bar and a
"Cancel printing" menu item.

**Document properties:** ⋯ › Document properties (Ctrl+D) shows file name,
location and size, plus title, author, subject, keywords, dates, creator,
producer, PDF version, page count, page size and whether it is encrypted.

**Copy as image:** ⋯ › Copy page as image copies the current page, and
⋯ › Copy area as image lets you drag a rectangle. Both are also on the
right-click menu. Images are rendered at 200 DPI (or the current zoom if
sharper, up to about 40 megapixels) and pasted as normal bitmaps into any
program.

### Editing

**Pages:** open the thumbnails panel (Ctrl+Shift+B) and select pages with a
click, Ctrl+click, Shift+click or Ctrl+A. Right-click a page, or use
⋯ › Edit pages, for these commands:

* **Delete pages** (or press Del in the thumbnails).
* **Reorder pages:** drag the selected thumbnails to a new position. A blue
  bar shows where they will go.
* **Rotate clockwise / counterclockwise:** saved into the file, unlike the
  view-only Rotate left/right (Ctrl+L / Ctrl+R).
* **Insert blank page after** the selection, the same size as its neighbour.
* **Insert pages from file:** another PDF is inserted after the selection.
* **Merge PDFs into this document:** choose several files; they are added
  to the end in the order chosen.
* **Merge open tabs into this document:** every other open tab is added to
  the end, in tab order, including any unsaved changes.
* **Extract or split pages:** save a page range (such as `1-3, 7, 10-`) as
  a new PDF, or save each page as its own file (`name-1.pdf`,
  `name-2.pdf`, …). You can then open the new file in a tab.

Without the thumbnails panel, page commands apply to the current page.

**Text markup:** select text, then right-click › Highlight (Ctrl+H),
Underline (Ctrl+U) or Strikethrough (Ctrl+K), or use ⋯ › Mark up text.
Choose the highlight colour (yellow, green, blue or pink) in the same menu.
These are standard PDF annotations, so Acrobat, Edge, Chrome and other
viewers show them too.

**Save, undo and redo:** Save (Ctrl+S) or Save as (Ctrl+Shift+S), and
Undo (Ctrl+Z) or Redo (Ctrl+Y / Ctrl+Shift+Z). These are also on the
toolbar. A tab with unsaved changes shows "•" before its name, as does the
title bar. Closing that tab, or the window, asks whether to save, with
Save / Don't save / Cancel. Undo keeps working after saving.

**Theme:** ⋯ › Theme › System default / Light / Dark. The choice is
remembered, and "System default" follows the Windows setting live.

**Windows integration:** Per-monitor V2 high-DPI support (crisp on mixed-DPI
setups), light/dark theme (title bar, tabs, toolbar, scrollbars, menus),
remembered window position/size, zoom mode and view mode, application icon
and version info, and `.pdf` association.

### Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+O / Ctrl+T | Open PDF (in a new tab) |
| Ctrl+W / Ctrl+F4 | Close tab |
| Ctrl+Tab / Ctrl+Page Down | Next tab |
| Ctrl+Shift+Tab / Ctrl+Page Up | Previous tab |
| Ctrl+C | Copy selected text |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+Z | Undo |
| Ctrl+Y / Ctrl+Shift+Z | Redo |
| Ctrl+H / Ctrl+U / Ctrl+K | Highlight / underline / strike through the selected text |
| Del (in thumbnails) | Delete the selected pages |
| Ctrl+A (in thumbnails) | Select all pages |
| Ctrl+P | Print |
| Ctrl+D | Document properties |
| Ctrl+B | Bookmarks panel |
| Ctrl+Shift+B | Thumbnails panel |
| Ctrl+L / Ctrl+R | Rotate left / right |
| Ctrl+A | Select all text |
| Double-click | Select word |
| Ctrl+F | Search |
| Enter / F3 | Next match |
| Shift+Enter / Shift+F3 | Previous match |
| Esc | Cancel area copy, clear selection, close search, or leave full screen |
| Ctrl++ / Ctrl+= / Ctrl+Numpad+ | Zoom in |
| Ctrl+- / Ctrl+Numpad- | Zoom out |
| Ctrl+0 | Fit page (reset zoom) |
| Ctrl+1 | Actual size (100%) |
| Ctrl+2 | Fit width |
| Ctrl+Mouse wheel | Zoom at the cursor |
| Ctrl+G | Go to page (focus the page box) |
| Home / End | First / last page |
| Page Up / Page Down | Previous / next page |
| Left / Right | Previous / next page |
| Up / Down | Scroll |
| Space / Shift+Space | Scroll one screen down / up |
| Shift+Mouse wheel | Scroll horizontally |
| F11 | Full screen |

### Command line

```
FeatherPDF.exe "C:\docs\manual.pdf"            open a file (as a tab if already running)
FeatherPDF.exe "C:\docs\manual.pdf" /page 42   open at page 42
FeatherPDF.exe /newwindow "C:\docs\a.pdf"      open in a separate window
FeatherPDF.exe /register                      register as a PDF handler (current user)
FeatherPDF.exe /unregister                    remove that registration
```

---

## Building

### Requirements

* Windows 10/11 with **Visual Studio 2022** (Desktop development with C++)
  and **CMake 3.20+** (included with Visual Studio). The other supported
  options are MinGW-w64 on Windows, or cross-compiling from Linux.
* Internet access on the first configure: CMake downloads the pinned PDFium
  package (about 4 MB). To build offline, download
  `pdfium-win-x64.tgz` from the pdfium-binaries release `chromium/8066`,
  extract it, and pass `-DPDFIUM_DIR=<folder>`.

### Build a release `.exe` (Visual Studio / MSVC)

From a *Developer PowerShell for VS 2022*, in the `FeatherPDF` folder:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
# -> build\Release\FeatherPDF.exe + pdfium.dll

# Portable, ready-to-zip folder (exe, dll, licenses):
cmake --install build --config Release --prefix dist
```

The MSVC release build links the CRT statically (`/MT`), uses whole-program
optimisation (`/GL /LTCG`), and wraps every PDFium call in a
structured-exception guard. If PDFium ever faults on a hostile file, the
affected page is shown blank and the application keeps running.

For ARM64 (Windows on ARM), use `-A ARM64`. CMake then fetches
`pdfium-win-arm64`.

### Cross-compile from Linux (MinGW-w64)

```bash
sudo apt install g++-mingw-w64-x86-64 cmake
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix dist
```

(MinGW builds have no structured-exception guard around PDFium, so prefer
MSVC for releases.)

### Continuous integration

`.github/workflows/featherpdf.yml` (at the repository root) builds the MSVC
release, the portable folder and the installer on `windows-latest`, and
uploads them as workflow artifacts. It also runs a MinGW cross-compile check
on Linux.

## Creating an installer

The repository includes an [Inno Setup 6](https://jrsoftware.org/isinfo.php)
script:

```powershell
cmake --install build --config Release --prefix dist        # 1. portable folder
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" installer\FeatherPDF.iss   # 2. compile
# -> installer\Output\FeatherPDF-Setup-1.3.0.exe
```

The installer:

* installs per user by default (no admin prompt), or for all users if the
  user chooses that;
* creates a Start-menu shortcut and an optional desktop shortcut;
* registers the `FeatherPDF.Document` ProgID, the ".pdf → Open with" entry
  and Default-apps capabilities;
* offers to open *Settings › Default apps*. Windows 10/11 do not allow any
  installer to silently take over `.pdf`; the user has to confirm the choice
  there;
* uninstalls cleanly, removing its registry entries.

Without the installer, use **⋯ › Set as default PDF viewer…** (or run
`FeatherPDF.exe /register`) from the portable folder.

Code signing is recommended for public distribution (SmartScreen). Sign both
`FeatherPDF.exe` and the setup file with `signtool sign /fd sha256 /tr
<timestamp-url> /td sha256 ...`.

---

## Dependencies and licenses

| Dependency | How it is used | License |
|---|---|---|
| PDFium (`pdfium.dll`) | PDF engine, loaded at run time | BSD-3-Clause / Apache-2.0 |
| Its bundled libraries (FreeType, libjpeg-turbo, libpng, zlib, OpenJPEG, lcms2, AGG, ICU, Abseil, and others) | Inside `pdfium.dll` | Permissive (see `THIRD_PARTY_NOTICES.md`) |
| Windows SDK system libraries | UI, GDI, dialogs, registry | Part of Windows |

There are no other dependencies. The full license texts are installed to
`licenses/` by `cmake --install`.

## Performance considerations

* **Startup:** there is no runtime to load. The window is created and shown
  before any PDF work starts. `pdfium.dll` is mapped by the loader (no
  initialisation cost until the worker calls `FPDF_InitLibrary`).
* **Idle:** no timers, no polling, no background threads with work. The
  worker sleeps on a condition variable.
* **Scrolling:** each frame is a back-buffer fill plus 1:1 `StretchDIBits`
  of cached tiles, costing a few milliseconds even at 4K. New tiles
  invalidate only their own rectangle, and prefetched off-screen tiles cause
  no repaint.
* **Zoom:** placeholder stretching gives instant feedback. Re-rendering is
  debounced during wheel zoom, and results for zoom levels you have already
  left are discarded.
* **GPU:** none used. GDI blits run on the CPU, which avoids GPU wake-ups
  on battery.
* The worker runs at *below normal* priority so the UI thread always wins on
  low-core machines.

## Known limitations (version 1)

* Printing always fits each page to the paper; there is no "actual size"
  or booklet option. Printing ignores the night/dim page colours.
* Link and bookmark targets jump to the right page. The position within the
  page is only approximated (from the top-left of an unrotated page), so for
  rotated views or unusual page boxes the jump goes to the top of the page.
* The view rotation is per tab and is not remembered after closing.
* Text selection follows PDFium's character order. It works well for normal
  documents, but multi-column layouts can select across columns, and
  rotated or vertical text is only roughly supported.
* Tabs cannot be reordered by dragging or torn off into a new window. When
  there are very many tabs they shrink to a minimum width and the rest are
  clipped.
* Search runs in the active tab only; switching tabs stops it.
* No rotate-view command. Pages are shown as the PDF specifies (`/Rotate`
  is honoured).
* The file is not reloaded automatically when it changes on disk. If
  another program changes a file that has unsaved edits in Feather PDF,
  undo replays those edits on the changed file.
* Annotations can be added but not selected, moved or deleted afterwards,
  except with undo. Existing annotations and forms are kept when saving.
* Merging a password-protected file from disk is refused. Open it in a tab
  first and use "Merge open tabs".
* Saving always writes a complete new file, not an incremental update. Any
  digital signatures in the original become invalid, as with most editors.
* Files over 4 GB are rejected (PDFium's custom-file-access API is 32-bit on
  Windows).
* XFA forms and PDF JavaScript are not supported (PDFium build without
  V8/XFA). Normal AcroForm field *appearances* are rendered.
* The Linux-built MinGW binary lacks the SEH crash guard around PDFium.
* Dark popup menus use a long-standing but undocumented uxtheme API. On
  builds before Windows 10 1903 menus stay light.
* Only the tabs open at exit are remembered, not a longer per-file history.

## Suggested improvements for version 2

1. Back/forward navigation (Alt+← / Alt+→) after following links and bookmarks.
2. Print options: actual size, multiple pages per sheet, booklet.
3. Presentation mode and auto-scroll.
4. Remember rotation per document.
5. A recent-files list (Jump List integration) and per-document zoom memory.
6. Reload when the file changes on disk (`ReadDirectoryChangesW`).
7. A low-resolution thumbnail layer per page, so very fast scrolling shows
   page outlines with content instead of blank white pages.
8. An optional Direct2D presentation path for smooth animated scrolling on
   high-refresh displays (off by default to keep GPU use at zero).
9. Drag-to-reorder tabs, and a portable-mode INI file next to the executable.
10. An MSIX package and signed releases published from CI.
