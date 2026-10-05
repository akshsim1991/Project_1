Imports System.Drawing.Drawing2D

''' <summary>The drawing area: shows the circuit and handles editing and operating it.</summary>
Public Class CircuitCanvas
    Inherits ScrollableControl

    Public Const GridSize As Single = 10
    Private Shared ReadOnly WorkArea As New SizeF(3000, 2000)
    Private Const ClipboardHeader As String = "PneuSimClipboard:"
    Private Shared _internalClipboard As String

    Private _circuit As New Circuit()
    Private _zoom As Single = 1.0F
    Private ReadOnly _selection As New List(Of CircuitElement)
    Private _selectedTube As Tube

    ' Interaction state.
    Private _dragging As Boolean
    Private _dragStart As PointF
    Private _dragOrigins As New Dictionary(Of CircuitElement, PointF)
    Private _dragMoved As Boolean
    Private _draggingTube As Tube
    Private _connectFrom As Port
    Private _connectMoved As Boolean
    Private _rubberBand As RectangleF?
    Private _rubberStart As PointF
    Private _mouseWorld As PointF
    Private _hoverPort As Port
    Private _pressedElement As CircuitElement

    Public Event SelectionChanged As EventHandler
    ''' <summary>Raised whenever the user modifies the circuit.</summary>
    Public Event CircuitModified As EventHandler
    ''' <summary>Raised when a manual actuator was operated during simulation.</summary>
    Public Event ElementOperated As EventHandler
    Public Event StatusMessage As EventHandler(Of String)
    ''' <summary>Raised for keyboard commands the main window handles ("Undo", "Redo").</summary>
    Public Event CommandKey As EventHandler(Of String)

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
            _zoom = Math.Max(0.3F, Math.Min(3.0F, value))
            UpdateScrollSize()
            Invalidate()
        End Set
    End Property

    ''' <summary>The selected element when exactly one is selected.</summary>
    Public ReadOnly Property SelectedElement As CircuitElement
        Get
            Return If(_selection.Count = 1, _selection(0), Nothing)
        End Get
    End Property

    Public ReadOnly Property SelectedElements As IReadOnlyList(Of CircuitElement)
        Get
            Return _selection
        End Get
    End Property

    Private Sub UpdateScrollSize()
        AutoScrollMinSize = New Size(CInt(WorkArea.Width * _zoom), CInt(WorkArea.Height * _zoom))
    End Sub

    ' ---------------------------------------------------------------- commands

    Public Sub ClearSelection()
        _selection.Clear()
        _selectedTube = Nothing
        RaiseEvent SelectionChanged(Me, EventArgs.Empty)
        Invalidate()
    End Sub

    Private Sub SetSelection(items As IEnumerable(Of CircuitElement))
        _selection.Clear()
        _selection.AddRange(items)
        _selectedTube = Nothing
        RaiseEvent SelectionChanged(Me, EventArgs.Empty)
        Invalidate()
    End Sub

    Public Sub SelectAll()
        If Simulating Then Return
        SetSelection(_circuit.Elements)
    End Sub

    Public Sub DeleteSelection()
        If Simulating Then Return
        If _selection.Count > 0 Then
            For Each e In _selection.ToList()
                _circuit.Remove(e)
            Next
        ElseIf _selectedTube IsNot Nothing Then
            _circuit.RemoveTube(_selectedTube)
        Else
            Return
        End If
        ClearSelection()
        RaiseEvent CircuitModified(Me, EventArgs.Empty)
    End Sub

    Public Sub RotateSelection()
        If Simulating OrElse _selection.Count = 0 Then Return
        For Each el In _selection
            ' Rotate about the symbol's centre so it stays where it is.
            Dim before = el.WorldBounds()
            el.Rotation = (el.Rotation + 1) Mod 4
            Dim after = el.WorldBounds()
            el.X = Snap(el.X + (before.X + before.Width / 2) - (after.X + after.Width / 2))
            el.Y = Snap(el.Y + (before.Y + before.Height / 2) - (after.Y + after.Height / 2))
        Next
        Invalidate()
        RaiseEvent CircuitModified(Me, EventArgs.Empty)
    End Sub

    Public Sub CopySelection()
        If _selection.Count = 0 Then Return
        _internalClipboard = _circuit.ExtractXml(_selection)
        Try
            Clipboard.SetText(ClipboardHeader & _internalClipboard)
        Catch ex As Exception
            ' The system clipboard is optional; the internal copy is enough within PneuSim.
        End Try
        RaiseEvent StatusMessage(Me, $"Copied {_selection.Count} component(s).")
    End Sub

    Public Sub CutSelection()
        If Simulating Then Return
        CopySelection()
        DeleteSelection()
    End Sub

    Public Sub Paste()
        If Simulating Then Return
        Dim xml = _internalClipboard
        Try
            If Clipboard.ContainsText() AndAlso Clipboard.GetText().StartsWith(ClipboardHeader) Then
                xml = Clipboard.GetText().Substring(ClipboardHeader.Length)
            End If
        Catch ex As Exception
            ' Fall back to the internal clipboard.
        End Try
        If String.IsNullOrEmpty(xml) Then Return
        Try
            Dim added = _circuit.Merge(xml, 40, 40)
            ' Paste again moves the clipboard further so copies do not stack exactly.
            _internalClipboard = _circuit.ExtractXml(added)
            SetSelection(added)
            RaiseEvent CircuitModified(Me, EventArgs.Empty)
        Catch ex As Exception
            RaiseEvent StatusMessage(Me, "The clipboard does not contain PneuSim components.")
        End Try
    End Sub

    Public Sub DuplicateSelection()
        If Simulating OrElse _selection.Count = 0 Then Return
        Dim added = _circuit.Merge(_circuit.ExtractXml(_selection), 40, 40)
        SetSelection(added)
        RaiseEvent CircuitModified(Me, EventArgs.Empty)
    End Sub

    Public Sub PlaceElement(preset As LibraryPreset, world As PointF)
        Dim e = preset.Factory.Invoke()
        Dim b = e.LocalBounds
        ' Put the symbol's centre under the cursor, snapped to the grid.
        _circuit.Add(e, Snap(world.X - b.Width / 2 - b.Left), Snap(world.Y - b.Height / 2 - b.Top))
        SetSelection({e})
        RaiseEvent CircuitModified(Me, EventArgs.Empty)
    End Sub

    ''' <summary>Zooms so the whole circuit fits in the window (never above 100 %).</summary>
    Public Sub ZoomToFit()
        Dim b = _circuit.Bounds()
        If b.IsEmpty OrElse ClientSize.Width < 50 OrElse ClientSize.Height < 50 Then Return
        b.Inflate(30, 30)
        Zoom = Math.Min(1.0F, Math.Min(ClientSize.Width / b.Width, ClientSize.Height / b.Height))
        ScrollToCircuit()
    End Sub

    ''' <summary>Scrolls so the whole circuit is visible.</summary>
    Public Sub ScrollToCircuit()
        Dim b = _circuit.Bounds()
        If b.IsEmpty Then Return
        AutoScrollPosition = New Point(CInt(Math.Max(0, (b.Left - 40) * _zoom)), CInt(Math.Max(0, (b.Top - 40) * _zoom)))
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
        ' Paint everything (the buffer is double buffered anyway); some GDI+ implementations
        ' mis-scale the clip region once zoom and per-symbol transforms are combined.
        g.ResetClip()
        g.TranslateTransform(AutoScrollPosition.X, AutoScrollPosition.Y)
        g.ScaleTransform(_zoom, _zoom)
        If Not Simulating Then DrawGrid(g, ClientRectangle)
        DrawContent(g, interactive:=True)
    End Sub

    ''' <summary>Draws the circuit (without grid, ports or selection) onto any graphics surface.</summary>
    Public Sub PaintTo(g As Graphics)
        g.SmoothingMode = SmoothingMode.AntiAlias
        DrawContent(g, interactive:=False)
    End Sub

    Private Sub DrawContent(g As Graphics, interactive As Boolean)
        Using ctx As New RenderContext() With {.Simulating = Simulating, .Interactive = interactive}
            DrawTubes(g, ctx)
            Using labelFont As New Font("Segoe UI", 8.5F, FontStyle.Bold)
                For Each el In _circuit.Elements
                    Dim state = g.Save()
                    Using m = el.GetMatrix()
                        g.MultiplyTransform(m)
                    End Using
                    el.DrawSymbol(g, ctx)
                    g.Restore(state)
                    If Not String.IsNullOrEmpty(el.Label) AndAlso TypeOf el IsNot TextNote Then
                        Dim a = el.ToWorld(el.LabelAnchor)
                        g.DrawString(el.Label, labelFont, Brushes.Black, a.X, a.Y - 15)
                    End If
                Next
            End Using
            DrawPorts(g, interactive)
            If Not interactive Then Return
            DrawSelection(g)
            If _connectFrom IsNot Nothing AndAlso _connectMoved Then
                Using p As New Pen(Color.FromArgb(0, 140, 70), 1.5F) With {.DashStyle = DashStyle.Dash}
                    Dim d = _connectFrom.WorldDir()
                    g.DrawLines(p, Tube.RouteBetween(_connectFrom.WorldPos(), d, _mouseWorld, New PointF(-d.X, -d.Y)))
                End Using
            End If
            If _rubberBand.HasValue Then
                Dim rb = _rubberBand.Value
                Using b As New SolidBrush(Color.FromArgb(30, 0, 120, 215)), p As New Pen(Color.FromArgb(0, 120, 215))
                    g.FillRectangle(b, rb)
                    g.DrawRectangle(p, rb.X, rb.Y, rb.Width, rb.Height)
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
        Using idleAir As New Pen(If(Simulating, RenderContext.IdleTubeColor, Color.Black), 1.6F),
              idleWire As New Pen(If(Simulating, Color.FromArgb(90, 90, 90), Color.FromArgb(40, 40, 40)), 1.2F),
              highlight As New Pen(Color.FromArgb(120, 255, 170, 0), 7)
            highlight.LineJoin = LineJoin.Round
            For Each t In _circuit.Tubes
                Dim pts = t.Route()
                If t Is _selectedTube Then g.DrawLines(highlight, pts)
                Dim pen As Pen
                If t.IsElectric Then
                    pen = If(Simulating AndAlso t.IsPressurized, ctx.Energized, idleWire)
                Else
                    pen = If(Simulating AndAlso t.IsPressurized, ctx.Pressure, idleAir)
                End If
                g.DrawLines(pen, pts)
            Next
        End Using
    End Sub

    Private Sub DrawPorts(g As Graphics, interactive As Boolean)
        For Each p In _circuit.AllPorts()
            If TypeOf p.Owner Is Junction Then Continue For
            Dim w = p.WorldPos()
            If p.ConnectionCount = 0 AndAlso Not Simulating AndAlso interactive Then
                g.FillEllipse(Brushes.White, w.X - 3, w.Y - 3, 6, 6)
                g.DrawEllipse(If(p.Kind = PortKind.Electric, Pens.Firebrick, Pens.Black), w.X - 3, w.Y - 3, 6, 6)
            ElseIf p.ConnectionCount >= 2 Then
                Dim c = Color.Black
                If Simulating AndAlso p.IsPressurized Then
                    c = If(p.Kind = PortKind.Electric, RenderContext.EnergizedColor, RenderContext.PressureColor)
                End If
                Using b As New SolidBrush(c)
                    g.FillEllipse(b, w.X - 3, w.Y - 3, 6, 6)
                End Using
            End If
        Next
        If _hoverPort IsNot Nothing AndAlso Not Simulating AndAlso interactive Then
            Dim w = _hoverPort.WorldPos()
            Dim ok = _connectFrom Is Nothing OrElse Circuit.CanConnect(_connectFrom, _hoverPort) OrElse _hoverPort Is _connectFrom
            Using p As New Pen(If(ok, Color.FromArgb(0, 160, 80), Color.Red), 2)
                g.DrawEllipse(p, w.X - 6, w.Y - 6, 12, 12)
            End Using
            Using f As New Font("Segoe UI", 7.5F)
                g.DrawString(_hoverPort.Name, f, If(ok, Brushes.DarkGreen, Brushes.Red), w.X + 6, w.Y - 16)
            End Using
        End If
    End Sub

    Private Sub DrawSelection(g As Graphics)
        If Simulating Then Return
        Using p As New Pen(Color.FromArgb(255, 140, 0), 1.2F) With {.DashStyle = DashStyle.Dash}
            For Each el In _selection
                Dim r = el.WorldBounds()
                r.Inflate(5, 5)
                g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height)
            Next
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
                el.OnSimMouseDown(el.ToLocal(w))
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

        Dim ctrl = (ModifierKeys And Keys.Control) <> 0
        Dim port = _circuit.FindPortAt(w, 6 / _zoom + 2)
        ' A selected junction is moved rather than wired from.
        If port IsNot Nothing AndAlso TypeOf port.Owner Is Junction AndAlso _selection.Contains(port.Owner) Then port = Nothing
        If e.Button = MouseButtons.Left AndAlso port IsNot Nothing AndAlso Not ctrl Then
            _connectFrom = port
            _connectMoved = False
            Return
        End If

        Dim hit = _circuit.FindElementAt(w)
        If hit IsNot Nothing Then
            If ctrl Then
                If _selection.Contains(hit) Then _selection.Remove(hit) Else _selection.Add(hit)
                _selectedTube = Nothing
                RaiseEvent SelectionChanged(Me, EventArgs.Empty)
                Invalidate()
                Return
            End If
            If Not _selection.Contains(hit) Then SetSelection({hit})
            If e.Button = MouseButtons.Left Then BeginDrag(w)
            Return
        End If

        Dim tube = _circuit.FindTubeAt(w, 5 / _zoom + 1)
        If tube IsNot Nothing Then
            _selection.Clear()
            _selectedTube = tube
            RaiseEvent SelectionChanged(Me, EventArgs.Empty)
            If e.Button = MouseButtons.Left AndAlso tube.MidAxis() IsNot Nothing Then
                _draggingTube = tube
                _dragMoved = False
            End If
            Invalidate()
            Return
        End If

        If Not ctrl Then ClearSelection()
        If e.Button = MouseButtons.Left Then
            _rubberStart = w
            _rubberBand = New RectangleF(w, SizeF.Empty)
        End If
    End Sub

    Private Sub BeginDrag(w As PointF)
        _dragging = True
        _dragMoved = False
        _dragStart = w
        _dragOrigins = _selection.ToDictionary(Function(el) el, Function(el) New PointF(el.X, el.Y))
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

        If _dragging Then
            Dim dx = Snap(w.X - _dragStart.X), dy = Snap(w.Y - _dragStart.Y)
            For Each kv In _dragOrigins
                Dim nx = kv.Value.X + dx, ny = kv.Value.Y + dy
                If nx <> kv.Key.X OrElse ny <> kv.Key.Y Then
                    kv.Key.X = nx
                    kv.Key.Y = ny
                    _dragMoved = True
                End If
            Next
            Invalidate()
            Return
        End If

        If _draggingTube IsNot Nothing Then
            _draggingTube.Mid = Snap(If(_draggingTube.MidAxis() = "X", w.X, w.Y))
            _dragMoved = True
            Invalidate()
            Return
        End If

        If _rubberBand.HasValue Then
            _rubberBand = RectangleF.FromLTRB(Math.Min(_rubberStart.X, w.X), Math.Min(_rubberStart.Y, w.Y),
                                              Math.Max(_rubberStart.X, w.X), Math.Max(_rubberStart.Y, w.Y))
            Invalidate()
            Return
        End If

        Dim port = _circuit.FindPortAt(w, 6 / _zoom + 2)
        If port IsNot _hoverPort Then
            _hoverPort = port
            Invalidate()
        End If
        If _connectFrom IsNot Nothing Then
            _connectMoved = True
            Invalidate()
        ElseIf PlacingPreset IsNot Nothing OrElse port IsNot Nothing Then
            Cursor = Cursors.Cross
        ElseIf _circuit.FindElementAt(w) IsNot Nothing Then
            Cursor = Cursors.SizeAll
        ElseIf _circuit.FindTubeAt(w, 5 / _zoom + 1) IsNot Nothing Then
            Dim t = _circuit.FindTubeAt(w, 5 / _zoom + 1)
            Cursor = If(t.MidAxis() = "X", Cursors.SizeWE, If(t.MidAxis() = "Y", Cursors.SizeNS, Cursors.Default))
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
                ElseIf target.Kind <> _connectFrom.Kind Then
                    RaiseEvent StatusMessage(Me, "Electrical terminals can only be wired to electrical terminals, and tubes only join pneumatic ports.")
                ElseIf _circuit.Connect(_connectFrom, target) IsNot Nothing Then
                    RaiseEvent StatusMessage(Me, $"Connected {Describe(_connectFrom)} to {Describe(target)}.")
                    RaiseEvent CircuitModified(Me, EventArgs.Empty)
                End If
            ElseIf Not _connectMoved Then
                ' A click on a port without dragging selects its component.
                SetSelection({_connectFrom.Owner})
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

        If _draggingTube IsNot Nothing Then
            _draggingTube = Nothing
            If _dragMoved Then RaiseEvent CircuitModified(Me, EventArgs.Empty)
        End If

        If _rubberBand.HasValue Then
            Dim rb = _rubberBand.Value
            _rubberBand = Nothing
            If rb.Width > 2 OrElse rb.Height > 2 Then
                Dim inside = _circuit.Elements.Where(Function(el) rb.IntersectsWith(el.WorldBounds()))
                Dim ctrl = (ModifierKeys And Keys.Control) <> 0
                SetSelection(If(ctrl, _selection.Union(inside).ToList(), inside.ToList()))
            End If
            Invalidate()
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
        Select Case keyData And Keys.KeyCode
            Case Keys.Delete, Keys.Escape, Keys.Left, Keys.Right, Keys.Up, Keys.Down
                Return True
        End Select
        Return MyBase.IsInputKey(keyData)
    End Function

    Protected Overrides Sub OnKeyDown(e As KeyEventArgs)
        MyBase.OnKeyDown(e)
        If e.Control Then
            Select Case e.KeyCode
                Case Keys.Z : RaiseEvent CommandKey(Me, If(e.Shift, "Redo", "Undo"))
                Case Keys.Y : RaiseEvent CommandKey(Me, "Redo")
                Case Keys.C : CopySelection()
                Case Keys.X : CutSelection()
                Case Keys.V : Paste()
                Case Keys.D : DuplicateSelection()
                Case Keys.A : SelectAll()
            End Select
            Return
        End If
        Select Case e.KeyCode
            Case Keys.Delete, Keys.Back
                DeleteSelection()
            Case Keys.R
                If e.Modifiers = Keys.None Then RotateSelection()
            Case Keys.Left, Keys.Right, Keys.Up, Keys.Down
                NudgeSelection(e.KeyCode)
            Case Keys.Escape
                PlacingPreset = Nothing
                _connectFrom = Nothing
                RaiseEvent StatusMessage(Me, "")
                ClearSelection()
        End Select
    End Sub

    Private Sub NudgeSelection(key As Keys)
        If Simulating OrElse _selection.Count = 0 Then Return
        Dim dx = If(key = Keys.Left, -GridSize, If(key = Keys.Right, GridSize, 0))
        Dim dy = If(key = Keys.Up, -GridSize, If(key = Keys.Down, GridSize, 0))
        For Each el In _selection
            el.X += dx
            el.Y += dy
        Next
        Invalidate()
        RaiseEvent CircuitModified(Me, EventArgs.Empty)
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
