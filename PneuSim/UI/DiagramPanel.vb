Imports System.Drawing.Drawing2D

''' <summary>Displacement-time diagram of the cylinders and switching states of the labelled valves.</summary>
Public Class DiagramPanel
    Inherits Control

    Private Const LabelWidth As Integer = 90
    Private Const MaxRowHeight As Integer = 44
    Private Const MinRowHeight As Integer = 24
    Private Const TimeWindow As Double = 10

    Public Property Simulator As Simulator

    Public Sub New()
        SetStyle(ControlStyles.AllPaintingInWmPaint Or ControlStyles.UserPaint Or
                 ControlStyles.OptimizedDoubleBuffer Or ControlStyles.ResizeRedraw, True)
        BackColor = Color.White
    End Sub

    Protected Overrides Sub OnPaint(e As PaintEventArgs)
        Dim g = e.Graphics
        g.SmoothingMode = SmoothingMode.AntiAlias
        Using titleFont As New Font("Segoe UI", 9, FontStyle.Bold), f As New Font("Segoe UI", 8)
            g.DrawString("Displacement-step diagram", titleFont, Brushes.Black, 6, 4)

            Dim rows = DiagramRows()
            If Simulator Is Nothing OrElse rows.Count = 0 Then
                g.DrawString("Start the simulation to record cylinder movements and valve states.", f, Brushes.Gray, 6, 26)
                Return
            End If

            Dim rowHeight = Math.Max(MinRowHeight, Math.Min(MaxRowHeight, (Height - 46) \ rows.Count))
            Dim plotLeft = LabelWidth
            Dim plotRight = Width - 16
            If plotRight - plotLeft < 40 Then Return
            Dim tEnd = Math.Max(TimeWindow, Simulator.Time)
            Dim tStart = tEnd - TimeWindow

            ' Time axis.
            Dim axisY = 26 + rows.Count * rowHeight
            Using gridPen As New Pen(Color.FromArgb(225, 228, 235))
                For s = Math.Ceiling(tStart) To Math.Floor(tEnd)
                    Dim x = CSng(plotLeft + (s - tStart) / TimeWindow * (plotRight - plotLeft))
                    g.DrawLine(gridPen, x, 24, x, axisY)
                    g.DrawString($"{s:0} s", f, Brushes.Gray, x - 8, axisY + 2)
                Next
            End Using

            Dim y = 26
            For Each el In rows
                Dim isCylinder = TypeOf el Is CylinderBase
                Dim top = y + 6, bottom = y + rowHeight - 6
                g.DrawString(el.Label, titleFont, Brushes.Black, 6, y + 3)
                If rowHeight >= 36 Then g.DrawString(If(isCylinder, "cylinder", "valve"), f, Brushes.Gray, 6, y + 19)
                g.DrawString(If(isCylinder, "1", "a"), f, Brushes.Gray, plotLeft - 14, top - 7)
                g.DrawString(If(isCylinder, "0", "b"), f, Brushes.Gray, plotLeft - 14, bottom - 7)
                g.DrawLine(Pens.LightGray, plotLeft, bottom, plotRight, bottom)
                g.DrawLine(Pens.LightGray, plotLeft, top, plotRight, top)

                Dim samples As List(Of PointF) = Nothing
                If Simulator.History.TryGetValue(el, samples) AndAlso samples.Count > 1 Then
                    Dim pts As New List(Of PointF)
                    For Each s In samples
                        If s.X < tStart - 0.1 Then Continue For
                        Dim x = CSng(plotLeft + (s.X - tStart) / TimeWindow * (plotRight - plotLeft))
                        Dim v = CSng(bottom - s.Y * (bottom - top))
                        ' Valves switch instantly: draw steps.
                        If Not isCylinder AndAlso pts.Count > 0 Then pts.Add(New PointF(x, pts(pts.Count - 1).Y))
                        pts.Add(New PointF(Math.Max(plotLeft, x), v))
                    Next
                    If pts.Count > 1 Then
                        Using p As New Pen(If(isCylinder, RenderContext.PressureColor, Color.FromArgb(200, 90, 20)), 2)
                            g.DrawLines(p, pts.ToArray())
                        End Using
                    End If
                End If
                y += rowHeight
            Next
        End Using
    End Sub

    ''' <summary>Cylinders first, then valves that have a label.</summary>
    Private Function DiagramRows() As List(Of CircuitElement)
        Dim result As New List(Of CircuitElement)
        If Simulator Is Nothing Then Return result
        Dim els = Simulator.Circuit.Elements
        result.AddRange(els.OfType(Of CylinderBase)())
        result.AddRange(els.OfType(Of DirectionalValve)().Where(Function(v) Not String.IsNullOrEmpty(v.Label)))
        Return result
    End Function
End Class
