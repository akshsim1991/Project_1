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

## Version 1.3.1: fixes and smaller additions

| What | How it works now |
|------|------------------|
| Fillet radius | The first prompt shows the radius and takes a number: type `5` and pick two objects. With radius 0 a message says why nothing changed (the corner is already sharp). Chamfer works the same way (a number sets both distances). |
| Fillet and Chamfer with polylines | A polyline's straight segment can be filleted or chamfered with a separate line, arc or circle. An end segment of an open polyline is cut back or extended; a closed polyline (a rectangle) is left whole and the other object is trimmed. |
| Tangent line (TANLINE, TL) | A line touching two circles or arcs. Click near where it should touch each: picks on the same side give an outside tangent, on opposite sides one crossing between them. |
| Circle tangent, tangent, radius | CIRCLE, then T: pick two lines, circles or arcs and type the radius. |
| Distance between parallel edges | In DIM (and DLI, DAL after picking a line), click a parallel line instead of a place: the dimension measures straight across and stays attached to both lines. |
| Snap letters | The status bar has **E** (endpoint), **M** (midpoint), **I** (intersection) and **C** (centre) next to OSNAP: click one to turn that snap on or off. Right-click for all the snaps. |
| Splines | Trim, break and offset work on splines. A trimmed spline keeps its fit points and remembers the part kept, so it can still be reshaped with its grips. |
| Ellipses | Offset works on ellipses and elliptical arcs (the offset is a spline, as in other CAD programs). |
| Lengthen | New option DYnamic: pick near an end and drag it along the line or round the arc. |
| Trim and Extend | Choosing edges (T or B) and pressing Enter without any: every object is an edge again. |
| Object snap tracking | No longer picks up points while a value is being typed. |

## Version 1.4 (Phase B3): layers, line types and properties

**Layers** (LA, or the Layers button)
- Every object is on a layer. Layers can be switched off, ★ frozen (hidden and left out of everything), locked (shown faded; they can be snapped to and measured, but not selected or changed), and kept from printing.
- Each layer has a colour, line type and line weight that its objects use "by layer".
- The layer box on the tool bar shows the current layer; choosing a layer there puts the selected objects on it (or makes it current when nothing is selected).
- ★ **Layer states** save which layers are on, frozen and locked and their colours and line types, under a name, to bring back later.
- Layer changes can be undone.

**Colours, line types, line weights**
- Each object can have its own colour (the nine standard colours, or any colour), line type and line weight, or take them from its layer.
- Line types: CONTINUOUS, DASHED, HIDDEN, CENTER, PHANTOM, DOT, DASHDOT, DIVIDE, BORDER and half-size ones, as in AutoCAD. LTSCALE makes all the patterns larger or smaller; each object also has its own scale.
- **LWT** on the status bar shows the line weights.

**Properties panel** (Ctrl+1, or PR)
- On the right of the drawing. With nothing selected it sets the layer, colour, line type and weight for new objects.
- With objects selected it changes theirs. For one object it shows its sizes, to read and to change: end points, length and angle of a line; centre, radius and diameter of a circle; angles of an arc; size of an ellipse; area and length of a polyline; text, text height and decimals of a dimension.

**More**

| Command | Short name | What it does |
|---------|------------|--------------|
| MATCHPROP | MA | Copies the layer, colour, line type and weight of one object onto others (and the dimension style between dimensions). |
| CONSTRUCTION | CONS | Makes objects construction geometry: reference lines shown faint and dotted, for drawing, not for printing. Again: normal. |
| OFFSET, B | | Offsets on both sides at once (symmetric offset). |
| ORIGIN | UCS | Moves the grid origin: coordinates are typed and shown from there, the grid and snap start there. Reset puts it back at 0,0. |
| LTSCALE | LTS | The size of line type patterns. |

- Copies, trims, explodes and other edits keep the objects' layer and look. Paste puts objects on the layers of the same names in the other drawing (made if missing).
- DXF export and import keep layers (on, frozen, locked), colours, line types and line type scale. Construction geometry goes on AutoCAD's non-printing Defpoints layer.

## Version 1.5 (Phase C): text, leaders, hatching and blocks

**Text** (T, also MTEXT)
- One or several lines, any font on the PC, any height, angle, width and line spacing, and nine ways to justify it (top left to bottom right). A frame (box or circle) can go round it.
- Symbols: buttons in the editor for ° Ø ± ℄ ² ³ µ Ω ≈ ≤ ≥ ∆ × √, or the AutoCAD codes %%d, %%c and %%p.
- ★ **Fields**: {date}, {time}, {filename} and {author} in a text show today's date, the drawing's name and so on, and stay up to date.
- Double-click a text (or ED) to change it. The Properties panel shows its content, font, height, angle and frame.

**Leaders and balloons** (LE, BALLOON)
- An arrow and lines to a note; the note can sit in a box or in a circle (an item balloon).

**Centre marks and centre lines** (CM, CL)
- Centre lines across circles and arcs, or midway between two lines, in the CENTER line type.

**Hatching and fill** (H, FILL)
- Click inside an area: Feather CAD finds its outline, with the islands inside it left empty (holes, text boxes). Or select closed objects.
- Patterns ANSI31 to ANSI38, NET, LINE, DOTS, BRICK, CROSS, EARTH, HONEY, and SOLID fill; scale and angle.
- ★ **Associative**: the hatch follows its outline when that is stretched, trimmed or moved. Moved on its own, it lets go.

**Blocks** (B, I, ATT, ATE, PU)
- BLOCK makes a named block from objects (with a base point); INSERT places it again, at any scale and angle. Changing a block's definition (BLOCK with the same name) changes every copy.
- **Attributes** (ATTDEF): values asked for each time a block is placed, such as a part number or size. ATTEDIT, or the Properties panel, changes them. EXPLODE takes a block apart; PURGE removes unused ones.
- Objects on layer 0 and "by block" colours take the colour and layer of each copy.
- (★ Dynamic blocks, with stretch and flip grips, come in Phase F.)

**More dimensions**

| Command | Short name | What it does |
|---------|------------|--------------|
| DIMORDINATE | DOR | Ordinate (coordinate) dimension: the X or Y of a point from the origin. |
| DIMANGCOORD | DAC | ★ Angular coordinate dimension: the angle of points about a centre, from a 0° direction. |
| DIMDRIVEN | DDR | Driven dimensions only report (shown in brackets); they never change the drawing. Again: driving. Also in the Properties panel. |
| DIMSPACE | DSP | Spaces parallel dimensions evenly from a base one (Auto, or a distance). |

**Also**
- ★ **Revision clouds** (RC): round a rectangle, a polygon or an existing closed object.
- **Area** (AA): area and perimeter of picked points, a closed object, or the area around a point (arcs measured exactly), with a running total (Add / Subtract).
- DXF import now brings in text (single and multi-line, with fonts), hatches (patterns and solid fills) and blocks with their attributes, kept as blocks. DXF export writes text, blocks with attributes, solid fills and hatch patterns.

## Version 1.6 (Phase D): sheets, printing and PDF

**Sheets** (the tabs under the drawing; + for a new one)
- A sheet is a page of paper: A4 to A0, Letter, Legal, ANSI B to E or any size, portrait or landscape, with a margin.
- A new sheet comes with an ISO-style **border and title block** and a viewport showing the whole drawing at a usual scale (1:1, 1:2, 1:5... or 2:1, 5:1...).
- The title block fills itself in: title, drawing number, drawn by, checked by, company, material and revision come from **File > Drawing properties**; the date, file name, scale and "sheet 2 / 3" are worked out. Any text can use these fields too ({title}, {scale}, {sheet}...).
- Notes, dimensions and other objects can be drawn on the sheet itself, in millimetres.
- Right-click a sheet tab: page setup (name, paper, orientation, margin, printing), border and title block again, print, delete.

**Viewports**

| Command | Short name | What it does |
|---------|------------|--------------|
| MVIEW | MV | A viewport: two corners, then a scale (1:2, 2:1...) or Fit. Several on one sheet, each at its own scale. |
| VPSCALE | VPS | Changes a viewport's scale. |
| VPPAN | VPP | Moves the drawing inside a viewport. |
| VPLOCK | | ★ Locks viewports so their scale and view cannot change by mistake. |

Viewports can also be resized with their corner grips, and their scale, view centre and lock changed in the Properties panel.

**Printing** (Ctrl+P)
- Print this sheet, all sheets, or the drawing itself (as on screen, or all of it) on any paper at a scale or fitted.
- A preview of every page; colour, black (monochrome) or shades of grey; line weights on or off; fit to the printer's paper or real size.
- **Pens**: a line weight for each of the nine standard colours, as with AutoCAD's colour-dependent plot styles (page setup).
- Viewport edges and construction geometry are not printed, nor layers set not to print.

**PDF** (File > Export PDF)
- ★ All the sheets in one PDF, one page each, at their real size: lines, fills and text stay sharp at any zoom.

**DXF**
- The first sheet is written as AutoCAD's paper space, with its viewports (DXF R12 has one paper space). Paper space layouts in DXF files come in as sheets.

## Version 1.7 (Phase E): relationships, variables and engineering symbols

**Relationships** (Relate menu) ★
Relationships say how objects belong together, as in Solid Edge's 2D sketching. They are kept as the drawing changes: drag an end, change a dimension or a radius, and the related objects follow, while the ones just edited stay where they were put.

| Command | Short name | What it keeps |
|---------|------------|---------------|
| COINCIDENT | COIN, CONNECT | A point (an end, centre or midpoint) on another point, or on a line, circle or arc |
| HORIZONTAL / VERTICAL | HOR / VER | A line level or upright |
| PARALLEL / PERPENDICULAR | PAR / PERP | Two lines parallel or at right angles (the second one turns) |
| COLLINEAR | COLL | Two lines on one straight line |
| TANGENT | TANG | A line and a circle or arc, or two circles, touching (on the side they are now) |
| CONCENTRIC | CONC | Circles and arcs on one centre |
| EQUAL | EQ | Two lines the same length, or circles and arcs the same radius |
| MIDPOINT | MIDP | A point on the middle of a line |
| SYMMETRIC | SYMM | Two points mirror images about a line |
| FIX | | An object stays where it is; the others move around it |

- Small orange marks show the relationships (H, V, a box for connect, symbols for parallel and the others). **Relate > Show relationships** hides them; they never print.
- **Add relationships while drawing** (AUTOREL): lines drawn get horizontal, vertical and connect relationships by themselves. On in new drawings since 2.1.
- **Maintain relationships** (MAINTAINREL) turns the keeping off and on.
- Erasing an object takes its relationships with it. When relationships contradict each other the command line says so and nothing jumps.

**Locked dimensions, variables and formulas** ★
- **DIMLOCK** (DLO), or "Locked" in the Properties panel: a dimension that holds its value. Typing a new value in a locked dimension moves its objects, together with everything related to them.
- Every dimension gets a name (D1, D2...), shown and changed in the Properties panel.
- **Variables** (Relate > Variables, or VAR on the command line): named values and formulas such as `Width = 120`, `Hole = Width / 4`, and dimension formulas such as `D3 = 2 * D1 + 5`. Formulas may use + - * / ^, brackets, sin cos tan (degrees), sqrt, abs, round, min, max and pi. When a variable changes, every dimension that uses it changes its objects.
- **GOALSEEK** (GS): pick the dimension that should reach a value, type the value, then pick the dimension (or name the variable) that may change. Feather CAD finds the value that gives it, through all the relationships and formulas.

**Engineering symbols** (Annotate menu)

| Command | Short name | What it makes |
|---------|------------|---------------|
| FCF | GDT, TOL | ★ A **feature control frame**: the characteristic symbol (straightness, flatness, circularity, cylindricity, profile, angularity, perpendicularity, parallelism, position, concentricity, symmetry, runout, total runout), the tolerance with %%c and circled modifiers such as (M), and datums A, B, C. Several rows make a composite frame. On its own or on a leader. |
| DATUM | | A datum feature symbol: a letter in a box on a filled triangle (the next free letter is offered). |
| SURFACE | ROUGHNESS | A surface texture symbol (any process, material removal required or not allowed) with its roughness, such as Ra 1.6. |
| WELD | | A weld symbol on a leader: fillet, square, V, bevel, plug or spot, on the arrow side, the other side or both, with size, length, all round. |
| PARTSLIST | BOM | ★ A **parts list**: the blocks placed in the drawing, counted. The part number comes from a PN (or PART) attribute, or else the block's name; the description from DESC and the material from MATERIAL. Run it again to bring the table up to date. On a sheet it lists the parts in the drawing. |

The symbols are text objects with a frame: their contents can be changed like any text (double-click, or the Properties panel), and they go to DXF and PDF as lines and text.

## Version 1.7.1: easier to use

- **The guide strip** above the drawing always says, in plain words, what to do next ("Click the first point", "Type the offset distance"). While a command runs, its choices are buttons (Close, Undo, 3 points...), with **Done** (the same as Enter) and **Cancel** (the same as Esc), so nothing has to be typed or remembered.
- With objects selected, the strip offers **Move, Copy, Rotate, Mirror, Scale, Erase** and **Clear selection**.
- **Colours**: eleven colour swatches on the strip (and More... for any colour). Click one to colour the selected objects, or, with nothing selected, the objects drawn next.
- **Thickness**: the Thickness button sets how thick lines are, from very thin (0.13 mm) to extra heavy (2 mm), for the selection or for new objects. Line thickness now shows on screen from the start, each step clearly thicker than the one before (it used to need LWT turned on, and thin weights all looked alike).
- The cursor tip reads "Length 42.00  Angle 30.00°"; the status bar buttons have names (Obj snap, End, Mid, Cross, Centre, Track...) and explain themselves when the mouse rests on them.
- Prompts say "Click" and "Type" instead of "Specify" and "Enter"; Trim and Extend explain what a click does.

## Version 1.8 (Phase F): FreeSketch, adjustable blocks, tables and pictures

**FreeSketch** ★ (Draw > FreeSketch, or FS)
- Hold the left mouse button down and draw. Each stroke is tidied up: a wobbly line becomes a straight line (exactly level or upright when it nearly is), a round stroke a circle, a bend an arc, and a stroke with corners a polyline - a rectangle drawn in one go comes out square.
- Ends that finish near another object's end, middle or centre join it exactly. With Relate > Add relationships while drawing, the lines get their horizontal, vertical and connect relationships too.
- Done (or Enter) finishes; each stroke can be undone on its own.

**Adjustable (dynamic) blocks** ★ (Annotate > Make a block adjustable, or BPARAM)
- Pick a placed block, then **Stretch**: click the point that stays, the point that moves, and draw a box round the part that moves; give it a name (Length) and, if wanted, the sizes allowed (50 75 100).
- Or **Flip**: two points on the line it flips about.
- Select the block: drag its stretch grip (it jumps to the nearest size allowed) or click its flip grip. The values are in the Properties panel too (a list of the sizes). Every copy of the block has its own values.

**Tables** (Annotate > Table, or TB)
- Columns and rows, placed by their top left corner. Double-click the table to type in it: one row per line, cells separated by |. A table copied from a spreadsheet can be pasted straight in. The first row is the heading.

**Part balloons** (Annotate > Part balloon, or PB)
- Pick a part (a placed block) and where the balloon goes: the balloon shows its item number in the parts list, and stays right when parts are added or the list is renumbered.

**Pictures** (Draw > Picture, or IMAGE)
- A photo or scan (PNG, JPEG, BMP, GIF, TIFF) placed by its corner and width, to trace over or to show on a sheet. Corner grips resize it keeping its shape. It prints and goes into PDFs; in DXF only its frame is written.

**1.8.1**: Spline, Polyline and Line close by clicking their first point again: near it, a green ring and "Click to close" show, with the closed shape previewed.

**Also**: the guide strip above the drawing (since 1.7.1) gives every command's choices as buttons - the "Options bar" planned for this phase.

## Version 1.9: helpers for projections of solids

Drawing projections (a prism resting on HP, its axis inclined to HP and VP...) by the usual steps, faster:

| Command | Short name | What it does |
|---------|------------|--------------|
| PROJECTOR | PJ, XL | Click points: a faint construction line (not printed) runs through each, right across the drawing - upright (to carry a point from the top view to the front view), level, both, or at any angle (45 for a mitre line). Points snap to where projectors cross (Cross snap). |
| LABEL | LB | Click corners to letter them: a, b, c... follow on by themselves. Type a label first to change it: digits after a letter are written small and low, ' is a prime, so a1' shows as a with a small 1 and a prime, and [c1'] marks a hidden point. Each label sits beside its corner where the drawing leaves room, and moves with the corner. |
| HIDDEN | HID | Click an edge to make it a hidden (dashed) line; click it again to make it visible. Objects selected first are all changed at once. |

**1.9.1**: relationships keep shapes whole. Before a relationship is added (or a dimension locked), the shape gets the relationships it already has: a connect at every corner where objects meet - all through the shape of joined lines, arcs and polylines - and a rectangle (RECTANGLE) keeps its right angles. So Parallel or Equal on a rectangle drawn with LINE no longer pulls its corners apart, and Equal on two sides of a RECTANGLE makes a square. The command line says how many were added; Undo takes them back with the relationship.

**Worked example**: `samples/Prism-axis-45-to-HP-edge-30-to-VP.fcad` (and its PDF) - a square prism, 35 mm base and 65 mm axis, resting on HP on an edge of its base inclined to VP at 30 deg, the axis inclined to HP at 45 deg: the three stages with projectors, every corner labelled, hidden edges dashed, and the 35, 65, 45 deg and 30 deg dimensions.

The toolbar has a Point button before Line, and a group with Projector, Label points and Hidden line. On a narrow window, the toolbar buttons that do not fit are in the » menu at its right end.

## Version 2.1: relationships that behave

- **Regular polygons stay regular.** When a relationship is added to a POLYGON (or a dimension on it is locked), it gets a Regular polygon relationship (a small hexagon mark in its middle): all sides and corners stay equal, so dragging a corner or changing a side's length resizes the whole polygon. A rectangle (four sides) keeps its right angles, as before.
- **Relationships while drawing are on in new drawings** (Relate > Add relationships while drawing): lines drawn get horizontal, vertical and connect relationships, so a rectangle drawn with LINE moves and stretches as one shape. Drawings saved earlier keep their own setting.
- **Trim, Extend, Fillet, Chamfer, Break, Lengthen and Join** no longer fight the relationships they deliberately break: a corner that is cut or rounded lets go of its connect instead of being pulled back together (and dragging the shape with it), and a locked dimension on a line that is cut takes the new length. A line broken in two passes its corner's connect to the piece left at the corner. With relationships while drawing on, a fillet arc is joined and tangent to both lines, and a chamfer edge is joined to both.
- **Relationship marks stay one size on the screen**, whatever the zoom: readable when zoomed out, not huge when zoomed in.

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
