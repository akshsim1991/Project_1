Imports System.Drawing.Drawing2D

''' <summary>
''' The drawing operations used by symbols. Implemented for the screen (GDI+, optionally with
''' dark-mode colours) and for vector export (SVG, DXF, PDF).
''' </summary>
Public MustInherit Class DrawSurface
    Public MustOverride Sub DrawLine(pen As Pen, x1 As Single, y1 As Single, x2 As Single, y2 As Single)
    Public MustOverride Sub DrawLines(pen As Pen, points As PointF())
    Public MustOverride Sub DrawPolygon(pen As Pen, points As PointF())
    Public MustOverride Sub FillPolygon(brush As Brush, points As PointF())
    Public MustOverride Sub DrawEllipse(pen As Pen, x As Single, y As Single, w As Single, h As Single)
    Public MustOverride Sub FillEllipse(brush As Brush, x As Single, y As Single, w As Single, h As Single)
    Public MustOverride Sub DrawArc(pen As Pen, x As Single, y As Single, w As Single, h As Single, startAngle As Single, sweepAngle As Single)
    Public MustOverride Sub DrawString(text As String, font As Font, brush As Brush, x As Single, y As Single)
    Public MustOverride Function MeasureString(text As String, font As Font) As SizeF
    Public MustOverride Function Save() As Object
    Public MustOverride Sub Restore(state As Object)
    Public MustOverride Sub MultiplyTransform(m As Matrix)
    Public MustOverride Sub TranslateTransform(dx As Single, dy As Single)
    Public MustOverride Sub ScaleTransform(sx As Single, sy As Single)

    ' Convenience overloads expressed with the primitives above.
    Public Sub DrawLine(pen As Pen, a As PointF, b As PointF)
        DrawLine(pen, a.X, a.Y, b.X, b.Y)
    End Sub

    Public Overridable Sub DrawRectangle(pen As Pen, x As Single, y As Single, w As Single, h As Single)
        DrawPolygon(pen, {New PointF(x, y), New PointF(x + w, y), New PointF(x + w, y + h), New PointF(x, y + h)})
    End Sub

    Public Overridable Sub FillRectangle(brush As Brush, x As Single, y As Single, w As Single, h As Single)
        FillPolygon(brush, {New PointF(x, y), New PointF(x + w, y), New PointF(x + w, y + h), New PointF(x, y + h)})
    End Sub

    Public Sub FillRectangle(brush As Brush, r As RectangleF)
        FillRectangle(brush, r.X, r.Y, r.Width, r.Height)
    End Sub

    Public Sub DrawRectangle(pen As Pen, r As RectangleF)
        DrawRectangle(pen, r.X, r.Y, r.Width, r.Height)
    End Sub

    ''' <summary>Text measuring without a target surface.</summary>
    Protected Shared ReadOnly Measurer As Graphics = Graphics.FromImage(New Bitmap(1, 1))

    Public Shared Function Measure(text As String, font As Font) As SizeF
        SyncLock Measurer
            Return Measurer.MeasureString(text, font)
        End SyncLock
    End Function

    Public Shared Function ColorOf(brush As Brush) As Color
        Dim sb = TryCast(brush, SolidBrush)
        Return If(sb Is Nothing, Color.Black, sb.Color)
    End Function
End Class

''' <summary>Maps the drawing colours for dark mode.</summary>
Public Class ColorScheme
    Public Shared ReadOnly Light As New ColorScheme(False)
    Public Shared ReadOnly Dark As New ColorScheme(True)

    Public Sub New(isDark As Boolean)
        Me.IsDark = isDark
    End Sub

    Public ReadOnly Property IsDark As Boolean

    Public ReadOnly Property Background As Color
        Get
            Return If(IsDark, Color.FromArgb(30, 32, 38), Color.White)
        End Get
    End Property

    Public ReadOnly Property GridDot As Color
        Get
            Return If(IsDark, Color.FromArgb(70, 74, 86), Color.FromArgb(205, 210, 220))
        End Get
    End Property

    ''' <summary>Black becomes light grey, white becomes the background, other colours are brightened.</summary>
    Public Function Map(c As Color) As Color
        If Not IsDark Then Return c
        Dim lum = (0.299 * c.R + 0.587 * c.G + 0.114 * c.B) / 255
        Dim sat = (Math.Max(c.R, Math.Max(c.G, c.B)) - Math.Min(c.R, Math.Min(c.G, c.B))) / 255.0
        If sat < 0.12 Then
            ' Greys are inverted: black lines become light, white fills become dark.
            Dim v = CInt(Math.Round(38 + (1 - lum) * (225 - 38)))
            Return Color.FromArgb(c.A, v, v, v)
        End If
        ' Saturated colours: lift dark ones so they stay visible on the dark background.
        Dim boost = If(lum < 0.45, 0.45 - lum, 0)
        Return Color.FromArgb(c.A, Lift(c.R, boost), Lift(c.G, boost), Lift(c.B, boost))
    End Function

    Private Shared Function Lift(v As Byte, boost As Double) As Integer
        Return CInt(Math.Min(255, v + (255 - v) * boost * 1.4))
    End Function
End Class

''' <summary>Draws on a GDI+ <see cref="Graphics"/>, optionally remapping colours.</summary>
Public Class GdiSurface
    Inherits DrawSurface
    Implements IDisposable

    Private ReadOnly _g As Graphics
    Private ReadOnly _scheme As ColorScheme
    Private ReadOnly _pens As New Dictionary(Of String, Pen)
    Private ReadOnly _brushes As New Dictionary(Of Integer, SolidBrush)

    Public Sub New(g As Graphics, Optional scheme As ColorScheme = Nothing)
        _g = g
        _scheme = If(scheme, ColorScheme.Light)
    End Sub

    Public ReadOnly Property Graphics As Graphics
        Get
            Return _g
        End Get
    End Property

    Private Function P(pen As Pen) As Pen
        If Not _scheme.IsDark Then Return pen
        Dim key = $"{pen.Color.ToArgb()}|{pen.Width}|{CInt(pen.DashStyle)}|{CInt(pen.LineJoin)}"
        Dim mapped As Pen = Nothing
        If Not _pens.TryGetValue(key, mapped) Then
            mapped = New Pen(_scheme.Map(pen.Color), pen.Width) With {.DashStyle = pen.DashStyle, .LineJoin = pen.LineJoin}
            _pens(key) = mapped
        End If
        Return mapped
    End Function

    Private Function B(brush As Brush) As Brush
        If Not _scheme.IsDark Then Return brush
        Dim c = ColorOf(brush)
        Dim mapped As SolidBrush = Nothing
        If Not _brushes.TryGetValue(c.ToArgb(), mapped) Then
            mapped = New SolidBrush(_scheme.Map(c))
            _brushes(c.ToArgb()) = mapped
        End If
        Return mapped
    End Function

    Public Overrides Sub DrawLine(pen As Pen, x1 As Single, y1 As Single, x2 As Single, y2 As Single)
        _g.DrawLine(P(pen), x1, y1, x2, y2)
    End Sub

    Public Overrides Sub DrawLines(pen As Pen, points As PointF())
        If points.Length >= 2 Then _g.DrawLines(P(pen), points)
    End Sub

    Public Overrides Sub DrawPolygon(pen As Pen, points As PointF())
        _g.DrawPolygon(P(pen), points)
    End Sub

    Public Overrides Sub FillPolygon(brush As Brush, points As PointF())
        _g.FillPolygon(B(brush), points)
    End Sub

    Public Overrides Sub DrawRectangle(pen As Pen, x As Single, y As Single, w As Single, h As Single)
        _g.DrawRectangle(P(pen), x, y, w, h)
    End Sub

    Public Overrides Sub FillRectangle(brush As Brush, x As Single, y As Single, w As Single, h As Single)
        _g.FillRectangle(B(brush), x, y, w, h)
    End Sub

    Public Overrides Sub DrawEllipse(pen As Pen, x As Single, y As Single, w As Single, h As Single)
        _g.DrawEllipse(P(pen), x, y, w, h)
    End Sub

    Public Overrides Sub FillEllipse(brush As Brush, x As Single, y As Single, w As Single, h As Single)
        _g.FillEllipse(B(brush), x, y, w, h)
    End Sub

    Public Overrides Sub DrawArc(pen As Pen, x As Single, y As Single, w As Single, h As Single, startAngle As Single, sweepAngle As Single)
        _g.DrawArc(P(pen), x, y, w, h, startAngle, sweepAngle)
    End Sub

    Public Overrides Sub DrawString(text As String, font As Font, brush As Brush, x As Single, y As Single)
        _g.DrawString(text, font, B(brush), x, y)
    End Sub

    Public Overrides Function MeasureString(text As String, font As Font) As SizeF
        Return _g.MeasureString(text, font)
    End Function

    Public Overrides Function Save() As Object
        Return _g.Save()
    End Function

    Public Overrides Sub Restore(state As Object)
        _g.Restore(DirectCast(state, GraphicsState))
    End Sub

    Public Overrides Sub MultiplyTransform(m As Matrix)
        _g.MultiplyTransform(m)
    End Sub

    Public Overrides Sub TranslateTransform(dx As Single, dy As Single)
        _g.TranslateTransform(dx, dy)
    End Sub

    Public Overrides Sub ScaleTransform(sx As Single, sy As Single)
        _g.ScaleTransform(sx, sy)
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        For Each pn In _pens.Values : pn.Dispose() : Next
        For Each br In _brushes.Values : br.Dispose() : Next
    End Sub
End Class

''' <summary>A recorded vector shape in drawing (world) coordinates.</summary>
Public Class VectorShape
    Public Points As PointF()
    Public Closed As Boolean
    Public Stroke As Color = Color.Empty
    Public StrokeWidth As Single = 1
    Public Dashed As Boolean
    Public Fill As Color = Color.Empty
    ' Text shapes.
    Public Text As String
    Public TextPos As PointF
    Public FontSize As Single
    Public Bold As Boolean
    Public AngleDeg As Single

    Public ReadOnly Property IsText As Boolean
        Get
            Return Text IsNot Nothing
        End Get
    End Property
End Class

''' <summary>Records everything drawn as vector shapes, for SVG, DXF and PDF export.</summary>
Public Class VectorSurface
    Inherits DrawSurface

    Private _matrix As New Matrix()
    Public ReadOnly Property Shapes As New List(Of VectorShape)

    Private Function T(pts As PointF()) As PointF()
        Dim copy = CType(pts.Clone(), PointF())
        _matrix.TransformPoints(copy)
        Return copy
    End Function

    Private Sub AddPath(pts As PointF(), closed As Boolean, pen As Pen, fill As Color)
        If pts.Length < 2 Then Return
        Dim s As New VectorShape With {.Points = T(pts), .Closed = closed, .Fill = fill}
        If pen IsNot Nothing Then
            s.Stroke = pen.Color
            s.StrokeWidth = pen.Width * ScaleFactor()
            s.Dashed = pen.DashStyle <> DashStyle.Solid
        End If
        Shapes.Add(s)
    End Sub

    Private Function ScaleFactor() As Single
        Dim v = {New PointF(1, 0)}
        _matrix.TransformVectors(v)
        Return CSng(Math.Sqrt(v(0).X * v(0).X + v(0).Y * v(0).Y))
    End Function

    Private Shared Function EllipsePoints(x As Single, y As Single, w As Single, h As Single, start As Single, sweep As Single, n As Integer) As PointF()
        Dim pts(n) As PointF
        Dim cx = x + w / 2, cy = y + h / 2
        For i = 0 To n
            Dim a = (start + sweep * i / n) * Math.PI / 180
            pts(i) = New PointF(CSng(cx + w / 2 * Math.Cos(a)), CSng(cy + h / 2 * Math.Sin(a)))
        Next
        Return pts
    End Function

    Public Overrides Sub DrawLine(pen As Pen, x1 As Single, y1 As Single, x2 As Single, y2 As Single)
        AddPath({New PointF(x1, y1), New PointF(x2, y2)}, False, pen, Color.Empty)
    End Sub

    Public Overrides Sub DrawLines(pen As Pen, points As PointF())
        AddPath(points, False, pen, Color.Empty)
    End Sub

    Public Overrides Sub DrawPolygon(pen As Pen, points As PointF())
        AddPath(points, True, pen, Color.Empty)
    End Sub

    Public Overrides Sub FillPolygon(brush As Brush, points As PointF())
        AddPath(points, True, Nothing, ColorOf(brush))
    End Sub

    Public Overrides Sub DrawEllipse(pen As Pen, x As Single, y As Single, w As Single, h As Single)
        AddPath(EllipsePoints(x, y, w, h, 0, 360, 36), True, pen, Color.Empty)
    End Sub

    Public Overrides Sub FillEllipse(brush As Brush, x As Single, y As Single, w As Single, h As Single)
        AddPath(EllipsePoints(x, y, w, h, 0, 360, 36), True, Nothing, ColorOf(brush))
    End Sub

    Public Overrides Sub DrawArc(pen As Pen, x As Single, y As Single, w As Single, h As Single, startAngle As Single, sweepAngle As Single)
        AddPath(EllipsePoints(x, y, w, h, startAngle, sweepAngle, 18), False, pen, Color.Empty)
    End Sub

    Public Overrides Sub DrawString(text As String, font As Font, brush As Brush, x As Single, y As Single)
        If String.IsNullOrEmpty(text) Then Return
        Dim sz = Measure("X", font)
        Dim lineH = sz.Height * 0.95F
        Dim lines = text.Replace(vbCr, "").Split(ChrW(10))
        Dim angle = CSng(Math.Atan2(_matrix.Elements(1), _matrix.Elements(0)) * 180 / Math.PI)
        For i = 0 To lines.Length - 1
            ' Baseline about 78 % down the GDI+ text box.
            Dim p = T({New PointF(x + font.Size * 0.12F, y + i * lineH + sz.Height * 0.78F)})(0)
            Shapes.Add(New VectorShape With {
                .Text = lines(i), .TextPos = p, .FontSize = font.SizeInPoints * 1.333F * ScaleFactor() * 0.98F,
                .Bold = font.Bold, .Stroke = ColorOf(brush), .AngleDeg = angle})
        Next
    End Sub

    Public Overrides Function MeasureString(text As String, font As Font) As SizeF
        Return Measure(text, font)
    End Function

    Public Overrides Function Save() As Object
        Return _matrix.Clone()
    End Function

    Public Overrides Sub Restore(state As Object)
        _matrix = DirectCast(state, Matrix).Clone()
    End Sub

    Public Overrides Sub MultiplyTransform(m As Matrix)
        _matrix.Multiply(m, MatrixOrder.Prepend)
    End Sub

    Public Overrides Sub TranslateTransform(dx As Single, dy As Single)
        _matrix.Translate(dx, dy, MatrixOrder.Prepend)
    End Sub

    Public Overrides Sub ScaleTransform(sx As Single, sy As Single)
        _matrix.Scale(sx, sy, MatrixOrder.Prepend)
    End Sub

    ''' <summary>Bounding box of everything recorded.</summary>
    Public Function Bounds() As RectangleF
        Dim xs As New List(Of Single), ys As New List(Of Single)
        For Each s In Shapes
            If s.IsText Then
                xs.Add(s.TextPos.X) : ys.Add(s.TextPos.Y)
                xs.Add(s.TextPos.X + s.FontSize * 0.6F * s.Text.Length) : ys.Add(s.TextPos.Y - s.FontSize)
            Else
                For Each p In s.Points
                    xs.Add(p.X) : ys.Add(p.Y)
                Next
            End If
        Next
        If xs.Count = 0 Then Return RectangleF.Empty
        Return RectangleF.FromLTRB(xs.Min(), ys.Min(), xs.Max(), ys.Max())
    End Function
End Class
