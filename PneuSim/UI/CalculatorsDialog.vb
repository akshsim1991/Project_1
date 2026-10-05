''' <summary>Engineering calculators: cylinder force, air consumption, valve sizing and hydraulics.</summary>
Public Class CalculatorsDialog
    Inherits Form

    Private ReadOnly _tabs As New TabControl() With {.Dock = DockStyle.Fill}

    Public Sub New()
        Text = "Calculators"
        Font = New Font("Segoe UI", 9)
        Size = New Size(560, 470)
        StartPosition = FormStartPosition.CenterParent
        ShowInTaskbar = False
        Controls.Add(_tabs)

        AddCalculator("Cylinder force",
            {("Bore (mm)", 32.0), ("Rod diameter (mm)", 12.0), ("Pressure (bar)", 6.0), ("Efficiency (%)", 90.0)},
            Function(v)
                Dim a = Math.PI / 4 * (v(0) / 1000) ^ 2
                Dim ar = Math.PI / 4 * ((v(0) / 1000) ^ 2 - (v(1) / 1000) ^ 2)
                Dim eff = v(3) / 100
                Dim fe = v(2) * 100000.0 * a * eff
                Dim fr = v(2) * 100000.0 * ar * eff
                Return $"Piston area: {a * 10000:0.00} cm²   Annulus area: {ar * 10000:0.00} cm²" & vbCrLf & vbCrLf &
                       $"Extending force:  {fe:0} N  ({fe / 9.81:0.0} kgf)" & vbCrLf &
                       $"Retracting force: {fr:0} N  ({fr / 9.81:0.0} kgf)" & vbCrLf & vbCrLf &
                       "F = p × A × efficiency.  1 bar = 100 000 N/m²." & vbCrLf &
                       "Choose a cylinder about 1.5–2 × larger than the load for reliable speed."
            End Function)

        AddCalculator("Air consumption",
            {("Bore (mm)", 32.0), ("Rod diameter (mm)", 12.0), ("Stroke (mm)", 100.0), ("Pressure (bar)", 6.0),
             ("Cycles per minute", 10.0), ("Air cost (₹ per m³)", 2.0), ("Hours per day", 8.0), ("Days per year", 300.0)},
            Function(v)
                Dim s = v(2) / 1000
                Dim ratio = (v(3) + 1.013) / 1.013
                Dim ext = Math.PI / 4 * (v(0) / 1000) ^ 2 * s * ratio * 1000
                Dim ret = Math.PI / 4 * ((v(0) / 1000) ^ 2 - (v(1) / 1000) ^ 2) * s * ratio * 1000
                Dim perCycle = ext + ret
                Dim perMin = perCycle * v(4)
                Dim perYear = perMin * 60 * v(6) * v(7) / 1000
                Return $"Free air per cycle: {perCycle:0.00} NL  (extend {ext:0.00} + retract {ret:0.00})" & vbCrLf &
                       $"Consumption: {perMin:0.0} NL/min" & vbCrLf &
                       $"Per year: {perYear:#,##0} m³ → about ₹{perYear * v(5):#,##0}" & vbCrLf & vbCrLf &
                       "Free air = swept volume × (p + 1.013) / 1.013.  Add 10–20 % for tubing and leaks."
            End Function)

        AddCalculator("Valve size",
            {("Bore (mm)", 32.0), ("Desired speed (m/s)", 0.5), ("Pressure (bar)", 6.0)},
            Function(v)
                Dim a = Math.PI / 4 * (v(0) / 1000) ^ 2
                Dim ratio = (v(2) + 1.013) / 1.013
                Dim q = a * v(1) * ratio * 60000 ' NL/min
                Dim qn = q * 1.2
                Return $"Flow needed while moving: {q:0} NL/min" & vbCrLf &
                       $"Choose a valve with nominal flow Qn ≥ {qn:0} NL/min" & vbCrLf &
                       $"   ≈ Cv {qn / 981:0.00}   ≈ Kv {qn / 1078:0.00}   ≈ sonic conductance C {qn / 60 / 7 / 0.96:0.00} NL/(s·bar)" & vbCrLf & vbCrLf &
                       "Qn is the flow at 6 bar inlet and 1 bar pressure drop (catalogue value)." & vbCrLf &
                       "Rule of thumb: Qn ≈ 981 × Cv ≈ 1078 × Kv (l/min)."
            End Function)

        AddCalculator("Hydraulics",
            {("Bore (mm)", 40.0), ("Rod diameter (mm)", 22.0), ("Pump flow (l/min)", 8.0), ("Pressure (bar)", 60.0), ("Stroke (mm)", 200.0)},
            Function(v)
                Dim a = Math.PI / 4 * (v(0) / 1000) ^ 2
                Dim ar = Math.PI / 4 * ((v(0) / 1000) ^ 2 - (v(1) / 1000) ^ 2)
                Dim q = v(2) / 60000
                Dim ve = q / a, vr = q / ar
                Dim power = v(3) * v(2) / 600
                Return $"Extending speed: {ve:0.000} m/s  ({v(4) / 1000 / ve:0.00} s per stroke)" & vbCrLf &
                       $"Retracting speed: {vr:0.000} m/s  ({v(4) / 1000 / vr:0.00} s per stroke)" & vbCrLf &
                       $"Extending force: {v(3) * 100000.0 * a / 1000:0.0} kN   Retracting: {v(3) * 100000.0 * ar / 1000:0.0} kN" & vbCrLf &
                       $"Hydraulic power: {power:0.00} kW   (drive motor ≈ {power / 0.85:0.00} kW)" & vbCrLf & vbCrLf &
                       "v = Q / A,  F = p × A,  P (kW) = p (bar) × Q (l/min) / 600."
            End Function)
    End Sub

    Private Sub AddCalculator(title As String, inputs As (Label As String, Value As Double)(), calc As Func(Of Double(), String))
        Dim page As New TabPage(title) With {.Padding = New Padding(10)}
        Dim table As New TableLayoutPanel() With {.Dock = DockStyle.Top, .AutoSize = True, .ColumnCount = 2}
        Dim boxes As New List(Of NumericUpDown)
        Dim result As New TextBox() With {.Multiline = True, .ReadOnly = True, .Dock = DockStyle.Fill, .Font = New Font("Consolas", 9.5F),
                                          .BackColor = Color.White, .ScrollBars = ScrollBars.Vertical}
        Dim update = Sub()
                         Try
                             result.Text = calc(boxes.Select(Function(b) CDbl(b.Value)).ToArray())
                         Catch ex As Exception
                             result.Text = ex.Message
                         End Try
                     End Sub
        For Each inp In inputs
            table.Controls.Add(New Label() With {.Text = inp.Label, .AutoSize = True, .Padding = New Padding(0, 6, 10, 0)})
            Dim box As New NumericUpDown() With {.DecimalPlaces = 2, .Maximum = 100000, .Minimum = 0, .Width = 110,
                                                .Value = CDec(inp.Value), .Increment = If(inp.Value >= 10, 1D, 0.1D)}
            AddHandler box.ValueChanged, Sub() update()
            boxes.Add(box)
            table.Controls.Add(box)
        Next
        Dim resultPanel As New Panel() With {.Dock = DockStyle.Fill, .Padding = New Padding(0, 10, 0, 0)}
        resultPanel.Controls.Add(result)
        page.Controls.Add(resultPanel)
        page.Controls.Add(table)
        _tabs.TabPages.Add(page)
        update()
    End Sub
End Class
