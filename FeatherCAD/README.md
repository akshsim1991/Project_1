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

## Coming in later phases

| Phase | Contents |
|-------|----------|
| B2    | Grips, polar and object snap tracking, on-cursor input, ellipse, polygon, spline, Quick Select |
| B3    | Layers, line types, properties panel, DXF import, match properties |
| C     | Annotation: text, dimensions, hatching and blocks |
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
  `Geometry`, `Entity`, `Curve`, `Modify`, `Document`, `Snap`, `Engine`,
  `Commands` and `CommandsModify`.
- The unit tests (`tests/CoreTests.cpp`) drive the engine by typed input
  alone.
- The window side is `MainWindow`, `Canvas` (Direct2D), `CommandLine` and
  `StatusBar`.
