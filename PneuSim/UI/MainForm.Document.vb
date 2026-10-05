Imports System.IO

''' <summary>Main window: project, pages, undo history, files and exports.</summary>
Partial Public Class MainForm

    Private _project As Project
    Private _pageIndex As Integer
    Private _switchingPages As Boolean
    Private _filePath As String
    Private _dirty As Boolean

    ' Undo history: snapshots of the project after each change.
    Private ReadOnly _history As New List(Of (Xml As String, Page As Integer))
    Private _historyIndex As Integer = -1
    Private Const MaxHistory = 100
    Private _menuUndo, _menuRedo As ToolStripMenuItem
    Private _btnUndo, _btnRedo As ToolStripButton

    Private ReadOnly Property CurrentCircuit As Circuit
        Get
            Return _project.Pages(_pageIndex).Circuit
        End Get
    End Property

    Private Sub NewProject(p As Project, Optional path As String = Nothing)
        StopSimulation()
        _project = p
        _pageIndex = 0
        _filePath = path
        _dirty = False
        _history.Clear()
        _history.Add((p.ToXml(), 0))
        _historyIndex = 0
        RefreshPageTabs()
        ShowPage(0)
        UpdateTitle()
        UpdateUndoButtons()
        ScheduleCheck()
    End Sub

    Private Sub RefreshPageTabs()
        _switchingPages = True
        _pageTabs.TabPages.Clear()
        For Each pg In _project.Pages
            _pageTabs.TabPages.Add(New TabPage(pg.Name))
        Next
        _pageTabs.SelectedIndex = Math.Min(_pageIndex, _project.Pages.Count - 1)
        _switchingPages = False
    End Sub

    Private Sub ShowPage(index As Integer, Optional keepView As Boolean = False)
        If index < 0 OrElse index >= _project.Pages.Count Then Return
        _pageIndex = index
        If _pageTabs.SelectedIndex <> index Then
            _switchingPages = True
            _pageTabs.SelectedIndex = index
            _switchingPages = False
        End If
        _project.UpdateCrossReferences()
        _canvas.Circuit = CurrentCircuit
        _canvas.Simulator = _simulator
        _properties.SelectedObject = Nothing
        If Not keepView Then _canvas.ScrollToCircuit()
        _canvas.Invalidate()
    End Sub

    Private Sub AddPage()
        If _running Then Return
        _project.AddPage()
        RefreshPageTabs()
        ShowPage(_project.Pages.Count - 1)
        OnCircuitModified()
    End Sub

    Private Sub RenamePage()
        If _running Then Return
        Dim name = Microsoft.VisualBasic.Interaction.InputBox("Page name:", AppName, _project.Pages(_pageIndex).Name)
        If String.IsNullOrWhiteSpace(name) Then Return
        _project.Pages(_pageIndex).Name = name.Trim()
        RefreshPageTabs()
        OnCircuitModified()
    End Sub

    Private Sub DeletePage()
        If _running OrElse _project.Pages.Count <= 1 Then Return
        If CurrentCircuit.Elements.Count > 0 AndAlso
           MessageBox.Show($"Delete page '{_project.Pages(_pageIndex).Name}' and everything on it?", AppName,
                           MessageBoxButtons.YesNo, MessageBoxIcon.Warning) <> DialogResult.Yes Then Return
        _project.Pages.RemoveAt(_pageIndex)
        _pageIndex = Math.Max(0, _pageIndex - 1)
        RefreshPageTabs()
        ShowPage(_pageIndex)
        OnCircuitModified()
    End Sub

    ''' <summary>Records an undo snapshot after every change.</summary>
    Private Sub OnCircuitModified()
        If _historyIndex < _history.Count - 1 Then _history.RemoveRange(_historyIndex + 1, _history.Count - _historyIndex - 1)
        _history.Add((_project.ToXml(), _pageIndex))
        If _history.Count > MaxHistory Then _history.RemoveAt(0)
        _historyIndex = _history.Count - 1
        _project.UpdateCrossReferences()
        MarkDirty()
        UpdateUndoButtons()
        ScheduleCheck()
    End Sub

    Private Sub Undo()
        If _running Then _statusMessage.Text = "Stop the simulation (F11) to undo changes." : Return
        If _historyIndex <= 0 Then Return
        _historyIndex -= 1
        RestoreSnapshot()
    End Sub

    Private Sub Redo()
        If _running Then _statusMessage.Text = "Stop the simulation (F11) to redo changes." : Return
        If _historyIndex >= _history.Count - 1 Then Return
        _historyIndex += 1
        RestoreSnapshot()
    End Sub

    Private Sub RestoreSnapshot()
        Dim snap = _history(_historyIndex)
        Dim samePage = snap.Page = _pageIndex
        Dim scroll = _canvas.AutoScrollPosition
        _project = Project.FromXml(snap.Xml)
        _pageIndex = Math.Min(snap.Page, _project.Pages.Count - 1)
        RefreshPageTabs()
        ShowPage(_pageIndex, keepView:=samePage)
        If samePage Then _canvas.AutoScrollPosition = New Point(-scroll.X, -scroll.Y)
        MarkDirty()
        UpdateUndoButtons()
        ScheduleCheck()
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
        Dim name = If(_filePath Is Nothing, _project.Info.Title, Path.GetFileNameWithoutExtension(_filePath))
        Text = $"{name}{If(_dirty, " *", "")} - {AppName} {AppVersion}"
    End Sub

    ''' <summary>Asks to save unsaved changes. Returns False if the user cancelled.</summary>
    Private Function ConfirmDiscard() As Boolean
        If Not _dirty Then Return True
        Select Case MessageBox.Show("Save changes to the current project?", AppName, MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question)
            Case DialogResult.Yes : Return SaveProject(_filePath)
            Case DialogResult.No : Return True
            Case Else : Return False
        End Select
    End Function

    Private Sub OnNew(sender As Object, e As EventArgs)
        If ConfirmDiscard() Then NewProject(New Project(New Circuit()))
    End Sub

    Private Sub OnOpen(sender As Object, e As EventArgs)
        If Not ConfirmDiscard() Then Return
        Using dlg As New OpenFileDialog() With {.Filter = FileFilter}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            Try
                NewProject(Project.Load(dlg.FileName), dlg.FileName)
            Catch ex As Exception
                MessageBox.Show($"Could not open the file:{vbCrLf}{ex.Message}", AppName, MessageBoxButtons.OK, MessageBoxIcon.Error)
            End Try
        End Using
    End Sub

    ''' <summary>Opens a project given on the command line (double-clicking a .pneu file).</summary>
    Public Sub OpenFile(path As String)
        Try
            NewProject(Project.Load(path), path)
        Catch ex As Exception
            MessageBox.Show($"Could not open the file:{vbCrLf}{ex.Message}", AppName, MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    Private Sub OnSave(sender As Object, e As EventArgs)
        SaveProject(_filePath)
    End Sub

    Private Sub OnSaveAs(sender As Object, e As EventArgs)
        SaveProject(Nothing)
    End Sub

    Private Function SaveProject(path As String) As Boolean
        If path Is Nothing Then
            Using dlg As New SaveFileDialog() With {.Filter = FileFilter, .DefaultExt = "pneu", .FileName = SafeName(_project.Info.Title) & ".pneu"}
                If dlg.ShowDialog(Me) <> DialogResult.OK Then Return False
                path = dlg.FileName
            End Using
        End If
        Try
            _project.Save(path)
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

    Private Shared Function SafeName(s As String) As String
        Dim bad = Path.GetInvalidFileNameChars()
        Dim r = New String(If(s, "circuit").Select(Function(c) If(bad.Contains(c), "_"c, c)).ToArray()).Trim()
        Return If(r = "", "circuit", r)
    End Function

    Private Sub OnProjectInfo(sender As Object, e As EventArgs)
        Dim before = _project.ToXml()
        Using f As New Form() With {.Text = "Project information", .Size = New Size(520, 480), .StartPosition = FormStartPosition.CenterParent,
                                    .ShowInTaskbar = False, .Font = Font, .KeyPreview = True}
            Dim grid As New PropertyGrid() With {.Dock = DockStyle.Fill, .SelectedObject = _project.Info, .ToolbarVisible = False}
            f.Controls.Add(grid)
            AddHandler f.KeyDown, Sub(s2, k)
                                      If k.KeyCode = Keys.Escape Then f.Close()
                                  End Sub
            Theme.Apply(f)
            f.ShowDialog(Me)
        End Using
        If _project.ToXml() <> before Then OnCircuitModified()
    End Sub

    ' ----------------------------------------------------------------- export

    Private Function AskPath(filter As String, ext As String, name As String) As String
        Using dlg As New SaveFileDialog() With {.Filter = filter, .DefaultExt = ext, .FileName = SafeName(name) & "." & ext}
            Return If(dlg.ShowDialog(Me) = DialogResult.OK, dlg.FileName, Nothing)
        End Using
    End Function

    Private Sub OnExportImage(sender As Object, e As EventArgs)
        If CurrentCircuit.Elements.Count = 0 Then Return
        Dim p = AskPath("PNG image (*.png)|*.png", "png", _project.Pages(_pageIndex).Name)
        If p Is Nothing Then Return
        Using bmp = RenderCircuitImage(CurrentCircuit, _running)
            bmp.Save(p, Imaging.ImageFormat.Png)
        End Using
        _statusMessage.Text = $"Exported {p}"
    End Sub

    Private Sub OnExportSvg(sender As Object, e As EventArgs)
        If CurrentCircuit.Elements.Count = 0 Then Return
        Dim p = AskPath("SVG drawing (*.svg)|*.svg", "svg", _project.Pages(_pageIndex).Name)
        If p Is Nothing Then Return
        VectorExport.SaveSvg(CurrentCircuit, p)
        _statusMessage.Text = $"Exported {p}"
    End Sub

    Private Sub OnExportDxf(sender As Object, e As EventArgs)
        If CurrentCircuit.Elements.Count = 0 Then Return
        Dim p = AskPath("AutoCAD DXF (*.dxf)|*.dxf", "dxf", _project.Pages(_pageIndex).Name)
        If p Is Nothing Then Return
        VectorExport.SaveDxf(CurrentCircuit, p)
        _statusMessage.Text = $"Exported {p} (units: millimetres)"
    End Sub

    Private Sub OnExportPdf(sender As Object, e As EventArgs)
        Dim p = AskPath("PDF document (*.pdf)|*.pdf", "pdf", _project.Info.Title)
        If p Is Nothing Then Return
        Dim explanation = ""
        Try
            explanation = CircuitAnalysis.Explain(_project.SimulationCircuit(), Nothing, _realPhysics)
        Catch ex As Exception
            explanation = ""
        End Try
        Try
            Reports.SavePdfReport(_project, p, includeParts:=True, explanation:=explanation)
            _statusMessage.Text = $"Exported {p}"
            Try
                Process.Start(New ProcessStartInfo(p) With {.UseShellExecute = True})
            Catch ex As Exception
                ' Opening the PDF is a convenience only.
            End Try
        Catch ex As Exception
            MessageBox.Show("Could not write the PDF: " & ex.Message, AppName, MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    ''' <summary>Renders a whole circuit to a bitmap (also used for documentation screenshots).</summary>
    Public Shared Function RenderCircuitImage(c As Circuit, simulating As Boolean) As Bitmap
        Dim b = c.Bounds()
        b.Inflate(30, 30)
        ' Room for the column numbers above the drawing.
        b = RectangleF.FromLTRB(b.Left, b.Top - 30, b.Right, b.Bottom)
        Dim bmp As New Bitmap(Math.Max(1, CInt(b.Width)), Math.Max(1, CInt(b.Height)))
        Using g = Graphics.FromImage(bmp)
            g.Clear(Color.White)
            g.TextRenderingHint = Drawing.Text.TextRenderingHint.AntiAliasGridFit
            g.TranslateTransform(-b.Left, -b.Top)
            Using canvas As New CircuitCanvas() With {.Circuit = c, .Simulating = simulating, .ShowColumns = True}
                canvas.PaintTo(g)
            End Using
        End Using
        Return bmp
    End Function

    Private Sub OpenExample(build As Func(Of Circuit))
        If Not ConfirmDiscard() Then Return
        NewProject(New Project(build()))
        _statusMessage.Text = "Example loaded. Press Start (F9), then click the push buttons and switches."
    End Sub

    Private Sub LoadLessonCircuit(c As Circuit)
        If Not ConfirmDiscard() Then Return
        NewProject(New Project(c, "Lesson"))
        FitView()
        _statusMessage.Text = "Lesson parts loaded. Connect them, then press 'Check my solution'."
    End Sub

    Protected Overrides Sub OnFormClosing(e As FormClosingEventArgs)
        If Not ConfirmDiscard() Then e.Cancel = True
        If Not e.Cancel Then DeleteAutosave()
        MyBase.OnFormClosing(e)
    End Sub

    ' ----------------------------------------------------------------- autosave and recovery

    Private ReadOnly _autosaveTimer As New Timer() With {.Interval = 60000}

    Private Shared ReadOnly Property AutosavePath As String
        Get
            Return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "PneuSim", "autosave.pneu")
        End Get
    End Property

    Private Sub StartAutosave()
        AddHandler _autosaveTimer.Tick, Sub() Autosave()
        _autosaveTimer.Start()
    End Sub

    ''' <summary>Writes unsaved work to the recovery file (and the name of the original file next to it).</summary>
    Private Sub Autosave()
        If Not _dirty Then Return
        Try
            Directory.CreateDirectory(Path.GetDirectoryName(AutosavePath))
            _project.Save(AutosavePath)
            File.WriteAllText(AutosavePath & ".source", If(_filePath, ""))
        Catch ex As Exception
            ' Autosave is a safety net only.
        End Try
    End Sub

    Private Sub DeleteAutosave()
        Try
            If File.Exists(AutosavePath) Then File.Delete(AutosavePath)
            If File.Exists(AutosavePath & ".source") Then File.Delete(AutosavePath & ".source")
        Catch ex As Exception
            ' Ignore.
        End Try
    End Sub

    ''' <summary>After a crash or power cut: offers to restore the work saved in the recovery file.</summary>
    Public Sub OfferRecovery()
        If Not File.Exists(AutosavePath) Then Return
        Dim answer = MessageBox.Show("PneuSim was not closed normally last time. Restore the unsaved work from " &
                                     $"{File.GetLastWriteTime(AutosavePath):dd-MM-yyyy HH:mm}?", AppName,
                                     MessageBoxButtons.YesNo, MessageBoxIcon.Question)
        If answer = DialogResult.Yes Then
            Try
                Dim source = If(File.Exists(AutosavePath & ".source"), File.ReadAllText(AutosavePath & ".source").Trim(), "")
                NewProject(Project.Load(AutosavePath), If(source = "", Nothing, source))
                _dirty = True
                UpdateTitle()
                _statusMessage.Text = "Recovered unsaved work. Save it to keep it."
                Return
            Catch ex As Exception
                MessageBox.Show("The recovery file could not be read: " & ex.Message, AppName, MessageBoxButtons.OK, MessageBoxIcon.Warning)
            End Try
        End If
        DeleteAutosave()
    End Sub

    ''' <summary>Called by the global error handler: stop the simulation and save the work.</summary>
    Public Sub RecoverFromError()
        Try
            StopSimulation()
        Catch ex As Exception
            _running = False
            _timer.Stop()
        End Try
        _dirty = True
        Autosave()
    End Sub

    Private Sub OnPropertyValueChanged(s As Object, e As PropertyValueChangedEventArgs)
        CurrentCircuit.CleanupTubes()
        _properties.Refresh()
        _canvas.Invalidate()
        OnCircuitModified()
    End Sub
End Class
