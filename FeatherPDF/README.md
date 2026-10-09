# Feather PDF 2.3

A small, fast, native PDF **viewer and editor** for Windows 10 and 11.
It opens PDFs in tabs and lets you scroll, jump to pages, zoom, search,
follow links, browse bookmarks and thumbnails, read in two-page, rotated or
night mode, select and copy text or images, print and present. With **Edit
PDF** and **Annotate** you can change the text of a PDF in place, find and
replace text, fill in forms, sign, add comments, highlights, drawings,
stamps, pictures and new text, and delete, reorder, rotate, insert, merge,
split and extract pages, with undo/redo and crash-safe saving. It makes
scanned pages searchable with the text recognition built into Windows, adds
watermarks and page numbers, truly redacts text and pictures, compresses
pictures, compares two versions of a document, protects documents with an
AES-256 password, measures distances and angles (Geometry menu), and
exports pages as pictures, plain text or Markdown. A command
palette (Ctrl+K) finds any command by typing. It has no accounts, cloud features, telemetry or
background services.

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
| `src/PdfEngine.*` | PDFium wrapper: open, page sizes, tile rendering (rotation, night/dim colours), text, link and comment layer, bookmarks, metadata, text copy, search, printing, page edits, text markup, text editing (runs, fonts), find and replace, comments, writing files |
| `src/DocEditor.*` | One open document on the worker: edit history, undo/redo by replay, safe saving |
| `src/Search.*` | UI-side search state (sorted matches, current match, status) |
| `src/Toolbar.*` | Flat themed toolbar with icon-font buttons and hosted edits |
| `src/Theme.*` | Light/dark palette (System/Light/Dark), dark title bar/scrollbars/menus |
| `src/Settings.*` | Persisted window placement, zoom mode, view mode, theme, open tabs and pages, recent files, drawing options |
| `src/Picture.*` | Pictures (GDI+): loading image files, the saved signature, the signature dialog |
| `src/CommandPalette.*` | The Ctrl+K command palette |
| `src/Ocr.*` | Text recognition with Windows.Media.Ocr on its own thread (raw WinRT interfaces, no extra libraries) |
| `src/Export.*` | Exporting pages: PNG/JPEG through GDI+, text files, and the Markdown builder |
| `src/ToolDialogs.*` | The Recognise text, Watermark, Page numbers, Export, Compress, Password protection and Find-and-mark dialogs |
| `src/PdfCrypt.*` | AES-256 PDF encryption (Windows CNG) and the clean-up of unused objects when saving |
| `src/Compare.*` | Comparing two documents word by word (Myers diff) and marking the differences |
| `src/ViewGeometry.cpp` | Ruler, protractor, rulers along the edges and redaction marks in the page view |
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

PDFium writes every object created while editing, also ones that a later
edit replaced (an earlier version of a page's content, a picture before it
was compressed). Before the file is written, `DropUnusedObjects`
(`PdfCrypt.cpp`) therefore keeps only the objects reachable from the
document's root, so redacted text can not survive in an old copy and
compressed pictures really make the file smaller.

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

**Presentation (F5):** the current page fills the screen on black, with no
toolbars. Click, Space, Page Down or the arrow keys go forward; right-click
or Page Up go back; Esc returns to the previous view and zoom.

**Command palette (Ctrl+K):** type what you want to do and press Enter, for
example "rotate", "dark", "stamp", "merge", "sign". A number goes to that
page ("72"), "zoom 150" zooms, and "find bearing" (or any text that is not
a command) searches the document. Recent files can be opened from it too.

**Back and forward (Alt+Left / Alt+Right, or the mouse's back and forward
buttons):** returns to the pages you jumped from, through links,
bookmarks, page numbers, thumbnails, the comment list and the palette.

**Open recent:** ⋯ › Open recent lists the last 20 files opened.

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
Edit PDF › Edit pages, for these commands:

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

**Edit PDF menu:** the **Edit PDF** button on the toolbar (also ⋯ › Edit
PDF) holds everything that changes a document: Edit text, Find and replace
text, the comment commands, Recognise text, Watermark, Page numbers,
Redact, Compress, Password protection, Annotate and Edit pages.

**Edit text (Ctrl+E):** every line of text on the page gets a dotted
outline, and a banner at the top says what to do. Click a line, change it
in the box that opens over it, and press Enter (or click elsewhere); Esc
cancels the change, and Esc again (or Ctrl+E) leaves edit mode.

* The changed text is written into the PDF itself, at the same place, size
  and colour. Other programs see the new text, and search and copy find it.
* It keeps its original font when that font has every letter needed. PDF
  files often contain only the letters a document uses, so a new letter
  (for example a "9" where the document had none) means a similar font is
  used instead: Helvetica, Times or Courier for Western text, or an
  installed Windows font (Arial, Nirmala UI for Indian scripts, Segoe UI and
  others) for other letters. Feather PDF says so the first time it happens.
* Lines that a PDF stores word by word (or letter by letter) are joined, so
  you always edit a whole line.
* Deleting all the text of a line removes it.
* Scanned pages are pictures and have no text to edit.

**Find and replace text (Ctrl+Shift+H):** changes every occurrence in the
whole document, with an optional "Match case", and says how many places
were changed. The same font rules apply.

**Comments:**

* **Comment** on the toolbar (Ctrl+M): click where the comment should go,
  type it and press OK. A yellow note icon appears on the page.
* **Comment on selected text (Ctrl+Shift+M):** select text first; it is
  highlighted and the comment is attached to the highlight.
* Right-click anywhere › **Add comment here**.
* Hover a note or a commented highlight to read it, with its author and
  date. Click a note (or right-click a comment › Edit comment) to change or
  delete it. Right-click a highlight without a comment to add one or to
  remove the highlight.
* **Edit PDF › All comments** lists every comment in the document (page,
  type, author, text): go to one (it is scrolled into view), edit it or
  delete it.
* New comments are signed with your Windows user name. They are standard
  PDF annotations, so Acrobat, Edge, Chrome and other viewers show them.

**Forms:** fillable PDF forms work straight away. Fields are tinted; click
a text field and type (Enter keeps it, Shift+Enter starts a new line in
multi-line fields), click a check box or option button to set it, and
click a drop-down or list to choose from its options. Clicking a signature
field signs it with your signature (see below). The values are saved into
the form, so other programs see them.

**Annotate:** the **Annotate** button on the toolbar (also in Edit PDF):

* **Highlight, Underline, Strikethrough, Squiggly underline** for selected
  text, with a choice of highlight colour.
* **Add text:** click where the text should start and type; Shift+Enter
  starts a new line, Enter places it. The text becomes part of the page
  (and can be changed later with Edit text).
* **Rectangle, Ellipse, Line, Arrow, Pen (freehand):** drag on the page.
  The tool stays on until Esc, so you can draw several.
* **Colour, Line width, Text size** for these tools, remembered.
* **Stamp:** APPROVED, REJECTED, DRAFT, CONFIDENTIAL, REVIEWED, FINAL, PAID,
  RECEIVED, NOT APPROVED, FOR INFORMATION, or your own words. Click to place
  it, or drag to choose its size.
* **Signature:** draw it with the mouse or a pen, type your name (shown in
  a handwriting font), or use a picture of it (a scan's white paper becomes
  transparent). Choose black or blue ink. It can be remembered on this PC
  (in %APPDATA%\FeatherPDF) so next time it is one click. Click or drag to
  place it.
* **Insert picture:** PNG, JPEG, BMP, GIF or TIFF, placed and sized the same
  way.
* Drawings, stamps and signatures are standard PDF annotations: right-click
  one to add a comment to it or delete it. They also appear in All comments.

**Recognise text (OCR):** Edit PDF › Recognise text. Scanned pages are
pictures, so they cannot be searched or copied. Text recognition finds the
words on them and lays invisible text exactly over each word: the pages
look the same, but search (Ctrl+F), selection, copying and other PDF
programs now find the text. Choose all pages, the current page or a range;
pages that already have text are skipped unless you untick that option.
Pages are recognised in the background (the title bar shows the progress)
and the result is one edit, so Undo takes it back. Recognition uses the
engine built into Windows 10 and 11, in the languages of your Windows
profile, so nothing is uploaded and nothing extra is installed. If Windows
has no recognition language yet, Feather PDF says how to add one (Settings
› Time & language › Language & region › Language options › Optical
character recognition).

**Watermark:** Edit PDF › Watermark adds text such as CONFIDENTIAL or DRAFT
to every page or to chosen pages: diagonal or across, in red, grey, blue,
green or black, with an opacity from 10% to 100% and a size that fits the
page or a fixed size. It goes over the content by default, or behind it.

**Page numbers:** Edit PDF › Page numbers: "1", "Page 1", "Page 1 of 9",
"1 / 9" or "- 1 -", at the bottom or top (left, centre or right), in a
chosen size, starting from any number, on all pages or a range (for example
`2-` to leave the cover unnumbered). Watermarks and page numbers are real
page content, upright on rotated pages too; Undo removes them until you
save.

**Export:** ⋯ › Export as pictures or text (or Ctrl+K › Export):

* **PNG or JPEG pictures,** one per page, at 72, 150 or 300 dpi, named after
  the file you choose with the page number added (`Report-1.png`).
* **Plain text (.txt),** UTF-8, in reading order.
* **Markdown (.md):** larger type becomes headings (`#`, `##`, `###`),
  bullets become lists, wrapped lines are joined into paragraphs and bold
  lines stay bold, ready for notes apps, wikis and AI tools.

Exports include unsaved changes and run in the background with progress in
the title bar (⋯ › Cancel export stops one). Scanned pages have text only
after Recognise text.

**Redact:** Edit PDF › Redact removes information for good, not just
covers it:

* **Mark areas to redact:** drag over text, pictures or anything else.
* **Mark selected text:** select text first.
* **Find and mark:** every occurrence of a word or phrase, and optionally
  every e-mail address, phone number and long number (accounts, cards, IDs).
* Marks are hatched red. Right-click one to remove it, to clear them all or
  to apply them; **Apply redactions** asks once more, then removes the
  letters under the marks (the rest of each line stays where it was), paints
  the pixels of pictures under them black inside the picture itself,
  deletes drawings and pictures entirely inside them and comments, links
  and form fields touching them, and draws black boxes. Undo works until
  you save; use Save as to keep the original.

**Compress:** Edit PDF › Compress makes files with photos and scans much
smaller: pictures stored at more than the chosen resolution (96, 150 or
220 dpi) are scaled down and saved as JPEG. Text and drawings are not
touched, pictures with transparent parts are left alone, and a picture is
only replaced when the result is really smaller. Feather PDF says how big
the file will be once saved.

**Compare:** ⋯ › Compare with › an open tab or a file. The two documents'
words are compared (with the same algorithm as `diff` and git) and a copy
of the other document opens in a new tab with every difference marked:
added text in green, changed text in orange (its comment holds the old
text) and a note where text was removed. Edit PDF › All comments lists
them all, and the copy can be saved like any document.

**Password protection:** Edit PDF › Password protection:

* **Require a password to open the document**, and/or **limit what others
  can do** (allow or forbid printing, copying, and changes) with an owner
  password that lifts the limits.
* Encryption is AES-256 (PDF 2.0 / Acrobat X and later), which Acrobat,
  Edge, Chrome, Firefox and other current readers open. It is applied when
  you save; Undo takes it back before then. The unprotected file is never
  written to the disk: it is encrypted in memory.
* **Remove protection** saves the document without a password.
* Documents that limit printing, copying or changes are respected: those
  commands explain why they are not available. If you have the owner
  password, Password protection unlocks the document.

**Geometry:** the **Geometry** button on the toolbar:

* **Ruler:** drag to measure a distance; the line shows a scale in the
  chosen unit and its length. Shift keeps the line at 15° steps.
* **Protractor:** drag from the corner along the first arm, then click where
  the second arm ends; the angle is shown with a degree scale.
* **Show rulers on the page:** rulers along the top and left edges, measured
  from the current page's top-left corner, with a marker following the
  mouse.
* **Units:** millimetres, centimetres, metres, inches, feet or points.
* **Drawing scale:** 1:1 (actual size) to 1:1000, or any other, so lengths
  on plans and maps read in real-world size.
* Measurements stay on screen until cleared. Right-click one to **keep it on
  the page** (it is drawn into the document) or remove it.

**Text size and colour:** in Edit text mode (Ctrl+E), right-click a line of
text › Text size or Text colour (or Delete this text).

**Text markup:** select text, then right-click › Highlight (Ctrl+H),
Underline (Ctrl+U) or Strikethrough (Ctrl+Shift+K), or use Annotate.
Choose the highlight colour (yellow, green, blue or pink) in Annotate.
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
| Ctrl+K | Command palette |
| Ctrl+H / Ctrl+U / Ctrl+Shift+K | Highlight / underline / strike through the selected text |
| Ctrl+E | Edit text on/off (click a line to change it; Enter keeps, Esc cancels) |
| Ctrl+Shift+H | Find and replace text |
| Ctrl+M | Add a comment (then click on the page) |
| Ctrl+Shift+M | Comment on the selected text |
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
| F5 | Presentation (Esc ends it) |
| Alt+Left / Alt+Right | Back / forward |

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
uploads them as workflow artifacts. It also runs `OcrSmoke`, a test that
recognises a drawn sentence with the same text recognition code as the app
(build it with `-DFEATHERPDF_TESTS=ON`), and a MinGW cross-compile check on
Linux.

## Creating an installer

The repository includes an [Inno Setup 6](https://jrsoftware.org/isinfo.php)
script:

```powershell
cmake --install build --config Release --prefix dist        # 1. portable folder
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" installer\FeatherPDF.iss   # 2. compile
# -> installer\Output\FeatherPDF-Setup-2.2.0.exe
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

* Comparing looks at the words of the documents, not at pictures or layout;
  scanned pages need Recognise text first.
* Redaction removes the letters under a mark from the page's own text. Text
  inside form XObjects (reused page parts) that a mark touches is removed as
  a whole, and drawings only partly under a mark stay (covered by the black
  box). Bookmarks and document properties are not redacted.
* Compress leaves pictures with transparent parts, rotated pictures and
  pictures inside form XObjects as they are.

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
