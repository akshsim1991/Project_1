Imports System.ComponentModel
Imports System.Drawing.Imaging
Imports System.IO
Imports System.Runtime.InteropServices

Public Class Form1

    Private Shared ReadOnly ImageExtensions As String() = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff"}
    Private Shared ReadOnly GoodColor As Color = Color.FromArgb(0, 130, 60)
    Private Shared ReadOnly WarningColor As Color = Color.DarkOrange

    Private _settings As New AppSettings
    Private _project As New GraphProject
    Private _image As Bitmap
    Private _pixelBuffer As PixelBuffer
    Private _projectPath As String
    Private _dirty As Boolean
    Private ReadOnly _history As New UndoHistory
    Private _activeIndex As Integer
    ' Graph values of the active curve, shown in the data table.
    Private _activeData As New List(Of PointD)

    ' Calibration point being picked ("X1" …), or Nothing.
    Private _pickingReference As String
    Private _toolBeforePick As CanvasTool
    Private ReadOnly _valueBoxes As New Dictionary(Of TextBox, String)
    Private ReadOnly _pixelLabels As New Dictionary(Of String, Label)

    Private _fit As FitResult
    ' True while controls are being filled from the project, so their change events are ignored.
    Private _updating As Boolean = True

#Region "Start-up and shut-down"

    Private Sub Form1_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        _settings = AppSettings.Load()
        RestoreWindow()

        _valueBoxes.Add(txtValueX1, "X1") : _valueBoxes.Add(txtValueX2, "X2")
        _valueBoxes.Add(txtValueY1, "Y1") : _valueBoxes.Add(txtValueY2, "Y2")
        _pixelLabels.Add("X1", lblPixelX1) : _pixelLabels.Add("X2", lblPixelX2)
        _pixelLabels.Add("Y1", lblPixelY1) : _pixelLabels.Add("Y2", lblPixelY2)

        For Each kind In [Enum].GetValues(Of FitKind)()
            cmbFitKind.Items.Add(CurveFit.KindNames(kind))
        Next
        cmbFitKind.SelectedIndex = 0

        SetValue(nudDragSpacing, _settings.DragSpacing)
        SetValue(nudTolerance, _settings.ColorTolerance)
        SetValue(nudAutoStep, _settings.AutoTraceStep)
        SetValue(nudResampleCount, _settings.ResampleCount)
        canvas.DragSpacing = CDbl(nudDragSpacing.Value)
        SetViewOption(magnifier:=_settings.ShowMagnifier, grid:=_settings.ShowGrid, lines:=_settings.ShowLines)

        StartNewProject(Nothing, Nothing, Nothing)
        AllowDrop = True
        canvas.AllowDrop = True
        AddHandler canvas.DragEnter, AddressOf Files_DragEnter
        AddHandler canvas.DragDrop, AddressOf Files_DragDrop
        canvas.Tool = CanvasTool.AddPoint
        _updating = False
        RefreshAll()
    End Sub

    Private Sub Form1_Shown(sender As Object, e As EventArgs) Handles MyBase.Shown
        ' A file given on the command line (or dropped onto Graphing.exe) is opened straight away.
        Dim file = My.Application.CommandLineArgs.FirstOrDefault()
        If file IsNot Nothing Then OpenAnyFile(file)
    End Sub

    Private Sub Form1_FormClosing(sender As Object, e As FormClosingEventArgs) Handles MyBase.FormClosing
        If Not ConfirmDiscardChanges() Then
            e.Cancel = True
            Return
        End If
        _settings.WindowMaximized = (WindowState = FormWindowState.Maximized)
        _settings.WindowBounds = If(WindowState = FormWindowState.Normal, Bounds, RestoreBounds)
        _settings.DragSpacing = CInt(nudDragSpacing.Value)
        _settings.ColorTolerance = CInt(nudTolerance.Value)
        _settings.AutoTraceStep = CInt(nudAutoStep.Value)
        _settings.ResampleCount = CInt(nudResampleCount.Value)
        _settings.Save()
    End Sub

    Private Sub Form1_FormClosed(sender As Object, e As FormClosedEventArgs) Handles MyBase.FormClosed
        canvas.Image = Nothing
        _image?.Dispose()
    End Sub

    Private Sub RestoreWindow()
        Dim saved = _settings.WindowBounds
        If saved.Width >= MinimumSize.Width AndAlso saved.Height >= MinimumSize.Height AndAlso
           Screen.AllScreens.Any(Function(sc) sc.WorkingArea.IntersectsWith(saved)) Then
            StartPosition = FormStartPosition.Manual
            Bounds = saved
        End If
        If _settings.WindowMaximized Then WindowState = FormWindowState.Maximized
    End Sub

#End Region

#Region "Images and projects"

    Private Sub OpenImage_Click(sender As Object, e As EventArgs) Handles mnuOpenImage.Click, tsOpenImage.Click
        If Directory.Exists(_settings.LastImageFolder) Then dlgOpenImage.InitialDirectory = _settings.LastImageFolder
        dlgOpenImage.FileName = ""
        If dlgOpenImage.ShowDialog(Me) <> DialogResult.OK Then Return
        _settings.LastImageFolder = Path.GetDirectoryName(dlgOpenImage.FileName)
        OpenImageFile(dlgOpenImage.FileName)
    End Sub

    Private Sub PasteImage_Click(sender As Object, e As EventArgs) Handles mnuPasteImage.Click, tsPaste.Click
        ' Ctrl+V inside a text box pastes text as usual.
        If sender Is mnuPasteImage AndAlso TypeOf ActiveControl Is TextBoxBase Then
            DirectCast(ActiveControl, TextBoxBase).Paste()
            Return
        End If
        Try
            If Clipboard.ContainsImage() Then
                Using pasted = Clipboard.GetImage(), stream As New MemoryStream()
                    pasted.Save(stream, ImageFormat.Png)
                    StartProjectFromImageBytes(stream.ToArray(), "Pasted image.png")
                End Using
            ElseIf Clipboard.ContainsFileDropList() AndAlso Clipboard.GetFileDropList().Count > 0 Then
                OpenAnyFile(Clipboard.GetFileDropList()(0))
            Else
                MessageBox.Show("The clipboard has no picture. Copy a graph (for example with Win+Shift+S) and try again.",
                                "Paste image", MessageBoxButtons.OK, MessageBoxIcon.Information)
            End If
        Catch ex As ExternalException
            MessageBox.Show("The clipboard is busy. Please try again.", "Paste image", MessageBoxButtons.OK, MessageBoxIcon.Information)
        End Try
    End Sub

    Private Sub OpenProject_Click(sender As Object, e As EventArgs) Handles mnuOpenProject.Click, tsOpenProject.Click
        If Directory.Exists(_settings.LastProjectFolder) Then dlgOpenProject.InitialDirectory = _settings.LastProjectFolder
        dlgOpenProject.FileName = ""
        If dlgOpenProject.ShowDialog(Me) <> DialogResult.OK Then Return
        OpenProjectFile(dlgOpenProject.FileName)
    End Sub

    Private Sub SaveProject_Click(sender As Object, e As EventArgs) Handles mnuSaveProject.Click, tsSaveProject.Click
        SaveProject(saveAs:=False)
    End Sub

    Private Sub SaveProjectAs_Click(sender As Object, e As EventArgs) Handles mnuSaveProjectAs.Click
        SaveProject(saveAs:=True)
    End Sub

    Private Sub Exit_Click(sender As Object, e As EventArgs) Handles mnuExit.Click
        Close()
    End Sub

    ''' <summary>Opens an image or a project, depending on the file type.</summary>
    Private Sub OpenAnyFile(filePath As String)
        If Not File.Exists(filePath) Then
            MessageBox.Show("File not found:" & vbCrLf & filePath, "Graphing", MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If
        If Path.GetExtension(filePath).Equals(GraphProject.FileExtension, StringComparison.OrdinalIgnoreCase) Then
            OpenProjectFile(filePath)
        Else
            OpenImageFile(filePath)
        End If
    End Sub

    Private Sub OpenImageFile(filePath As String)
        Dim bytes As Byte()
        Try
            bytes = File.ReadAllBytes(filePath)
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
            MessageBox.Show("The image could not be read:" & vbCrLf & ex.Message, "Open image", MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End Try
        StartProjectFromImageBytes(bytes, Path.GetFileName(filePath))
    End Sub

    ''' <summary>Starts a new project on an image (asking to save the current one first).</summary>
    Private Sub StartProjectFromImageBytes(bytes As Byte(), displayName As String)
        Dim bitmap = TryDecodeImage(bytes)
        If bitmap Is Nothing Then
            MessageBox.Show(displayName & " is not an image Graphing can open." & vbCrLf & vbCrLf & "Supported: PNG, JPG, BMP, GIF and TIFF.",
                            "Open image", MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If
        If Not ConfirmDiscardChanges() Then
            bitmap.Dispose()
            Return
        End If
        StartNewProject(bitmap, bytes, displayName)
        tabsMain.SelectedTab = tabCalibrate
        SetStatus(String.Format("Image loaded ({0} × {1} px). Step 1: calibrate — click ""Pick X1"" on the Calibrate tab.", bitmap.Width, bitmap.Height))
    End Sub

    Private Sub StartNewProject(bitmap As Bitmap, bytes As Byte(), displayName As String)
        _project = New GraphProject With {.ImageData = bytes, .ImageFileName = displayName}
        _project.Series.Add(_project.CreateSeries())
        _projectPath = Nothing
        _dirty = False
        _history.Clear()
        _activeIndex = 0
        _fit = Nothing
        txtFitResult.Text = ""
        CancelPick()
        SetImage(bitmap)
        canvas.Project = _project
        RefreshAll()
    End Sub

    Private Sub OpenProjectFile(filePath As String)
        If Not ConfirmDiscardChanges() Then Return
        Dim loaded As GraphProject
        Try
            loaded = GraphProject.Load(filePath)
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
            MessageBox.Show("The project could not be opened:" & vbCrLf & ex.Message, "Open project", MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End Try
        Dim bitmap = If(loaded.ImageData Is Nothing, Nothing, TryDecodeImage(loaded.ImageData))
        If bitmap Is Nothing Then
            MessageBox.Show("The project's image is missing or damaged.", "Open project", MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If
        If loaded.Series.Count = 0 Then loaded.Series.Add(loaded.CreateSeries())
        _settings.LastProjectFolder = Path.GetDirectoryName(filePath)
        _project = loaded
        _projectPath = filePath
        _dirty = False
        _history.Clear()
        _activeIndex = 0
        _fit = Nothing
        txtFitResult.Text = ""
        CancelPick()
        SetImage(bitmap)
        canvas.Project = _project
        RefreshAll()
        SetStatus("Opened " & Path.GetFileName(filePath) & ".")
    End Sub

    Private Function SaveProject(saveAs As Boolean) As Boolean
        If _image Is Nothing Then
            MessageBox.Show("Open an image first.", "Save project", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return False
        End If
        Dim target = _projectPath
        If saveAs OrElse target Is Nothing Then
            If Directory.Exists(_settings.LastProjectFolder) Then dlgSaveProject.InitialDirectory = _settings.LastProjectFolder
            dlgSaveProject.FileName = If(target IsNot Nothing, Path.GetFileName(target),
                                         Path.GetFileNameWithoutExtension(If(_project.ImageFileName, "Graph")) & GraphProject.FileExtension)
            If dlgSaveProject.ShowDialog(Me) <> DialogResult.OK Then Return False
            target = dlgSaveProject.FileName
        End If
        Try
            _project.Save(target)
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
            MessageBox.Show("The project could not be saved:" & vbCrLf & ex.Message, "Save project", MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return False
        End Try
        _projectPath = target
        _settings.LastProjectFolder = Path.GetDirectoryName(target)
        _dirty = False
        RefreshAll()
        SetStatus("Saved " & Path.GetFileName(target) & ".")
        Return True
    End Function

    ''' <summary>Asks whether to save unsaved work. Returns False if the user cancelled.</summary>
    Private Function ConfirmDiscardChanges() As Boolean
        If Not _dirty Then Return True
        Select Case MessageBox.Show("Save the changes to the current project first?", "Graphing",
                                    MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question)
            Case DialogResult.Yes : Return SaveProject(saveAs:=False)
            Case DialogResult.No : Return True
            Case Else : Return False
        End Select
    End Function

    Private Shared Function TryDecodeImage(bytes As Byte()) As Bitmap
        Try
            Using stream As New MemoryStream(bytes), decoded = Image.FromStream(stream)
                ' Copy into a plain 32-bit bitmap at its pixel size (ignoring the file's DPI), independent of the stream.
                Dim copy As New Bitmap(decoded.Width, decoded.Height, PixelFormat.Format32bppArgb)
                Using g = Graphics.FromImage(copy)
                    g.Clear(Color.White)
                    g.DrawImage(decoded, New Rectangle(0, 0, decoded.Width, decoded.Height))
                End Using
                Return copy
            End Using
        Catch ex As Exception When TypeOf ex Is ArgumentException OrElse TypeOf ex Is OutOfMemoryException OrElse TypeOf ex Is ExternalException
            Return Nothing
        End Try
    End Function

    Private Sub SetImage(bitmap As Bitmap)
        Dim old = _image
        _image = bitmap
        _pixelBuffer = Nothing
        canvas.Image = bitmap
        old?.Dispose()
    End Sub

    ''' <summary>The image's pixels for automatic tracing (created once per image).</summary>
    Private Function GetPixelBuffer() As PixelBuffer
        If _pixelBuffer Is Nothing AndAlso _image IsNot Nothing Then
            Dim rect = New Rectangle(0, 0, _image.Width, _image.Height)
            Dim data = _image.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb)
            Try
                Dim pixels(_image.Width * _image.Height - 1) As Integer
                If data.Stride = _image.Width * 4 Then
                    Marshal.Copy(data.Scan0, pixels, 0, pixels.Length)
                Else
                    For y = 0 To _image.Height - 1
                        Marshal.Copy(data.Scan0 + y * data.Stride, pixels, y * _image.Width, _image.Width)
                    Next
                End If
                _pixelBuffer = New PixelBuffer(_image.Width, _image.Height, pixels)
            Finally
                _image.UnlockBits(data)
            End Try
        End If
        Return _pixelBuffer
    End Function

    Private Sub Files_DragEnter(sender As Object, e As DragEventArgs) Handles MyBase.DragEnter
        e.Effect = If(e.Data.GetDataPresent(DataFormats.FileDrop), DragDropEffects.Copy, DragDropEffects.None)
    End Sub

    Private Sub Files_DragDrop(sender As Object, e As DragEventArgs) Handles MyBase.DragDrop
        Dim files = TryCast(e.Data.GetData(DataFormats.FileDrop), String())
        If files Is Nothing OrElse files.Length = 0 Then Return
        ' Open after the drop has finished, so Explorer is not left waiting while a message box is open.
        BeginInvoke(Sub()
                        Activate()
                        OpenAnyFile(files(0))
                    End Sub)
    End Sub

#End Region

#Region "Undo, redo and refreshing"

    ''' <summary>Call just before changing the project.</summary>
    Private Sub RecordUndo()
        _history.Record(_project.CaptureState())
    End Sub

    ''' <summary>Call after changing the project.</summary>
    Private Sub ProjectChanged()
        _dirty = True
        _fit = Nothing
        txtFitResult.Text = ""
        RefreshAll()
    End Sub

    Private Sub Undo_Click(sender As Object, e As EventArgs) Handles mnuUndo.Click, tsUndo.Click
        If sender Is mnuUndo AndAlso TypeOf ActiveControl Is TextBoxBase Then
            DirectCast(ActiveControl, TextBoxBase).Undo()
            Return
        End If
        Dim previous = _history.Undo(_project.CaptureState())
        If previous Is Nothing Then Return
        _project.RestoreState(previous)
        ProjectChanged()
        SetStatus("Undone.")
    End Sub

    Private Sub Redo_Click(sender As Object, e As EventArgs) Handles mnuRedo.Click, tsRedo.Click
        Dim nextState = _history.Redo(_project.CaptureState())
        If nextState Is Nothing Then Return
        _project.RestoreState(nextState)
        ProjectChanged()
        SetStatus("Redone.")
    End Sub

    Private ReadOnly Property ActiveSeries As DataSeries
        Get
            If _project.Series.Count = 0 Then Return Nothing
            _activeIndex = Math.Max(0, Math.Min(_activeIndex, _project.Series.Count - 1))
            Return _project.Series(_activeIndex)
        End Get
    End Property

    Private ReadOnly Property Calibration As Calibration
        Get
            Return _project.Calibration
        End Get
    End Property

    ''' <summary>Updates every part of the window from the project.</summary>
    Private Sub RefreshAll()
        Dim wasUpdating = _updating
        _updating = True
        Try
            Dim hasImage = _image IsNot Nothing
            Dim active = ActiveSeries
            Dim calibrated = Calibration.IsComplete

            Dim name = If(_projectPath IsNot Nothing, Path.GetFileName(_projectPath), If(_project.ImageFileName, "Untitled"))
            Text = If(hasImage, name & If(_dirty, " *", "") & " — Graphing", "Graphing")

            ' Calibration tab
            For Each item In Calibration.References()
                Dim p = item.Reference.Pixel
                _pixelLabels(item.Name).Text = If(p.HasValue, String.Format("({0:0.#}, {1:0.#})", p.Value.X, p.Value.Y), "not set")
                _pixelLabels(item.Name).ForeColor = If(p.HasValue, SystemColors.ControlText, Color.DimGray)
            Next
            For Each pair In _valueBoxes
                If Not pair.Key.Focused Then
                    Dim v = Calibration.References().First(Function(r) r.Name = pair.Value).Reference.Value
                    pair.Key.Text = If(v.HasValue, v.Value.ToString("R", Globalization.CultureInfo.CurrentCulture), "")
                    errValues.SetError(pair.Key, "")
                End If
            Next
            chkLogX.Checked = Calibration.LogX
            chkLogY.Checked = Calibration.LogY
            Dim problem = Calibration.Validate()
            lblCalState.Text = If(problem Is Nothing, "✓ Calibrated. Go to the Trace tab to trace your curves.", problem)
            lblCalState.ForeColor = If(problem Is Nothing, GoodColor, WarningColor)
            For Each b In {btnPickX1, btnPickX2, btnPickY1, btnPickY2}
                b.Enabled = hasImage
            Next

            ' Trace tab
            lstSeries.BeginUpdate()
            lstSeries.Items.Clear()
            For Each s In _project.Series
                lstSeries.Items.Add(s.Name)
            Next
            lstSeries.SelectedIndex = If(active Is Nothing, -1, _activeIndex)
            lstSeries.EndUpdate()
            btnDeleteSeries.Enabled = _project.Series.Count > 1
            canvas.ActiveSeries = active
            If active Is Nothing OrElse canvas.SelectedIndex >= active.Points.Count Then canvas.SelectedIndex = -1

            ' Data tab
            _activeData = If(active Is Nothing, New List(Of PointD), active.DataPoints(Calibration))
            Dim seriesLabel = If(active Is Nothing, "No curve", String.Format("{0} — {1} point{2}", active.Name, active.Points.Count, If(active.Points.Count = 1, "", "s")))
            lblDataSeries.Text = seriesLabel & If(calibrated OrElse active Is Nothing OrElse active.Points.Count = 0, "", "  (calibrate to see values)")
            lblAnalyzeSeries.Text = seriesLabel
            dgvData.RowCount = _activeData.Count
            dgvData.Invalidate()
            SyncGridSelection()

            ' Analyze tab
            UpdateStatistics()
            canvas.FitCurve = If(chkShowFit.Checked AndAlso _fit IsNot Nothing, BuildFitCurve(_fit), Nothing)

            ' Commands
            Dim anyPoints = _project.Series.Any(Function(s) s.Points.Count > 0)
            mnuUndo.Enabled = _history.CanUndo : tsUndo.Enabled = _history.CanUndo
            mnuRedo.Enabled = _history.CanRedo : tsRedo.Enabled = _history.CanRedo
            mnuSaveProject.Enabled = hasImage : tsSaveProject.Enabled = hasImage : mnuSaveProjectAs.Enabled = hasImage
            mnuExportCsv.Enabled = anyPoints : mnuExportExcel.Enabled = anyPoints
            btnExportCsv.Enabled = anyPoints : btnExportExcel.Enabled = anyPoints
            Dim hasActivePoints = active IsNot Nothing AndAlso active.Points.Count > 0
            For Each c As Control In {btnCopyData, btnSortX, btnRemoveDuplicates, btnResampleCount, btnResampleStep, btnClearSeries, btnInterpolate, btnFit}
                c.Enabled = hasActivePoints
            Next
            mnuCopyData.Enabled = hasActivePoints
            canvas.Invalidate()
        Finally
            _updating = wasUpdating
        End Try
    End Sub

    Private Sub SetStatus(text As String)
        lblStatus.Text = text
    End Sub

#End Region

#Region "Calibration"

    Private Sub PickReference_Click(sender As Object, e As EventArgs) Handles btnPickX1.Click, btnPickX2.Click, btnPickY1.Click, btnPickY2.Click
        If _image Is Nothing Then Return
        Dim name = DirectCast(sender, Button).Text.Replace("Pick ", "")
        If _pickingReference Is Nothing Then _toolBeforePick = canvas.Tool
        _pickingReference = name
        canvas.PickingReferenceName = name
        canvas.Tool = CanvasTool.PickReference
        canvas.Focus()
        SetStatus(String.Format("Click on the image exactly where {0} is (zoom in and use the magnifier for precision). Esc cancels.", name))
    End Sub

    Private Sub CancelPick()
        If _pickingReference Is Nothing Then Return
        _pickingReference = Nothing
        canvas.PickingReferenceName = Nothing
        canvas.Tool = _toolBeforePick
    End Sub

    Private Sub CompletePick(imagePoint As PointD)
        Dim name = _pickingReference
        CancelPick()
        Dim reference = Calibration.References().First(Function(r) r.Name = name).Reference
        RecordUndo()
        reference.Pixel = imagePoint
        ProjectChanged()
        ' Next, type the value of the point just picked.
        Dim box = _valueBoxes.First(Function(pair) pair.Value = name).Key
        tabsMain.SelectedTab = tabCalibrate
        box.Focus()
        box.SelectAll()
        SetStatus(If(reference.Value.HasValue, name & " moved.", String.Format("Now type the value of {0} and press Enter.", name)))
    End Sub

    Private Sub ValueBox_KeyDown(sender As Object, e As KeyEventArgs) Handles txtValueX1.KeyDown, txtValueX2.KeyDown, txtValueY1.KeyDown, txtValueY2.KeyDown
        If e.KeyCode = Keys.Enter Then
            CommitValue(DirectCast(sender, TextBox))
            e.SuppressKeyPress = True
        End If
    End Sub

    Private Sub ValueBox_Validating(sender As Object, e As CancelEventArgs) Handles txtValueX1.Validating, txtValueX2.Validating, txtValueY1.Validating, txtValueY2.Validating
        CommitValue(DirectCast(sender, TextBox))
    End Sub

    Private Sub CommitValue(box As TextBox)
        Dim reference = Calibration.References().First(Function(r) r.Name = _valueBoxes(box)).Reference
        Dim newValue As Double? = Nothing
        If box.Text.Trim().Length > 0 Then
            Dim parsed As Double
            If Not DataTools.TryParseNumber(box.Text, parsed) Then
                errValues.SetError(box, "Type a number, for example 25, -3.5 or 1e-3.")
                Return
            End If
            newValue = parsed
        End If
        errValues.SetError(box, "")
        If Nullable.Equals(newValue, reference.Value) Then Return
        Dim wasComplete = Calibration.IsComplete
        RecordUndo()
        reference.Value = newValue
        ProjectChanged()
        If Not wasComplete AndAlso Calibration.IsComplete Then
            SetStatus("Calibrated. Step 2: trace your curves on the Trace tab. Tip: View ▸ Calibrated Grid checks the calibration.")
        End If
    End Sub

    Private Sub LogAxis_CheckedChanged(sender As Object, e As EventArgs) Handles chkLogX.CheckedChanged, chkLogY.CheckedChanged
        If _updating Then Return
        RecordUndo()
        Calibration.LogX = chkLogX.Checked
        Calibration.LogY = chkLogY.Checked
        ProjectChanged()
    End Sub

    Private Sub btnClearCalibration_Click(sender As Object, e As EventArgs) Handles btnClearCalibration.Click
        RecordUndo()
        _project.Calibration = New Calibration()
        ProjectChanged()
        SetStatus("Calibration cleared. Ctrl+Z brings it back.")
    End Sub

#End Region

#Region "Curves and tracing"

    Private Sub lstSeries_DrawItem(sender As Object, e As DrawItemEventArgs) Handles lstSeries.DrawItem
        e.DrawBackground()
        If e.Index < 0 OrElse e.Index >= _project.Series.Count Then Return
        Dim s = _project.Series(e.Index)
        Dim swatch = New Rectangle(e.Bounds.X + 4, e.Bounds.Y + 4, 14, e.Bounds.Height - 8)
        Using brush As New SolidBrush(Color.FromArgb(s.ColorArgb))
            e.Graphics.FillRectangle(brush, swatch)
        End Using
        e.Graphics.DrawRectangle(Pens.Gray, swatch)
        Dim text = String.Format("{0}  ({1})", s.Name, s.Points.Count)
        TextRenderer.DrawText(e.Graphics, text, e.Font, New Rectangle(swatch.Right + 6, e.Bounds.Y, e.Bounds.Width - swatch.Right - 6, e.Bounds.Height),
                              e.ForeColor, TextFormatFlags.VerticalCenter Or TextFormatFlags.EndEllipsis)
        e.DrawFocusRectangle()
    End Sub

    Private Sub lstSeries_SelectedIndexChanged(sender As Object, e As EventArgs) Handles lstSeries.SelectedIndexChanged
        If _updating OrElse lstSeries.SelectedIndex < 0 Then Return
        _activeIndex = lstSeries.SelectedIndex
        canvas.SelectedIndex = -1
        _fit = Nothing
        txtFitResult.Text = ""
        RefreshAll()
    End Sub

    Private Sub btnAddSeries_Click(sender As Object, e As EventArgs) Handles btnAddSeries.Click
        RecordUndo()
        _project.Series.Add(_project.CreateSeries())
        _activeIndex = _project.Series.Count - 1
        ProjectChanged()
        SetStatus("Added " & ActiveSeries.Name & ". It is now the curve you are tracing.")
    End Sub

    Private Sub btnRenameSeries_Click(sender As Object, e As EventArgs) Handles btnRenameSeries.Click
        Dim s = ActiveSeries
        If s Is Nothing Then Return
        Dim newName = InputBox("New name for the curve:", "Rename curve", s.Name).Trim()
        If newName.Length = 0 OrElse newName = s.Name Then Return
        RecordUndo()
        s.Name = newName
        ProjectChanged()
    End Sub

    Private Sub btnColorSeries_Click(sender As Object, e As EventArgs) Handles btnColorSeries.Click
        Dim s = ActiveSeries
        If s Is Nothing Then Return
        dlgColor.Color = Color.FromArgb(s.ColorArgb)
        If dlgColor.ShowDialog(Me) <> DialogResult.OK Then Return
        RecordUndo()
        s.ColorArgb = dlgColor.Color.ToArgb()
        ProjectChanged()
    End Sub

    Private Sub btnDeleteSeries_Click(sender As Object, e As EventArgs) Handles btnDeleteSeries.Click
        Dim s = ActiveSeries
        If s Is Nothing OrElse _project.Series.Count <= 1 Then Return
        If s.Points.Count > 0 AndAlso MessageBox.Show(String.Format("Delete ""{0}"" and its {1} points?", s.Name, s.Points.Count), "Delete curve",
                                                      MessageBoxButtons.YesNo, MessageBoxIcon.Question) <> DialogResult.Yes Then Return
        RecordUndo()
        _project.Series.RemoveAt(_activeIndex)
        ProjectChanged()
    End Sub

    Private Sub btnClearSeries_Click(sender As Object, e As EventArgs) Handles btnClearSeries.Click
        Dim s = ActiveSeries
        If s Is Nothing OrElse s.Points.Count = 0 Then Return
        RecordUndo()
        s.Points.Clear()
        ProjectChanged()
        SetStatus("Points cleared. Ctrl+Z brings them back.")
    End Sub

    Private Sub Tool_CheckedChanged(sender As Object, e As EventArgs) Handles rbPan.CheckedChanged, rbAddPoints.CheckedChanged, rbDragTrace.CheckedChanged,
                                                                              rbEditPoints.CheckedChanged, rbAutoTrace.CheckedChanged
        If Not DirectCast(sender, RadioButton).Checked Then Return
        _pickingReference = Nothing
        canvas.PickingReferenceName = Nothing
        canvas.Tool = SelectedTool()
        Select Case canvas.Tool
            Case CanvasTool.AddPoint : SetStatus("Click on the curve to add points.")
            Case CanvasTool.DragTrace : SetStatus("Hold the left mouse button and move along the curve.")
            Case CanvasTool.EditPoints : SetStatus("Drag a point to move it. Del or right-click deletes it; arrow keys nudge it.")
            Case CanvasTool.AutoTrace : SetStatus("Click on a curve; Graphing follows its color to the left and right.")
            Case CanvasTool.Pan : SetStatus("Drag to move the image; use the mouse wheel to zoom.")
        End Select
    End Sub

    Private Function SelectedTool() As CanvasTool
        If rbPan.Checked Then Return CanvasTool.Pan
        If rbDragTrace.Checked Then Return CanvasTool.DragTrace
        If rbEditPoints.Checked Then Return CanvasTool.EditPoints
        If rbAutoTrace.Checked Then Return CanvasTool.AutoTrace
        Return CanvasTool.AddPoint
    End Function

    Private Sub nudDragSpacing_ValueChanged(sender As Object, e As EventArgs) Handles nudDragSpacing.ValueChanged
        canvas.DragSpacing = CDbl(nudDragSpacing.Value)
    End Sub

    Private Sub canvas_ImageClicked(sender As Object, e As ImageClickEventArgs) Handles canvas.ImageClicked
        If e.Tool = CanvasTool.PickReference Then
            If _pickingReference IsNot Nothing Then CompletePick(e.ImagePoint)
            Return
        End If
        Dim s = ActiveSeries
        If s Is Nothing Then Return
        If e.ImagePoint.X < 0 OrElse e.ImagePoint.Y < 0 OrElse e.ImagePoint.X >= _image.Width OrElse e.ImagePoint.Y >= _image.Height Then Return
        Select Case e.Tool
            Case CanvasTool.AddPoint
                RecordUndo()
                s.Points.Add(e.ImagePoint)
                ProjectChanged()
                ShowPointStatus(e.ImagePoint)
            Case CanvasTool.AutoTrace
                RunAutoTrace(e.ImagePoint)
        End Select
    End Sub

    Private Sub ShowPointStatus(p As PointD)
        If Calibration.IsComplete Then
            Dim d = Calibration.PixelToData(p)
            SetStatus(String.Format("Added point X = {0}, Y = {1}.", DataTools.FormatNumber(d.X), DataTools.FormatNumber(d.Y)))
        Else
            SetStatus("Point added. Calibrate the axes to see its values.")
        End If
    End Sub

    Private Sub RunAutoTrace(clicked As PointD)
        Dim image = GetPixelBuffer()
        Dim x = CInt(Math.Floor(clicked.X)), y = CInt(Math.Floor(clicked.Y))
        Dim curveColor = AutoTracer.PickCurveColor(image, x, y)
        If Not curveColor.HasValue Then
            MessageBox.Show("There is no curve right where you clicked. Zoom in and click directly on the line.",
                            "Automatic trace", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        Dim options As New AutoTraceOptions With {.ColorTolerance = CInt(nudTolerance.Value), .StepPixels = CInt(nudAutoStep.Value)}
        Dim found As List(Of PointD)
        Cursor = Cursors.WaitCursor
        Try
            found = AutoTracer.Trace(image, x, y, curveColor.Value, options)
        Finally
            Cursor = Cursors.Default
        End Try
        pnlAutoColor.BackColor = Color.FromArgb(curveColor.Value)
        If found.Count < 2 Then
            MessageBox.Show("Graphing could not follow a curve from there. Click right on the line, or raise the color tolerance.",
                            "Automatic trace", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        ' The tracer works in whole pixels; use pixel centres.
        found = found.Select(Function(p) New PointD(p.X + 0.5, p.Y + 0.5)).ToList()

        Dim s = ActiveSeries
        Dim replace = True
        If s.Points.Count > 0 Then
            Select Case MessageBox.Show(String.Format("{0} points were found." & vbCrLf & vbCrLf &
                                                      "Yes: replace the {1} points already in ""{2}""." & vbCrLf &
                                                      "No: add them to the existing points.", found.Count, s.Points.Count, s.Name),
                                        "Automatic trace", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question)
                Case DialogResult.Yes : replace = True
                Case DialogResult.No : replace = False
                Case Else : Return
            End Select
        End If
        RecordUndo()
        If replace Then s.Points.Clear()
        s.Points.AddRange(found)
        ProjectChanged()
        SetStatus(String.Format("Automatic trace found {0} points. Check them, and use Edit points to fix any strays.", found.Count))
    End Sub

    Private Sub canvas_PointsChanging(sender As Object, e As EventArgs) Handles canvas.PointsChanging
        RecordUndo()
    End Sub

    Private Sub canvas_PointsChanged(sender As Object, e As EventArgs) Handles canvas.PointsChanged
        ProjectChanged()
    End Sub

    Private Sub canvas_HoverChanged(sender As Object, e As EventArgs) Handles canvas.HoverChanged
        Dim p = canvas.HoverImagePoint
        If Not p.HasValue Then
            lblCoords.Text = ""
            Return
        End If
        Dim text = String.Format("pixel ({0:0.0}, {1:0.0})", p.Value.X, p.Value.Y)
        If Calibration.IsComplete Then
            Dim d = Calibration.PixelToData(p.Value)
            text &= String.Format("   X = {0}   Y = {1}", DataTools.FormatNumber(d.X), DataTools.FormatNumber(d.Y))
        End If
        lblCoords.Text = text
    End Sub

    Private Sub canvas_ViewChanged(sender As Object, e As EventArgs) Handles canvas.ViewChanged
        lblZoom.Text = String.Format("{0:0}%", canvas.Zoom * 100)
    End Sub

    Private Sub canvas_SelectedIndexChanged(sender As Object, e As EventArgs) Handles canvas.SelectedIndexChanged
        SyncGridSelection()
    End Sub

    Private Sub DeletePoint_Click(sender As Object, e As EventArgs) Handles mnuDeletePoint.Click
        canvas.DeleteSelectedPoint()
    End Sub

#End Region

#Region "Data table, clean-up and export"

    Private Sub dgvData_CellValueNeeded(sender As Object, e As DataGridViewCellValueEventArgs) Handles dgvData.CellValueNeeded
        If e.RowIndex < 0 OrElse e.RowIndex >= _activeData.Count Then Return
        Dim p = _activeData(e.RowIndex)
        Select Case e.ColumnIndex
            Case 0 : e.Value = e.RowIndex + 1
            Case 1 : e.Value = DataTools.FormatNumber(p.X)
            Case 2 : e.Value = DataTools.FormatNumber(p.Y)
        End Select
    End Sub

    Private Sub dgvData_SelectionChanged(sender As Object, e As EventArgs) Handles dgvData.SelectionChanged
        If _updating OrElse dgvData.CurrentRow Is Nothing Then Return
        canvas.SelectedIndex = dgvData.CurrentRow.Index
    End Sub

    Private Sub SyncGridSelection()
        Dim wasUpdating = _updating
        _updating = True
        Try
            Dim index = canvas.SelectedIndex
            If index >= 0 AndAlso index < dgvData.RowCount Then
                dgvData.CurrentCell = dgvData.Rows(index).Cells(0)
            Else
                dgvData.ClearSelection()
            End If
        Finally
            _updating = wasUpdating
        End Try
    End Sub

    ''' <summary>Shows a message and returns False if the calibration is not usable yet.</summary>
    Private Function RequireCalibration(action As String) As Boolean
        Dim problem = Calibration.Validate()
        If problem Is Nothing Then Return True
        MessageBox.Show(String.Format("To {0}, calibrate the axes first (Calibrate tab)." & vbCrLf & vbCrLf & "{1}", action, problem),
                        "Graphing", MessageBoxButtons.OK, MessageBoxIcon.Information)
        Return False
    End Function

    ''' <summary>Runs a change on a copy of the active curve, then applies it with undo; shows a message if it fails.</summary>
    Private Sub ChangeActiveSeries(action As String, change As Action(Of DataSeries), Optional successMessage As Func(Of DataSeries, String) = Nothing)
        Dim s = ActiveSeries
        If s Is Nothing Then Return
        Dim copy = s.Clone()
        Try
            change(copy)
        Catch ex As Exception When TypeOf ex Is InvalidOperationException OrElse TypeOf ex Is ArgumentException
            MessageBox.Show(ex.Message, action, MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End Try
        RecordUndo()
        s.Points = copy.Points
        canvas.SelectedIndex = -1
        ProjectChanged()
        If successMessage IsNot Nothing Then SetStatus(successMessage(s))
    End Sub

    Private Sub btnSortX_Click(sender As Object, e As EventArgs) Handles btnSortX.Click
        If Not RequireCalibration("sort by X") Then Return
        ChangeActiveSeries("Sort by X", Sub(s) DataTools.SortByX(s, Calibration), Function(s) "Sorted by X.")
    End Sub

    Private Sub btnRemoveDuplicates_Click(sender As Object, e As EventArgs) Handles btnRemoveDuplicates.Click
        Dim s = ActiveSeries
        If s Is Nothing Then Return
        Dim probe = s.Clone()
        Dim removed = DataTools.RemoveDuplicates(probe)
        If removed = 0 Then
            SetStatus("No duplicate points found.")
            Return
        End If
        ChangeActiveSeries("Remove duplicates", Sub(c) DataTools.RemoveDuplicates(c), Function(c) String.Format("Removed {0} duplicate point{1}.", removed, If(removed = 1, "", "s")))
    End Sub

    Private Sub btnResampleCount_Click(sender As Object, e As EventArgs) Handles btnResampleCount.Click
        If Not RequireCalibration("resample") Then Return
        Dim count = CInt(nudResampleCount.Value)
        ChangeActiveSeries("Resample", Sub(s) DataTools.ResampleByCount(s, Calibration, count), Function(s) String.Format("Resampled to {0} points.", s.Points.Count))
    End Sub

    Private Sub btnResampleStep_Click(sender As Object, e As EventArgs) Handles btnResampleStep.Click
        If Not RequireCalibration("resample") Then Return
        Dim stepX As Double
        If Not DataTools.TryParseNumber(txtResampleStep.Text, stepX) Then
            MessageBox.Show("Type the X step, for example 0.5.", "Resample", MessageBoxButtons.OK, MessageBoxIcon.Information)
            txtResampleStep.Focus()
            Return
        End If
        ChangeActiveSeries("Resample", Sub(s) DataTools.ResampleByStep(s, Calibration, stepX), Function(s) String.Format("Resampled to {0} points.", s.Points.Count))
    End Sub

    Private Sub CopyData_Click(sender As Object, e As EventArgs) Handles btnCopyData.Click, mnuCopyData.Click
        Dim s = ActiveSeries
        If s Is Nothing OrElse s.Points.Count = 0 OrElse Not RequireCalibration("copy the values") Then Return
        Try
            Clipboard.SetText(Exporters.ToTabSeparated(s, Calibration))
            SetStatus(String.Format("Copied {0} points of {1}. Paste them into Excel, Origin or any spreadsheet.", s.Points.Count, s.Name))
        Catch ex As ExternalException
            MessageBox.Show("The clipboard is busy. Please try again.", "Copy", MessageBoxButtons.OK, MessageBoxIcon.Information)
        End Try
    End Sub

    Private Sub ExportCsv_Click(sender As Object, e As EventArgs) Handles btnExportCsv.Click, mnuExportCsv.Click
        If Not RequireCalibration("export") Then Return
        Dim target = AskExportPath("CSV file (*.csv)|*.csv", "csv")
        If target Is Nothing Then Return
        Try
            File.WriteAllText(target, Exporters.ToCsv(_project.Series, Calibration), New Text.UTF8Encoding(True))
            SetStatus("Saved " & Path.GetFileName(target) & ".")
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
            MessageBox.Show("The file could not be saved:" & vbCrLf & ex.Message, "Export", MessageBoxButtons.OK, MessageBoxIcon.Warning)
        End Try
    End Sub

    Private Sub ExportExcel_Click(sender As Object, e As EventArgs) Handles btnExportExcel.Click, mnuExportExcel.Click
        If Not RequireCalibration("export") Then Return
        Dim target = AskExportPath("Excel workbook (*.xlsx)|*.xlsx", "xlsx")
        If target Is Nothing Then Return
        Try
            Exporters.SaveXlsx(target, _project.Series, Calibration)
            SetStatus(String.Format("Saved {0} (one sheet per curve).", Path.GetFileName(target)))
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
            MessageBox.Show("The file could not be saved:" & vbCrLf & ex.Message & vbCrLf & vbCrLf & "If it is open in Excel, close it and try again.",
                            "Export", MessageBoxButtons.OK, MessageBoxIcon.Warning)
        End Try
    End Sub

    Private Function AskExportPath(filter As String, extension As String) As String
        dlgExport.Filter = filter
        dlgExport.DefaultExt = extension
        dlgExport.FileName = Path.GetFileNameWithoutExtension(If(_projectPath, If(_project.ImageFileName, "Graph data"))) & "." & extension
        If Directory.Exists(_settings.LastExportFolder) Then dlgExport.InitialDirectory = _settings.LastExportFolder
        If dlgExport.ShowDialog(Me) <> DialogResult.OK Then Return Nothing
        _settings.LastExportFolder = Path.GetDirectoryName(dlgExport.FileName)
        Return dlgExport.FileName
    End Function

#End Region

#Region "Analysis"

    Private Sub UpdateStatistics()
        Dim s = ActiveSeries
        If s Is Nothing OrElse s.Points.Count = 0 Then
            lblStats.Text = "No points traced yet."
            Return
        End If
        If Not Calibration.IsComplete Then
            lblStats.Text = "Calibrate the axes to see statistics."
            Return
        End If
        Dim st = DataTools.Statistics(s, Calibration)
        lblStats.Text = String.Format("Points:  {0}" & vbCrLf & "X from  {1}  to  {2}" & vbCrLf & "Y from  {3}  to  {4}" & vbCrLf & "Area under the curve:  {5}",
                                      st.Count, DataTools.FormatNumber(st.MinX), DataTools.FormatNumber(st.MaxX),
                                      DataTools.FormatNumber(st.MinY), DataTools.FormatNumber(st.MaxY), DataTools.FormatNumber(st.Area))
    End Sub

    Private Sub btnInterpolate_Click(sender As Object, e As EventArgs) Handles btnInterpolate.Click
        If Not RequireCalibration("read values") Then Return
        Dim x As Double
        If Not DataTools.TryParseNumber(txtInterpolateX.Text, x) Then
            lblInterpolateResult.Text = "Type an X value first."
            Return
        End If
        Dim y = DataTools.InterpolateAt(ActiveSeries, Calibration, x)
        If y.HasValue Then
            lblInterpolateResult.Text = String.Format("Y = {0}", DataTools.FormatNumber(y.Value))
        Else
            Dim st = DataTools.Statistics(ActiveSeries, Calibration)
            lblInterpolateResult.Text = String.Format("Outside the traced range ({0} to {1}).", DataTools.FormatNumber(st.MinX), DataTools.FormatNumber(st.MaxX))
        End If
    End Sub

    Private Sub txtInterpolateX_KeyDown(sender As Object, e As KeyEventArgs) Handles txtInterpolateX.KeyDown
        If e.KeyCode = Keys.Enter Then
            btnInterpolate.PerformClick()
            e.SuppressKeyPress = True
        End If
    End Sub

    Private Sub btnFit_Click(sender As Object, e As EventArgs) Handles btnFit.Click
        If Not RequireCalibration("fit a curve") Then Return
        Dim kind = [Enum].GetValues(Of FitKind)()(Math.Max(0, cmbFitKind.SelectedIndex))
        Try
            _fit = CurveFit.Fit(ActiveSeries.DataPoints(Calibration), kind)
        Catch ex As InvalidOperationException
            _fit = Nothing
            txtFitResult.Text = ex.Message
            RefreshAll()
            Return
        End Try
        txtFitResult.Text = String.Format("{0}{1}{1}R² = {2:0.######}{1}Points used: {3}",
                                          _fit.Equation, vbCrLf, _fit.RSquared, _fit.PointsUsed)
        RefreshAll()
        SetStatus("Fitted. The dashed line on the graph shows the fit; the equation can be copied from the box.")
    End Sub

    Private Sub chkShowFit_CheckedChanged(sender As Object, e As EventArgs) Handles chkShowFit.CheckedChanged
        If Not _updating Then RefreshAll()
    End Sub

    ''' <summary>The fitted curve as image points across the traced X range (NaN where undefined or far off the image).</summary>
    Private Function BuildFitCurve(fit As FitResult) As List(Of PointD)
        Dim data = ActiveSeries?.DataPoints(Calibration).Where(Function(p) p.IsValid).ToList()
        If data Is Nothing OrElse data.Count < 2 OrElse Not Calibration.IsComplete Then Return Nothing
        Dim minX = data.Min(Function(p) p.X), maxX = data.Max(Function(p) p.X)
        If Calibration.LogX AndAlso minX <= 0 Then Return Nothing
        Dim result As New List(Of PointD)
        Const samples = 300
        For i = 0 To samples
            Dim t = i / samples
            Dim x = If(Calibration.LogX, Math.Pow(10, Math.Log10(minX) + t * (Math.Log10(maxX) - Math.Log10(minX))), minX + t * (maxX - minX))
            Dim y = fit.Evaluate(x)
            Dim pixel = If(Double.IsNaN(y) OrElse (Calibration.LogY AndAlso y <= 0), New PointD(Double.NaN, Double.NaN), Calibration.DataToPixel(New PointD(x, y)))
            ' Leave out points far outside the image so the line never needs absurd coordinates.
            If pixel.IsValid AndAlso (Math.Abs(pixel.X) > _image.Width * 3 OrElse Math.Abs(pixel.Y) > _image.Height * 3) Then pixel = New PointD(Double.NaN, Double.NaN)
            result.Add(pixel)
        Next
        Return result
    End Function

#End Region

#Region "View, help and keyboard"

    Private Sub ZoomIn_Click(sender As Object, e As EventArgs) Handles mnuZoomIn.Click, tsZoomIn.Click
        canvas.ZoomBy(1.25)
    End Sub

    Private Sub ZoomOut_Click(sender As Object, e As EventArgs) Handles mnuZoomOut.Click, tsZoomOut.Click
        canvas.ZoomBy(1 / 1.25)
    End Sub

    Private Sub Fit_Click(sender As Object, e As EventArgs) Handles mnuFitWindow.Click, tsFit.Click
        canvas.FitToWindow()
    End Sub

    Private Sub Magnifier_Click(sender As Object, e As EventArgs) Handles mnuMagnifier.Click, tsMagnifier.Click
        SetViewOption(magnifier:=If(sender Is mnuMagnifier, mnuMagnifier.Checked, tsMagnifier.Checked))
    End Sub

    Private Sub Grid_Click(sender As Object, e As EventArgs) Handles mnuGrid.Click, tsGrid.Click
        SetViewOption(grid:=If(sender Is mnuGrid, mnuGrid.Checked, tsGrid.Checked))
        If _settings.ShowGrid AndAlso Not Calibration.IsComplete Then SetStatus("The calibrated grid appears once the axes are calibrated.")
    End Sub

    Private Sub Lines_Changed(sender As Object, e As EventArgs) Handles mnuLines.Click, chkShowLines.CheckedChanged
        If _updating Then Return
        SetViewOption(lines:=If(sender Is mnuLines, mnuLines.Checked, chkShowLines.Checked))
    End Sub

    ''' <summary>Applies view options and keeps the menu, toolbar and check boxes in step.</summary>
    Private Sub SetViewOption(Optional magnifier As Boolean? = Nothing, Optional grid As Boolean? = Nothing, Optional lines As Boolean? = Nothing)
        Dim wasUpdating = _updating
        _updating = True
        Try
            If magnifier.HasValue Then _settings.ShowMagnifier = magnifier.Value
            If grid.HasValue Then _settings.ShowGrid = grid.Value
            If lines.HasValue Then _settings.ShowLines = lines.Value
            mnuMagnifier.Checked = _settings.ShowMagnifier : tsMagnifier.Checked = _settings.ShowMagnifier
            mnuGrid.Checked = _settings.ShowGrid : tsGrid.Checked = _settings.ShowGrid
            mnuLines.Checked = _settings.ShowLines : chkShowLines.Checked = _settings.ShowLines
            canvas.ShowMagnifier = _settings.ShowMagnifier
            canvas.ShowGrid = _settings.ShowGrid
            canvas.ShowLines = _settings.ShowLines
            canvas.Invalidate()
        Finally
            _updating = wasUpdating
        End Try
    End Sub

    Private Sub HowTo_Click(sender As Object, e As EventArgs) Handles mnuHowTo.Click
        MessageBox.Show(
            "1. Open the graph image (File ▸ Open Image, paste a screenshot with Ctrl+V, or drag the file onto the window)." & vbCrLf & vbCrLf &
            "2. Calibrate: on the Calibrate tab click ""Pick X1"", click a labelled tick on the X axis, and type its value. " &
            "Do the same for X2 (another X tick) and for Y1 and Y2 on the Y axis. Tick ""logarithmic"" for log axes." & vbCrLf & vbCrLf &
            "3. Trace: on the Trace tab choose a tool — click points, drag along the curve, or let Automatic trace follow the curve's color. " &
            "Use Add to trace several curves." & vbCrLf & vbCrLf &
            "4. Check and clean the data on the Data tab, then copy it or save it as CSV or Excel." & vbCrLf & vbCrLf &
            "5. Analyze: read Y at any X, see statistics, and fit a curve with its equation and R²." & vbCrLf & vbCrLf &
            "Save the project (Ctrl+S) to continue later. Mouse wheel zooms, right-drag moves the image, Ctrl+Z undoes.",
            "How to use Graphing", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub About_Click(sender As Object, e As EventArgs) Handles mnuAbout.Click
        MessageBox.Show(String.Format("Graphing v{0}" & vbCrLf & vbCrLf &
                                      "This application traces the graph on top of a base graph for research purposes." & vbCrLf & vbCrLf &
                                      "© 2026 ASRD", My.Application.Info.Version.ToString(2)),
                        "About Graphing", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub Form1_KeyDown(sender As Object, e As KeyEventArgs) Handles MyBase.KeyDown
        If e.KeyCode = Keys.Escape AndAlso _pickingReference IsNot Nothing Then
            CancelPick()
            SetStatus("Picking cancelled.")
            e.Handled = True
        End If
    End Sub

    Private Shared Sub SetValue(control As NumericUpDown, value As Integer)
        control.Value = Math.Max(control.Minimum, Math.Min(control.Maximum, CDec(value)))
    End Sub

#End Region

End Class
