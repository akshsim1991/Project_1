# PneuSim 3.0 – Pneumatic, Electro-pneumatic & Hydraulic Circuit Simulator

A free, FluidSIM-style desktop application for designing, simulating, checking and learning
pneumatic, electro-pneumatic and hydraulic circuits. Written in VB.NET (Windows Forms,
.NET Framework 4.7.2).

(c) 2026 Akshaya Simha. Developed in VB.NET initially, and improved it with vibe coding using
Claude Pro. Software is used to study pneumatic and hydraulic systems, so that it can be
simulated to understand different types of circuits.

![A circuit designed by the generator from "A+ B+ B- A-", running](docs/generator-electro.png)

## What's new in 3.0

- **Check my circuit.** A live checker lists problems while you draw: open ports, missing
  exhausts, no air supply, cylinders that can never move, solenoids without a coil, short
  circuits, and **signal overlap** (two opposing signals on one valve). For an overlap, one
  click redesigns the circuit as a pneumatic cascade or an electro-pneumatic relay step chain.
  *Run full check* also test-runs the circuit.
- **Explain my circuit.** A plain-English description: what is in the circuit, how each
  actuator is controlled, the motion sequence, and a step-by-step story of what happens when
  you press a button.
- **Realistic physics mode** (toolbar *Realistic*): pressure builds up in the cylinder
  chambers, flow follows ISO 6358, cylinders move according to bore, rod, load, friction and
  cushioning, pressure sequence valves switch at the real pressure, and the status bar shows
  air consumption per cycle and the running cost in ₹ per year.
- **Hydraulics:** pump, tank, pressure relief valve, 4/3 valves (closed, tandem, float, open
  centre), 4/2 valve, pressure-compensated flow control, counterbalance valve, hydraulic
  cylinder and motor, accumulator and pressure gauge (0–160 bar). Oil lines turn orange.
- **Learn:** 10 lessons whose tasks are checked automatically, an animated valve cutaway
  (F7), hover help for every component, a practice quiz and a timed exam.
- **Measure and report:** a plotter (position, speed, chamber pressures, valve positions,
  air consumption) with cycle time, force and valve-size calculators, a parts list with
  costs, and a PDF report with a title block.
- **Drawing and projects:** several pages per project with page connectors and
  cross-references (e.g. relay contacts show where their coil is), SVG and DXF export for
  CAD, animated GIF recording, and dark mode.
- **Installer** with a desktop icon and *.pneu* file association.

![Plotter, cycle time and air cost](docs/plotter-and-cost.png)

## Highlights from earlier versions

- **Automatic circuit generator.** Type a motion sequence such as `A+ B+ B- A-` (or
  `A+ (B+ C+) B- C- A-` with simultaneous moves). PneuSim designs, draws and explains the
  complete circuit, as an electro-pneumatic relay step chain or a pneumatic cascade.
- **Electro-pneumatics:** +24 V / 0 V, push buttons, selector switches, relay contacts,
  proximity sensors, limit switches, relays, timer relays, valve solenoids and lamps.
- **Live simulation** with a displacement-step diagram, and a full editor (undo/redo,
  copy/paste, box selection, rotate, tube bend points and junctions, zoom, PNG export).

![Dark mode](docs/dark-mode.png)

## Components

| Group | Components |
|---|---|
| Supply and air preparation | Compressed air supply, service unit, pressure regulator, pressure gauge, silencer, tube junction |
| Actuators | Single-acting cylinder, double-acting cylinder, semi-rotary actuator, air motor |
| Directional control valves | 2/2, 3/2 (NC/NO), 4/2, 5/2 and 5/3 (closed, exhaust or pressure centre) valves, operated by push button, selector switch, roller lever, pneumatic pilot, time-delayed pilot or solenoid; spring, pilot or solenoid return |
| Logic, non-return and flow | Shuttle valve (OR), two-pressure valve (AND), check valve, quick exhaust valve, one-way flow control valve, flow control valve |
| Electrical (24 V DC) | +24 V / 0 V connection, push button NO/NC, selector switch, relay contact NO/NC, proximity sensor, limit switch, relay coil, on-delay and off-delay timer relays, valve solenoid, indicator lamp, wire junction |
| Hydraulics | Pump, tank, pressure relief valve, 4/3 valve (closed, tandem, float, open centre), 4/2 valve, pressure-compensated flow control valve, counterbalance valve, hydraulic cylinder, hydraulic motor, accumulator, hydraulic gauge |
| Pressure control | Pressure regulator, pressure sequence valve |
| Drawing | Text notes, page connectors |

## Examples (menu *Examples*)

1. Direct control of a single-acting cylinder
2. Indirect control of a double-acting cylinder with speed control
3. OR: operation from two places (shuttle valve)
4. AND: two-hand safety control (two-pressure valve)
5. Automatic reciprocation with limit valves
6. Delayed return with a time delay valve
7. Electro-pneumatic: direct control with a solenoid valve
8. Electro-pneumatic: self-holding circuit with start / stop and lamp
9. Hydraulics: cylinder with 4/3 valve, relief valve and gauge
10. Realistic mode: pressure sequence valve (clamp, then drill)

![Cascade circuit generated from "A+ B+ C+ C- B- A-"](docs/generator-cascade.png)

## Using it

| Task | How |
|---|---|
| Place a component | Click it in the library and click on the drawing, or drag it onto the drawing |
| Connect | Drag from one port (circle) to another. Red circles are electrical terminals |
| Select several | Drag a box on empty space; Ctrl+click adds or removes |
| Edit | Properties panel on the right |
| Undo / redo | Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z), wherever the focus is |
| Copy / cut / paste / duplicate | Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+D |
| Rotate / delete / nudge | R / Del / arrow keys |
| Move a tube | Drag its middle segment |
| Zoom | Ctrl + mouse wheel, Ctrl+9 zooms to fit |
| Simulate | F9 start, F10 pause, F11 stop and reset; click push buttons and switches |
| Generate a circuit | Tools → Circuit Generator (Ctrl+G) |
| Check / explain | Bottom tabs *Check circuit* and *Explain* |
| Realistic physics | Toolbar *Realistic* (bore, load and friction are cylinder properties) |
| Several pages | Page menu; join pages with page connectors of the same name |
| Export | File → Export: PDF report, SVG, DXF, PNG, parts list (CSV) |
| Record | Toolbar *Record GIF*, operate the circuit, click again to save |
| Learn | Learn menu: lessons, quiz, timed exam, valve cutaway (F7) |
| Dark mode | View → Dark mode |

Names link things together:
- A valve's *solenoid label* (e.g. `1M1`) matches a *Valve solenoid* coil.
- A relay contact's *Reference* (e.g. `K1`) matches a relay coil.
- A roller valve, proximity sensor or limit switch reacts to a cylinder's *position mark*
  (e.g. `1S2` or `1B2`).

## Building

Open `PneuSim.vbproj` in **Visual Studio 2022** and press **F5**, or build from the command line:

```
dotnet build PneuSim/PneuSim.vbproj -c Release
```

The output is `PneuSim\bin\Release\net472\PneuSim.exe`. A ready-to-run build is included as
`PneuSim_App.zip` at the top of the repository.

The GitHub workflow *PneuSim* (Actions tab) builds the program, runs the tests and publishes
two downloads: **PneuSim-Windows** (the program folder) and **PneuSim-Setup** (the installer,
made with Inno Setup from `installer/PneuSim.iss`).

### Code signing

Windows SmartScreen warns about any program that is not signed with a code-signing
certificate. The workflow signs the EXE and the installer automatically when two repository
secrets exist: `SIGNING_CERT_BASE64` (the .pfx file as base64) and `SIGNING_CERT_PASSWORD`.
A certificate has to be bought from a certificate authority; without one the program works
the same, and you click *More info → Run anyway* the first time.

## How the simulation works

In **realistic mode** each cylinder chamber holds a mass of air; flow through each path
follows ISO 6358 (sonic conductance and critical pressure ratio), the piston force comes from
the chamber pressures, load, friction and spring, and the motion is integrated in 0.2 ms
steps. In **hydraulics**, cylinder speed is pump flow divided by piston area, the pressure is
set by the load, and the relief valve limits it. The ideal (default) mode works as follows:

- Every port is a node. Tubes, wires, open valve passages and closed contacts are edges.
  An edge can allow flow in one direction only (check valve, quick exhaust valve) and can limit
  the pressure it passes on (regulator).
- Ports reachable from a supply (or +24 V) are **pressurized** (or live). Ports that can vent to
  an open exhaust (or reach 0 V) are **exhausted**. All other ports keep their trapped pressure.
- Valves, relays and contacts then switch: pilot pressure ≥ 1.5 bar, energized coils, buttons,
  rollers and timers. The network is solved again until nothing changes.
- Flow control valves reduce the capacity of their passage, which can depend on direction.
  A cylinder's speed is set by the narrowest point on its supply path and on its exhaust path,
  so meter-in and meter-out control behave as in a real circuit.
- Double-acting cylinders compare the forces on both sides (the rod side has 70 % of the
  piston area). A cylinder whose outlet air is trapped cannot move.

## Project layout

```
PneuSim/
  Core/        Circuit model, ports, tubes, projects, simulator (ideal, realistic, hydraulic),
               drawing surfaces, SVG / DXF / PDF export
  Elements/    Supply, cylinders, directional valves, logic/flow valves, extras, electrical
  Generator/   Sequence parser, circuit generator (relay step chain, cascade), dialog
  Learning/    Lessons, question bank, component help
  Tools/       Circuit checker and explainer, parts list, PDF report, GIF recorder
  UI/          Main window, canvas, diagram, plotter, dialogs, library, examples
  installer/   Inno Setup script
```

To add a component: derive from `CircuitElement`, add ports in the constructor, draw it in
`DrawSymbol`, and add its passages in `AddEdges`. Then register it in
`Elements/ElementFactory.vb` and in `UI/Library.vb`.
