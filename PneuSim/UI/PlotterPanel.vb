Imports System.Drawing.Drawing2D
Imports System.Globalization
Imports System.Text

''' <summary>
''' Oscilloscope-style plot of any recorded quantity: two measuring cursors with minimum, maximum
''' and average between them, a trigger, zoom (mouse wheel) and hold, and CSV / PNG export.
''' </summary>
Public Class PlotterPanel
    Inherits UserControl

    Private Shared ReadOnly Palette As Color() = {
        Color.FromArgb(0, 84, 200), Color.FromArgb(215, 60, 30), Color.FromArgb(0, 140, 70), Color.FromArgb(150, 60, 190),
        Color.FromArgb(200, 120, 0), Color.FromArgb(0, 150, 160), Color.FromArgb(180, 40, 110), Color.FromArgb(90, 90, 90)}

    Private Shared ReadOnly WindowChoices As Double() = {1, 2, 5, 10, 30, 60, 120}

    Private ReadOnly _channels As New CheckedListBox() With {.Dock = DockStyle.Left, .Width = 230, .CheckOnClick = True, .IntegralHeight = False}
    Private ReadOnly _plot As New PlotArea(Me) With {.Dock = DockStyle.Fill}
    Private ReadOnly _window As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .Width = 64}
    Private ReadOnly _hold As New CheckBox() With {.Text = "Hold", .AutoSize = True, .Padding = New Padding(4, 4, 0, 0)}
    Private ReadOnly _trigChannel As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .Width = 200}
    Private ReadOnly _trigEdge As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .Width = 70}
    Private ReadOnly _trigLevel As New NumericUpDown() With {.DecimalPlaces = 2, .Minimum = -100000, .Maximum = 100000, .Width = 70, .Increment = 0.5D}
    Private ReadOnly _trigSingle As New CheckBox() With {.Text = "Single", .AutoSize = True, .Padding = New Padding(4, 4, 0, 0)}
    Private ReadOnly _info As New Label() With {.AutoSize = True, .Padding = New Padding(8, 6, 0, 0)}
    Private _simulator As Simulator
    Private _windowSeconds As Double = 10
    Private _holdEnd As Double
    Private _armTime As Double
    Private _singleTrigger As Double = Double.NaN

    Public CursorA As Double = Double.NaN
    Public CursorB As Double = Double.NaN

    Public Sub New()
        Dim top As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 62, .WrapContents = True}
        Dim lbl = Function(t As String) New Label() With {.Text = t, .AutoSize = True, .Padding = New Padding(4, 7, 0, 0)}
        top.Controls.Add(lbl("Time window:"))
        For Each w In WindowChoices
            _window.Items.Add($"{w:0} s")
        Next
        _window.SelectedIndex = 3
        top.Controls.Add(_window)
        top.Controls.Add(_hold)
        Dim clear As New Button() With {.Text = "Clear cursors", .AutoSize = True}
        AddHandler clear.Click, Sub()
                                    CursorA = Double.NaN : CursorB = Double.NaN
                                    UpdateInfo() : _plot.Invalidate()
                                End Sub
        top.Controls.Add(clear)
        Dim csv As New Button() With {.Text = "Export CSV…", .AutoSize = True}
        AddHandler csv.Click, AddressOf OnExportCsv
        Dim png As New Button() With {.Text = "Export PNG…", .AutoSize = True}
        AddHandler png.Click, AddressOf OnExportPng
        top.Controls.Add(csv)
        top.Controls.Add(png)
        top.SetFlowBreak(png, True)
        top.Controls.Add(lbl("Trigger:"))
        _trigChannel.Items.Add("off")
        _trigChannel.SelectedIndex = 0
        top.Controls.Add(_trigChannel)
        _trigEdge.Items.AddRange({"rising", "falling"})
        _trigEdge.SelectedIndex = 0
        top.Controls.Add(_trigEdge)
        top.Controls.Add(lbl("at"))
        top.Controls.Add(_trigLevel)
        top.Controls.Add(_trigSingle)
        Dim arm As New Button() With {.Text = "Arm", .AutoSize = True}
        AddHandler arm.Click, Sub() Rearm()
        top.Controls.Add(arm)
        top.Controls.Add(_info)
        Controls.Add(_plot)
        Controls.Add(_channels)
        Controls.Add(top)
        ' The ticked state changes after this event, so repaint a moment later.
        AddHandler _channels.ItemCheck, Sub() If IsHandleCreated Then BeginInvoke(New Action(Sub() _plot.Invalidate()))
        AddHandler _window.SelectedIndexChanged, Sub()
                                                     _windowSeconds = WindowChoices(Math.Max(0, _window.SelectedIndex))
                                                     _plot.Invalidate()
                                                 End Sub
        AddHandler _hold.CheckedChanged, Sub()
                                             _holdEnd = TimeRangeEndLive()
                                             _plot.Invalidate()
                                         End Sub
        For Each c As Control In {_trigChannel, _trigEdge}
            AddHandler DirectCast(c, ComboBox).SelectedIndexChanged, Sub() Rearm()
        Next
        AddHandler _trigLevel.ValueChanged, Sub() Rearm()
        AddHandler _trigSingle.CheckedChanged, Sub() Rearm()
        UpdateInfo()
    End Sub

    ''' <summary>Colours (light or dark mode).</summary>
    Public Property Scheme As ColorScheme = ColorScheme.Light

    Public Property Simulator As Simulator
        Get
            Return _simulator
        End Get
        Set(value As Simulator)
            _simulator = value
            CursorA = Double.NaN : CursorB = Double.NaN
            _hold.Checked = False
            Rearm()
            SyncChannels()
            _plot.Invalidate()
        End Set
    End Property

    Friend ReadOnly Property WindowSeconds As Double
        Get
            Return _windowSeconds
        End Get
    End Property

    Friend Function SelectedChannels() As List(Of String)
        Return _channels.CheckedItems.Cast(Of String)().ToList()
    End Function

    Friend Function ColorFor(name As String) As Color
        Return Palette(Math.Max(0, _channels.Items.IndexOf(name)) Mod Palette.Length)
    End Function

    ''' <summary>Adds newly recorded channels to the lists; ticks a useful default set the first time.</summary>
    Public Sub SyncChannels()
        If _simulator Is Nothing Then Return
        Dim names = _simulator.Channels.Keys.ToList()
        Dim firstFill = _channels.Items.Count = 0
        For Each n In names
            If Not _channels.Items.Contains(n) Then
                Dim i = _channels.Items.Add(n)
                If firstFill AndAlso (n.EndsWith("position (mm)") OrElse n.Contains("pressure, cap side")) AndAlso _channels.CheckedItems.Count < 4 Then
                    _channels.SetItemChecked(i, True)
                End If
            End If
            If Not _trigChannel.Items.Contains(n) Then _trigChannel.Items.Add(n)
        Next
    End Sub

    Public Sub RefreshPlot()
        SyncChannels()
        UpdateInfo()
        _plot.Invalidate()
    End Sub

    ' ------------------------------------------------------------------ time range, trigger, zoom

    Private Function TimeRangeEndLive() As Double
        Return Math.Max(_windowSeconds, If(_simulator Is Nothing, 0, _simulator.Time))
    End Function

    Private Function TriggerOn() As Boolean
        Return _trigChannel.SelectedIndex > 0
    End Function

    ''' <summary>Sets the trigger (channel Nothing = off).</summary>
    Public Sub SetTrigger(channel As String, rising As Boolean, level As Double, singleShot As Boolean)
        SyncChannels()
        _trigChannel.SelectedIndex = If(channel Is Nothing, 0, Math.Max(0, _trigChannel.Items.IndexOf(channel)))
        _trigEdge.SelectedIndex = If(rising, 0, 1)
        _trigLevel.Value = CDec(level)
        _trigSingle.Checked = singleShot
        Rearm()
    End Sub

    ''' <summary>Ticks a channel for plotting.</summary>
    Public Sub ShowChannel(name As String)
        SyncChannels()
        Dim i = _channels.Items.IndexOf(name)
        If i >= 0 Then _channels.SetItemChecked(i, True)
    End Sub

    ''' <summary>Starts waiting for a new trigger (single mode) from now on.</summary>
    Public Sub Rearm()
        _armTime = If(_simulator Is Nothing, 0, _simulator.Time)
        _singleTrigger = Double.NaN
        _plot.Invalidate()
    End Sub

    ''' <summary>Times where the trigger channel crosses the level in the chosen direction.</summary>
    Public Function TriggerTimes() As List(Of Double)
        Dim result As New List(Of Double)
        If Not TriggerOn() OrElse _simulator Is Nothing Then Return result
        Dim list As List(Of PointF) = Nothing
        If Not _simulator.Channels.TryGetValue(CStr(_trigChannel.SelectedItem), list) Then Return result
        Dim level = CDbl(_trigLevel.Value)
        Dim rising = _trigEdge.SelectedIndex = 0
        For i = 1 To list.Count - 1
            Dim a = list(i - 1).Y, b = list(i).Y
            If (rising AndAlso a < level AndAlso b >= level) OrElse (Not rising AndAlso a > level AndAlso b <= level) Then result.Add(list(i).X)
        Next
        Return result
    End Function

    ''' <summary>The trigger time the display is lined up on, or NaN.</summary>
    Public Function TriggerTime() As Double
        Dim times = TriggerTimes()
        If times.Count = 0 Then Return Double.NaN
        If _trigSingle.Checked Then
            If Double.IsNaN(_singleTrigger) Then
                Dim later = times.Where(Function(t) t >= _armTime - 0.000001).ToList()
                If later.Count > 0 Then _singleTrigger = later(0)
            End If
            Return _singleTrigger
        End If
        Return times(times.Count - 1)
    End Function

    ''' <summary>The time span shown: held, triggered, or the latest window.</summary>
    Public Function TimeRange() As (Start As Double, [End] As Double)
        Dim trig = TriggerTime()
        If Not Double.IsNaN(trig) AndAlso Not _hold.Checked Then
            Dim start = trig - 0.2 * _windowSeconds
            Return (start, start + _windowSeconds)
        End If
        Dim tEnd = If(_hold.Checked, _holdEnd, TimeRangeEndLive())
        Return (tEnd - _windowSeconds, tEnd)
    End Function

    ''' <summary>Mouse wheel: zoom the time axis around the pointer; Shift + wheel: move it while held.</summary>
    Friend Sub ZoomAt(t As Double, delta As Integer, pan As Boolean)
        Dim r = TimeRange()
        If pan Then
            If Not _hold.Checked Then _hold.Checked = True
            _holdEnd += If(delta > 0, -0.2, 0.2) * _windowSeconds
            _holdEnd = Math.Max(_windowSeconds * 0.2, _holdEnd)
        Else
            Dim factor = If(delta > 0, 0.8, 1.25)
            Dim newWindow = Math.Max(0.2, Math.Min(120, _windowSeconds * factor))
            ' Keep the time under the pointer where it is: this needs a held view.
            Dim frac = (t - r.Start) / Math.Max(0.000001, r.End - r.Start)
            If Not _hold.Checked Then _hold.Checked = True
            _windowSeconds = newWindow
            _holdEnd = t + (1 - frac) * newWindow
        End If
        UpdateInfo()
        _plot.Invalidate()
    End Sub

    ' ------------------------------------------------------------------ cursors and values

    Friend Sub UpdateInfo()
        Dim parts As New List(Of String)
        If Not Double.IsNaN(CursorA) Then parts.Add($"A: {CursorA:0.00} s")
        If Not Double.IsNaN(CursorB) Then parts.Add($"B: {CursorB:0.00} s")
        If Not Double.IsNaN(CursorA) AndAlso Not Double.IsNaN(CursorB) Then parts.Add($"Δt = {Math.Abs(CursorB - CursorA):0.000} s")
        If _simulator IsNot Nothing AndAlso _simulator.LastCycleTime > 0 Then parts.Add($"Cycle time: {_simulator.LastCycleTime:0.00} s")
        If _hold.Checked OrElse _windowSeconds <> WindowChoices(Math.Max(0, _window.SelectedIndex)) Then parts.Add($"window {_windowSeconds:0.##} s")
        If TriggerOn() Then parts.Add(If(Double.IsNaN(TriggerTime()), "waiting for trigger…", $"triggered at {TriggerTime():0.00} s"))
        _info.Text = If(parts.Count = 0, "Left click: cursor A, right click: cursor B, wheel: zoom, Shift+wheel: scroll", String.Join("    ", parts))
    End Sub

    ''' <summary>Value of a channel at a time (nearest sample).</summary>
    Friend Function ValueAt(name As String, t As Double) As Double
        Dim list As List(Of PointF) = Nothing
        If _simulator Is Nothing OrElse Not _simulator.Channels.TryGetValue(name, list) OrElse list.Count = 0 Then Return Double.NaN
        Dim best = list(0)
        For Each p In list
            If Math.Abs(p.X - t) < Math.Abs(best.X - t) Then best = p
        Next
        Return best.Y
    End Function

    ''' <summary>Minimum, maximum and average of a channel between the two cursors (NaN if not both set).</summary>
    Public Function StatsBetweenCursors(name As String) As (Min As Double, Max As Double, Avg As Double)
        Dim none = (Double.NaN, Double.NaN, Double.NaN)
        If Double.IsNaN(CursorA) OrElse Double.IsNaN(CursorB) OrElse _simulator Is Nothing Then Return none
        Dim list As List(Of PointF) = Nothing
        If Not _simulator.Channels.TryGetValue(name, list) Then Return none
        Dim t0 = Math.Min(CursorA, CursorB), t1 = Math.Max(CursorA, CursorB)
        Dim inside = list.Where(Function(p) p.X >= t0 - 0.000001 AndAlso p.X <= t1 + 0.000001).ToList()
        If inside.Count = 0 Then Return none
        Return (inside.Min(Function(p) p.Y), inside.Max(Function(p) p.Y), inside.Average(Function(p) p.Y))
    End Function

    Friend ReadOnly Property Sim As Simulator
        Get
            Return _simulator
        End Get
    End Property

    ' ------------------------------------------------------------------ export

    ''' <summary>The ticked channels (or all, if none is ticked) as CSV: one row per sample time.</summary>
    Public Function ToCsv() As String
        If _simulator Is Nothing Then Return ""
        Dim names = SelectedChannels()
        If names.Count = 0 Then names = _simulator.Channels.Keys.ToList()
        Dim inv = CultureInfo.InvariantCulture
        Dim byTime As New SortedDictionary(Of Long, Double?())
        For c = 0 To names.Count - 1
            Dim list As List(Of PointF) = Nothing
            If Not _simulator.Channels.TryGetValue(names(c), list) Then Continue For
            For Each p In list
                Dim key = CLng(Math.Round(p.X * 1000))
                Dim row As Double?() = Nothing
                If Not byTime.TryGetValue(key, row) Then
                    ReDim row(names.Count - 1)
                    byTime(key) = row
                End If
                row(c) = p.Y
            Next
        Next
        Dim q = Function(s As String) """" & s.Replace("""", """""") & """"
        Dim sb As New StringBuilder()
        sb.AppendLine(q("Time (s)") & "," & String.Join(",", names.Select(q)))
        For Each kv In byTime
            sb.Append((kv.Key / 1000.0).ToString("0.000", inv))
            For Each v In kv.Value
                sb.Append(","c)
                If v.HasValue Then sb.Append(v.Value.ToString("0.#####", inv))
            Next
            sb.AppendLine()
        Next
        Return sb.ToString()
    End Function

    ''' <summary>The plot as a picture.</summary>
    Public Function ToImage() As Bitmap
        Dim bmp As New Bitmap(Math.Max(200, _plot.Width), Math.Max(100, _plot.Height))
        Using g = Graphics.FromImage(bmp)
            _plot.PaintOn(g, bmp.Width, bmp.Height)
        End Using
        Return bmp
    End Function

    Private Sub OnExportCsv(sender As Object, e As EventArgs)
        If _simulator Is Nothing OrElse _simulator.Channels.Count = 0 Then _info.Text = "Run the simulation first." : Return
        Using dlg As New SaveFileDialog() With {.Filter = "CSV file (*.csv)|*.csv", .FileName = "plotter.csv"}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            IO.File.WriteAllText(dlg.FileName, ToCsv(), Encoding.UTF8)
            _info.Text = "Saved " & dlg.FileName
        End Using
    End Sub

    Private Sub OnExportPng(sender As Object, e As EventArgs)
        Using dlg As New SaveFileDialog() With {.Filter = "PNG image (*.png)|*.png", .FileName = "plotter.png"}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            Using bmp = ToImage()
                bmp.Save(dlg.FileName, Imaging.ImageFormat.Png)
            End Using
            _info.Text = "Saved " & dlg.FileName
        End Using
    End Sub

    ''' <summary>The drawing area: one lane per selected channel.</summary>
    Private Class PlotArea
        Inherits Control

        Private ReadOnly _owner As PlotterPanel
        Private Const LabelW As Integer = 150

        Public Sub New(owner As PlotterPanel)
            _owner = owner
            SetStyle(ControlStyles.AllPaintingInWmPaint Or ControlStyles.UserPaint Or ControlStyles.OptimizedDoubleBuffer Or ControlStyles.ResizeRedraw Or ControlStyles.Selectable, True)
            BackColor = Color.White
        End Sub

        Private Function TimeAtX(x As Integer) As Double
            Dim r = _owner.TimeRange()
            Return r.Start + (x - LabelW) / Math.Max(1.0, Width - LabelW - 10) * (r.End - r.Start)
        End Function

        Protected Overrides Sub OnMouseEnter(e As EventArgs)
            MyBase.OnMouseEnter(e)
            Focus()
        End Sub

        Protected Overrides Sub OnMouseDown(e As MouseEventArgs)
            MyBase.OnMouseDown(e)
            Focus()
            If e.X < LabelW Then Return
            Dim t = TimeAtX(e.X)
            If e.Button = MouseButtons.Right Then _owner.CursorB = t Else _owner.CursorA = t
            _owner.UpdateInfo()
            Invalidate()
        End Sub

        Protected Overrides Sub OnMouseWheel(e As MouseEventArgs)
            MyBase.OnMouseWheel(e)
            _owner.ZoomAt(TimeAtX(Math.Max(LabelW, e.X)), e.Delta, (ModifierKeys And Keys.Shift) <> 0)
        End Sub

        Private Shared Function NiceCeiling(v As Double) As Double
            If v <= 0 Then Return 1
            Dim mag = Math.Pow(10, Math.Floor(Math.Log10(v)))
            For Each m In {1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0}
                If v <= m * mag + 0.000001 Then Return m * mag
            Next
            Return 10 * mag
        End Function

        Private Shared Function GridStep(window As Double) As Double
            For Each s In {0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0}
                If window / s <= 12 Then Return s
            Next
            Return 30
        End Function

        Protected Overrides Sub OnPaint(e As PaintEventArgs)
            PaintOn(e.Graphics, Width, Height)
        End Sub

        Friend Sub PaintOn(gr As Graphics, w As Integer, h As Integer)
            gr.SmoothingMode = SmoothingMode.AntiAlias
            gr.Clear(_owner.Scheme.Background)
            Using g As New GdiSurface(gr, _owner.Scheme)
                PaintPlot(g, w, h)
            End Using
        End Sub

        Private Sub PaintPlot(g As DrawSurface, w As Integer, h As Integer)
            Dim sim = _owner.Sim
            Dim chans = _owner.SelectedChannels()
            Using f As New Font("Segoe UI", 8), fb As New Font("Segoe UI", 8, FontStyle.Bold)
                If sim Is Nothing OrElse chans.Count = 0 Then
                    g.DrawString("Start the simulation and tick the quantities to plot on the left.", f, Brushes.Gray, 10, 10)
                    Return
                End If
                Dim r = _owner.TimeRange()
                Dim plotW = Math.Max(10, w - LabelW - 10)
                Dim laneH = Math.Max(30, (h - 24) \ chans.Count)
                Dim xOf = Function(t As Double) CSng(LabelW + (t - r.Start) / (r.End - r.Start) * plotW)
                ' Time grid.
                Using grid As New Pen(Color.FromArgb(230, 232, 238))
                    Dim stepS = GridStep(r.End - r.Start)
                    For s = Math.Ceiling(r.Start / stepS) * stepS To r.End + 0.000001 Step stepS
                        Dim x = xOf(s)
                        g.DrawLine(grid, x, 0.0F, x, CSng(h - 20))
                        g.DrawString(If(stepS < 1, $"{s:0.0#} s", $"{s:0} s"), f, Brushes.Gray, x - 8, h - 18)
                    Next
                End Using
                Dim trig = _owner.TriggerTime()
                For i = 0 To chans.Count - 1
                    Dim name = chans(i)
                    Dim top = i * laneH + 4, bottom = (i + 1) * laneH - 6
                    Dim col = _owner.ColorFor(name)
                    Dim pts As List(Of PointF) = Nothing
                    sim.Channels.TryGetValue(name, pts)
                    Dim visible = If(pts, New List(Of PointF)).Where(Function(p) p.X >= r.Start - 0.05 AndAlso p.X <= r.End + 0.05).ToList()
                    Dim lo = 0.0, hi = 1.0
                    If visible.Count > 0 Then
                        lo = Math.Min(0, visible.Min(Function(p) p.Y))
                        hi = Math.Max(visible.Max(Function(p) p.Y), lo + 0.001)
                        If hi - lo < 1 AndAlso hi <= 1.01 AndAlso lo >= 0 Then hi = 1
                        ' Round the scale to a tidy number (99.6 mm is shown on a 0-100 scale).
                        hi = NiceCeiling(hi)
                        If lo < 0 Then lo = -NiceCeiling(-lo)
                    End If
                    Using b As New SolidBrush(col)
                        ' The name keeps clear of the scale numbers on the right of the label column.
                        g.DrawString(name, fb, b, New RectangleF(4, top, LabelW - 52, laneH - 4))
                    End Using
                    g.DrawString($"{hi:0.##}", f, Brushes.Gray, LabelW - 40, top - 2)
                    g.DrawString($"{lo:0.##}", f, Brushes.Gray, LabelW - 40, bottom - 12)
                    g.DrawLine(Pens.Gainsboro, LabelW, bottom, LabelW + plotW, bottom)
                    Dim yOf = Function(v As Double) CSng(bottom - (v - lo) / (hi - lo) * (bottom - top))
                    If visible.Count > 1 Then
                        Using p As New Pen(col, 1.8F)
                            g.DrawLines(p, visible.Select(Function(q) New PointF(Math.Max(LabelW, Math.Min(LabelW + plotW, xOf(q.X))), yOf(q.Y))).ToArray())
                        End Using
                    End If
                    ' Trigger level on the trigger channel.
                    If Not Double.IsNaN(trig) AndAlso name = CStr(_owner._trigChannel.SelectedItem) Then
                        Using tp As New Pen(Color.FromArgb(200, 120, 0), 1) With {.DashStyle = DashStyle.Dot}
                            Dim ly = yOf(CDbl(_owner._trigLevel.Value))
                            If ly >= top AndAlso ly <= bottom Then g.DrawLine(tp, LabelW, ly, LabelW + plotW, ly)
                        End Using
                    End If
                    For Each c In {(_owner.CursorA, "A"), (_owner.CursorB, "B")}
                        If Double.IsNaN(c.Item1) OrElse c.Item1 < r.Start OrElse c.Item1 > r.End Then Continue For
                        Dim v = _owner.ValueAt(name, c.Item1)
                        If Not Double.IsNaN(v) Then g.DrawString($"{c.Item2}: {v:0.###}", f, Brushes.Black, xOf(c.Item1) + 3, top + If(c.Item2 = "A", 10, 22))
                    Next
                    ' Statistics between the cursors.
                    Dim st = _owner.StatsBetweenCursors(name)
                    If Not Double.IsNaN(st.Min) Then
                        Dim txt = $"A–B:  min {st.Min:0.###}   max {st.Max:0.###}   avg {st.Avg:0.###}"
                        Dim sz = g.MeasureString(txt, f)
                        Using bg As New SolidBrush(Color.FromArgb(220, 255, 255, 255))
                            g.FillRectangle(bg, LabelW + plotW - sz.Width - 4, top, sz.Width + 2, sz.Height)
                        End Using
                        g.DrawString(txt, f, Brushes.Black, LabelW + plotW - sz.Width - 3, top)
                    End If
                Next
                If Not Double.IsNaN(trig) Then
                    Dim tx = xOf(trig)
                    Using b As New SolidBrush(Color.FromArgb(200, 120, 0))
                        g.FillPolygon(b, {New PointF(tx - 5, 0), New PointF(tx + 5, 0), New PointF(tx, 8)})
                    End Using
                    g.DrawString("T", f, Brushes.DarkOrange, tx + 6, 0)
                End If
                For Each c In {(_owner.CursorA, Color.FromArgb(220, 60, 60)), (_owner.CursorB, Color.FromArgb(40, 140, 60))}
                    If Double.IsNaN(c.Item1) OrElse c.Item1 < r.Start OrElse c.Item1 > r.End Then Continue For
                    Using p As New Pen(c.Item2, 1.2F) With {.DashStyle = DashStyle.Dash}
                        g.DrawLine(p, xOf(c.Item1), 0, xOf(c.Item1), h - 20)
                    End Using
                Next
            End Using
        End Sub
    End Class
End Class
