Imports System.Drawing.Drawing2D

''' <summary>The drawing area: shows the circuit and handles editing and operating it.</summary>
Public Class CircuitCanvas
    Inherits ScrollableControl

    Public Const GridSize As Single = 10
    Private Shared ReadOnly WorkArea As New SizeF(2400, 1600)

    Private _circuit As New Circuit()
    Private _zoom As Single = 1.0F
    Private _selectedElement As CircuitElement
    Private _selectedTube As Tube

    ' Interaction state.
    Private _dragging As Boolean
    Private _dragOffset As PointF
    Private _dragMoved As Boolean
    Private _connectFrom As Port
    Private _mouseWorld As PointF
    Private _hoverPort As Port
    Private _pressedElement As CircuitElement

    Public Event SelectionChanged As EventHandler
    ''' <summary>Raised whenever the user modifies the circuit.</summary>
    Public Event CircuitModified As EventHandler
    ''' <summary>Raised when a manual actuator was operated during simulation.</summary>
    Public Event ElementOperated As EventHandler
    Public Event StatusMessage As EventHandler(Of String)

    Public Sub New()
        SetStyle(ControlStyles.AllPaintingInWmPaint Or ControlStyles.UserPaint Or
                 ControlStyles.OptimizedDoubleBuffer Or ControlStyles.ResizeRedraw Or
                 ControlStyles.Selectable, True)
        BackColor = Color.White
        AutoScroll = True
        AllowDrop = True
        UpdateScrollSize()
    End Sub

    ' ---------------------------------------------------------------- properties

    Public Property Circuit As Circuit
        Get
            Return _circuit
        End Get
        Set(value As Circuit)
            _circuit = value
            ClearSelection()
            Invalidate()
        End Set
    End Property

    ''' <summary>Simulator used while <see cref="Simulating"/> is True.</summary>
    Public Property Simulator As Simulator

    Public Property Simulating As Boolean

    ''' <summary>Library entry waiting to be placed with the next click.</summary>
    Public Property PlacingPreset As LibraryPreset

    Public Property Zoom As Single
        Get
            Return _zoom
        End Get
        Set(value As Single)
            _zoom = Math.Max(0.4F, Math.Min(3.0F, value))
            UpdateScrollSize()
            Invalidate()
        End Set
    End Property

    Public ReadOnly Property SelectedElement As CircuitElement
        Get
            Return _selectedElement
        End Get
    End Property

    Public ReadOnly Property HasSelection As Boolean
        Get
            Return _selectedElement IsNot Nothing OrElse _selectedTube IsNot Nothing
        End Get
    End Property

    Private Sub UpdateScrollSize()
        AutoScrollMinSize = New Size(CInt(WorkArea.Width * _zoom), CInt(WorkArea.Height * _zoom))
    End Sub

    ' ---------------------------------------------------------------- commands

    Public Sub ClearSelection()
        _selectedElement = Nothing
        _selectedTube = Nothing
        RaiseEvent SelectionChanged(Me, EventArgs.Empty)
        Invalidate()
    End Sub

    Private Sub SelectElement(e As CircuitElement)
        _selectedElement = e
        _selectedTube = Nothing
        RaiseEvent SelectionChanged(Me, EventArgs.Empty)
        Invalidate()
    End Sub

    Public Sub DeleteSelection()
        If Simulating Then Return
        If _selectedElement IsNot Nothing Then
            _circuit.Remove(_selectedElement)
        ElseIf _selectedTube IsNot Nothing Then
            _circuit.RemoveTube(_selectedTube)
        Else
            Return
        End If
        ClearSelection()
        RaiseEvent CircuitModified(Me, EventArgs.Empty)
    End Sub

    Public Sub RotateSelection()
        If Simulating OrElse _selectedElement Is Nothing Then Return
        ' Rotate about the symbol's centre so it stays where it is.
        Dim before = _selectedElement.WorldBounds()
        _selectedElement.Rotation = (_selectedElement.Rotation + 1) Mod 4
        Dim after = _selectedElement.WorldBounds()
        _selectedElement.X = Snap(_selectedElement.X + (before.X + before.Width / 2) - (after.X + after.Width / 2))
        _selectedElement.Y = Snap(_selectedElement.Y + (before.Y + before.Height / 2) - (after.Y + after.Height / 2))
        Invalidate()
        RaiseEvent CircuitModified(Me, EventArgs.Empty)
    End Sub

    Public Sub PlaceElement(preset As LibraryPreset, world As PointF)
        Dim e = preset.Factory.Invoke()
        Dim b = e.LocalBounds
        ' Put the symbol's centre under the cursor, snapped to the grid.
        _circuit.Add(e, Snap(world.X - b.Width / 2 - b.Left), Snap(world.Y - b.Height / 2 - b.Top))
        SelectElement(e)
        RaiseEvent CircuitModified(Me, EventArgs.Empty)
    End Sub

    Private Shared Function Snap(v As Single) As Single
        Return CSng(Math.Round(v / GridSize) * GridSize)
    End Function

    Private Function ToWorld(p As Point) As PointF
        Return New PointF((p.X - AutoScrollPosition.X) / _zoom, (p.Y - AutoScrollPosition.Y) / _zoom)
    End Function

    ' ---------------------------------------------------------------- painting

    Protected Overrides Sub OnPaint(e As PaintEventArgs)
        Dim g = e.Graphics
        g.SmoothingMode = SmoothingMode.AntiAlias
        g.TextRenderingHint = Drawing.Text.TextRenderingHint.ClearTypeGridFit
        g.TranslateTransform(AutoScrollPosition.X, AutoScrollPosition.Y)
        g.ScaleTransform(_zoom, _zoom)
        If Not Simulating Then DrawGrid(g, e.ClipRectangle)
        DrawContent(g, interactive:=True)
    End Sub

    ''' <summary>Draws the circuit (without grid, ports or selection) onto any graphics surface.</summary>
    Public Sub PaintTo(g As Graphics)
        g.SmoothingMode = SmoothingMode.AntiAlias
        DrawContent(g, interactive:=False)
    End Sub

    Private Sub DrawContent(g As Graphics, interactive As Boolean)
        Using ctx As New RenderContext() With {.Simulating = Simulating}
            DrawTubes(g, ctx)
            For Each el In _circuit.Elements
                Dim state = g.Save()
                Using m = el.GetMatrix()
                    g.MultiplyTransform(m)
                End Using
                el.DrawSymbol(g, ctx)
                g.Restore(state)
                If Not String.IsNullOrEmpty(el.Label) Then
                    Dim a = el.ToWorld(el.LabelAnchor)
                    Using f As New Font("Segoe UI", 8.5F, FontStyle.Bold)
                        g.DrawString(el.Label, f, Brushes.Black, a.X, a.Y - 15)
                    End Using
                End If
            Next
            DrawPorts(g, interactive)
            If Not interactive Then Return
            DrawSelection(g)
            If _connectFrom IsNot Nothing Then
                Using p As New Pen(Color.FromArgb(0, 140, 70), 1.5F) With {.DashStyle = DashStyle.Dash}
                    g.DrawLines(p, Tube.RouteBetween(_connectFrom.WorldPos(), _connectFrom.WorldDir(), _mouseWorld,
                                                     New PointF(-_connectFrom.WorldDir().X, -_connectFrom.WorldDir().Y)))
                End Using
            End If
        End Using
    End Sub

    Private Sub DrawGrid(g As Graphics, clip As Rectangle)
        Dim tl = ToWorld(clip.Location)
        Dim br = ToWorld(New Point(clip.Right, clip.Bottom))
        Dim stepSize = GridSize * If(_zoom < 0.8F, 2, 1)
        Using b As New SolidBrush(Color.FromArgb(205, 210, 220))
            Dim x0 = CSng(Math.Floor(tl.X / stepSize) * stepSize)
            Dim y0 = CSng(Math.Floor(tl.Y / stepSize) * stepSize)
            Dim y = y0
            While y <= br.Y
                Dim x = x0
                While x <= br.X
                    g.FillRectangle(b, x - 0.6F, y - 0.6F, 1.2F, 1.2F)
                    x += stepSize
                End While
                y += stepSize
            End While
        End Using
    End Sub

    Private Sub DrawTubes(g As Graphics, ctx As RenderContext)
        Using idle As New Pen(If(Simulating, RenderContext.IdleTubeColor, Color.Black), 1.6F),
              highlight As New Pen(Color.FromArgb(120, 255, 170, 0), 7)
            highlight.LineJoin = LineJoin.Round
            For Each t In _circuit.Tubes
                Dim pts = t.Route()
                If t Is _selectedTube Then g.DrawLines(highlight, pts)
                g.DrawLines(If(Simulating AndAlso t.IsPressurized, ctx.Pressure, idle), pts)
            Next
        End Using
    End Sub

    Private Sub DrawPorts(g As Graphics, interactive As Boolean)
        For Each p In _circuit.AllPorts()
            Dim w = p.WorldPos()
            If p.ConnectionCount = 0 AndAlso Not Simulating AndAlso interactive Then
                g.FillEllipse(Brushes.White, w.X - 3, w.Y - 3, 6, 6)
                g.DrawEllipse(Pens.Black, w.X - 3, w.Y - 3, 6, 6)
            ElseIf p.ConnectionCount >= 2 Then
                Dim b = If(Simulating AndAlso p.IsPressurized, New SolidBrush(RenderContext.PressureColor), New SolidBrush(Color.Black))
                g.FillEllipse(b, w.X - 3, w.Y - 3, 6, 6)
                b.Dispose()
            End If
        Next
        If _hoverPort IsNot Nothing AndAlso Not Simulating Then
            Dim w = _hoverPort.WorldPos()
            Using p As New Pen(Color.FromArgb(0, 160, 80), 2)
                g.DrawEllipse(p, w.X - 6, w.Y - 6, 12, 12)
            End Using
            Using f As New Font("Segoe UI", 7.5F)
                g.DrawString(_hoverPort.Name, f, Brushes.DarkGreen, w.X + 6, w.Y - 16)
            End Using
        End If
    End Sub

    Private Sub DrawSelection(g As Graphics)
        If _selectedElement Is Nothing OrElse Simulating Then Return
        Dim r = _selectedElement.WorldBounds()
        r.Inflate(5, 5)
        Using p As New Pen(Color.FromArgb(255, 140, 0), 1.2F) With {.DashStyle = DashStyle.Dash}
            g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height)
        End Using
    End Sub

    ' ---------------------------------------------------------------- mouse

    Protected Overrides Sub OnMouseDown(e As MouseEventArgs)
        MyBase.OnMouseDown(e)
        Focus()
        Dim w = ToWorld(e.Location)
        _mouseWorld = w

        If Simulating Then
            If e.Button <> MouseButtons.Left Then Return
            Dim el = _circuit.FindElementAt(w)
            If el IsNot Nothing AndAlso el.IsManuallyOperated Then
                _pressedElement = el
                el.OnSimMouseDown()
                RaiseEvent ElementOperated(Me, EventArgs.Empty)
            End If
            Return
        End If

        If e.Button = MouseButtons.Left AndAlso PlacingPreset IsNot Nothing Then
            PlaceElement(PlacingPreset, w)
            If (ModifierKeys And Keys.Shift) = 0 Then
                PlacingPreset = Nothing
                RaiseEvent StatusMessage(Me, "Drag from a port (small circle) to another port to connect them.")
            End If
            Return
        End If

        Dim port = _circuit.FindPortAt(w, 6 / _zoom + 2)
        If e.Button = MouseButtons.Left AndAlso port IsNot Nothing Then
            _connectFrom = port
            RaiseEvent StatusMessage(Me, $"Drag to another port to connect port {port.Name}.")
            Return
        End If

        Dim hit = _circuit.FindElementAt(w)
        If hit IsNot Nothing Then
            SelectElement(hit)
            If e.Button = MouseButtons.Left Then
                _dragging = True
                _dragMoved = False
                _dragOffset = New PointF(w.X - hit.X, w.Y - hit.Y)
            End If
            Return
        End If

        Dim tube = _circuit.FindTubeAt(w, 5 / _zoom + 1)
        If tube IsNot Nothing Then
            _selectedElement = Nothing
            _selectedTube = tube
            RaiseEvent SelectionChanged(Me, EventArgs.Empty)
            Invalidate()
            Return
        End If
        ClearSelection()
    End Sub

    Protected Overrides Sub OnMouseMove(e As MouseEventArgs)
        MyBase.OnMouseMove(e)
        Dim w = ToWorld(e.Location)
        _mouseWorld = w

        If Simulating Then
            Dim el = _circuit.FindElementAt(w)
            Cursor = If(el IsNot Nothing AndAlso el.IsManuallyOperated, Cursors.Hand, Cursors.Default)
            Return
        End If

        If _dragging AndAlso _selectedElement IsNot Nothing Then
            Dim nx = Snap(w.X - _dragOffset.X), ny = Snap(w.Y - _dragOffset.Y)
            If nx <> _selectedElement.X OrElse ny <> _selectedElement.Y Then
                _selectedElement.X = nx
                _selectedElement.Y = ny
                _dragMoved = True
                Invalidate()
            End If
            Return
        End If

        Dim port = _circuit.FindPortAt(w, 6 / _zoom + 2)
        If port IsNot _hoverPort Then
            _hoverPort = port
            Invalidate()
        End If
        If _connectFrom IsNot Nothing Then
            Invalidate()
        ElseIf PlacingPreset IsNot Nothing OrElse port IsNot Nothing Then
            Cursor = Cursors.Cross
        ElseIf _circuit.FindElementAt(w) IsNot Nothing Then
            Cursor = Cursors.SizeAll
        Else
            Cursor = Cursors.Default
        End If
    End Sub

    Protected Overrides Sub OnMouseUp(e As MouseEventArgs)
        MyBase.OnMouseUp(e)
        Dim w = ToWorld(e.Location)

        If Simulating Then
            If _pressedElement IsNot Nothing Then
                _pressedElement.OnSimMouseUp()
                _pressedElement = Nothing
                RaiseEvent ElementOperated(Me, EventArgs.Empty)
            End If
            Return
        End If

        If _connectFrom IsNot Nothing Then
            Dim target = _circuit.FindPortAt(w, 8 / _zoom + 2)
            If target IsNot Nothing AndAlso target IsNot _connectFrom Then
                If target.Owner Is _connectFrom.Owner Then
                    RaiseEvent StatusMessage(Me, "A tube cannot connect two ports of the same component.")
                ElseIf _circuit.Connect(_connectFrom, target) IsNot Nothing Then
                    RaiseEvent StatusMessage(Me, $"Connected {Describe(_connectFrom)} to {Describe(target)}.")
                    RaiseEvent CircuitModified(Me, EventArgs.Empty)
                End If
            Else
                RaiseEvent StatusMessage(Me, "")
            End If
            _connectFrom = Nothing
            Invalidate()
        End If

        If _dragging Then
            _dragging = False
            If _dragMoved Then RaiseEvent CircuitModified(Me, EventArgs.Empty)
        End If
    End Sub

    Private Shared Function Describe(p As Port) As String
        Dim owner = If(String.IsNullOrEmpty(p.Owner.Label), p.Owner.DisplayName, p.Owner.Label)
        Return $"{owner} port {p.Name}"
    End Function

    Protected Overrides Sub OnMouseWheel(e As MouseEventArgs)
        If (ModifierKeys And Keys.Control) <> 0 Then
            Zoom *= If(e.Delta > 0, 1.1F, 1 / 1.1F)
            Return
        End If
        MyBase.OnMouseWheel(e)
        Invalidate()
    End Sub

    Protected Overrides Sub OnScroll(se As ScrollEventArgs)
        MyBase.OnScroll(se)
        Invalidate()
    End Sub

    ' ---------------------------------------------------------------- keyboard

    Protected Overrides Function IsInputKey(keyData As Keys) As Boolean
        Return keyData = Keys.Delete OrElse keyData = Keys.Escape OrElse MyBase.IsInputKey(keyData)
    End Function

    Protected Overrides Sub OnKeyDown(e As KeyEventArgs)
        MyBase.OnKeyDown(e)
        Select Case e.KeyCode
            Case Keys.Delete, Keys.Back
                DeleteSelection()
            Case Keys.R
                If e.Modifiers = Keys.None Then RotateSelection()
            Case Keys.Escape
                PlacingPreset = Nothing
                _connectFrom = Nothing
                RaiseEvent StatusMessage(Me, "")
                ClearSelection()
        End Select
    End Sub

    ' ---------------------------------------------------------------- drag and drop from the library

    Protected Overrides Sub OnDragEnter(e As DragEventArgs)
        MyBase.OnDragEnter(e)
        e.Effect = If(Not Simulating AndAlso e.Data.GetDataPresent(GetType(LibraryPreset)), DragDropEffects.Copy, DragDropEffects.None)
    End Sub

    Protected Overrides Sub OnDragDrop(e As DragEventArgs)
        MyBase.OnDragDrop(e)
        Dim preset = TryCast(e.Data.GetData(GetType(LibraryPreset)), LibraryPreset)
        If preset Is Nothing OrElse Simulating Then Return
        PlaceElement(preset, ToWorld(PointToClient(New Point(e.X, e.Y))))
    End Sub
End Class
