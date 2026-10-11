# Feather CAD

A lightweight 2D drafting program for Windows 10 and 11, in the style of
AutoCAD and Solid Edge 2D Drafting. It is made for learning and academic use.

Created by: Akshaya Simha. © 2026 Akshaya Simha.

It is written in C++ with the Windows API and Direct2D. It uses no
third-party libraries, comes as one small `.exe` and starts instantly.

## Phase A (version 1.0)

**Workspace**
- Menu strip, tool bar, drawing area, command line and status bar
- Light and dark themes, which follow Windows by default
- Sharp at any display scaling (per-monitor DPI)
- Command search (Ctrl+K): type what you want to do, such as "circle" or "dark"

**Command line**
- Type a command or its short name, then press Enter or Space:

  | Short name | Command   |
  |------------|-----------|
  | L          | LINE      |
  | PL         | PLINE     |
  | C          | CIRCLE    |
  | A          | ARC       |
  | REC        | RECTANG   |
  | M          | MOVE      |
  | CO         | COPY      |
  | RO         | ROTATE    |
  | E          | ERASE     |
  | Z          | ZOOM      |
  | DI         | DIST      |
  | U          | UNDO      |

- Enter on its own repeats the last command. Esc cancels.
- Up and Down recall what you typed earlier. Tab completes a command name.
- Options appear in the prompt, for example `[Close/Undo]`. Type the
  capital letters, or right-click to pick one.

**Typing points**

| Input    | Meaning |
|----------|---------|
| `10,20`  | Absolute point |
| `@10,0`  | Relative to the last point |
| `@50<30` | Distance 50 at 30° |
| `50<30`  | Distance and angle from the origin |
| `25`     | A single number: that far in the direction of the cursor |

**Drawing**
- **Lines:** with Close and Undo.
- **Polylines:** line and tangent-arc segments in one object.
- **Circles:** centre and radius or diameter, 2 points, or 3 points.
- **Arcs:** 3 points; or start, centre, end; or centre, start, end or included angle.
- **Rectangles:** two corners, or length and width.

**Editing**
- **Move and Copy:** base point and second point, or a typed displacement.
  Copy places as many copies as you like.
- **Rotate:** by an angle, with a copy option, or by reference angle.
- **Erase:** with E or the Delete key.
- **Undo and Redo:** unlimited, one step per command (Ctrl+Z, Ctrl+Y).

**Selecting**
- Click objects to select them. Shift+click removes one.
- Drag (or click twice) for a window:
  - Left to right selects what is completely inside.
  - Right to left selects what the window crosses.
- Ctrl+A selects everything. Esc deselects.
- Works both ways: select first, then the command, or start the command
  and then select.

**Drafting aids**

| Key           | Aid |
|---------------|-----|
| F7            | Grid. It adapts as you zoom. |
| F9            | Snap to the grid spacing |
| F8, or Shift held | Ortho: horizontal and vertical only |
| F3            | Object snaps |
| F10           | Polar tracking |
| F11           | Object snap tracking |
| F12           | Dynamic input |

- The object snaps are endpoint, midpoint, centre, quadrant, intersection,
  perpendicular, tangent and nearest. A marker and its name show where the
  next point will snap.
- Type END, MID, CEN, QUA, INT, PER, TAN or NEA for a one-off snap. The
  right-click menu also has these under "Snap override".
- While drawing, a tooltip shows the length and angle from the last point.

**Viewing**
- The mouse wheel zooms at the cursor.
- Drag with the middle button to pan. Double-click it to zoom to the drawing.
- ZOOM has Extents, Window, Previous and factor (`2x`). PAN pans with the left button.

**Files**
- **Feather CAD drawing (`.fcad`):** a readable text file, saved safely so
  that a crash never leaves a half-written file.
- **Export DXF (R12):** opens in AutoCAD, LibreCAD, DraftSight, QCAD,
  Inkscape and others.
- **Auto-save with version history:** a copy is kept every few minutes while
  you work and each time you save.
  - File > Version history restores any earlier version. Undo takes the
    restore back.
  - After a crash, Feather CAD offers to recover drawings that were not saved.
- Recent files, and drag-and-drop to open a drawing.
- Drawing units (mm, cm, m, inches, feet) and decimal places. New drawings
  are in millimetres.

**Inquiry**
- DIST: distance, angle and ΔX/ΔY between two points.
- ID: the coordinates of a point.

## Phase B1: editing (version 1.1)

| Command | Short name | What it does |
|---------|------------|--------------|
| TRIM | TR | Click the part to cut away. It is removed between the nearest crossings, shown in red before you click. Every object is a cutting edge, or choose particular ones (T). Shift+click extends instead. Undo option. |
| EXTEND | EX | Click near the end to lengthen; it grows to the next object in the way. Shift+click trims instead. |
| OFFSET | O | A parallel copy at a distance, or through a point. Works on lines, arcs, circles and polylines; polyline corners are extended or cut back so they stay sharp. |
| FILLET | F | Rounds the corner between lines, arcs and circles, picked near the parts to keep. Radius 0, or Shift+click, makes a sharp corner. Polyline option for all corners; Trim/No trim; Multiple. |
| CHAMFER | CHA | Bevels the corner between two lines, by two distances or by a length and an angle. Polyline option. |
| MIRROR | MI | Mirrors about a line, keeping or erasing the originals. |
| SCALE | SC | Scales by a factor or by reference lengths, with a copy option. |
| STRETCH | S | Moves the points inside a crossing window; objects completely inside move whole. |
| BREAK | BR | Removes the part between two points. BREAKATPOINT splits an object in two. |
| JOIN | J | Joins lines, arcs and polylines whose ends meet into one polyline. Straight runs become one line; arcs of one circle become one arc or a circle. |
| EXPLODE | X | Splits polylines into lines and arcs, and arrays into separate copies. |
| LENGTHEN | LEN | Changes length by a delta, a percentage or to a total length. Picking an object shows its length. |
| ARRAY | AR | ★ Associative arrays: rectangular (rows and columns), polar (around a centre, items turned or not), path (along a curve, aligned or not). The array stays one object, so moving, rotating, scaling or mirroring it moves every copy. |
| ARRAYEDIT | | Changes the counts, spacing, fill angle or alignment of an array; its Explode option makes the copies separate objects. |

★ = usually a paid feature in other CAD programs.

Arrays are saved in the `.fcad` file. They are exported to DXF as their separate copies.

## Phase B2: faster editing and more shapes (version 1.2)

**Grips**
- Select objects (up to 100) to see their grips: end points, midpoints, centres, quadrants, vertices and fit points.
- Click a grip to edit with it, without starting a command:

| Grip | What dragging it does |
|------|-----------------------|
| Line end | Stretches the line |
| Line middle | Moves the line |
| Circle quadrant | Changes the radius |
| Arc end or middle | Reshapes the arc through it |
| Polyline segment middle | Moves a straight segment, or bends an arc segment |
| Ellipse axis end | Changes that axis |

- Grips that sit on the same point move together, so the shared ends of joined lines stay connected.
- Press **Enter** to switch between STRETCH, MOVE, ROTATE, SCALE and MIRROR.
- Options: **Base point**, **Copy** (keep the original and make several copies), **Undo** and **eXit**.

**Tracking and input**

| Feature | What it does |
|---------|--------------|
| Polar tracking (F10) | From the last point, the cursor is drawn onto rays every 15° (the step can be changed). A green dashed line and a tooltip (*Polar: 50.00 < 30°*) show it. Ortho and polar switch each other off. |
| Object snap tracking (F11) | Rest the cursor on a snap point for half a second to pick it up (a small green cross); rest on it again to drop it. The cursor then lines up horizontally, vertically or at the polar angles with those points, and snaps to where two such lines cross. |
| Dynamic input (F12) | The prompt and what you type show at the cursor, together with the length and angle from the last point. Type a length and press **Tab** to type the angle (it becomes `@length<angle`). |

**New objects**

| Command | Short name | What it draws |
|---------|------------|---------------|
| ELLIPSE | EL | An ellipse by its axis ends, by its centre, or an elliptical arc |
| POLYGON | POL | A regular polygon, 3 to 1024 sides: inscribed or circumscribed about a circle, or by one edge |
| SPLINE | SPL | A smooth curve through the points you click (Close and Undo options) |
| POINT | PO | Point markers |

- Ellipses can be trimmed and broken into elliptical arcs.
- All the new objects move, rotate, scale, mirror and stretch, and have grips.
- DXF export writes ellipses and splines as polylines that follow the curve, and points as POINT.

**Selecting**
- ★ **Quick select** (Edit menu): select by object type and property. The properties are length, radius, diameter, area, centre X/Y, number of vertices and closed. Compare with =, <>, > or <. The result replaces the selection, adds to it or is removed from it.
- ★ **Select similar**: every object of the same kind as the selected ones.

## Version 1.3: dimensions, drawing tabs and DXF import

**Dimensions**

| Command | Short name | What it does |
|---------|------------|--------------|
| DIM | SD | ★ **Smart dimension.** Click a line, circle or arc and the right dimension follows the cursor. A line gives its length: horizontal above or below it, vertical beside it, aligned along a slanted line. A circle gives its diameter, an arc its radius (R and D switch). Click a second line instead of a place for the angle between them. P dimensions between two points, A starts an angle. It keeps going until Enter. |
| DIMLINEAR | DLI | Horizontal or vertical distance between two points (Enter: pick a line) |
| DIMALIGNED | DAL | Distance along the line between two points |
| DIMRADIUS / DIMDIAMETER | DRA / DDI | Radius or diameter of an arc, circle or polyline arc |
| DIMANGULAR | DAN | Angle between two lines, of an arc, or by a vertex and two points (Enter) |
| DIMCONTINUE | DCO | A chain of dimensions, each from the end of the last |
| DIMBASELINE | DBA | A stack of dimensions, all from the same first point |
| DIMEDIT | DED | A new value or new text for a dimension |
| DIMSTYLE | D | Text height, arrow size, extension lines, decimal places and trailing zeros |

- ★ **Driving dimensions.** While placing a dimension, type a number and the object becomes that size. Double-click a dimension (or use DIMEDIT) to give it a new value later. For a length, the part of the shape beyond the middle moves, so a rectangle stays a rectangle and a rounded corner keeps its shape. For a radius or diameter, the circle or arc changes size. For an angle, the second line turns about the corner.
- ★ **Associative dimensions.** A dimension remembers which ends, vertices or circles it measures. Stretch, move, trim or turn them and the dimension follows. A dimension moved on its own stops following.
- Dimensions are objects like the others: select, move, copy, rotate, mirror, scale, stretch and erase them. Their grips move the measured points and the text. Copies made together with their objects stay attached to the copies.
- The double-click dialog also changes a dimension's text and decimal places. `<>` in the text stands for the value, as in `<> TYP` or `4x <>`.

**Drawing tabs**
- Every drawing opens in a tab of its own, with a * when it has unsaved changes. Ctrl+N or the + button starts a new one, Ctrl+Tab and Ctrl+Shift+Tab switch, Ctrl+W (or the tab's ×, or a middle click) closes.
- Each drawing keeps its own view, undo history, auto-saved versions and selection. Closing the program asks about every drawing with unsaved changes.
- Copy and paste between drawings: Ctrl+C, Ctrl+X, Ctrl+V. Dimensions pasted with their objects stay attached.

**DXF import**
- File > Open (or drag and drop) opens `.dxf` files from AutoCAD, LibreCAD, DraftSight, QCAD, Inkscape and others, of any release (ASCII DXF), in a new tab. Saving asks for a `.fcad` name.
- File > Import DXF brings a DXF drawing's objects into the current drawing.
- Brought in: lines, circles, arcs, polylines (with arcs), ellipses, splines, points, solids, leaders and dimensions (as real dimensions). Blocks come in as their objects, placed, scaled and turned as inserted (also rows and columns of them). Units and the dimension style come from the file. Paper space is left out.
- Not brought in yet: text and hatching (Phase C). The command line says how many were left out.
- Export DXF writes dimensions as what they look like: lines, filled arrows and text.

## Coming in later phases

| Phase | Contents |
|-------|----------|
| B3    | Layers, line types, properties panel, match properties |
| C     | Annotation: text, leaders, hatching and blocks |
| D     | Sheets, printing and PDF |
| E     | Engineering (constraints, GD&T, parts lists) |
| F     | Professional extras |

## Building

```
cmake -S . -B build -A x64 -DFEATHERCAD_TESTS=ON
cmake --build build --config Release
build\Release\CoreTests.exe
```

You can also cross-compile from Linux with MinGW-w64:

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
```

**Source layout**
- The drawing model and the command engine have no Windows code:
  `Geometry`, `Entity`, `Spline`, `Curve`, `Modify`, `Grips`, `Select`,
  `Dimension`, `Document`, `DxfImport`, `Snap`, `Engine`, `Commands`,
  `CommandsModify`, `CommandsDraw` and `CommandsDim`.
- The unit tests (`tests/CoreTests.cpp`) drive the engine by typed input
  alone.
- The window side is `MainWindow`, `Canvas` (Direct2D), `TabBar`,
  `CommandLine` and `StatusBar`.
