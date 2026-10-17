# CurveForge

Fit curves to your data quickly and professionally: 20 built-in models or
your own equation, with parameter errors, confidence and prediction bands,
residuals, a best-fit ranking, predictions, batch fitting and reports.

© 2026 Akshaya Simha. Developed for faster experience.

CurveForge replaces the original VB.NET "Curve Fitter" (`Curve Fitter.zip`).
That app could open a CSV but fitted only a cubic polynomial to columns
that had to be named exactly `X_Column` and `Y_Column`, and it never showed
the result. CurveForge is a single native executable (about 800 KB): no
.NET, no MathNet or other DLLs, and it starts instantly.

## Features

**Data**

* **Open CSV, TSV, TXT and DAT files.** These are detected automatically:
  * the separator (comma, semicolon, tab, `|` or spaces);
  * a header row;
  * decimal commas (`12,5`);
  * quoted values;
  * UTF-8, UTF-16 or ANSI text.

  Drag a file onto the window to open it, or pass it on the command line.
* **Paste from Excel** (Ctrl+V). Into an empty table, or with
  Ctrl+Shift+V, the clipboard becomes a new table with its column names.
  Otherwise the cells are pasted at the cursor.
* **Spreadsheet-like table:**
  * Arrows move between cells. Typing, Enter or F2 edits a cell; Enter or
    Tab confirms and moves on.
  * Delete clears cells. Insert adds a row and Ctrl+Delete deletes rows.
  * The last row is always empty, so typing there adds data.
  * Cells that are not numbers in the X, Y or σ column are shown in red.
* **Any columns as X and Y**, plus an optional **error column (σ)**: each
  point is then weighted by 1/σ².
* **Columns:**
  * Add, rename, delete and sort columns.
  * **Create a column from a formula**, such as `ln(Signal)`, `x*1000` or
    `(y - y_blank)/y_max`.
* **Leave points out:** click a point on the graph, or select rows and
  press Ctrl+E. Left-out points stay visible (hollow, crossed) and can be
  brought back the same way.
* **Undo and redo** (Ctrl+Z / Ctrl+Y) for every change: data, columns,
  model, left-out points and settings of the fit.

**Models**

| Group | Models |
|---|---|
| Lines and polynomials | Straight line, line through the origin, polynomial (degree 2–10), inverse (a + b/x) |
| Growth and decay | Exponential, exponential with offset, double exponential, logarithmic, power law |
| Peaks | Gaussian, Lorentzian, several Gaussian peaks (2–6) |
| S-curves and saturation | Logistic, dose-response (4PL), Hill, Michaelis-Menten |
| Waves | Sine, damped sine |
| Other | Cubic spline through every point, **your own equation** |

* **Your own equation:** type any equation in the equation box, for example
  `y = A*exp(-x/tau) + B`.
  * Every name other than `x` becomes a parameter.
  * Functions: `sin cos tan asin acos atan sinh cosh tanh exp ln log log10
    log2 sqrt abs sign erf floor ceil pow min max atan2`.
  * Constants: `pi`, `e`.
  * Implicit multiplication such as `2x` works.
* **Automatic starting values** for every built-in model are estimated from
  the data. Your own equations are tried from many starting points, so
  they usually fit without any setup.
* **Parameters** (Ctrl+P): set a starting value, fix a parameter to a known
  value, or limit its range (lowest/highest allowed).
* **Robust fitting:** outliers are ignored automatically (Tukey bisquare)
  and circled in red on the graph.
* The fit runs again automatically after every change. This can be turned
  off in Settings; then use Fit (F5).

**Results**

* The equation with the fitted values filled in, for example
  `y = 100.07*exp(-0.2347*x) + 5.23`, and what each parameter means.
* For each parameter: its value, standard error and confidence interval
  (90, 95 or 99%). Uncertain parameters are flagged.
* Goodness of fit:
  * R² and adjusted R²;
  * RMSE and the standard error of the fit;
  * sum of squares, AICc and BIC;
  * reduced χ² for weighted fits;
  * points used and degrees of freedom.
* Clear warnings, for example when the fit did not settle, when parameters
  depend too strongly on each other, or when a model needs x > 0.

**Graph**

* Data points, the fitted curve, the **confidence band** and the
  **prediction band**, with a **residual plot** underneath.
* Mouse controls:
  * the wheel zooms (Shift: only x, Ctrl: only y);
  * drag to move, Ctrl+drag to zoom into a box;
  * double-click or Ctrl+0 shows everything.
* Hover a point to see its row, x, y, fitted value and residual.
* Logarithmic x and/or y axes, and optional grid lines.
* The legend places itself in the emptiest corner.

**Best fit** (Ctrl+B)

Fits every suitable model and ranks them by AICc, which rewards a close fit
but penalises extra parameters. It shows ΔAICc, R², adjusted R², RMSE, the
number of parameters and notes. Double-click a model to use it.

**Predict and calculate** (Ctrl+R)

* y for any x, with confidence and prediction intervals and the slope.
* x for a given y: every place where the curve reaches it.
* Area under the curve between two x values, and the average value.
* A table of fitted values: copy it, or add it to the data.

**Batch fit**

Fits the current model to every number column of the table (each as Y),
or to many files at once. The results appear side by side, ready to copy or
save as CSV.

**Export**

* Fitted values as CSV: x, y, fitted, residual, confidence and prediction
  limits, and whether each point was used.
* The table as CSV.
* The graph as PNG (high resolution) or SVG (sharp at any size), or copied
  to the clipboard for Word or PowerPoint. Exports always use a white,
  print-friendly style.
* The results, the equation, or the equation as an **Excel formula** (with
  x in cell A2).
* **Report:** an HTML page with the model, equations, graph, parameter
  table, statistics and data. Print it from the browser to make a PDF.

**Projects**

* Save (Ctrl+S) keeps the data, the model, left-out points, parameter
  settings and log axes in one `.cforge` file. It is plain text.
* Recent files, an unsaved-changes marker (•) in the title, and a prompt
  before closing with unsaved changes.
* **Examples** (More menu):
  * radioactive decay;
  * a spectrum peak;
  * a dose-response curve;
  * a damped oscillation;
  * a calibration line with an outlier;
  * enzyme kinetics;
  * two overlapping peaks.

**Other**

* Light and dark theme, following Windows or set in Settings.
* Crisp on high-DPI screens. The table/graph and graph/results dividers can
  be dragged.
* Settings: theme, significant digits (3–12), confidence level, automatic
  refitting and grid lines.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S | New, open, save, save as |
| F5 | Fit now |
| Ctrl+M | Choose the model |
| Ctrl+B | Best fit |
| Ctrl+P | Parameters |
| Ctrl+R | Predict and calculate |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy, cut, paste rows |
| Ctrl+Shift+V | Paste as a new table |
| Insert / Ctrl+Delete | Insert a row / delete rows |
| Ctrl+E | Leave out / use the selected rows |
| Ctrl+0 | Show the whole graph |
| Ctrl+Shift+E | Save fitted values as CSV |
| Ctrl+Shift+C | Copy the graph |
| Ctrl+Shift+R | Save a report |
| Ctrl+1 / Ctrl+2 | Go to the table / the equation |
| Ctrl+, | Settings |
| F1 | About |

## Building

Requires CMake 3.20+ and Visual Studio 2022 (C++ desktop workload), or
MinGW-w64.

```bat
cd CurveForge
cmake -S . -B build -A x64
cmake --build build --config Release
:: -> build\Release\CurveForge.exe
```

From Linux:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

GitHub Actions (`.github/workflows/curveforge.yml`) builds the program on
every push. Download it from the run's **CurveForge-x64** artifact. The
`.exe` is not code-signed, so Windows SmartScreen or Smart App Control may
warn about it.

## How it works

| File | Responsibility |
|---|---|
| `src/Expr.*` | Equation parser and fast evaluator (compiled to a small stack program) |
| `src/Fit.*` | Levenberg-Marquardt least squares with Householder QR and column scaling, weights, robust reweighting, bounds, fixed parameters, several starting points, covariance, confidence bands, t-quantiles, derivative, integral and equation solving |
| `src/Models.*` | Built-in models, automatic starting values, cubic spline |
| `src/Engine.*` | From a table and a model to a finished fit (shared by the main window, Best fit and Batch) |
| `src/Data.*` | Table, text import (separator, header and decimal detection), project files |
| `src/Chart.*` | Graph drawn once for the screen (GDI+), PNG and SVG; zoom, pan and point picking |
| `src/DataGrid.*` | Spreadsheet-like table on a virtual list view |
| `src/MainWindow.cpp`, `src/MainActions.cpp` | Window, commands, undo, menus, tools, export, report and examples |
| `src/Dialogs.*` | Settings, Parameters, Predict, results tables, Batch, formula column |
| `src/Toolbar.*`, `src/Theme.*` | Self-drawn toolbars and status bar, light and dark palette (shared with the other apps) |

## Limitations

* Fits one Y against one X. Surface fits z = f(x, y) are not included.
* Weights come from a σ column. Errors in x are not taken into account.
* Very high-degree polynomials on data far from x = 0 are poorly
  conditioned: the curve is right, but the individual coefficients are
  uncertain (the results say so).
* Reports are HTML: use the browser's "Print to PDF" for a PDF.
