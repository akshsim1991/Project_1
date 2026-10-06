Imports System.ComponentModel
Imports System.Drawing.Drawing2D
Imports System.IO

''' <summary>Main window: layout, menus and toolbar. Other parts live in MainForm.*.vb.</summary>
Partial Public Class MainForm
    Inherits Form

    Private Const AppName = "PneuSim"
    Private Const AppVersion = "3.2"
    Private Const FileFilter = "PneuSim projects (*.pneu)|*.pneu|All files (*.*)|*.*"

    Private ReadOnly _canvas As New CircuitCanvas() With {.Dock = DockStyle.Fill}
    Private ReadOnly _pageTabs As New TabControl() With {.Dock = DockStyle.Top, .Height = 24}
    Private ReadOnly _diagram As New DiagramPanel() With {.Dock = DockStyle.Fill}
    Private ReadOnly _plotter As New PlotterPanel() With {.Dock = DockStyle.Fill}
    Private ReadOnly _lessons As New LessonsPanel() With {.Dock = DockStyle.Fill}
    Private ReadOnly _bottomTabs As New TabControl() With {.Dock = DockStyle.Fill}
    Private ReadOnly _library As New ListView()
    Private ReadOnly _properties As New PropertyGrid()
    Private ReadOnly _tooltip As New ToolTip() With {.AutoPopDelay = 12000, .InitialDelay = 700, .ReshowDelay = 300}

    Private ReadOnly _statusMode As New ToolStripStatusLabel() With {.BorderSides = ToolStripStatusLabelBorderSides.Right, .Padding = New Padding(0, 0, 8, 0)}
    Private ReadOnly _statusMessage As New ToolStripStatusLabel() With {.Spring = True, .TextAlign = ContentAlignment.MiddleLeft}
    Private ReadOnly _statusZoom As New ToolStripStatusLabel()
    Private _status As StatusStrip
    Private _menu As MenuStrip
    Private _toolbar As ToolStrip

    Private _btnStart, _btnPause, _btnStop, _btnReal, _btnRecord As ToolStripButton
    Private _menuStart, _menuPause, _menuStop, _menuReal, _menuDark, _menuRecord As ToolStripMenuItem
    Private _speedBox As ToolStripComboBox
    Private _diagramSplit As SplitContainer

    Public Sub New()
        AppSettings.Load()
        Text = AppName
        Font = New Font("Segoe UI", 9)
        Size = New Size(1400, 900)
        StartPosition = FormStartPosition.CenterScreen
        KeyPreview = True
        Icon = Icons.AppIcon()

        BuildLayout()
        Theme.OwnerDrawTabs(_bottomTabs)
        Theme.OwnerDrawTabs(_pageTabs)
        AddHandler _canvas.SelectionChanged, Sub() _properties.SelectedObjects = _canvas.SelectedElements.Cast(Of Object)().ToArray()
        AddHandler _canvas.CircuitModified, Sub() OnCircuitModified()
        AddHandler _canvas.ElementOperated, AddressOf OnElementOperated
        AddHandler _canvas.StatusMessage, Sub(s, msg) _statusMessage.Text = msg
        AddHandler _canvas.HoverElementChanged, AddressOf OnHoverElement
        AddHandler _canvas.ZoomChanged, Sub() _statusZoom.Text = $"Zoom {_canvas.Zoom * 100:0}%"
        AddHandler _properties.PropertyValueChanged, AddressOf OnPropertyValueChanged
        AddHandler _timer.Tick, AddressOf OnTick
        AddHandler _pageTabs.SelectedIndexChanged, Sub() If Not _switchingPages Then ShowPage(_pageTabs.SelectedIndex)
        AddHandler _lessons.LoadCircuit, Sub(s, c) LoadLessonCircuit(c)
        _lessons.CurrentCircuit = Function() _project.SimulationCircuit()
        _canvas.NameScope = Function() _project.AllElements()
        InitTroubleshooting()

        NewProject(New Project(New Circuit()))
        _realPhysics = AppSettings.RealPhysics
        ApplyTheme(AppSettings.DarkMode)
        UpdateUiState()
        StartAutosave()
    End Sub

    ' ================================================================= layout

    Private Sub BuildLayout()
        ' Left: component library.
        _library.Dock = DockStyle.Fill
        _library.View = View.Tile
        _library.TileSize = New Size(236, 58)
        _library.MultiSelect = False
        _library.HideSelection = False
        _library.LargeImageList = New ImageList() With {.ImageSize = New Size(72, 48), .ColorDepth = ColorDepth.Depth32Bit}
        For Each cat In Library.Presets.Select(Function(p) p.Category).Distinct()
            _library.Groups.Add(cat, cat)
        Next
        RenderLibraryImages(ColorScheme.Light)
        For i = 0 To Library.Presets.Length - 1
            Dim preset = Library.Presets(i)
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

        ' Centre: page tabs + canvas, above the bottom tool tabs.
        Dim canvasPanel As New Panel() With {.Dock = DockStyle.Fill}
        canvasPanel.Controls.Add(_canvas)
        canvasPanel.Controls.Add(_pageTabs)
        Dim pageMenu As New ContextMenuStrip()
        pageMenu.Items.Add("Add page", Nothing, Sub() AddPage())
        pageMenu.Items.Add("Rename page...", Nothing, Sub() RenamePage())
        pageMenu.Items.Add("Delete page", Nothing, Sub() DeletePage())
        _pageTabs.ContextMenuStrip = pageMenu

        AddBottomTab("Displacement-step diagram", _diagram)
        AddBottomTab("Plotter", _plotter)
        AddBottomTab("Check circuit", BuildCheckPanel())
        AddBottomTab("Explain", BuildExplainPanel())
        AddBottomTab("Lessons", _lessons)
        AddBottomTab("Inspector", _inspector)
        AddBottomTab("Troubleshoot", _trouble)

        _diagramSplit = New SplitContainer() With {.Dock = DockStyle.Fill, .Orientation = Orientation.Horizontal, .FixedPanel = FixedPanel.Panel2}
        _diagramSplit.Panel1.Controls.Add(canvasPanel)
        _diagramSplit.Panel2.Controls.Add(_bottomTabs)

        Dim rightSplit As New SplitContainer() With {.Dock = DockStyle.Fill, .FixedPanel = FixedPanel.Panel2}
        rightSplit.Panel1.Controls.Add(_diagramSplit)
        rightSplit.Panel2.Controls.Add(propPanel)

        Dim mainSplit As New SplitContainer() With {.Dock = DockStyle.Fill, .FixedPanel = FixedPanel.Panel1}
        mainSplit.Panel1.Controls.Add(libPanel)
        mainSplit.Panel2.Controls.Add(rightSplit)

        _status = New StatusStrip()
        _status.Items.AddRange({_statusMode, _statusMessage, _statusZoom})

        Controls.Add(mainSplit)
        _toolbar = BuildToolbar()
        Controls.Add(_toolbar)
        _menu = BuildMenu()
        Controls.Add(_menu)
        Controls.Add(_status)

        AddHandler Load, Sub()
                             mainSplit.SplitterDistance = 270
                             rightSplit.SplitterDistance = Math.Max(200, rightSplit.Width - 290)
                             _diagramSplit.SplitterDistance = Math.Max(200, _diagramSplit.Height - 230)
                         End Sub
    End Sub

    Private Sub AddBottomTab(title As String, content As Control)
        Dim page As New TabPage(title)
        content.Dock = DockStyle.Fill
        page.Controls.Add(content)
        _bottomTabs.TabPages.Add(page)
    End Sub

    Private Sub ShowBottomTab(title As String)
        For Each p As TabPage In _bottomTabs.TabPages
            If p.Text = title Then _bottomTabs.SelectedTab = p
        Next
        _diagramSplit.Panel2Collapsed = False
    End Sub

    Private Shared Function TitledPanel(title As String, content As Control) As Panel
        Dim p As New Panel() With {.Dock = DockStyle.Fill}
        Dim header As New Label() With {
            .Text = title, .Dock = DockStyle.Top, .Height = 24, .Padding = New Padding(6, 4, 0, 0),
            .BackColor = Color.FromArgb(232, 236, 244), .Font = New Font("Segoe UI", 8.5F, FontStyle.Bold), .Tag = "header"}
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
        file.DropDownItems.Add(Item("Project &information (title block)...", AddressOf OnProjectInfo))
        Dim export = New ToolStripMenuItem("&Export")
        export.DropDownItems.Add(Item("PDF report with title block...", AddressOf OnExportPdf, Keys.Control Or Keys.P))
        export.DropDownItems.Add(Item("SVG drawing (this page)...", AddressOf OnExportSvg))
        export.DropDownItems.Add(Item("DXF drawing for CAD (this page)...", AddressOf OnExportDxf))
        export.DropDownItems.Add(Item("PNG image (this page)...", AddressOf OnExportImage))
        export.DropDownItems.Add(Item("Parts list (CSV)...", AddressOf OnPartsList))
        file.DropDownItems.Add(export)
        file.DropDownItems.Add(New ToolStripSeparator())
        file.DropDownItems.Add(Item("E&xit", Sub() Close()))

        ' Edit shortcuts are handled in ProcessCmdKey so they work wherever the focus is,
        ' but still leave Ctrl+Z / Ctrl+C etc. to a text box being edited.
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

        Dim pages = New ToolStripMenuItem("&Page")
        pages.DropDownItems.Add(Item("&Add page", Sub() AddPage()))
        pages.DropDownItems.Add(Item("&Rename page...", Sub() RenamePage()))
        pages.DropDownItems.Add(Item("&Delete page", Sub() DeletePage()))
        pages.DropDownItems.Add(New ToolStripSeparator())
        pages.DropDownItems.Add(Item("&Next page", Sub() ShowPage(Math.Min(_project.Pages.Count - 1, _pageIndex + 1)), Keys.Control Or Keys.PageDown))
        pages.DropDownItems.Add(Item("&Previous page", Sub() ShowPage(Math.Max(0, _pageIndex - 1)), Keys.Control Or Keys.PageUp))

        Dim view = New ToolStripMenuItem("&View")
        view.DropDownItems.Add(Item("Zoom &In", Sub() SetZoom(_canvas.Zoom * 1.2F), Keys.Control Or Keys.Oemplus))
        view.DropDownItems.Add(Item("Zoom &Out", Sub() SetZoom(_canvas.Zoom / 1.2F), Keys.Control Or Keys.OemMinus))
        view.DropDownItems.Add(Item("&Actual Size", Sub() SetZoom(1), Keys.Control Or Keys.D0))
        view.DropDownItems.Add(Item("Zoom to &Fit", Sub() FitView(), Keys.Control Or Keys.D9))
        view.DropDownItems.Add(New ToolStripSeparator())
        Dim tools = Item("&Bottom panel (diagram, plotter, check, explain, lessons, inspector, troubleshoot)", Nothing)
        tools.Checked = True
        tools.CheckOnClick = True
        AddHandler tools.CheckedChanged, Sub() _diagramSplit.Panel2Collapsed = Not tools.Checked
        view.DropDownItems.Add(tools)
        view.DropDownItems.Add(Item("&Cutaway view (animated)", AddressOf OnCutaway, Keys.F7))
        _menuDark = Item("&Dark mode", Sub() ApplyTheme(Not AppSettings.DarkMode))
        view.DropDownItems.Add(_menuDark)

        Dim sim = New ToolStripMenuItem("&Simulation")
        _menuStart = Item("&Start", AddressOf OnStart, Keys.F9)
        _menuPause = Item("&Pause", AddressOf OnPause, Keys.F10)
        _menuStop = Item("S&top and Reset", AddressOf OnStop, Keys.F11)
        _menuReal = Item("&Realistic physics (pressure build-up, loads, air consumption)", AddressOf OnToggleReal)
        _menuRecord = Item("Record animated &GIF", AddressOf OnToggleRecord)
        sim.DropDownItems.AddRange({_menuStart, _menuPause, _menuStop, New ToolStripSeparator(),
                                    Item("Step &back", Sub() ReplayStep(-1), Keys.Shift Or Keys.F12),
                                    Item("Step &forward", Sub() ReplayStep(+1), Keys.F12),
                                    Item("Back to the &previous event", Sub() ReplayEvent(-1), Keys.Control Or Keys.Shift Or Keys.F12),
                                    Item("Forward to the &next event", Sub() ReplayEvent(+1), Keys.Control Or Keys.F12),
                                    New ToolStripSeparator(), _menuReal, _menuRecord})

        Dim toolsMenu = New ToolStripMenuItem("&Tools")
        toolsMenu.DropDownItems.Add(Item("&Circuit Generator (from a sequence)...", AddressOf OnGenerator, Keys.Control Or Keys.G))
        toolsMenu.DropDownItems.Add(Item("C&heck my circuit", AddressOf OnFullCheck, Keys.F6))
        toolsMenu.DropDownItems.Add(Item("&Explain my circuit", AddressOf OnExplain, Keys.F8))
        toolsMenu.DropDownItems.Add(New ToolStripSeparator())
        toolsMenu.DropDownItems.Add(Item("Ca&lculators...", AddressOf OnCalculators))
        toolsMenu.DropDownItems.Add(Item("&Parts list and costs...", AddressOf OnPartsList))
        toolsMenu.DropDownItems.Add(Item("Parameter &sweep (compare settings)...", AddressOf OnSweep))
        toolsMenu.DropDownItems.Add(New ToolStripSeparator())
        toolsMenu.DropDownItems.Add(BuildTroubleshootMenu())

        Dim learn = New ToolStripMenuItem("&Learn")
        learn.DropDownItems.Add(Item("&Lessons", Sub() ShowBottomTab("Lessons")))
        learn.DropDownItems.Add(Item("Practice &quiz...", Sub() ShowQuiz(False)))
        learn.DropDownItems.Add(Item("Timed &exam...", Sub() ShowQuiz(True)))
        learn.DropDownItems.Add(Item("&Cutaway view (inside the components)", AddressOf OnCutaway))

        Dim exMenu = New ToolStripMenuItem("E&xamples")
        For Each ex In Examples.All
            Dim build = ex.Build
            exMenu.DropDownItems.Add(Item(ex.Name, Sub() OpenExample(build)))
        Next

        Dim help = New ToolStripMenuItem("&Help")
        help.DropDownItems.Add(Item("&Quick Guide", AddressOf OnQuickGuide, Keys.F1))
        help.DropDownItems.Add(Item("&About PneuSim", AddressOf OnAbout))

        menu.Items.AddRange({file, edit, pages, view, sim, toolsMenu, learn, exMenu, help})
        MainMenuStrip = menu
        Return menu
    End Function

    Private Function BuildToolbar() As ToolStrip
        Dim bar As New ToolStrip() With {.GripStyle = ToolStripGripStyle.Hidden, .ImageScalingSize = New Size(16, 16)}
        ' Common commands as icons (with tooltips) so the whole toolbar fits on a normal screen.
        bar.Items.Add(Button("New (Ctrl+N)", Icons.NewFile, AddressOf OnNew, iconOnly:=True))
        bar.Items.Add(Button("Open (Ctrl+O)", Icons.Open, AddressOf OnOpen, iconOnly:=True))
        bar.Items.Add(Button("Save (Ctrl+S)", Icons.Save, AddressOf OnSave, iconOnly:=True))
        bar.Items.Add(New ToolStripSeparator())
        _btnUndo = Button("Undo (Ctrl+Z)", Icons.UndoIcon(False), Sub() Undo(), iconOnly:=True)
        _btnRedo = Button("Redo (Ctrl+Y)", Icons.UndoIcon(True), Sub() Redo(), iconOnly:=True)
        bar.Items.AddRange({_btnUndo, _btnRedo})
        bar.Items.Add(New ToolStripSeparator())
        bar.Items.Add(Button("Generator", Icons.Wand, AddressOf OnGenerator))
        bar.Items.Add(Button("Check", Icons.CheckIcon, AddressOf OnFullCheck))
        bar.Items.Add(New ToolStripSeparator())
        _btnStart = Button("Start", Icons.Play, AddressOf OnStart)
        _btnPause = Button("Pause", Icons.Pause, AddressOf OnPause)
        _btnStop = Button("Stop", Icons.StopIcon, AddressOf OnStop)
        bar.Items.AddRange({_btnStart, _btnPause, _btnStop})
        AddReplayButtons(bar)
        bar.Items.Add(New ToolStripLabel("  Speed:"))
        _speedBox = New ToolStripComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .AutoSize = False, .Width = 60}
        _speedBox.Items.AddRange({"0.1x", "0.25x", "0.5x", "1x", "2x", "4x"})
        _speedBox.SelectedIndex = 3
        bar.Items.Add(_speedBox)
        _btnReal = Button("Realistic", Icons.GaugeIcon, AddressOf OnToggleReal)
        _btnRecord = Button("Record GIF", Icons.RecordIcon, AddressOf OnToggleRecord)
        bar.Items.AddRange({_btnReal, _btnRecord})
        bar.Items.Add(New ToolStripSeparator())
        bar.Items.Add(Button("Rotate (R)", Icons.RotateIcon, Sub() _canvas.RotateSelection(), iconOnly:=True))
        bar.Items.Add(Button("Delete (Del)", Icons.DeleteIcon, Sub() _canvas.DeleteSelection(), iconOnly:=True))
        bar.Items.Add(New ToolStripSeparator())
        bar.Items.Add(Button("Zoom in", Icons.ZoomIn, Sub() SetZoom(_canvas.Zoom * 1.2F), iconOnly:=True))
        bar.Items.Add(Button("Zoom out", Icons.ZoomOut, Sub() SetZoom(_canvas.Zoom / 1.2F), iconOnly:=True))
        bar.Items.Add(New ToolStripButton("Fit") With {.ToolTipText = "Zoom to fit (Ctrl+9)"})
        AddHandler bar.Items(bar.Items.Count - 1).Click, Sub() FitView()
        Return bar
    End Function

    ''' <summary>Undo, redo and clipboard keys for the drawing, from any part of the window.</summary>
    Protected Overrides Function ProcessCmdKey(ByRef msg As Message, keyData As Keys) As Boolean
        If (keyData And Keys.Control) = Keys.Control AndAlso (keyData And Keys.Alt) <> Keys.Alt AndAlso Not IsEditingText() Then
            Dim shift = (keyData And Keys.Shift) = Keys.Shift
            Select Case keyData And Keys.KeyCode
                Case Keys.Z : If shift Then Redo() Else Undo()
                    Return True
                Case Keys.Y : Redo() : Return True
                Case Keys.X : _canvas.CutSelection() : Return True
                Case Keys.C : _canvas.CopySelection() : Return True
                Case Keys.V : _canvas.Paste() : Return True
                Case Keys.D : _canvas.DuplicateSelection() : Return True
                Case Keys.A : _canvas.SelectAll() : Return True
            End Select
        End If
        Return MyBase.ProcessCmdKey(msg, keyData)
    End Function

    ''' <summary>True while the keyboard focus is in a text box (property value, explanation text, dialog field).</summary>
    Private Function IsEditingText() As Boolean
        Dim c As Control = Me
        While TypeOf c Is ContainerControl AndAlso DirectCast(c, ContainerControl).ActiveControl IsNot Nothing
            c = DirectCast(c, ContainerControl).ActiveControl
        End While
        ' The property grid is a container whose focused child may be its in-place text editor.
        Dim focused = FocusedLeaf(c)
        Return TypeOf focused Is TextBoxBase OrElse TypeOf focused Is ComboBox OrElse TypeOf focused Is NumericUpDown
    End Function

    Private Shared Function FocusedLeaf(c As Control) As Control
        For Each child As Control In c.Controls
            If child.ContainsFocus Then Return FocusedLeaf(child)
        Next
        Return c
    End Function

    Private Shared Function Item(text As String, handler As EventHandler, Optional keys As Keys = Keys.None) As ToolStripMenuItem
        Dim mi As New ToolStripMenuItem(text)
        If handler IsNot Nothing Then AddHandler mi.Click, handler
        If keys <> Keys.None Then
            mi.ShortcutKeys = keys
            Dim key = keys And Keys.KeyCode
            Dim name = If(key = Keys.Oemplus, "+", If(key = Keys.OemMinus, "-", If(key >= Keys.D0 AndAlso key <= Keys.D9, ChrW(AscW("0"c) + (key - Keys.D0)).ToString(), Nothing)))
            If name IsNot Nothing Then mi.ShortcutKeyDisplayString = If((keys And Keys.Control) = Keys.Control, "Ctrl+", "") & If((keys And Keys.Shift) = Keys.Shift, "Shift+", "") & name
        End If
        Return mi
    End Function

    Private Shared Function Button(text As String, image As Image, handler As EventHandler, Optional iconOnly As Boolean = False) As ToolStripButton
        Dim b As New ToolStripButton(text, image) With {
            .DisplayStyle = If(iconOnly, ToolStripItemDisplayStyle.Image, ToolStripItemDisplayStyle.ImageAndText),
            .ToolTipText = text}
        AddHandler b.Click, handler
        Return b
    End Function

    Private Sub OnLibraryActivate(sender As Object, e As EventArgs)
        If _running OrElse _library.SelectedItems.Count = 0 Then Return
        Dim preset = DirectCast(_library.SelectedItems(0).Tag, LibraryPreset)
        _canvas.PlacingPreset = preset
        _statusMessage.Text = $"Click on the drawing area to place: {preset.Name}  (hold Shift to place several, Esc to cancel)"
        _canvas.Focus()
    End Sub

    Private Sub OnHoverElement(sender As Object, e As CircuitElement)
        Dim text = If(e Is Nothing, Nothing, ComponentHelp.HelpFor(e))
        _tooltip.SetToolTip(_canvas, text)
    End Sub

    Private Sub FitView()
        _canvas.ZoomToFit()
        UpdateUiState()
    End Sub

    Private Sub SetZoom(z As Single)
        _canvas.Zoom = z
        UpdateUiState()
    End Sub

    Private Sub UpdateUiState()
        _btnStart.Enabled = Not _running OrElse _paused
        _menuStart.Enabled = _btnStart.Enabled
        _btnPause.Enabled = _running
        _menuPause.Enabled = _running
        _btnPause.Checked = _paused
        _btnStop.Enabled = _running
        _menuStop.Enabled = _running
        _btnReal.Checked = _realPhysics
        _menuReal.Checked = _realPhysics
        _btnRecord.Checked = _gif IsNot Nothing
        _menuRecord.Checked = _gif IsNot Nothing
        _menuDark.Checked = AppSettings.DarkMode
        _library.Enabled = Not _running
        _properties.Enabled = Not _running
        If _running Then
            ShowSimulationStatus()
        Else
            _statusMode.Text = "Edit mode" & If(_realPhysics, "  (realistic physics on)", "")
        End If
        _statusZoom.Text = $"Zoom {_canvas.Zoom * 100:0}%"
        UpdateUndoButtons()
        UpdateReplayUi()
    End Sub

    ' ================================================================= theme

    Private Sub ApplyTheme(dark As Boolean)
        AppSettings.DarkMode = dark
        AppSettings.Save()
        Dim scheme = If(dark, ColorScheme.Dark, ColorScheme.Light)
        _canvas.Scheme = scheme
        _diagram.Scheme = scheme
        _plotter.Scheme = scheme
        RenderLibraryImages(scheme)
        Dim back = If(dark, Color.FromArgb(37, 39, 46), SystemColors.Control)
        Dim fore = If(dark, Color.FromArgb(225, 228, 235), SystemColors.ControlText)
        Dim field = If(dark, Color.FromArgb(28, 30, 36), SystemColors.Window)
        Dim header = If(dark, Color.FromArgb(52, 56, 66), Color.FromArgb(232, 236, 244))
        BackColor = back
        ForeColor = fore
        ThemeControls(Controls, back, fore, field, header)
        Dim renderer = If(dark, New ToolStripProfessionalRenderer(New DarkColorTable()), New ToolStripProfessionalRenderer())
        For Each strip As ToolStrip In {_menu, _toolbar, _status}
            strip.Renderer = renderer
            strip.ForeColor = fore
            For Each it As ToolStripItem In strip.Items
                ThemeItem(it, fore, back, renderer)
            Next
        Next
        _properties.ViewBackColor = field
        _properties.ViewForeColor = fore
        _properties.LineColor = header
        _properties.CategoryForeColor = fore
        _properties.HelpBackColor = back
        _properties.HelpForeColor = fore
        If dark Then
            Try
                SetSplitterColor(header)
            Catch ex As MissingMethodException
                ' Older WinForms implementations (Mono) do not have this property.
            End Try
        End If
        _speedBox.ComboBox.BackColor = field
        _speedBox.ComboBox.ForeColor = fore
        If _menuDark IsNot Nothing Then _menuDark.Checked = dark
        _canvas.Invalidate() : _diagram.Invalidate() : _plotter.Invalidate()
    End Sub

    <Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.NoInlining)>
    Private Sub SetSplitterColor(c As Color)
        _properties.CategorySplitterColor = c
    End Sub

    Private Sub RenderLibraryImages(scheme As ColorScheme)
        Dim images = _library.LargeImageList.Images
        Dim old = images.Cast(Of Image)().ToList()
        images.Clear()
        For Each preset In Library.Presets
            images.Add(Library.RenderThumbnail(preset.Factory.Invoke(), 72, 48, scheme))
        Next
        For Each img In old
            img.Dispose()
        Next
        _library.Invalidate()
    End Sub

    Private Sub ThemeItem(it As ToolStripItem, fore As Color, back As Color, renderer As ToolStripRenderer)
        it.ForeColor = fore
        Dim mi = TryCast(it, ToolStripDropDownItem)
        If mi IsNot Nothing Then
            mi.DropDown.Renderer = renderer
            mi.DropDown.BackColor = back
            For Each child As ToolStripItem In mi.DropDownItems
                ThemeItem(child, fore, back, renderer)
            Next
        End If
    End Sub

    Private Sub ThemeControls(controls As Control.ControlCollection, back As Color, fore As Color, field As Color, header As Color)
        For Each c As Control In controls
            If TypeOf c Is CircuitCanvas OrElse TypeOf c Is ToolStrip Then Continue For
            If TypeOf c Is Label AndAlso Equals(c.Tag, "header") Then
                c.BackColor = header
                c.ForeColor = fore
            ElseIf TypeOf c Is TextBox OrElse TypeOf c Is ListBox OrElse TypeOf c Is ListView OrElse TypeOf c Is ComboBox Then
                c.BackColor = field
                c.ForeColor = fore
            ElseIf TypeOf c Is Button Then
                c.BackColor = back
                c.ForeColor = fore
                DirectCast(c, Button).FlatStyle = If(AppSettings.DarkMode, FlatStyle.Flat, FlatStyle.Standard)
            ElseIf Not TypeOf c Is PropertyGrid Then
                c.BackColor = back
                c.ForeColor = fore
            End If
            If c.HasChildren Then ThemeControls(c.Controls, back, fore, field, header)
        Next
    End Sub

    ''' <summary>Menu and toolbar colours for dark mode.</summary>
    Private Class DarkColorTable
        Inherits ProfessionalColorTable
        Private Shared ReadOnly Back As Color = Color.FromArgb(37, 39, 46)
        Private Shared ReadOnly Hot As Color = Color.FromArgb(62, 68, 82)
        Private Shared ReadOnly Edge As Color = Color.FromArgb(70, 74, 86)
        Public Overrides ReadOnly Property MenuStripGradientBegin As Color = Back
        Public Overrides ReadOnly Property MenuStripGradientEnd As Color = Back
        Public Overrides ReadOnly Property ToolStripGradientBegin As Color = Back
        Public Overrides ReadOnly Property ToolStripGradientMiddle As Color = Back
        Public Overrides ReadOnly Property ToolStripGradientEnd As Color = Back
        Public Overrides ReadOnly Property ToolStripDropDownBackground As Color = Back
        Public Overrides ReadOnly Property ImageMarginGradientBegin As Color = Back
        Public Overrides ReadOnly Property ImageMarginGradientMiddle As Color = Back
        Public Overrides ReadOnly Property ImageMarginGradientEnd As Color = Back
        Public Overrides ReadOnly Property MenuItemSelected As Color = Hot
        Public Overrides ReadOnly Property MenuItemSelectedGradientBegin As Color = Hot
        Public Overrides ReadOnly Property MenuItemSelectedGradientEnd As Color = Hot
        Public Overrides ReadOnly Property MenuItemPressedGradientBegin As Color = Hot
        Public Overrides ReadOnly Property MenuItemPressedGradientEnd As Color = Hot
        Public Overrides ReadOnly Property MenuItemBorder As Color = Edge
        Public Overrides ReadOnly Property MenuBorder As Color = Edge
        Public Overrides ReadOnly Property ButtonSelectedGradientBegin As Color = Hot
        Public Overrides ReadOnly Property ButtonSelectedGradientEnd As Color = Hot
        Public Overrides ReadOnly Property ButtonCheckedGradientBegin As Color = Hot
        Public Overrides ReadOnly Property ButtonCheckedGradientEnd As Color = Hot
        Public Overrides ReadOnly Property ButtonSelectedBorder As Color = Edge
        Public Overrides ReadOnly Property SeparatorDark As Color = Edge
        Public Overrides ReadOnly Property SeparatorLight As Color = Back
        Public Overrides ReadOnly Property StatusStripGradientBegin As Color = Back
        Public Overrides ReadOnly Property StatusStripGradientEnd As Color = Back
        Public Overrides ReadOnly Property ToolStripBorder As Color = Back
    End Class
End Class
