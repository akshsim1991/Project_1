Imports System.Drawing.Drawing2D

''' <summary>Shared pens, brushes and fonts used while drawing symbols.</summary>
Public Class RenderContext
    Implements IDisposable

    Public Shared ReadOnly PressureColor As Color = Color.FromArgb(0, 84, 200)
    Public Shared ReadOnly IdleTubeColor As Color = Color.FromArgb(120, 170, 230)
    Public Shared ReadOnly EnergizedColor As Color = Color.FromArgb(215, 30, 30)

    Public Property Simulating As Boolean

    ''' <summary>True when drawing on the editing canvas (shows helper marks such as bend points).</summary>
    Public Property Interactive As Boolean

    Public ReadOnly Line As New Pen(Color.Black, 1.6F)
    Public ReadOnly Thin As New Pen(Color.Black, 1.0F)
    Public ReadOnly Dashed As New Pen(Color.Black, 1.2F) With {.DashStyle = DashStyle.Dash}
    Public ReadOnly Pressure As New Pen(PressureColor, 2.4F)
    Public ReadOnly PressureDashed As New Pen(PressureColor, 1.8F) With {.DashStyle = DashStyle.Dash}
    Public ReadOnly Energized As New Pen(EnergizedColor, 2.0F)
    Public ReadOnly Font As New Font("Segoe UI", 8.0F)
    Public ReadOnly SmallFont As New Font("Segoe UI", 7.0F)
    Public ReadOnly TextBrush As New SolidBrush(Color.Black)
    Public ReadOnly MarkBrush As New SolidBrush(Color.FromArgb(160, 40, 40))
    Public ReadOnly BodyBrush As New SolidBrush(Color.White)

    ''' <summary>Pen for a line carrying the air of the given port.</summary>
    Public Function PenFor(p As Port) As Pen
        If Simulating AndAlso p IsNot Nothing AndAlso p.IsPressurized Then
            Return If(p.Kind = PortKind.Electric, Energized, Pressure)
        End If
        Return Line
    End Function

    Public Function DashedFor(p As Port) As Pen
        If Simulating AndAlso p IsNot Nothing AndAlso p.IsPressurized Then Return PressureDashed
        Return Dashed
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        Line.Dispose() : Thin.Dispose() : Dashed.Dispose()
        Pressure.Dispose() : PressureDashed.Dispose() : Energized.Dispose()
        Font.Dispose() : SmallFont.Dispose()
        TextBrush.Dispose() : MarkBrush.Dispose() : BodyBrush.Dispose()
    End Sub
End Class

''' <summary>Drawing helpers for standard fluid power (ISO 1219) symbol parts.</summary>
Public Module Symbols

    Public Sub Arrow(g As Graphics, pen As Pen, p1 As PointF, p2 As PointF, Optional head As Single = 6)
        g.DrawLine(pen, p1, p2)
        Dim dx = p2.X - p1.X, dy = p2.Y - p1.Y
        Dim len = CSng(Math.Sqrt(dx * dx + dy * dy))
        If len < 0.01F Then Return
        dx /= len : dy /= len
        Dim baseX = p2.X - dx * head, baseY = p2.Y - dy * head
        Dim w = head * 0.45F
        Using b As New SolidBrush(pen.Color)
            g.FillPolygon(b, {p2, New PointF(baseX - dy * w, baseY + dx * w), New PointF(baseX + dy * w, baseY - dx * w)})
        End Using
    End Sub

    ''' <summary>Closed port symbol: a short stem with a cross bar ("T").</summary>
    Public Sub Blocked(g As Graphics, pen As Pen, edge As PointF, inward As PointF)
        Dim tip As New PointF(edge.X + inward.X * 9, edge.Y + inward.Y * 9)
        g.DrawLine(pen, edge, tip)
        Dim px = -inward.Y * 5, py = inward.X * 5
        g.DrawLine(pen, tip.X - px, tip.Y - py, tip.X + px, tip.Y + py)
    End Sub

    Public Sub Spring(g As Graphics, pen As Pen, x1 As Single, x2 As Single, cy As Single, amp As Single)
        Const n = 6
        Dim pts As New List(Of PointF) From {New PointF(x1, cy)}
        For i = 1 To n
            Dim x = x1 + (x2 - x1) * (i - 0.5F) / n
            pts.Add(New PointF(x, cy + If(i Mod 2 = 0, amp, -amp)))
        Next
        pts.Add(New PointF(x2, cy))
        g.DrawLines(pen, pts.ToArray())
    End Sub

    ''' <summary>Hollow exhaust triangle pointing away from the port along <paramref name="dir"/>.</summary>
    Public Sub Exhaust(g As Graphics, pen As Pen, pt As PointF, dir As PointF)
        Dim apex As New PointF(pt.X + dir.X * 9, pt.Y + dir.Y * 9)
        Dim px = -dir.Y * 5, py = dir.X * 5
        g.DrawPolygon(pen, {apex, New PointF(pt.X + px + dir.X, pt.Y + py + dir.Y), New PointF(pt.X - px + dir.X, pt.Y - py + dir.Y)})
    End Sub

    Public Function Lerp(a As PointF, b As PointF, t As Single) As PointF
        Return New PointF(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t)
    End Function
End Module
