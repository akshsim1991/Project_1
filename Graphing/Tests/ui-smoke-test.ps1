# End-to-end check of the real Graphing window on Windows (used by GitHub Actions).
# Opens a generated, calibrated project, auto-traces the curve with a real mouse click, then sorts, resamples
# and fits it, reading results back through UI Automation. Screenshots are saved to $OutDir.
param(
    [Parameter(Mandatory)] [string] $Exe,
    [Parameter(Mandatory)] [string] $OutDir
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms, UIAutomationClient, UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Mouse {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    public static void Click(int x, int y) {
        SetCursorPos(x, y); System.Threading.Thread.Sleep(150);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(80);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }
}
'@
New-Item -ItemType Directory -Force $OutDir | Out-Null
$A = [System.Windows.Automation.AutomationElement]
$Tree = [System.Windows.Automation.TreeScope]

function Save-Screenshot([string] $name) {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $g.Dispose(); $bmp.Save((Join-Path $OutDir "$name.png")); $bmp.Dispose()
}
function Find([string] $automationId) {
    $cond = New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, $automationId)
    for ($i = 0; $i -lt 20; $i++) {
        $el = $script:window.FindFirst($Tree::Descendants, $cond)
        if ($el) { return $el }
        Start-Sleep -Milliseconds 250
    }
    Show-Tree
    throw "Control '$automationId' not found"
}
function Find-ByName([string] $name) {
    $cond = New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $name)
    $el = $script:window.FindFirst($Tree::Descendants, $cond)
    if (-not $el) { Show-Tree; throw "Element named '$name' not found" }
    return $el
}
function Invoke-Button([string] $automationId) {
    (Find $automationId).GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 700
}
function Select-Item($element) {
    # Tabs and radio buttons: use whichever pattern the control offers.
    $pattern = $null
    if ($element.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref] $pattern)) { $pattern.Select() }
    elseif ($element.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref] $pattern)) { $pattern.Invoke() }
    elseif ($element.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref] $pattern)) { $pattern.Toggle() }
    else { throw "Cannot select '$($element.Current.Name)'" }
    Start-Sleep -Milliseconds 600
}
function Invoke-ByName([string] $name) {
    (Find-ByName $name).GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 700
}
function Get-Text([string] $automationId) {
    $el = Find $automationId
    $pattern = $null
    if ($el.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref] $pattern)) { return $pattern.Current.Value }
    return $el.Current.Name
}
function Show-Tree {
    # Lists what UI Automation can see, to make failures easy to diagnose from the log.
    if (-not $script:window) { return }
    Write-Host '---- controls in the window ----'
    foreach ($el in $script:window.FindAll($Tree::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
        $c = $el.Current
        if ($c.IsOffscreen) { continue }
        Write-Host ("{0,-22} id={1,-22} name='{2}' rect={3}" -f $c.ControlType.ProgrammaticName.Replace('ControlType.', ''), $c.AutomationId, $c.Name, $c.BoundingRectangle)
    }
}
function Assert($condition, [string] $message) {
    if (-not $condition) { Save-Screenshot 'failure'; Show-Tree; throw "CHECK FAILED: $message" }
    Write-Host "OK   $message"
}

# ---- 1. A test graph: axes and a red curve y = 50 + 30·sin(x/15) for x in 0..100 --------------------------
# Pixel mapping: px = 50 + 7x, py = 550 - 5y (X 0..100 → 50..750, Y 0..100 → 550..50).
$work = Join-Path $env:RUNNER_TEMP 'graphing-ui'
New-Item -ItemType Directory -Force $work | Out-Null
$img = New-Object System.Drawing.Bitmap 800, 600
$g = [System.Drawing.Graphics]::FromImage($img)
$g.Clear([System.Drawing.Color]::White)
$g.SmoothingMode = 'AntiAlias'
$axis = New-Object System.Drawing.Pen ([System.Drawing.Color]::Black), 2
$g.DrawLine($axis, 50, 550, 750, 550); $g.DrawLine($axis, 50, 550, 50, 50)
$points = for ($x = 0; $x -le 100; $x += 0.5) { New-Object System.Drawing.PointF ([single](50 + 7 * $x)), ([single](550 - 5 * (50 + 30 * [Math]::Sin($x / 15)))) }
$red = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(220, 30, 30)), 3
$g.DrawLines($red, [System.Drawing.PointF[]] $points)
$g.Dispose()
$ms = New-Object System.IO.MemoryStream
$img.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose()

# ---- 2. A calibrated project with an empty curve, and settings that switch on the grid and magnifier -------
$project = [ordered]@{
    Version = 2; ImageFileName = 'test-graph.png'; ImageData = [Convert]::ToBase64String($ms.ToArray())
    Calibration = [ordered]@{
        X1 = @{ Pixel = @{ X = 50; Y = 550 }; Value = 0 };  X2 = @{ Pixel = @{ X = 750; Y = 550 }; Value = 100 }
        Y1 = @{ Pixel = @{ X = 50; Y = 550 }; Value = 0 };  Y2 = @{ Pixel = @{ X = 50; Y = 50 }; Value = 100 }
        LogX = $false; LogY = $false
    }
    Series = @(@{ Name = 'Series 1'; ColorArgb = -16776961; Points = @() })
}
$projectPath = Join-Path $work 'test.graphproj'
$project | ConvertTo-Json -Depth 6 | Set-Content -Path $projectPath -Encoding utf8
$settingsDir = Join-Path $env:LOCALAPPDATA 'Graphing'
New-Item -ItemType Directory -Force $settingsDir | Out-Null
@{ WindowBounds = @{ X = 0; Y = 0; Width = 1010; Height = 740 }; ShowGrid = $true; ShowMagnifier = $true; ShowLines = $true } |
    ConvertTo-Json | Set-Content (Join-Path $settingsDir 'settings.json') -Encoding utf8
$crashLog = Join-Path $settingsDir 'crash.log'
Remove-Item $crashLog -ErrorAction SilentlyContinue

# ---- 3. Open it ------------------------------------------------------------------------------------------
$process = Start-Process -FilePath $Exe -ArgumentList "`"$projectPath`"" -PassThru
try {
    for ($i = 0; $i -lt 60 -and $process.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 500; $process.Refresh() }
    Assert ($process.MainWindowHandle -ne 0) 'main window appeared'
    Start-Sleep -Seconds 2
    $script:window = $A::FromHandle($process.MainWindowHandle)
    Assert ($window.Current.Name -like 'test.graphproj*Graphing') "title shows the project ('$($window.Current.Name)')"
    Assert ((Get-Text 'lblCalState') -like '*Calibrated*') 'project opened already calibrated'
    Save-Screenshot '1-opened'

    # ---- 4. Automatic trace with a real click on the curve at X = 30 ----------------------------------
    Select-Item (Find-ByName '2. Trace')
    Select-Item (Find 'rbAutoTrace')
    $canvasRect = (Find-ByName 'Graph canvas').Current.BoundingRectangle
    $zoom = [Math]::Min(($canvasRect.Width - 24) / 800, ($canvasRect.Height - 24) / 600)
    $offX = ($canvasRect.Width - 800 * $zoom) / 2; $offY = ($canvasRect.Height - 600 * $zoom) / 2
    $px = 50 + 7 * 30; $py = 550 - 5 * (50 + 30 * [Math]::Sin(2))
    $sx = [int]($canvasRect.Left + $offX + $px * $zoom); $sy = [int]($canvasRect.Top + $offY + $py * $zoom)
    Write-Host "Clicking on the curve at screen ($sx, $sy), zoom $zoom"
    [Mouse]::Click($sx, $sy)
    Start-Sleep -Seconds 2
    Save-Screenshot '2-auto-traced'

    Select-Item (Find-ByName '3. Data')
    $label = Get-Text 'lblDataSeries'
    $count = if ($label -match '(\d+) points?') { [int] $Matches[1] } else { 0 }
    Assert ($count -ge 100) "automatic trace extracted the curve ($label)"

    # ---- 5. Clean-up and analysis ----------------------------------------------------------------------
    Invoke-Button 'btnSortX'
    Invoke-Button 'btnResampleCount'
    Assert ((Get-Text 'lblDataSeries') -like '*50 points*') "resampled to 50 points ($(Get-Text 'lblDataSeries'))"
    Save-Screenshot '3-data'

    Select-Item (Find-ByName '4. Analyze')
    Assert ((Get-Text 'lblStats') -like '*Points:*50*') 'statistics shown'
    Invoke-Button 'btnFit'
    $fitText = Get-Text 'txtFitResult'
    Assert ($fitText -like '*R²*') "linear fit computed ($($fitText -replace "`r?`n", ' | '))"
    # Move the mouse over the graph to draw the magnifier, then capture the finished screen.
    [Mouse]::SetCursorPos($sx + 40, $sy + 20) | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot '4-analyze'

    # ---- 6. Undo works and nothing crashed ---------------------------------------------------------------
    Select-Item (Find-ByName '3. Data')
    Invoke-ByName 'Undo'
    Assert ((Get-Text 'lblDataSeries') -notlike '*50 points*') "undo reverted the resample ($(Get-Text 'lblDataSeries'))"
    $process.Refresh()
    Assert (-not $process.HasExited) 'Graphing is still running'
    Assert (-not (Test-Path $crashLog)) 'no crash was logged'
    Write-Host 'UI SMOKE TEST PASSED'
}
finally {
    if (Test-Path $crashLog) { Write-Host '---- crash.log ----'; Get-Content $crashLog }
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
}
