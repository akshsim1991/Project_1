Imports System.ComponentModel
Imports System.Drawing.Drawing2D

''' <summary>What a left click or drag on the canvas does.</summary>
Public Enum CanvasTool
    ''' <summary>Left-drag moves the image; nothing is recorded.</summary>
    Pan
    ''' <summary>Each click adds one point to the active curve.</summary>
    AddPoint
    ''' <summary>Dragging adds points spaced <see cref="GraphCanvas.DragSpacing"/> image pixels apart.</summary>
    DragTrace
    ''' <summary>Drag points to move them, Delete removes the selected point, arrow keys nudge it.</summary>
    EditPoints
    ''' <summary>A click starts automatic tracing of the curve under the mouse.</summary>
    AutoTrace
    ''' <summary>A click sets a calibration reference point.</summary>
    PickReference
End Enum

Public Class ImageClickEventArgs
    Inherits EventArgs
    ''' <summary>Position in image pixel coordinates (continuous; the centre of pixel (i, j) is (i + 0.5, j + 0.5)).</summary>
    Public ReadOnly Property ImagePoint As PointD
    Public ReadOnly Property Tool As CanvasTool

    Public Sub New(imagePoint As PointD, tool As CanvasTool)
        Me.ImagePoint = imagePoint
        Me.Tool = tool
    End Sub
End Class

''' <summary>
''' Shows the base graph with zoom and pan, the calibration markers, every traced curve, an optional calibrated
''' grid and fitted curve, and a magnifier around the mouse. Mouse wheel zooms; right- or middle-drag pans.
''' </summary>
Public Class GraphCanvas
    Inherits Control

    Private Const MinZoom As Double = 0.02
    Private Const MaxZoom As Double = 40
    Private Const HitRadius As Double = 7
    Private Const MagnifierSize As Integer = 170

    Private Shared ReadOnly XReferenceColor As Color = Color.FromArgb(0, 110, 220)
    Private Shared ReadOnly YReferenceColor As Color = Color.FromArgb(0, 150, 70)
    Private Shared ReadOnly GridColor As Color = Color.FromArgb(150, 0, 170, 200)

    ''' <summary>Raised for clicks the form handles: adding points, automatic trace and calibration picks.</summary>
    Public Event ImageClicked As EventHandler(Of ImageClickEventArgs)
    ''' <summary>Raised just before the canvas itself changes points (dragging, editing), so the form can record undo.</summary>
    Public Event PointsChanging As EventHandler
    ''' <summary>Raised after the canvas changed points.</summary>
    Public Event PointsChanged As EventHandler
    Public Event HoverChanged As EventHandler
    Public Event SelectedIndexChanged As EventHandler
    Public Event ViewChanged As EventHandler

    Private _image As Bitmap
    Private _scaledCache As Bitmap
    Private _scaledCacheZoom As Double
    Private _zoom As Double = 1
    Private _offset As PointD
    Private _tool As CanvasTool = CanvasTool.AddPoint
    Private _selectedIndex As Integer = -1
    ' True when an image was set before the control had a usable size; it is fitted on the next resize.
    Private _fitPending As Boolean

    ' Mouse interaction state.
    Private _panning As Boolean
    Private _panLast As Point
    Private _pressButton As MouseButtons = MouseButtons.None
    Private _pressScreen As Point
    Private _dragMoved As Boolean
    Private _lastTracePoint As PointD
    Private _movingPoint As Boolean
    Private _changeStarted As Boolean

    Public Sub New()
        SetStyle(ControlStyles.OptimizedDoubleBuffer Or ControlStyles.AllPaintingInWmPaint Or ControlStyles.UserPaint Or
                 ControlStyles.ResizeRedraw Or ControlStyles.Selectable, True)
        BackColor = Color.FromArgb(58, 58, 62)
        ForeColor = Color.Gainsboro
        TabStop = True
        ' Lets screen readers and UI automation find the graph area.
        AccessibleName = "Graph canvas"
        AccessibleRole = AccessibleRole.Graphic
    End Sub

#Region "Properties"

    ''' <summary>The base graph. The canvas does not dispose it; the owner does.</summary>
    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public Property Image As Bitmap
        Get
            Return _image
        End Get
        Set(value As Bitmap)
            _image = value
            ClearScaledCache()
            FitToWindow()
        End Set
    End Property

    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public Property Project As GraphProject

    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public Property ActiveSeries As DataSeries

    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public Property Tool As CanvasTool
        Get
            Return _tool
        End Get
        Set(value As CanvasTool)
            _tool = value
            Cursor = If(value = CanvasTool.Pan, Cursors.SizeAll, If(value = CanvasTool.EditPoints, Cursors.Default, Cursors.Cross))
            Invalidate()
        End Set
    End Property

    ''' <summary>Name of the reference being picked ("X1" …), shown next to the cursor.</summary>
    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public Property PickingReferenceName As String

    ''' <summary>Distance between points recorded while drag-tracing, in image pixels.</summary>
    <DefaultValue(5.0)>
    Public Property DragSpacing As Double = 5

    <DefaultValue(True)>
    Public Property ShowMagnifier As Boolean = True

    <DefaultValue(False)>
    Public Property ShowGrid As Boolean

    <DefaultValue(True)>
    Public Property ShowLines As Boolean = True

    ''' <summary>A fitted curve to draw, as image-pixel points, or Nothing.</summary>
    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public Property FitCurve As IList(Of PointD)

    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public ReadOnly Property HoverImagePoint As PointD?

    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public Property SelectedIndex As Integer
        Get
            Return _selectedIndex
        End Get
        Set(value As Integer)
            If value = _selectedIndex Then Return
            _selectedIndex = value
            Invalidate()
            RaiseEvent SelectedIndexChanged(Me, EventArgs.Empty)
        End Set
    End Property

    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Public ReadOnly Property Zoom As Double
        Get
            Return _zoom
        End Get
    End Property

#End Region

#Region "Zoom and pan"

    Public Sub FitToWindow()
        If _image Is Nothing OrElse ClientSize.Width < 10 OrElse ClientSize.Height < 10 Then
            _zoom = 1
            _offset = New PointD(0, 0)
            _fitPending = _image IsNot Nothing
        Else
            _fitPending = False
            Const margin = 12
            _zoom = Math.Min((ClientSize.Width - 2 * margin) / _image.Width, (ClientSize.Height - 2 * margin) / _image.Height)
            _zoom = Math.Max(MinZoom, Math.Min(MaxZoom, _zoom))
            _offset = New PointD((ClientSize.Width - _image.Width * _zoom) / 2, (ClientSize.Height - _image.Height * _zoom) / 2)
        End If
        Invalidate()
        RaiseEvent ViewChanged(Me, EventArgs.Empty)
    End Sub

    ''' <summary>Zooms by <paramref name="factor"/>, keeping the image point under <paramref name="screenAnchor"/> still.</summary>
    Public Sub ZoomBy(factor As Double, Optional screenAnchor As Point? = Nothing)
        If _image Is Nothing Then Return
        Dim anchor = If(screenAnchor, New Point(ClientSize.Width \ 2, ClientSize.Height \ 2))
        Dim before = ScreenToImage(anchor)
        _zoom = Math.Max(MinZoom, Math.Min(MaxZoom, _zoom * factor))
        _offset = New PointD(anchor.X - before.X * _zoom, anchor.Y - before.Y * _zoom)
        Invalidate()
        RaiseEvent ViewChanged(Me, EventArgs.Empty)
    End Sub

    Public Function ImageToScreen(p As PointD) As PointF
        Return New PointF(CSng(_offset.X + p.X * _zoom), CSng(_offset.Y + p.Y * _zoom))
    End Function

    Public Function ScreenToImage(p As Point) As PointD
        Return New PointD((p.X - _offset.X) / _zoom, (p.Y - _offset.Y) / _zoom)
    End Function

    Private Function IsOnImage(p As PointD) As Boolean
        Return _image IsNot Nothing AndAlso p.X >= 0 AndAlso p.Y >= 0 AndAlso p.X < _image.Width AndAlso p.Y < _image.Height
    End Function

    Protected Overrides Sub OnResize(e As EventArgs)
        MyBase.OnResize(e)
        If _fitPending Then FitToWindow() Else Invalidate()
    End Sub

#End Region

#Region "Mouse and keyboard"

    Protected Overrides Sub OnMouseWheel(e As MouseEventArgs)
        MyBase.OnMouseWheel(e)
        ZoomBy(If(e.Delta > 0, 1.25, 1 / 1.25), e.Location)
    End Sub

    Protected Overrides Sub OnMouseDown(e As MouseEventArgs)
        MyBase.OnMouseDown(e)
        Focus()
        If _pressButton <> MouseButtons.None Then Return
        _pressButton = e.Button
        _pressScreen = e.Location
        _dragMoved = False
        Dim p = ScreenToImage(e.Location)

        If e.Button = MouseButtons.Right OrElse e.Button = MouseButtons.Middle OrElse
           (e.Button = MouseButtons.Left AndAlso _tool = CanvasTool.Pan) Then
            StartPan(e.Location)
            Return
        End If
        If e.Button <> MouseButtons.Left OrElse _image Is Nothing Then Return

        Select Case _tool
            Case CanvasTool.DragTrace
                If ActiveSeries Is Nothing OrElse Not IsOnImage(p) Then
                    RaiseEvent ImageClicked(Me, New ImageClickEventArgs(p, _tool))
                    _pressButton = MouseButtons.None
                    Return
                End If
                BeginChange()
                ActiveSeries.Points.Add(p)
                _lastTracePoint = p
                Invalidate()
            Case CanvasTool.EditPoints
                Dim hit = HitTest(e.Location)
                SelectedIndex = hit
                If hit >= 0 Then
                    _movingPoint = True
                Else
                    StartPan(e.Location)
                End If
        End Select
    End Sub

    Protected Overrides Sub OnMouseMove(e As MouseEventArgs)
        MyBase.OnMouseMove(e)
        Dim p = ScreenToImage(e.Location)
        _HoverImagePoint = If(IsOnImage(p), p, CType(Nothing, PointD?))
        RaiseEvent HoverChanged(Me, EventArgs.Empty)

        If _pressButton <> MouseButtons.None AndAlso
           (Math.Abs(e.X - _pressScreen.X) > 3 OrElse Math.Abs(e.Y - _pressScreen.Y) > 3) Then
            _dragMoved = True
        End If

        If _panning Then
            _offset = New PointD(_offset.X + e.X - _panLast.X, _offset.Y + e.Y - _panLast.Y)
            _panLast = e.Location
            RaiseEvent ViewChanged(Me, EventArgs.Empty)
        ElseIf _pressButton = MouseButtons.Left AndAlso _tool = CanvasTool.DragTrace AndAlso ActiveSeries IsNot Nothing Then
            If IsOnImage(p) AndAlso p.DistanceTo(_lastTracePoint) >= Math.Max(0.5, DragSpacing) Then
                ActiveSeries.Points.Add(p)
                _lastTracePoint = p
            End If
        ElseIf _movingPoint AndAlso _dragMoved AndAlso ActiveSeries IsNot Nothing AndAlso _selectedIndex >= 0 AndAlso _selectedIndex < ActiveSeries.Points.Count Then
            BeginChange()
            ActiveSeries.Points(_selectedIndex) = ClampToImage(p)
        End If
        Invalidate()
    End Sub

    Protected Overrides Sub OnMouseUp(e As MouseEventArgs)
        MyBase.OnMouseUp(e)
        If e.Button <> _pressButton Then Return
        Dim wasClick = Not _dragMoved
        Dim p = ScreenToImage(e.Location)
        _pressButton = MouseButtons.None
        _panning = False
        _movingPoint = False

        If e.Button = MouseButtons.Left AndAlso wasClick AndAlso _image IsNot Nothing Then
            Select Case _tool
                Case CanvasTool.AddPoint, CanvasTool.AutoTrace, CanvasTool.PickReference
                    RaiseEvent ImageClicked(Me, New ImageClickEventArgs(p, _tool))
            End Select
        ElseIf e.Button = MouseButtons.Right AndAlso wasClick AndAlso _tool = CanvasTool.EditPoints Then
            ' Right-click on a point deletes it.
            Dim hit = HitTest(e.Location)
            If hit >= 0 Then
                SelectedIndex = hit
                DeleteSelectedPoint()
            End If
        End If
        EndChange()
        Invalidate()
    End Sub

    Protected Overrides Sub OnMouseLeave(e As EventArgs)
        MyBase.OnMouseLeave(e)
        _HoverImagePoint = Nothing
        RaiseEvent HoverChanged(Me, EventArgs.Empty)
        Invalidate()
    End Sub

    Protected Overrides Function IsInputKey(keyData As Keys) As Boolean
        Select Case keyData And Keys.KeyCode
            Case Keys.Left, Keys.Right, Keys.Up, Keys.Down
                Return True
        End Select
        Return MyBase.IsInputKey(keyData)
    End Function

    Protected Overrides Sub OnKeyDown(e As KeyEventArgs)
        MyBase.OnKeyDown(e)
        If _tool <> CanvasTool.EditPoints OrElse ActiveSeries Is Nothing OrElse _selectedIndex < 0 OrElse _selectedIndex >= ActiveSeries.Points.Count Then Return
        If e.KeyCode = Keys.Delete OrElse e.KeyCode = Keys.Back Then
            DeleteSelectedPoint()
            e.Handled = True
            Return
        End If
        ' Arrow keys nudge the selected point by one image pixel (a tenth with Shift) for precise placement.
        Dim nudge = If(e.Shift, 0.1, 1.0)
        Dim dx = 0.0, dy = 0.0
        Select Case e.KeyCode
            Case Keys.Left : dx = -nudge
            Case Keys.Right : dx = nudge
            Case Keys.Up : dy = -nudge
            Case Keys.Down : dy = nudge
            Case Else : Return
        End Select
        BeginChange()
        Dim old = ActiveSeries.Points(_selectedIndex)
        ActiveSeries.Points(_selectedIndex) = ClampToImage(New PointD(old.X + dx, old.Y + dy))
        EndChange()
        e.Handled = True
        Invalidate()
    End Sub

    Public Sub DeleteSelectedPoint()
        If ActiveSeries Is Nothing OrElse _selectedIndex < 0 OrElse _selectedIndex >= ActiveSeries.Points.Count Then Return
        BeginChange()
        ActiveSeries.Points.RemoveAt(_selectedIndex)
        EndChange()
        SelectedIndex = Math.Min(_selectedIndex, ActiveSeries.Points.Count - 1)
        Invalidate()
    End Sub

    Private Sub StartPan(location As Point)
        _panning = True
        _panLast = location
    End Sub

    Private Sub BeginChange()
        If _changeStarted Then Return
        _changeStarted = True
        RaiseEvent PointsChanging(Me, EventArgs.Empty)
    End Sub

    Private Sub EndChange()
        If Not _changeStarted Then Return
        _changeStarted = False
        RaiseEvent PointsChanged(Me, EventArgs.Empty)
    End Sub

    ''' <summary>Index of the active curve's point under the mouse, or -1.</summary>
    Private Function HitTest(screenPoint As Point) As Integer
        If ActiveSeries Is Nothing Then Return -1
        Dim best = -1
        Dim bestDistance = HitRadius
        For i = 0 To ActiveSeries.Points.Count - 1
            Dim s = ImageToScreen(ActiveSeries.Points(i))
            Dim d = Math.Sqrt((s.X - screenPoint.X) ^ 2 + (s.Y - screenPoint.Y) ^ 2)
            If d <= bestDistance Then
                bestDistance = d
                best = i
            End If
        Next
        Return best
    End Function

    Private Function ClampToImage(p As PointD) As PointD
        If _image Is Nothing Then Return p
        Return New PointD(Math.Max(0, Math.Min(_image.Width - 0.001, p.X)), Math.Max(0, Math.Min(_image.Height - 0.001, p.Y)))
    End Function

#End Region

#Region "Painting"

    Protected Overrides Sub OnPaint(e As PaintEventArgs)
        Dim g = e.Graphics
        g.Clear(BackColor)
        If _image Is Nothing Then
            Using brush As New SolidBrush(ForeColor), labelFont As New Font(Me.Font.FontFamily, 12)
                Dim text = "Open a graph image to start." & vbCrLf & vbCrLf &
                           "File ▸ Open Image (Ctrl+O), paste a screenshot (Ctrl+V)," & vbCrLf & "or drag an image or project file here."
                Dim format As New StringFormat With {.Alignment = StringAlignment.Center, .LineAlignment = StringAlignment.Center}
                g.DrawString(text, labelFont, brush, ClientRectangle, format)
            End Using
            Return
        End If

        DrawImage(g)
        g.SmoothingMode = SmoothingMode.AntiAlias
        If ShowGrid Then DrawCalibratedGrid(g)
        DrawReferences(g)
        DrawSeries(g)
        DrawFitCurve(g)
        DrawPickHint(g)
        If ShowMagnifier AndAlso HoverImagePoint.HasValue AndAlso _pressButton = MouseButtons.None OrElse
           ShowMagnifier AndAlso HoverImagePoint.HasValue AndAlso (_tool = CanvasTool.DragTrace OrElse _movingPoint) Then
            DrawMagnifier(g, HoverImagePoint.Value)
        End If
    End Sub

    Private Sub DrawImage(g As Graphics)
        Dim dest = New RectangleF(CSng(_offset.X), CSng(_offset.Y), CSng(_image.Width * _zoom), CSng(_image.Height * _zoom))
        If _zoom < 1 Then
            ' Shrinking a big scan on every repaint is slow; draw it from a copy scaled once for this zoom level.
            If _scaledCache Is Nothing OrElse _scaledCacheZoom <> _zoom Then
                ClearScaledCache()
                Dim w = Math.Max(1, CInt(Math.Round(_image.Width * _zoom))), h = Math.Max(1, CInt(Math.Round(_image.Height * _zoom)))
                _scaledCache = New Bitmap(w, h, Imaging.PixelFormat.Format32bppPArgb)
                Using cg = Graphics.FromImage(_scaledCache)
                    cg.InterpolationMode = InterpolationMode.HighQualityBicubic
                    cg.PixelOffsetMode = PixelOffsetMode.HighQuality
                    cg.DrawImage(_image, New Rectangle(0, 0, w, h))
                End Using
                _scaledCacheZoom = _zoom
            End If
            g.DrawImageUnscaled(_scaledCache, CInt(Math.Round(_offset.X)), CInt(Math.Round(_offset.Y)))
        Else
            ' Enlarged: show crisp pixels, drawing only the visible part.
            g.InterpolationMode = InterpolationMode.NearestNeighbor
            g.PixelOffsetMode = PixelOffsetMode.Half
            Dim visible = RectangleF.Intersect(dest, ClientRectangle)
            If visible.Width <= 0 OrElse visible.Height <= 0 Then Return
            Dim src = New RectangleF(CSng((visible.X - _offset.X) / _zoom), CSng((visible.Y - _offset.Y) / _zoom),
                                     CSng(visible.Width / _zoom), CSng(visible.Height / _zoom))
            g.DrawImage(_image, visible, src, GraphicsUnit.Pixel)
            g.PixelOffsetMode = PixelOffsetMode.Default
        End If
    End Sub

    Private Sub ClearScaledCache()
        _scaledCache?.Dispose()
        _scaledCache = Nothing
    End Sub

    Private Sub DrawReferences(g As Graphics)
        If Project Is Nothing Then Return
        For Each item In Project.Calibration.References()
            If Not item.Reference.Pixel.HasValue Then Continue For
            Dim markerColor = If(item.Name.StartsWith("X"), XReferenceColor, YReferenceColor)
            Dim s = ImageToScreen(item.Reference.Pixel.Value)
            Using pen As New Pen(markerColor, 2), halo As New Pen(Color.FromArgb(200, Color.White), 4), brush As New SolidBrush(markerColor),
                  labelFont As New Font(Me.Font, FontStyle.Bold)
                For Each p In {halo, pen}
                    g.DrawLine(p, s.X - 9, s.Y, s.X - 3, s.Y)
                    g.DrawLine(p, s.X + 3, s.Y, s.X + 9, s.Y)
                    g.DrawLine(p, s.X, s.Y - 9, s.X, s.Y - 3)
                    g.DrawLine(p, s.X, s.Y + 3, s.X, s.Y + 9)
                Next
                Dim label = item.Name
                If item.Reference.Value.HasValue Then label &= " = " & DataTools.FormatNumber(item.Reference.Value.Value)
                DrawLabel(g, label, labelFont, brush, New PointF(s.X + 8, s.Y + 6))
            End Using
        Next
    End Sub

    Private Sub DrawSeries(g As Graphics)
        If Project Is Nothing Then Return
        ' Inactive curves first, the active one on top.
        Dim active = If(ActiveSeries Is Nothing, Enumerable.Empty(Of DataSeries)(), New DataSeries() {ActiveSeries})
        For Each s In Project.Series.Where(Function(x) x IsNot ActiveSeries).Concat(active)
            Dim isActive = s Is ActiveSeries
            Dim seriesColor = Color.FromArgb(s.ColorArgb)
            If s.Points.Count = 0 Then Continue For
            Dim screen = s.Points.Select(Function(p) ImageToScreen(p)).ToArray()
            If ShowLines AndAlso screen.Length >= 2 Then
                Using pen As New Pen(Color.FromArgb(If(isActive, 230, 150), seriesColor), If(isActive, 1.8F, 1.2F))
                    g.DrawLines(pen, screen)
                End Using
            End If
            Dim size = If(isActive, 6.0F, 4.5F)
            Using brush As New SolidBrush(seriesColor), outline As New Pen(Color.White, 1)
                For Each p In screen
                    Dim r = New RectangleF(p.X - size / 2, p.Y - size / 2, size, size)
                    g.FillRectangle(brush, r)
                    If isActive Then g.DrawRectangle(outline, r.X, r.Y, r.Width, r.Height)
                Next
            End Using
            If isActive AndAlso _selectedIndex >= 0 AndAlso _selectedIndex < screen.Length Then
                Dim p = screen(_selectedIndex)
                Using ring As New Pen(Color.Black, 2), ring2 As New Pen(Color.Yellow, 2)
                    g.DrawEllipse(ring, p.X - 8, p.Y - 8, 16, 16)
                    g.DrawEllipse(ring2, p.X - 6, p.Y - 6, 12, 12)
                End Using
            End If
        Next
    End Sub

    Private Sub DrawFitCurve(g As Graphics)
        If FitCurve Is Nothing OrElse FitCurve.Count < 2 Then Return
        Using pen As New Pen(Color.FromArgb(220, 20, 20, 20), 2) With {.DashStyle = DashStyle.Dash}
            ' Split the line wherever the fit is undefined (NaN) so no false segments are drawn.
            Dim run As New List(Of PointF)
            For Each p In FitCurve.Concat({New PointD(Double.NaN, Double.NaN)})
                If p.IsValid Then
                    run.Add(ImageToScreen(p))
                Else
                    If run.Count >= 2 Then g.DrawLines(pen, run.ToArray())
                    run.Clear()
                End If
            Next
        End Using
    End Sub

    ''' <summary>Round-valued gridlines computed from the calibration; they should sit on the graph's own gridlines.</summary>
    Private Sub DrawCalibratedGrid(g As Graphics)
        If Project Is Nothing OrElse Not Project.Calibration.IsComplete Then Return
        Dim cal = Project.Calibration
        Dim corners = {New PointD(0, 0), New PointD(_image.Width, 0), New PointD(0, _image.Height), New PointD(_image.Width, _image.Height)}.
            Select(Function(c) cal.PixelToData(c)).Where(Function(d) d.IsValid).ToList()
        If corners.Count < 2 Then Return
        Dim minX = corners.Min(Function(d) d.X), maxX = corners.Max(Function(d) d.X)
        Dim minY = corners.Min(Function(d) d.Y), maxY = corners.Max(Function(d) d.Y)
        Using pen As New Pen(GridColor, 1), brush As New SolidBrush(Color.FromArgb(0, 120, 150)), labelFont As New Font(Me.Font.FontFamily, 7.5F)
            For Each x In AxisTicks.Generate(minX, maxX, 10, cal.LogX)
                Dim a = ImageToScreen(cal.DataToPixel(New PointD(x, minY))), b = ImageToScreen(cal.DataToPixel(New PointD(x, maxY)))
                g.DrawLine(pen, a, b)
                DrawLabel(g, DataTools.FormatNumber(x), labelFont, brush, If(a.Y > b.Y, New PointF(a.X + 2, Math.Min(a.Y, ClientSize.Height) - 14), New PointF(b.X + 2, Math.Min(b.Y, ClientSize.Height) - 14)))
            Next
            For Each y In AxisTicks.Generate(minY, maxY, 10, cal.LogY)
                Dim a = ImageToScreen(cal.DataToPixel(New PointD(minX, y))), b = ImageToScreen(cal.DataToPixel(New PointD(maxX, y)))
                g.DrawLine(pen, a, b)
                DrawLabel(g, DataTools.FormatNumber(y), labelFont, brush, If(a.X < b.X, New PointF(Math.Max(a.X, 0) + 2, a.Y - 13), New PointF(Math.Max(b.X, 0) + 2, b.Y - 13)))
            Next
        End Using
    End Sub

    Private Sub DrawPickHint(g As Graphics)
        If _tool <> CanvasTool.PickReference OrElse String.IsNullOrEmpty(PickingReferenceName) OrElse Not HoverImagePoint.HasValue Then Return
        Dim s = ImageToScreen(HoverImagePoint.Value)
        Using brush As New SolidBrush(If(PickingReferenceName.StartsWith("X"), XReferenceColor, YReferenceColor)), labelFont As New Font(Me.Font, FontStyle.Bold)
            DrawLabel(g, "Click to set " & PickingReferenceName, labelFont, brush, New PointF(s.X + 14, s.Y - 22))
        End Using
    End Sub

    Private Sub DrawMagnifier(g As Graphics, center As PointD)
        ' Place the magnifier in the top-left corner, or top-right if the mouse is there.
        Dim mouseScreen = ImageToScreen(center)
        Dim box = New Rectangle(10, 10, MagnifierSize, MagnifierSize)
        If mouseScreen.X < box.Right + 30 AndAlso mouseScreen.Y < box.Bottom + 30 Then box.X = ClientSize.Width - MagnifierSize - 10
        Dim magnification = Math.Max(4.0, _zoom * 3)
        Dim srcSize = MagnifierSize / magnification
        Dim src = New RectangleF(CSng(center.X - srcSize / 2), CSng(center.Y - srcSize / 2), CSng(srcSize), CSng(srcSize))

        Dim state = g.Save()
        Using path As New GraphicsPath()
            path.AddRectangle(box)
            g.SetClip(path)
        End Using
        g.Clear(BackColor)
        g.InterpolationMode = InterpolationMode.NearestNeighbor
        g.PixelOffsetMode = PixelOffsetMode.Half
        g.DrawImage(_image, box, src, GraphicsUnit.Pixel)
        g.PixelOffsetMode = PixelOffsetMode.Default
        ' Points of the active curve inside the magnifier.
        If ActiveSeries IsNot Nothing Then
            Using brush As New SolidBrush(Color.FromArgb(ActiveSeries.ColorArgb))
                For Each p In ActiveSeries.Points
                    If Not src.Contains(CSng(p.X), CSng(p.Y)) Then Continue For
                    Dim mx = CSng(box.X + (p.X - src.X) * magnification), my = CSng(box.Y + (p.Y - src.Y) * magnification)
                    g.FillEllipse(brush, mx - 3, my - 3, 6, 6)
                Next
            End Using
        End If
        Dim cx = box.X + box.Width / 2.0F, cy = box.Y + box.Height / 2.0F
        Using pen As New Pen(Color.FromArgb(220, 255, 0, 0), 1)
            g.DrawLine(pen, box.X, cy, cx - 5, cy)
            g.DrawLine(pen, cx + 5, cy, box.Right, cy)
            g.DrawLine(pen, cx, box.Y, cx, cy - 5)
            g.DrawLine(pen, cx, cy + 5, cx, box.Bottom)
        End Using
        g.Restore(state)
        Using border As New Pen(Color.Black, 2)
            g.DrawRectangle(border, box)
        End Using
    End Sub

    Private Shared Sub DrawLabel(g As Graphics, text As String, labelFont As Font, brush As Brush, location As PointF)
        Dim size = g.MeasureString(text, labelFont)
        Using background As New SolidBrush(Color.FromArgb(215, Color.White))
            g.FillRectangle(background, location.X - 2, location.Y - 1, size.Width + 2, size.Height)
        End Using
        g.DrawString(text, labelFont, brush, location)
    End Sub

    Protected Overrides Sub Dispose(disposing As Boolean)
        If disposing Then ClearScaledCache()
        MyBase.Dispose(disposing)
    End Sub

#End Region

End Class
