Imports System.Drawing.Drawing2D

''' <summary>Oscilloscope-style plot of any recorded quantity, with two measuring cursors.</summary>
Public Class PlotterPanel
    Inherits UserControl

    Private Shared ReadOnly Palette As Color() = {
        Color.FromArgb(0, 84, 200), Color.FromArgb(215, 60, 30), Color.FromArgb(0, 140, 70), Color.FromArgb(150, 60, 190),
        Color.FromArgb(200, 120, 0), Color.FromArgb(0, 150, 160), Color.FromArgb(180, 40, 110), Color.FromArgb(90, 90, 90)}

    Private ReadOnly _channels As New CheckedListBox() With {.Dock = DockStyle.Left, .Width = 230, .CheckOnClick = True, .IntegralHeight = False}
    Private ReadOnly _plot As New PlotArea(Me) With {.Dock = DockStyle.Fill}
    Private ReadOnly _window As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .Width = 70}
    Private ReadOnly _info As New Label() With {.AutoSize = True, .Padding = New Padding(8, 6, 0, 0)}
    Private _simulator As Simulator

    Friend CursorA As Double = Double.NaN
    Friend CursorB As Double = Double.NaN

    Public Sub New()
        Dim top As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 30, .WrapContents = False}
        top.Controls.Add(New Label() With {.Text = "Time window:", .AutoSize = True, .Padding = New Padding(4, 7, 0, 0)})
        _window.Items.AddRange({"5 s", "10 s", "30 s", "60 s"})
        _window.SelectedIndex = 1
        top.Controls.Add(_window)
        Dim clear As New Button() With {.Text = "Clear cursors", .AutoSize = True}
        AddHandler clear.Click, Sub()
                                    CursorA = Double.NaN : CursorB = Double.NaN
                                    _plot.Invalidate()
                                End Sub
        top.Controls.Add(clear)
        top.Controls.Add(_info)
        Controls.Add(_plot)
        Controls.Add(_channels)
        Controls.Add(top)
        AddHandler _channels.ItemCheck, Sub() BeginInvoke(New Action(Sub() _plot.Invalidate()))
        AddHandler _window.SelectedIndexChanged, Sub() _plot.Invalidate()
        _info.Text = "Left click: cursor A, right click: cursor B"
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
            SyncChannels()
            _plot.Invalidate()
        End Set
    End Property

    Friend ReadOnly Property WindowSeconds As Double
        Get
            Return {5.0, 10, 30, 60}(Math.Max(0, _window.SelectedIndex))
        End Get
    End Property

    Friend Function SelectedChannels() As List(Of String)
        Return _channels.CheckedItems.Cast(Of String)().ToList()
    End Function

    Friend Function ColorFor(name As String) As Color
        Return Palette(Math.Max(0, _channels.Items.IndexOf(name)) Mod Palette.Length)
    End Function

    ''' <summary>Adds newly recorded channels to the list; ticks a useful default set the first time.</summary>
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
        Next
    End Sub

    Public Sub RefreshPlot()
        SyncChannels()
        UpdateInfo()
        _plot.Invalidate()
    End Sub

    Friend Sub UpdateInfo()
        Dim parts As New List(Of String)
        If Not Double.IsNaN(CursorA) Then parts.Add($"A: {CursorA:0.00} s")
        If Not Double.IsNaN(CursorB) Then parts.Add($"B: {CursorB:0.00} s")
        If Not Double.IsNaN(CursorA) AndAlso Not Double.IsNaN(CursorB) Then parts.Add($"Δt = {Math.Abs(CursorB - CursorA):0.000} s")
        If _simulator IsNot Nothing AndAlso _simulator.LastCycleTime > 0 Then parts.Add($"Cycle time: {_simulator.LastCycleTime:0.00} s")
        _info.Text = If(parts.Count = 0, "Left click: cursor A, right click: cursor B", String.Join("    ", parts))
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

    Friend ReadOnly Property Sim As Simulator
        Get
            Return _simulator
        End Get
    End Property

    ''' <summary>The drawing area: one lane per selected channel.</summary>
    Private Class PlotArea
        Inherits Control

        Private ReadOnly _owner As PlotterPanel
        Private Const LabelW As Integer = 150

        Public Sub New(owner As PlotterPanel)
            _owner = owner
            SetStyle(ControlStyles.AllPaintingInWmPaint Or ControlStyles.UserPaint Or ControlStyles.OptimizedDoubleBuffer Or ControlStyles.ResizeRedraw, True)
            BackColor = Color.White
        End Sub

        Private Function TimeRange() As (Start As Double, [End] As Double)
            Dim sim = _owner.Sim
            Dim tEnd = Math.Max(_owner.WindowSeconds, If(sim Is Nothing, 0, sim.Time))
            Return (tEnd - _owner.WindowSeconds, tEnd)
        End Function

        Protected Overrides Sub OnMouseDown(e As MouseEventArgs)
            MyBase.OnMouseDown(e)
            If e.X < LabelW Then Return
            Dim r = TimeRange()
            Dim t = r.Start + (e.X - LabelW) / Math.Max(1.0, Width - LabelW - 10) * (r.End - r.Start)
            If e.Button = MouseButtons.Right Then _owner.CursorB = t Else _owner.CursorA = t
            _owner.UpdateInfo()
            Invalidate()
        End Sub

        Private Shared Function NiceCeiling(v As Double) As Double
            If v <= 0 Then Return 1
            Dim mag = Math.Pow(10, Math.Floor(Math.Log10(v)))
            For Each m In {1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0}
                If v <= m * mag + 0.000001 Then Return m * mag
            Next
            Return 10 * mag
        End Function

        Protected Overrides Sub OnPaint(e As PaintEventArgs)
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias
            e.Graphics.Clear(_owner.Scheme.Background)
            Using g As New GdiSurface(e.Graphics, _owner.Scheme)
                PaintPlot(g)
            End Using
        End Sub

        Private Sub PaintPlot(g As DrawSurface)
            Dim sim = _owner.Sim
            Dim chans = _owner.SelectedChannels()
            Using f As New Font("Segoe UI", 8), fb As New Font("Segoe UI", 8, FontStyle.Bold)
                If sim Is Nothing OrElse chans.Count = 0 Then
                    g.DrawString("Start the simulation and tick the quantities to plot on the left.", f, Brushes.Gray, 10, 10)
                    Return
                End If
                Dim r = TimeRange()
                Dim plotW = Math.Max(10, Width - LabelW - 10)
                Dim laneH = Math.Max(30, (Height - 24) \ chans.Count)
                Dim xOf = Function(t As Double) CSng(LabelW + (t - r.Start) / (r.End - r.Start) * plotW)
                ' Time grid.
                Using grid As New Pen(Color.FromArgb(230, 232, 238))
                    Dim stepS = If(_owner.WindowSeconds <= 10, 1, If(_owner.WindowSeconds <= 30, 5, 10))
                    For s = Math.Ceiling(r.Start / stepS) * stepS To r.End Step stepS
                        Dim x = xOf(s)
                        g.DrawLine(grid, x, 0.0F, x, CSng(Height - 20))
                        g.DrawString($"{s:0} s", f, Brushes.Gray, x - 8, Height - 18)
                    Next
                End Using
                For i = 0 To chans.Count - 1
                    Dim name = chans(i)
                    Dim top = i * laneH + 4, bottom = (i + 1) * laneH - 6
                    Dim col = _owner.ColorFor(name)
                    Dim pts As List(Of PointF) = Nothing
                    sim.Channels.TryGetValue(name, pts)
                    Dim visible = If(pts, New List(Of PointF)).Where(Function(p) p.X >= r.Start - 0.05).ToList()
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
                    If visible.Count > 1 Then
                        Dim yOf = Function(v As Double) CSng(bottom - (v - lo) / (hi - lo) * (bottom - top))
                        Using p As New Pen(col, 1.8F)
                            g.DrawLines(p, visible.Select(Function(q) New PointF(Math.Max(LabelW, xOf(q.X)), yOf(q.Y))).ToArray())
                        End Using
                    End If
                    For Each c In {(_owner.CursorA, "A"), (_owner.CursorB, "B")}
                        If Double.IsNaN(c.Item1) Then Continue For
                        Dim v = _owner.ValueAt(name, c.Item1)
                        If Not Double.IsNaN(v) Then g.DrawString($"{c.Item2}: {v:0.###}", f, Brushes.Black, xOf(c.Item1) + 3, top + If(c.Item2 = "A", 10, 22))
                    Next
                Next
                For Each c In {(_owner.CursorA, Color.FromArgb(220, 60, 60)), (_owner.CursorB, Color.FromArgb(40, 140, 60))}
                    If Double.IsNaN(c.Item1) Then Continue For
                    Using p As New Pen(c.Item2, 1.2F) With {.DashStyle = DashStyle.Dash}
                        g.DrawLine(p, xOf(c.Item1), 0, xOf(c.Item1), Height - 20)
                    End Using
                Next
            End Using
        End Sub
    End Class
End Class
