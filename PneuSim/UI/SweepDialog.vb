Imports System.Drawing.Drawing2D

''' <summary>Parameter sweep: change one setting step by step, run the circuit each time and compare.</summary>
Public Class SweepDialog
    Inherits Form

    Private ReadOnly _project As Project
    Private ReadOnly _all As List(Of CircuitElement)
    Private ReadOnly _component As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .Width = 260}
    Private ReadOnly _knob As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .Width = 260}
    Private ReadOnly _from As New NumericUpDown() With {.DecimalPlaces = 2, .Minimum = -100000, .Maximum = 100000, .Width = 80}
    Private ReadOnly _to As New NumericUpDown() With {.DecimalPlaces = 2, .Minimum = -100000, .Maximum = 100000, .Width = 80}
    Private ReadOnly _steps As New NumericUpDown() With {.Minimum = 2, .Maximum = 50, .Value = 6, .Width = 60}
    Private ReadOnly _seconds As New NumericUpDown() With {.Minimum = 1, .Maximum = 120, .Value = 10, .Width = 60}
    Private ReadOnly _operate As New CheckedListBox() With {.CheckOnClick = True, .IntegralHeight = False}
    Private ReadOnly _real As New CheckBox() With {.Text = "Realistic physics", .AutoSize = True}
    Private ReadOnly _grid As New DataGridView() With {.Dock = DockStyle.Fill, .ReadOnly = True, .AllowUserToAddRows = False, .RowHeadersVisible = False,
                                                      .AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.AllCells}
    Private ReadOnly _chart As New ChartBox() With {.Dock = DockStyle.Right, .Width = 360}
    Private ReadOnly _plotColumn As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .Width = 220}
    Private ReadOnly _status As New Label() With {.AutoSize = True, .Padding = New Padding(6, 7, 0, 0)}
    Private _result As SweepResult

    Public Sub New(project As Project, realPhysics As Boolean)
        _project = project
        _all = project.AllElements().ToList()
        Text = "Parameter sweep"
        Size = New Size(1100, 640)
        StartPosition = FormStartPosition.CenterParent
        Font = New Font("Segoe UI", 9)
        _real.Checked = realPhysics

        For Each e In _all.Where(Function(x) LiveTuning.KnobsFor(x).Count > 0)
            _component.Items.Add(New Choice(e, _all.IndexOf(e)))
        Next
        For Each e In _all.Where(Function(x) x.IsManuallyOperated AndAlso TypeOf x IsNot HydraulicPump AndAlso TypeOf x IsNot Compressor)
            _operate.Items.Add(New Choice(e, _all.IndexOf(e)))
        Next
        AddHandler _component.SelectedIndexChanged, AddressOf OnComponentChanged
        AddHandler _knob.SelectedIndexChanged, AddressOf OnKnobChanged
        AddHandler _plotColumn.SelectedIndexChanged, Sub() UpdateChart()

        Dim form As New TableLayoutPanel() With {.Dock = DockStyle.Left, .AutoSize = True, .ColumnCount = 2, .Padding = New Padding(6)}
        Dim lbl = Function(t As String) New Label() With {.Text = t, .AutoSize = True, .Padding = New Padding(0, 6, 0, 0)}
        form.Controls.Add(lbl("Component:")) : form.Controls.Add(_component)
        form.Controls.Add(lbl("Setting:")) : form.Controls.Add(_knob)
        Dim range As New FlowLayoutPanel() With {.AutoSize = True, .WrapContents = False}
        range.Controls.AddRange({lbl("from"), _from, lbl("to"), _to, lbl("in"), _steps, lbl("steps")})
        form.Controls.Add(lbl("Values:")) : form.Controls.Add(range)
        Dim runRow As New FlowLayoutPanel() With {.AutoSize = True, .WrapContents = False}
        Dim run As New Button() With {.Text = "Run sweep", .AutoSize = True}
        Dim csv As New Button() With {.Text = "Export CSV…", .AutoSize = True}
        AddHandler run.Click, AddressOf OnRun
        AddHandler csv.Click, AddressOf OnExport
        runRow.Controls.AddRange({lbl("Each run lasts"), _seconds, lbl("s"), _real, run, csv})
        form.Controls.Add(lbl("Run:")) : form.Controls.Add(runRow)
        Dim operateBox As New GroupBox() With {.Text = "Operate during each run (held pressed / switched on)", .Dock = DockStyle.Fill, .Padding = New Padding(6)}
        _operate.Dock = DockStyle.Fill
        operateBox.Controls.Add(_operate)
        Dim topPanel As New Panel() With {.Dock = DockStyle.Top, .Height = 150}
        topPanel.Controls.Add(operateBox)
        topPanel.Controls.Add(form)
        Dim statusBar As New Panel() With {.Dock = DockStyle.Top, .Height = 26}
        statusBar.Controls.Add(_status)

        Dim chartPanel As New Panel() With {.Dock = DockStyle.Right, .Width = 370}
        Dim chartTop As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 30}
        chartTop.Controls.AddRange({New Label() With {.Text = "Chart:", .AutoSize = True, .Padding = New Padding(0, 6, 0, 0)}, _plotColumn})
        _chart.Dock = DockStyle.Fill
        chartPanel.Controls.Add(_chart)
        chartPanel.Controls.Add(chartTop)
        Controls.Add(_grid)
        Controls.Add(chartPanel)
        Controls.Add(statusBar)
        Controls.Add(topPanel)
        _status.Text = If(_component.Items.Count = 0, "This circuit has no settings to vary.", "Choose what to vary, then click Run sweep.")
        If _component.Items.Count > 0 Then _component.SelectedIndex = 0
        ' Pre-tick selector switches and start buttons so the circuit runs by itself.
        For i = 0 To _operate.Items.Count - 1
            Dim e = DirectCast(_operate.Items(i), Choice).Element
            If e.Label IsNot Nothing AndAlso (e.Label.Contains("S1") OrElse e.Label.Contains("S2")) Then _operate.SetItemChecked(i, True)
        Next
        Theme.Apply(Me)
        AddHandler KeyDown, Sub(s, e) If e.KeyCode = Keys.Escape Then Me.Close()
        KeyPreview = True
    End Sub

    Private Class Choice
        Public Sub New(e As CircuitElement, index As Integer)
            Element = e : Me.Index = index
        End Sub
        Public ReadOnly Property Element As CircuitElement
        Public ReadOnly Property Index As Integer
        Public Overrides Function ToString() As String
            Return Element.ToString()
        End Function
    End Class

    Private Sub OnComponentChanged(sender As Object, e As EventArgs)
        _knob.Items.Clear()
        Dim c = TryCast(_component.SelectedItem, Choice)
        If c Is Nothing Then Return
        For Each k In LiveTuning.KnobsFor(c.Element)
            _knob.Items.Add(k.Caption)
        Next
        If _knob.Items.Count > 0 Then _knob.SelectedIndex = 0
    End Sub

    Private Function SelectedKnob() As LiveTuning.Knob
        Dim c = TryCast(_component.SelectedItem, Choice)
        If c Is Nothing OrElse _knob.SelectedIndex < 0 Then Return Nothing
        Return LiveTuning.KnobsFor(c.Element)(_knob.SelectedIndex)
    End Function

    Private Sub OnKnobChanged(sender As Object, e As EventArgs)
        Dim k = SelectedKnob()
        If k Is Nothing Then Return
        Dim c = DirectCast(_component.SelectedItem, Choice)
        Dim now = LiveTuning.GetValue(c.Element, k)
        ' Default range: around the present value, within the slider limits.
        _from.Value = CDec(Math.Max(k.Min, If(now > 0, now * 0.5, k.Min)))
        _to.Value = CDec(Math.Min(k.Max, If(now > 0, now * 1.5, k.Max)))
    End Sub

    Private Sub OnRun(sender As Object, e As EventArgs)
        Dim k = SelectedKnob()
        If k Is Nothing Then Return
        Dim settings As New SweepSettings With {
            .ElementIndex = DirectCast(_component.SelectedItem, Choice).Index, .Knob = k,
            .FromValue = CDbl(_from.Value), .ToValue = CDbl(_to.Value), .Steps = CInt(_steps.Value),
            .Seconds = CDbl(_seconds.Value), .RealPhysics = _real.Checked}
        For Each item In _operate.CheckedItems
            settings.Operate.Add(DirectCast(item, Choice).Index)
        Next
        Cursor = Cursors.WaitCursor
        Try
            _result = ParameterSweep.Run(_project, settings, Sub(pct)
                                                                 _status.Text = $"Running… {pct}%"
                                                                 _status.Refresh()
                                                             End Sub)
            _status.Text = $"{_result.Rows.Count} runs done."
        Catch ex As Exception
            _status.Text = "The sweep stopped: " & ex.Message
            Return
        Finally
            Cursor = Cursors.Default
        End Try
        ShowResult()
    End Sub

    Private Sub ShowResult()
        _grid.Columns.Clear()
        _grid.Rows.Clear()
        For Each c In _result.Columns
            _grid.Columns.Add(c, c)
        Next
        For Each r In _result.Rows
            _grid.Rows.Add(r.Select(Function(v) If(Double.IsNaN(v), "–", v.ToString("0.###"))).Cast(Of Object)().ToArray())
        Next
        _plotColumn.Items.Clear()
        For i = 1 To _result.Columns.Count - 1
            _plotColumn.Items.Add(_result.Columns(i))
        Next
        ' Cycle time if there is one, otherwise the first cylinder's timing.
        Dim pick = If(_result.Rows.Any(Function(r) Not Double.IsNaN(r(1))), 0, Math.Min(3, _plotColumn.Items.Count - 1))
        If _plotColumn.Items.Count > 0 Then _plotColumn.SelectedIndex = pick
    End Sub

    Private Sub UpdateChart()
        If _result Is Nothing OrElse _plotColumn.SelectedIndex < 0 Then Return
        Dim col = _plotColumn.SelectedIndex + 1
        _chart.SetData(_result.Columns(0), _result.Columns(col), _result.Rows.Select(Function(r) New PointF(CSng(r(0)), CSng(r(col)))).ToList())
    End Sub

    Private Sub OnExport(sender As Object, e As EventArgs)
        If _result Is Nothing Then _status.Text = "Run the sweep first." : Return
        Using dlg As New SaveFileDialog() With {.Filter = "CSV file (*.csv)|*.csv", .FileName = "sweep.csv"}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            IO.File.WriteAllText(dlg.FileName, _result.ToCsv(), System.Text.Encoding.UTF8)
            _status.Text = "Saved " & dlg.FileName
        End Using
    End Sub

    ''' <summary>A simple line chart of one result against the varied setting.</summary>
    Private Class ChartBox
        Inherits Control

        Private _xName, _yName As String
        Private _points As New List(Of PointF)

        Public Sub New()
            SetStyle(ControlStyles.AllPaintingInWmPaint Or ControlStyles.UserPaint Or ControlStyles.OptimizedDoubleBuffer Or ControlStyles.ResizeRedraw, True)
        End Sub

        Public Sub SetData(xName As String, yName As String, pts As List(Of PointF))
            _xName = xName : _yName = yName
            _points = pts.Where(Function(p) Not Single.IsNaN(p.Y)).ToList()
            Invalidate()
        End Sub

        Protected Overrides Sub OnPaint(e As PaintEventArgs)
            Dim g = e.Graphics
            g.SmoothingMode = SmoothingMode.AntiAlias
            g.Clear(If(AppSettings.DarkMode, Theme.DarkField, Color.White))
            Dim fore = If(AppSettings.DarkMode, Theme.DarkFore, Color.Black)
            Using f As New Font("Segoe UI", 8), tb As New SolidBrush(fore), axis As New Pen(Color.Gray)
                If _points.Count < 2 Then
                    g.DrawString("Run the sweep to see the chart.", f, tb, 10, 10)
                    Return
                End If
                Dim r As New RectangleF(50, 26, Width - 70, Height - 70)
                Dim x0 = _points.Min(Function(p) p.X), x1 = _points.Max(Function(p) p.X)
                Dim y0 = Math.Min(0, _points.Min(Function(p) p.Y)), y1 = _points.Max(Function(p) p.Y)
                If x1 - x0 < 0.000001 Then x1 = x0 + 1
                If y1 - y0 < 0.000001 Then y1 = y0 + 1
                g.DrawLine(axis, r.Left, r.Bottom, r.Right, r.Bottom)
                g.DrawLine(axis, r.Left, r.Top, r.Left, r.Bottom)
                g.DrawString(_yName, f, tb, 4, 4)
                g.DrawString(_xName, f, tb, r.Left, r.Bottom + 22)
                g.DrawString($"{y1:0.###}", f, tb, 2, r.Top - 6)
                g.DrawString($"{y0:0.###}", f, tb, 2, r.Bottom - 8)
                g.DrawString($"{x0:0.##}", f, tb, r.Left - 6, r.Bottom + 4)
                g.DrawString($"{x1:0.##}", f, tb, r.Right - 24, r.Bottom + 4)
                Dim map = Function(p As PointF) New PointF(CSng(r.Left + (p.X - x0) / (x1 - x0) * r.Width), CSng(r.Bottom - (p.Y - y0) / (y1 - y0) * r.Height))
                Dim pts = _points.OrderBy(Function(p) p.X).Select(map).ToArray()
                Using line As New Pen(Color.FromArgb(0, 110, 210), 2)
                    g.DrawLines(line, pts)
                End Using
                Using dot As New SolidBrush(Color.FromArgb(0, 110, 210))
                    For Each p In pts
                        g.FillEllipse(dot, p.X - 3, p.Y - 3, 6, 6)
                    Next
                End Using
            End Using
        End Sub
    End Class
End Class
