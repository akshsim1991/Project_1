Imports System.ComponentModel
Imports System.Drawing.Drawing2D
Imports System.IO

Public Class MainForm
    Inherits Form

    Private Const AppName = "PneuSim"
    Private Const FileFilter = "PneuSim circuits (*.pneu)|*.pneu|All files (*.*)|*.*"
    Private Const TickMs = 20
    Private Const SubSteps = 4

    Private ReadOnly _canvas As New CircuitCanvas() With {.Dock = DockStyle.Fill}
    Private ReadOnly _diagram As New DiagramPanel() With {.Dock = DockStyle.Fill}
    Private ReadOnly _library As New ListView()
    Private ReadOnly _properties As New PropertyGrid()
    Private ReadOnly _timer As New Timer() With {.Interval = TickMs}

    Private ReadOnly _statusMode As New ToolStripStatusLabel() With {.BorderSides = ToolStripStatusLabelBorderSides.Right, .Padding = New Padding(0, 0, 8, 0)}
    Private ReadOnly _statusMessage As New ToolStripStatusLabel() With {.Spring = True, .TextAlign = ContentAlignment.MiddleLeft}
    Private ReadOnly _statusZoom As New ToolStripStatusLabel()

    Private _btnStart, _btnPause, _btnStop As ToolStripButton
    Private _menuStart, _menuPause, _menuStop As ToolStripMenuItem
    Private _speedBox As ToolStripComboBox
    Private _diagramSplit As SplitContainer

    Private _simulator As Simulator
    Private _running As Boolean
    Private _paused As Boolean
    Private _filePath As String
    Private _dirty As Boolean

    ' Undo history: snapshots of the circuit after each change.
    Private ReadOnly _history As New List(Of String)
    Private _historyIndex As Integer = -1
    Private Const MaxHistory = 100
    Private _menuUndo, _menuRedo As ToolStripMenuItem
    Private _btnUndo, _btnRedo As ToolStripButton

    Public Sub New()
        Text = AppName
        Font = New Font("Segoe UI", 9)
        Size = New Size(1360, 860)
        StartPosition = FormStartPosition.CenterScreen
        KeyPreview = True
        Icon = Icons.AppIcon()

        BuildLayout()
        AddHandler _canvas.SelectionChanged, Sub() _properties.SelectedObjects = _canvas.SelectedElements.Cast(Of Object)().ToArray()
        AddHandler _canvas.CircuitModified, Sub() OnCircuitModified()
        AddHandler _canvas.CommandKey, Sub(s, cmd)
                                           If cmd = "Undo" Then Undo() Else Redo()
                                       End Sub
        AddHandler _canvas.ElementOperated, AddressOf OnElementOperated
        AddHandler _canvas.StatusMessage, Sub(s, msg) _statusMessage.Text = msg
        AddHandler _properties.PropertyValueChanged, AddressOf OnPropertyValueChanged
        AddHandler _timer.Tick, AddressOf OnTick

        NewCircuit(New Circuit())
        UpdateUiState()
    End Sub

    ' ================================================================= layout

    Private Sub BuildLayout()
        ' Left: component library.
        _library.Dock = DockStyle.Fill
        _library.View = View.Tile
        _library.TileSize = New Size(228, 58)
        _library.MultiSelect = False
        _library.HideSelection = False
        _library.LargeImageList = New ImageList() With {.ImageSize = New Size(72, 48), .ColorDepth = ColorDepth.Depth32Bit}
        For Each cat In Library.Presets.Select(Function(p) p.Category).Distinct()
            _library.Groups.Add(cat, cat)
        Next
        For i = 0 To Library.Presets.Length - 1
            Dim preset = Library.Presets(i)
            _library.LargeImageList.Images.Add(Library.RenderThumbnail(preset.Factory.Invoke(), 72, 48))
            _library.Items.Add(New ListViewItem(preset.Name, i, _library.Groups(preset.Category)) With {.Tag = preset})
        Next
        AddHandler _library.ItemDrag, Sub(s, e)
                                          Dim item = DirectCast(e.Item, ListViewItem)
                                          _library.DoDragDrop(item.Tag, DragDropEffects.Copy)
                                      End Sub
        AddHandler _library.ItemActivate, AddressOf OnLibraryActivate
        AddHandler _library.MouseClick, Sub() OnLibraryActivate(Nothing, EventArgs.Empty)

        Dim libPanel = TitledPanel("Component library", _library)

        ' Right: properties.
        _properties.Dock = DockStyle.Fill
        _properties.ToolbarVisible = False
        _properties.HelpVisible = True
        Dim propPanel = TitledPanel("Properties", _properties)

        ' Centre: canvas above the diagram.
        _diagramSplit = New SplitContainer() With {.Dock = DockStyle.Fill, .Orientation = Orientation.Horizontal, .FixedPanel = FixedPanel.Panel2}
        _diagramSplit.Panel1.Controls.Add(_canvas)
        _diagramSplit.Panel2.Controls.Add(_diagram)

        Dim rightSplit As New SplitContainer() With {.Dock = DockStyle.Fill, .FixedPanel = FixedPanel.Panel2}
        rightSplit.Panel1.Controls.Add(_diagramSplit)
        rightSplit.Panel2.Controls.Add(propPanel)

        Dim mainSplit As New SplitContainer() With {.Dock = DockStyle.Fill, .FixedPanel = FixedPanel.Panel1}
        mainSplit.Panel1.Controls.Add(libPanel)
        mainSplit.Panel2.Controls.Add(rightSplit)

        Dim status As New StatusStrip()
        status.Items.AddRange({_statusMode, _statusMessage, _statusZoom})

        Controls.Add(mainSplit)
        Controls.Add(BuildToolbar())
        Controls.Add(BuildMenu())
        Controls.Add(status)

        AddHandler Load, Sub()
                             mainSplit.SplitterDistance = 262
                             rightSplit.SplitterDistance = Math.Max(200, rightSplit.Width - 280)
                             _diagramSplit.SplitterDistance = Math.Max(200, _diagramSplit.Height - 190)
                         End Sub
    End Sub

    Private Shared Function TitledPanel(title As String, content As Control) As Panel
        Dim p As New Panel() With {.Dock = DockStyle.Fill}
        Dim header As New Label() With {
            .Text = title, .Dock = DockStyle.Top, .Height = 24, .Padding = New Padding(6, 4, 0, 0),
            .BackColor = Color.FromArgb(232, 236, 244), .Font = New Font("Segoe UI", 8.5F, FontStyle.Bold)}
        p.Controls.Add(content)
        p.Controls.Add(header)
        Return p
    End Function

    Private Function BuildMenu() As MenuStrip
        Dim menu As New MenuStrip()

        Dim file = New ToolStripMenuItem("&File")
        file.DropDownItems.Add(Item("&New", AddressOf OnNew, Keys.Control Or Keys.N))
        file.DropDownItems.Add(Item("&Open...", AddressOf OnOpen, Keys.Control Or Keys.O))
        file.DropDownItems.Add(Item("&Save", AddressOf OnSave, Keys.Control Or Keys.S))
        file.DropDownItems.Add(Item("Save &As...", AddressOf OnSaveAs))
        file.DropDownItems.Add(New ToolStripSeparator())
        file.DropDownItems.Add(Item("Export as &Image...", AddressOf OnExportImage))
        file.DropDownItems.Add(New ToolStripSeparator())
        file.DropDownItems.Add(Item("E&xit", Sub() Close()))

        ' Edit shortcuts are handled by the canvas so they do not steal keys from the property grid.
        Dim edit = New ToolStripMenuItem("&Edit")
        _menuUndo = Item("&Undo", Sub() Undo()) : _menuUndo.ShortcutKeyDisplayString = "Ctrl+Z"
        _menuRedo = Item("&Redo", Sub() Redo()) : _menuRedo.ShortcutKeyDisplayString = "Ctrl+Y"
        Dim cut = Item("Cu&t", Sub() _canvas.CutSelection()) : cut.ShortcutKeyDisplayString = "Ctrl+X"
        Dim copy = Item("&Copy", Sub() _canvas.CopySelection()) : copy.ShortcutKeyDisplayString = "Ctrl+C"
        Dim paste = Item("&Paste", Sub() _canvas.Paste()) : paste.ShortcutKeyDisplayString = "Ctrl+V"
        Dim dup = Item("D&uplicate", Sub() _canvas.DuplicateSelection()) : dup.ShortcutKeyDisplayString = "Ctrl+D"
        Dim all = Item("Select &All", Sub() _canvas.SelectAll()) : all.ShortcutKeyDisplayString = "Ctrl+A"
        Dim del = Item("&Delete", Sub() _canvas.DeleteSelection()) : del.ShortcutKeyDisplayString = "Del"
        Dim rot = Item("&Rotate 90°", Sub() _canvas.RotateSelection()) : rot.ShortcutKeyDisplayString = "R"
        edit.DropDownItems.AddRange({_menuUndo, _menuRedo, New ToolStripSeparator(), cut, copy, paste, dup,
                                     New ToolStripSeparator(), all, del, rot})

        Dim view = New ToolStripMenuItem("&View")
        view.DropDownItems.Add(Item("Zoom &In", Sub() SetZoom(_canvas.Zoom * 1.2F), Keys.Control Or Keys.Oemplus))
        view.DropDownItems.Add(Item("Zoom &Out", Sub() SetZoom(_canvas.Zoom / 1.2F), Keys.Control Or Keys.OemMinus))
        view.DropDownItems.Add(Item("&Actual Size", Sub() SetZoom(1), Keys.Control Or Keys.D0))
        view.DropDownItems.Add(Item("Zoom to &Fit", Sub() FitView(), Keys.Control Or Keys.D9))
        Dim diag = Item("Displacement-step &Diagram", Nothing)
        diag.Checked = True
        diag.CheckOnClick = True
        AddHandler diag.CheckedChanged, Sub() _diagramSplit.Panel2Collapsed = Not diag.Checked
        view.DropDownItems.Add(New ToolStripSeparator())
        view.DropDownItems.Add(diag)

        Dim sim = New ToolStripMenuItem("&Simulation")
        _menuStart = Item("&Start", AddressOf OnStart, Keys.F9)
        _menuPause = Item("&Pause", AddressOf OnPause, Keys.F10)
        _menuStop = Item("S&top and Reset", AddressOf OnStop, Keys.F11)
        sim.DropDownItems.AddRange({_menuStart, _menuPause, _menuStop})

        Dim exMenu = New ToolStripMenuItem("E&xamples")
        For Each ex In Examples.All
            Dim build = ex.Build
            exMenu.DropDownItems.Add(Item(ex.Name, Sub() OpenExample(build)))
        Next

        Dim help = New ToolStripMenuItem("&Help")
        help.DropDownItems.Add(Item("&Quick Guide", AddressOf OnQuickGuide, Keys.F1))
        help.DropDownItems.Add(Item("&About PneuSim", AddressOf OnAbout))

        Dim tools = New ToolStripMenuItem("&Tools")
        tools.DropDownItems.Add(Item("&Circuit Generator (from a sequence)...", AddressOf OnGenerator, Keys.Control Or Keys.G))

        menu.Items.AddRange({file, edit, view, sim, tools, exMenu, help})
        MainMenuStrip = menu
        Return menu
    End Function

    Private Function BuildToolbar() As ToolStrip
        Dim bar As New ToolStrip() With {.GripStyle = ToolStripGripStyle.Hidden, .ImageScalingSize = New Size(16, 16)}
        bar.Items.Add(Button("New", Icons.NewFile, AddressOf OnNew))
        bar.Items.Add(Button("Open", Icons.Open, AddressOf OnOpen))
        bar.Items.Add(Button("Save", Icons.Save, AddressOf OnSave))
        bar.Items.Add(New ToolStripSeparator())
        _btnUndo = Button("Undo", Icons.UndoIcon(False), Sub() Undo())
        _btnRedo = Button("Redo", Icons.UndoIcon(True), Sub() Redo())
        bar.Items.AddRange({_btnUndo, _btnRedo})
        bar.Items.Add(New ToolStripSeparator())
        bar.Items.Add(Button("Generator", Icons.Wand, AddressOf OnGenerator))
        bar.Items.Add(New ToolStripSeparator())
        _btnStart = Button("Start", Icons.Play, AddressOf OnStart)
        _btnPause = Button("Pause", Icons.Pause, AddressOf OnPause)
        _btnStop = Button("Stop", Icons.StopIcon, AddressOf OnStop)
        bar.Items.AddRange({_btnStart, _btnPause, _btnStop})
        bar.Items.Add(New ToolStripLabel("  Speed:"))
        _speedBox = New ToolStripComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .AutoSize = False, .Width = 60}
        _speedBox.Items.AddRange({"0.25x", "0.5x", "1x", "2x", "4x"})
        _speedBox.SelectedIndex = 2
        bar.Items.Add(_speedBox)
        bar.Items.Add(New ToolStripSeparator())
        bar.Items.Add(Button("Rotate", Icons.RotateIcon, Sub() _canvas.RotateSelection()))
        bar.Items.Add(Button("Delete", Icons.DeleteIcon, Sub() _canvas.DeleteSelection()))
        bar.Items.Add(New ToolStripSeparator())
        bar.Items.Add(Button("Zoom in", Icons.ZoomIn, Sub() SetZoom(_canvas.Zoom * 1.2F)))
        bar.Items.Add(Button("Zoom out", Icons.ZoomOut, Sub() SetZoom(_canvas.Zoom / 1.2F)))
        bar.Items.Add(Button("Fit", Icons.ZoomIn, Sub() FitView()))
        Return bar
    End Function

    Private Shared Function Item(text As String, handler As EventHandler, Optional keys As Keys = Keys.None) As ToolStripMenuItem
        Dim mi As New ToolStripMenuItem(text)
        If handler IsNot Nothing Then AddHandler mi.Click, handler
        If keys <> Keys.None Then mi.ShortcutKeys = keys
        Return mi
    End Function

    Private Shared Function Button(text As String, image As Image, handler As EventHandler) As ToolStripButton
        Dim b As New ToolStripButton(text, image) With {.DisplayStyle = ToolStripItemDisplayStyle.ImageAndText}
        AddHandler b.Click, handler
        Return b
    End Function

    ' ================================================================= document

    Private Sub NewCircuit(c As Circuit, Optional path As String = Nothing)
        StopSimulation()
        _canvas.Circuit = c
        _simulator = New Simulator(c)
        _canvas.Simulator = _simulator
        _diagram.Simulator = Nothing
        _diagram.Invalidate()
        _filePath = path
        _dirty = False
        _properties.SelectedObject = Nothing
        _history.Clear()
        _history.Add(c.ToXml())
        _historyIndex = 0
        UpdateTitle()
        UpdateUndoButtons()
        _canvas.ScrollToCircuit()
    End Sub

    ''' <summary>Records an undo snapshot after every change.</summary>
    Private Sub OnCircuitModified()
        If _historyIndex < _history.Count - 1 Then _history.RemoveRange(_historyIndex + 1, _history.Count - _historyIndex - 1)
        _history.Add(_canvas.Circuit.ToXml())
        If _history.Count > MaxHistory Then _history.RemoveAt(0)
        _historyIndex = _history.Count - 1
        MarkDirty()
        UpdateUndoButtons()
    End Sub

    Private Sub Undo()
        If _running OrElse _historyIndex <= 0 Then Return
        _historyIndex -= 1
        RestoreSnapshot()
    End Sub

    Private Sub Redo()
        If _running OrElse _historyIndex >= _history.Count - 1 Then Return
        _historyIndex += 1
        RestoreSnapshot()
    End Sub

    Private Sub RestoreSnapshot()
        Dim c = Circuit.FromXml(_history(_historyIndex))
        _canvas.Circuit = c
        _simulator = New Simulator(c)
        _canvas.Simulator = _simulator
        _properties.SelectedObject = Nothing
        MarkDirty()
        UpdateUndoButtons()
    End Sub

    Private Sub UpdateUndoButtons()
        If _btnUndo Is Nothing Then Return
        Dim canUndo = Not _running AndAlso _historyIndex > 0
        Dim canRedo = Not _running AndAlso _historyIndex < _history.Count - 1
        _btnUndo.Enabled = canUndo : _menuUndo.Enabled = canUndo
        _btnRedo.Enabled = canRedo : _menuRedo.Enabled = canRedo
    End Sub

    Private Sub MarkDirty()
        _dirty = True
        UpdateTitle()
    End Sub

    Private Sub UpdateTitle()
        Dim name = If(_filePath Is Nothing, "Untitled", Path.GetFileNameWithoutExtension(_filePath))
        Text = $"{name}{If(_dirty, " *", "")} - {AppName}"
    End Sub

    ''' <summary>Asks to save unsaved changes. Returns False if the user cancelled.</summary>
    Private Function ConfirmDiscard() As Boolean
        If Not _dirty Then Return True
        Select Case MessageBox.Show("Save changes to the current circuit?", AppName, MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question)
            Case DialogResult.Yes : Return SaveCircuit(_filePath)
            Case DialogResult.No : Return True
            Case Else : Return False
        End Select
    End Function

    Private Sub OnNew(sender As Object, e As EventArgs)
        If ConfirmDiscard() Then NewCircuit(New Circuit())
    End Sub

    Private Sub OnOpen(sender As Object, e As EventArgs)
        If Not ConfirmDiscard() Then Return
        Using dlg As New OpenFileDialog() With {.Filter = FileFilter}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            Try
                NewCircuit(Circuit.Load(dlg.FileName), dlg.FileName)
            Catch ex As Exception
                MessageBox.Show($"Could not open the file:{vbCrLf}{ex.Message}", AppName, MessageBoxButtons.OK, MessageBoxIcon.Error)
            End Try
        End Using
    End Sub

    Private Sub OnSave(sender As Object, e As EventArgs)
        SaveCircuit(_filePath)
    End Sub

    Private Sub OnSaveAs(sender As Object, e As EventArgs)
        SaveCircuit(Nothing)
    End Sub

    Private Function SaveCircuit(path As String) As Boolean
        If path Is Nothing Then
            Using dlg As New SaveFileDialog() With {.Filter = FileFilter, .DefaultExt = "pneu", .FileName = "circuit.pneu"}
                If dlg.ShowDialog(Me) <> DialogResult.OK Then Return False
                path = dlg.FileName
            End Using
        End If
        Try
            _canvas.Circuit.Save(path)
            _filePath = path
            _dirty = False
            UpdateTitle()
            _statusMessage.Text = $"Saved {path}"
            Return True
        Catch ex As Exception
            MessageBox.Show($"Could not save the file:{vbCrLf}{ex.Message}", AppName, MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return False
        End Try
    End Function

    Private Sub OnExportImage(sender As Object, e As EventArgs)
        Dim c = _canvas.Circuit
        If c.Elements.Count = 0 Then Return
        Using dlg As New SaveFileDialog() With {.Filter = "PNG image (*.png)|*.png", .DefaultExt = "png", .FileName = "circuit.png"}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            Using bmp = RenderCircuitImage(c, _running)
                bmp.Save(dlg.FileName, Imaging.ImageFormat.Png)
            End Using
            _statusMessage.Text = $"Exported {dlg.FileName}"
        End Using
    End Sub

    ''' <summary>Renders the whole circuit to a bitmap (also used for documentation screenshots).</summary>
    Public Shared Function RenderCircuitImage(c As Circuit, simulating As Boolean) As Bitmap
        Dim b = c.Bounds()
        b.Inflate(30, 30)
        Dim bmp As New Bitmap(CInt(b.Width), CInt(b.Height))
        Using g = Graphics.FromImage(bmp)
            g.Clear(Color.White)
            g.TranslateTransform(-b.Left, -b.Top)
            Using canvas As New CircuitCanvas() With {.Circuit = c, .Simulating = simulating}
                canvas.PaintTo(g)
            End Using
        End Using
        Return bmp
    End Function

    Private Sub OnGenerator(sender As Object, e As EventArgs)
        If _running Then Return
        Using dlg As New GeneratorDialog()
            If dlg.ShowDialog(Me) <> DialogResult.OK OrElse dlg.Result Is Nothing Then Return
            If Not ConfirmDiscard() Then Return
            NewCircuit(dlg.Result.Circuit)
            FitView()
            _dirty = True
            UpdateTitle()
            _statusMessage.Text = "Circuit generated. Press Start (F9), then press the start button S1 / 1S0."
            Using info As New Form() With {.Text = "Generated circuit", .Size = New Size(760, 480), .StartPosition = FormStartPosition.CenterParent,
                                           .ShowInTaskbar = False, .Font = Font}
                Dim box As New TextBox() With {.Multiline = True, .ReadOnly = True, .Dock = DockStyle.Fill, .ScrollBars = ScrollBars.Both,
                                               .WordWrap = False, .Font = New Font("Consolas", 9), .BackColor = Color.White,
                                               .Text = dlg.Result.Explanation.Replace(vbLf, vbCrLf).Replace(vbCr & vbCrLf, vbCrLf)}
                info.Controls.Add(box)
                info.ShowDialog(Me)
            End Using
        End Using
    End Sub

    Private Sub OpenExample(build As Func(Of Circuit))
        If Not ConfirmDiscard() Then Return
        NewCircuit(build())
        _statusMessage.Text = "Example loaded. Press Start (F9), then click the push buttons and switches."
    End Sub

    Protected Overrides Sub OnFormClosing(e As FormClosingEventArgs)
        If Not ConfirmDiscard() Then e.Cancel = True
        MyBase.OnFormClosing(e)
    End Sub

    ' ================================================================= editing

    Private Sub OnLibraryActivate(sender As Object, e As EventArgs)
        If _running OrElse _library.SelectedItems.Count = 0 Then Return
        Dim preset = DirectCast(_library.SelectedItems(0).Tag, LibraryPreset)
        _canvas.PlacingPreset = preset
        _statusMessage.Text = $"Click on the drawing area to place: {preset.Name}  (hold Shift to place several, Esc to cancel)"
        _canvas.Focus()
    End Sub

    Private Sub OnPropertyValueChanged(s As Object, e As PropertyValueChangedEventArgs)
        _canvas.Circuit.CleanupTubes()
        _properties.Refresh()
        _canvas.Invalidate()
        OnCircuitModified()
    End Sub

    Private Sub FitView()
        _canvas.ZoomToFit()
        UpdateUiState()
    End Sub

    Private Sub SetZoom(z As Single)
        _canvas.Zoom = z
        UpdateUiState()
    End Sub

    ' ================================================================= simulation

    Private Sub OnStart(sender As Object, e As EventArgs)
        If _running AndAlso Not _paused Then Return
        If Not _running Then
            If _canvas.Circuit.Elements.Count = 0 Then
                _statusMessage.Text = "Place some components first, or load one of the examples."
                Return
            End If
            _running = True
            _canvas.PlacingPreset = Nothing
            _canvas.ClearSelection()
            _canvas.Simulating = True
            _simulator.Reset()
            _diagram.Simulator = _simulator
            _statusMessage.Text = "Click push buttons and selector switches to operate the valves."
        End If
        _paused = False
        _timer.Start()
        UpdateUiState()
        _canvas.Invalidate()
    End Sub

    Private Sub OnPause(sender As Object, e As EventArgs)
        If Not _running Then Return
        _paused = Not _paused
        If _paused Then _timer.Stop() Else _timer.Start()
        UpdateUiState()
    End Sub

    Private Sub OnStop(sender As Object, e As EventArgs)
        StopSimulation()
    End Sub

    Private Sub StopSimulation()
        _timer.Stop()
        If Not _running Then Return
        _running = False
        _paused = False
        For Each el In _canvas.Circuit.Elements
            el.ResetSim()
        Next
        _canvas.Simulating = False
        _canvas.Invalidate()
        _diagram.Invalidate()
        _statusMessage.Text = ""
        UpdateUiState()
    End Sub

    Private Sub OnTick(sender As Object, e As EventArgs)
        Dim speed = {0.25, 0.5, 1.0, 2.0, 4.0}(Math.Max(0, _speedBox.SelectedIndex))
        Dim dt = TickMs / 1000.0 * speed / SubSteps
        For i = 1 To SubSteps
            _simulator.Step(dt)
        Next
        ShowSimulationStatus()
        _canvas.Invalidate()
        _diagram.Invalidate()
    End Sub

    Private Sub OnElementOperated(sender As Object, e As EventArgs)
        If Not _running Then Return
        _simulator.RunLogic()
        ShowSimulationStatus()
        _canvas.Invalidate()
    End Sub

    Private Sub ShowSimulationStatus()
        _statusMode.Text = $"Simulating   t = {_simulator.Time:0.00} s{If(_paused, "  (paused)", "")}"
        If _simulator.Warnings.Count > 0 Then
            _statusMessage.Text = "Warning: " & _simulator.Warnings(0)
            _statusMessage.ForeColor = Color.DarkRed
        ElseIf _statusMessage.ForeColor = Color.DarkRed Then
            _statusMessage.Text = ""
            _statusMessage.ForeColor = SystemColors.ControlText
        End If
    End Sub

    Private Sub UpdateUiState()
        _btnStart.Enabled = Not _running OrElse _paused
        _menuStart.Enabled = _btnStart.Enabled
        _btnPause.Enabled = _running
        _menuPause.Enabled = _running
        _btnPause.Checked = _paused
        _btnStop.Enabled = _running
        _menuStop.Enabled = _running
        _library.Enabled = Not _running
        _properties.Enabled = Not _running
        If _running Then
            ShowSimulationStatus()
        Else
            _statusMode.Text = "Edit mode"
        End If
        _statusZoom.Text = $"Zoom {_canvas.Zoom * 100:0}%"
        UpdateUndoButtons()
    End Sub

    ' ================================================================= help

    Private Sub OnQuickGuide(sender As Object, e As EventArgs)
        MessageBox.Show(
"BUILDING A CIRCUIT
• Click a component in the library, then click on the drawing area (or drag it there).
• Drag from one port (small circle) to another to connect them. Red circles are electrical terminals.
• Drag components to move them; drag a box to select several. Ctrl+click adds to the selection.
• R rotates, Del deletes, arrow keys nudge. Ctrl+Z / Ctrl+Y undo and redo.
• Ctrl+C, Ctrl+X, Ctrl+V and Ctrl+D copy, cut, paste and duplicate.
• Drag the middle segment of a tube to move it. Use tube / wire junctions as branch or bend points.
• Edit properties on the right: label, actuation, solenoid names, stroke time, pressures, delays...
• Ctrl + mouse wheel zooms.

ELECTRO-PNEUMATICS
• Wire +24 V and 0 V connections, contacts and coils. A valve with solenoid actuation switches when
  a 'Valve solenoid' coil with the same label (e.g. 1M1) is energized.
• Relay contacts follow the relay coil whose label is set as their Reference (e.g. K1).
• Proximity sensors and limit switches use a cylinder position mark as Reference (e.g. 1B2).

SIMULATING
• Press Start (F9). Pressurized tubes turn dark blue, live wires turn red.
• Click push buttons (hold the mouse button) and selector switches. Clicking a solenoid valve
  operates its manual override.
• Roller lever valves are operated when a cylinder reaches the position mark with the same name.
• One-way flow control valves throttle air flowing from port 2 to port 1 (free flow 1 → 2).
• The displacement-step diagram at the bottom records cylinders and labelled valves.
• Stop (F11) resets everything.

CIRCUIT GENERATOR (Ctrl+G)
• Type a sequence such as A+ B+ B- A- and PneuSim designs the complete circuit, either
  electro-pneumatic (relay step chain) or pneumatic (cascade method), with an explanation.",
            "PneuSim Quick Guide", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub OnAbout(sender As Object, e As EventArgs)
        MessageBox.Show($"{AppName} 2.0{vbCrLf}Pneumatic and electro-pneumatic circuit design and simulation{vbCrLf}{vbCrLf}" &
                        "Symbols follow ISO 1219. Intended for learning and training.",
                        "About " & AppName, MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub
End Class

''' <summary>Small toolbar images drawn in code so the project needs no image resources.</summary>
Public Module Icons
    Private Function Draw(paint As Action(Of Graphics)) As Bitmap
        Dim bmp As New Bitmap(16, 16)
        Using g = Graphics.FromImage(bmp)
            g.SmoothingMode = SmoothingMode.AntiAlias
            paint(g)
        End Using
        Return bmp
    End Function

    Public Function NewFile() As Image
        Return Draw(Sub(g)
                        g.FillPolygon(Brushes.White, {New Point(3, 1), New Point(10, 1), New Point(13, 4), New Point(13, 15), New Point(3, 15)})
                        g.DrawPolygon(Pens.DimGray, {New Point(3, 1), New Point(10, 1), New Point(13, 4), New Point(13, 15), New Point(3, 15)})
                    End Sub)
    End Function

    Public Function Open() As Image
        Return Draw(Sub(g)
                        g.FillRectangle(Brushes.Goldenrod, 1, 4, 14, 10)
                        g.FillRectangle(Brushes.Gold, 1, 6, 14, 8)
                        g.DrawRectangle(Pens.DarkGoldenrod, 1, 4, 14, 10)
                    End Sub)
    End Function

    Public Function Save() As Image
        Return Draw(Sub(g)
                        g.FillRectangle(Brushes.SteelBlue, 1, 1, 14, 14)
                        g.FillRectangle(Brushes.White, 4, 2, 8, 5)
                        g.FillRectangle(Brushes.LightGray, 4, 10, 8, 5)
                    End Sub)
    End Function

    Public Function Play() As Image
        Return Draw(Sub(g) g.FillPolygon(Brushes.ForestGreen, {New Point(3, 1), New Point(14, 8), New Point(3, 15)}))
    End Function

    Public Function Pause() As Image
        Return Draw(Sub(g)
                        g.FillRectangle(Brushes.DarkOrange, 3, 2, 4, 12)
                        g.FillRectangle(Brushes.DarkOrange, 9, 2, 4, 12)
                    End Sub)
    End Function

    Public Function StopIcon() As Image
        Return Draw(Sub(g) g.FillRectangle(Brushes.Firebrick, 2, 2, 12, 12))
    End Function

    Public Function RotateIcon() As Image
        Return Draw(Sub(g)
                        Using p As New Pen(Color.DimGray, 2)
                            g.DrawArc(p, 2, 2, 12, 12, 30, 280)
                        End Using
                        g.FillPolygon(Brushes.DimGray, {New Point(10, 0), New Point(15, 4), New Point(9, 6)})
                    End Sub)
    End Function

    Public Function DeleteIcon() As Image
        Return Draw(Sub(g)
                        Using p As New Pen(Color.Firebrick, 2.5F)
                            g.DrawLine(p, 3, 3, 13, 13)
                            g.DrawLine(p, 13, 3, 3, 13)
                        End Using
                    End Sub)
    End Function

    Public Function UndoIcon(redo As Boolean) As Image
        Return Draw(Sub(g)
                        If redo Then
                            g.TranslateTransform(16, 0)
                            g.ScaleTransform(-1, 1)
                        End If
                        Using p As New Pen(Color.SteelBlue, 2)
                            g.DrawArc(p, 3, 4, 11, 10, 180, 200)
                        End Using
                        g.FillPolygon(Brushes.SteelBlue, {New Point(0, 6), New Point(7, 6), New Point(3, 12)})
                    End Sub)
    End Function

    Public Function Wand() As Image
        Return Draw(Sub(g)
                        Using p As New Pen(Color.DimGray, 2.5F)
                            g.DrawLine(p, 2, 14, 10, 6)
                        End Using
                        g.FillPolygon(Brushes.Gold, {New Point(12, 0), New Point(13, 3), New Point(16, 4), New Point(13, 5), New Point(12, 8), New Point(11, 5), New Point(8, 4), New Point(11, 3)})
                    End Sub)
    End Function

    Public Function ZoomIn() As Image
        Return Draw(Sub(g) Magnifier(g, True))
    End Function

    Public Function ZoomOut() As Image
        Return Draw(Sub(g) Magnifier(g, False))
    End Function

    Private Sub Magnifier(g As Graphics, plus As Boolean)
        Using p As New Pen(Color.DimGray, 1.6F)
            g.DrawEllipse(p, 1, 1, 10, 10)
            g.DrawLine(p, 4, 6, 8, 6)
            If plus Then g.DrawLine(p, 6, 4, 6, 8)
        End Using
        Using p As New Pen(Color.DimGray, 3)
            g.DrawLine(p, 10, 10, 14, 14)
        End Using
    End Sub

    Public Function AppIcon() As Icon
        Using bmp As New Bitmap(32, 32)
            Using g = Graphics.FromImage(bmp)
                g.SmoothingMode = SmoothingMode.AntiAlias
                g.FillEllipse(New SolidBrush(RenderContext.PressureColor), 1, 1, 30, 30)
                g.FillRectangle(Brushes.White, 6, 11, 16, 10)
                g.FillRectangle(Brushes.White, 22, 14, 6, 4)
                g.FillRectangle(New SolidBrush(RenderContext.PressureColor), 13, 12, 3, 8)
            End Using
            Return Icon.FromHandle(bmp.GetHicon())
        End Using
    End Function
End Module
