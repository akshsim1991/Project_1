Imports System.IO
Imports System.Runtime.InteropServices

Public Class Form1

    Private Shared ReadOnly SupportedExtensions As String() = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tif", ".tiff"}

    Private Shared ReadOnly FitModeNames As String() = {"Fill cell (crop edges)", "Fit whole image"}

    ' An aspect ratio of 0 means "use the shape of the first image".
    Private Shared ReadOnly CellShapeNames As String() = {
        "Square (1:1)", "Landscape (4:3)", "Landscape (3:2)", "Widescreen (16:9)",
        "Portrait (3:4)", "Portrait (2:3)", "Tall (9:16)", "Match first image"}
    Private Shared ReadOnly CellShapeAspects As Double() = {1.0, 4 / 3, 3 / 2, 16 / 9, 3 / 4, 2 / 3, 9 / 16, 0}

    ' A width of 0 means "Custom": the user types the width themselves.
    Private Shared ReadOnly SizePresetNames As String() = {
        "Custom", "Instagram (1080 px)", "Full HD (1920 px)", "Standard (2000 px)", "4K (3840 px)", "Large print (6000 px)"}
    Private Shared ReadOnly SizePresetWidths As Integer() = {0, 1080, 1920, 2000, 3840, 6000}

    Private Shared ReadOnly NormalStatusColor As Color = Color.Gainsboro
    Private Shared ReadOnly WarningStatusColor As Color = Color.FromArgb(255, 190, 90)
    Private Shared ReadOnly ErrorStatusColor As Color = Color.FromArgb(255, 120, 120)

    Private ReadOnly _images As New List(Of CollageImage)
    Private _borderColor As Color = Color.Black
    Private _backgroundColor As Color = Color.White

    ' True while images are loading or the collage is being saved; most of the UI is disabled meanwhile.
    Private _busy As Boolean
    ' True while settings are being applied to the controls, so their change events are ignored.
    ' Starts True because the designer code also changes control values while the form is being built.
    Private _loadingSettings As Boolean = True
    Private _syncingPreset As Boolean
    ' True while the image list is being rebuilt; its selection events are ignored until it is complete.
    Private _refreshingList As Boolean

#Region "Start-up and shut-down"

    Private Sub Form1_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        cmbFitMode.Items.AddRange(FitModeNames)
        cmbCellShape.Items.AddRange(CellShapeNames)
        cmbSizePreset.Items.AddRange(SizePresetNames)

        LoadSettings()
        EnableDragDrop(Me)
        ResizeImageColumn()
        RefreshImageList(Enumerable.Empty(Of String)())
        UpdateControlStates()
        RenderPreview()
    End Sub

    Private Async Sub Form1_Shown(sender As Object, e As EventArgs) Handles MyBase.Shown
        ' Images passed on the command line, e.g. photos dropped onto PixelBlend.exe or its shortcut.
        Dim files = My.Application.CommandLineArgs.ToArray()
        If files.Length > 0 Then Await AddImagesAsync(files)
    End Sub

    Private Sub Form1_FormClosing(sender As Object, e As FormClosingEventArgs) Handles MyBase.FormClosing
        If _busy Then
            e.Cancel = True
            MessageBox.Show("Please wait until PixelBlend has finished loading or saving images.", "PixelBlend",
                            MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        SaveSettings()
    End Sub

    Private Sub Form1_FormClosed(sender As Object, e As FormClosedEventArgs) Handles MyBase.FormClosed
        previewTimer.Stop()
        SetPreviewImage(Nothing)
        For Each item In _images
            item.Dispose()
        Next
        _images.Clear()
    End Sub

    Private Sub LoadSettings()
        Dim s = My.Settings
        _loadingSettings = True
        Try
            chkAutoGrid.Checked = s.AutoGrid
            SetValue(nudRows, s.Rows)
            SetValue(nudColumns, s.Columns)
            cmbFitMode.SelectedIndex = ClampIndex(s.FitMode, cmbFitMode)
            cmbCellShape.SelectedIndex = ClampIndex(s.CellShape, cmbCellShape)
            SetValue(nudOutputWidth, s.OutputWidth)
            SetValue(nudSpacing, s.Spacing)
            SetValue(nudMargin, s.OuterMargin)
            chkBorder.Checked = s.BorderEnabled
            SetValue(nudBorderThickness, s.BorderThickness)
            SetValue(nudJpegQuality, s.JpegQuality)
            _borderColor = s.BorderColor
            _backgroundColor = s.BackgroundColor
            ShowColor(btnBorderColor, _borderColor)
            ShowColor(btnBackgroundColor, _backgroundColor)
            SyncPresetFromWidth()

            ' Restore the window only if it will be visible on one of the current screens.
            Dim savedBounds = s.WindowBounds
            If savedBounds.Width >= MinimumSize.Width AndAlso savedBounds.Height >= MinimumSize.Height AndAlso
               Screen.AllScreens.Any(Function(sc) sc.WorkingArea.IntersectsWith(savedBounds)) Then
                StartPosition = FormStartPosition.Manual
                Bounds = savedBounds
            End If
            If s.WindowMaximized Then WindowState = FormWindowState.Maximized
        Finally
            _loadingSettings = False
        End Try
    End Sub

    Private Sub SaveSettings()
        Dim s = My.Settings
        s.AutoGrid = chkAutoGrid.Checked
        s.Rows = CInt(nudRows.Value)
        s.Columns = CInt(nudColumns.Value)
        s.FitMode = cmbFitMode.SelectedIndex
        s.CellShape = cmbCellShape.SelectedIndex
        s.OutputWidth = CInt(nudOutputWidth.Value)
        s.Spacing = CInt(nudSpacing.Value)
        s.OuterMargin = CInt(nudMargin.Value)
        s.BorderEnabled = chkBorder.Checked
        s.BorderThickness = CInt(nudBorderThickness.Value)
        s.JpegQuality = CInt(nudJpegQuality.Value)
        s.BorderColor = _borderColor
        s.BackgroundColor = _backgroundColor
        s.WindowMaximized = (WindowState = FormWindowState.Maximized)
        s.WindowBounds = If(WindowState = FormWindowState.Normal, Bounds, RestoreBounds)
        s.Save()
    End Sub

#End Region

#Region "Adding images"

    Private Async Sub btnAddImages_Click(sender As Object, e As EventArgs) Handles btnAddImages.Click
        If _busy Then Return
        If Directory.Exists(My.Settings.LastOpenFolder) Then
            openImagesDialog.InitialDirectory = My.Settings.LastOpenFolder
        End If
        openImagesDialog.FileName = ""
        If openImagesDialog.ShowDialog(Me) <> DialogResult.OK Then Return

        My.Settings.LastOpenFolder = Path.GetDirectoryName(openImagesDialog.FileNames(0))
        Await AddImagesAsync(openImagesDialog.FileNames)
    End Sub

    ''' <summary>Turns on drag-and-drop for a control and everything inside it, so files can be dropped anywhere.</summary>
    Private Sub EnableDragDrop(parent As Control)
        parent.AllowDrop = True
        AddHandler parent.DragEnter, AddressOf Control_DragEnter
        AddHandler parent.DragDrop, AddressOf Control_DragDrop
        For Each child As Control In parent.Controls
            EnableDragDrop(child)
        Next
    End Sub

    Private Sub Control_DragEnter(sender As Object, e As DragEventArgs)
        e.Effect = If(Not _busy AndAlso e.Data.GetDataPresent(DataFormats.FileDrop), DragDropEffects.Copy, DragDropEffects.None)
    End Sub

    Private Async Sub Control_DragDrop(sender As Object, e As DragEventArgs)
        If _busy Then Return
        Dim paths = TryCast(e.Data.GetData(DataFormats.FileDrop), String())
        If paths Is Nothing OrElse paths.Length = 0 Then Return
        Activate()
        ' Let the drop finish first; otherwise Explorer stays frozen while any message box is open.
        Await Task.Yield()
        Await AddImagesAsync(paths)
    End Sub

    ''' <summary>Adds image files, and the images directly inside any folders, to the end of the collage.</summary>
    Private Async Function AddImagesAsync(paths As IEnumerable(Of String)) As Task
        Dim files = ExpandImagePaths(paths)
        If files.Count = 0 Then
            MessageBox.Show("No supported images were found." & vbCrLf & vbCrLf &
                            "PixelBlend can use JPG, PNG, BMP, GIF and TIFF files.", "Add images",
                            MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        Dim failed As New List(Of String)
        Dim addedKeys As New List(Of String)
        _busy = True
        UseWaitCursor = True
        UpdateControlStates()
        Try
            For i = 0 To files.Count - 1
                SetStatus(String.Format("Loading image {0} of {1}…", i + 1, files.Count), NormalStatusColor)
                Dim filePath = files(i)
                Dim item As CollageImage = Nothing
                Dim errorMessage As String = Nothing
                ' Decoding large photos is slow, so it runs in the background to keep the window responsive.
                Try
                    item = Await Task.Run(Function() CollageImage.Load(filePath))
                Catch ex As Exception
                    errorMessage = ex.Message
                End Try

                If item Is Nothing Then
                    failed.Add(Path.GetFileName(filePath) & " — " & errorMessage)
                Else
                    _images.Add(item)
                    imgThumbs.Images.Add(item.Key, item.Thumbnail)
                    addedKeys.Add(item.Key)
                End If
            Next
        Finally
            _busy = False
            UseWaitCursor = False
        End Try

        RefreshImageList(addedKeys)
        UpdateAutoGrid()
        UpdateControlStates()
        RenderPreview()

        If failed.Count > 0 Then
            Dim shown = failed.Take(10).ToList()
            Dim message = "These files could not be opened and were skipped:" & vbCrLf & vbCrLf & String.Join(vbCrLf, shown)
            If failed.Count > shown.Count Then message &= vbCrLf & String.Format("…and {0} more.", failed.Count - shown.Count)
            MessageBox.Show(message, "Add images", MessageBoxButtons.OK, MessageBoxIcon.Warning)
        End If
    End Function

    Private Shared Function ExpandImagePaths(paths As IEnumerable(Of String)) As List(Of String)
        Dim result As New List(Of String)
        For Each item In paths
            If Directory.Exists(item) Then
                Try
                    result.AddRange(Directory.GetFiles(item).Where(AddressOf IsSupportedImage).OrderBy(Function(f) f, StringComparer.CurrentCultureIgnoreCase))
                Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
                    ' A folder we can't read is simply skipped.
                End Try
            ElseIf File.Exists(item) AndAlso IsSupportedImage(item) Then
                result.Add(item)
            End If
        Next
        Return result
    End Function

    Private Shared Function IsSupportedImage(filePath As String) As Boolean
        Return SupportedExtensions.Contains(Path.GetExtension(filePath).ToLowerInvariant())
    End Function

#End Region

#Region "Managing the image list"

    ''' <summary>Rebuilds the list from <see cref="_images"/> and selects the images with the given keys.</summary>
    Private Sub RefreshImageList(selectedKeys As IEnumerable(Of String))
        Dim selected = New HashSet(Of String)(selectedKeys)
        _refreshingList = True
        lvImages.BeginUpdate()
        Try
            lvImages.Items.Clear()
            For i = 0 To _images.Count - 1
                Dim image = _images(i)
                Dim item As New ListViewItem(String.Format("{0}.  {1}", i + 1, image.FileName), image.Key) With {
                    .ToolTipText = String.Format("{0}{1}{2} × {3} px", image.FilePath, vbCrLf, image.OriginalSize.Width, image.OriginalSize.Height),
                    .Selected = selected.Contains(image.Key)
                }
                lvImages.Items.Add(item)
            Next
        Finally
            lvImages.EndUpdate()
            _refreshingList = False
        End Try
        UpdateControlStates()
        If lvImages.SelectedIndices.Count > 0 Then lvImages.EnsureVisible(lvImages.SelectedIndices(0))
        lblImagesHeader.Text = If(_images.Count = 0, "IMAGES", String.Format("IMAGES ({0})", _images.Count))
    End Sub

    Private Sub btnRemove_Click(sender As Object, e As EventArgs) Handles btnRemove.Click
        RemoveSelectedImages()
    End Sub

    Private Sub RemoveSelectedImages()
        If _busy OrElse lvImages.SelectedIndices.Count = 0 Then Return
        Dim indices = lvImages.SelectedIndices.Cast(Of Integer)().OrderByDescending(Function(i) i).ToList()
        For Each index In indices
            Dim item = _images(index)
            _images.RemoveAt(index)
            imgThumbs.Images.RemoveByKey(item.Key)
            item.Dispose()
        Next

        ' Keep a selection close to where the removed images were, so pressing Delete repeatedly works nicely.
        Dim nextIndex = Math.Min(indices.Last(), _images.Count - 1)
        RefreshImageList(If(nextIndex >= 0, {_images(nextIndex).Key}, Array.Empty(Of String)()))
        ImagesChanged()
    End Sub

    Private Sub btnClear_Click(sender As Object, e As EventArgs) Handles btnClear.Click
        If _busy OrElse _images.Count = 0 Then Return
        Dim answer = MessageBox.Show(String.Format("Remove all {0} images from the collage?", _images.Count), "Clear all",
                                     MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2)
        If answer <> DialogResult.Yes Then Return

        imgThumbs.Images.Clear()
        For Each item In _images
            item.Dispose()
        Next
        _images.Clear()
        RefreshImageList(Enumerable.Empty(Of String)())
        ImagesChanged()
    End Sub

    Private Sub btnMoveUp_Click(sender As Object, e As EventArgs) Handles btnMoveUp.Click
        MoveSelectedImages(-1)
    End Sub

    Private Sub btnMoveDown_Click(sender As Object, e As EventArgs) Handles btnMoveDown.Click
        MoveSelectedImages(1)
    End Sub

    ''' <summary>Moves every selected image one place up (-1) or down (+1), keeping them selected.</summary>
    Private Sub MoveSelectedImages(direction As Integer)
        If _busy OrElse lvImages.SelectedIndices.Count = 0 Then Return
        Dim indices = lvImages.SelectedIndices.Cast(Of Integer)().OrderBy(Function(i) i).ToList()
        If direction > 0 Then indices.Reverse()
        ' The selection is already against the top/bottom of the list.
        If indices(0) + direction < 0 OrElse indices(0) + direction >= _images.Count Then Return

        Dim keys = indices.Select(Function(i) _images(i).Key).ToList()
        For Each index In indices
            Dim target = index + direction
            Dim temp = _images(target)
            _images(target) = _images(index)
            _images(index) = temp
        Next
        RefreshImageList(keys)
        ImagesChanged()
    End Sub

    Private Sub lvImages_KeyDown(sender As Object, e As KeyEventArgs) Handles lvImages.KeyDown
        If e.KeyCode = Keys.Delete Then
            RemoveSelectedImages()
            e.Handled = True
        ElseIf e.Control AndAlso e.KeyCode = Keys.A Then
            For Each item As ListViewItem In lvImages.Items
                item.Selected = True
            Next
            e.Handled = True
        End If
    End Sub

    Private Sub lvImages_SelectedIndexChanged(sender As Object, e As EventArgs) Handles lvImages.SelectedIndexChanged
        If Not _refreshingList Then UpdateControlStates()
    End Sub

    Private Sub lvImages_Resize(sender As Object, e As EventArgs) Handles lvImages.Resize
        ResizeImageColumn()
    End Sub

    Private Sub ResizeImageColumn()
        colImage.Width = Math.Max(50, lvImages.ClientSize.Width - 4)
    End Sub

    ''' <summary>Called whenever images are added, removed or reordered.</summary>
    Private Sub ImagesChanged()
        UpdateAutoGrid()
        UpdateControlStates()
        RenderPreview()
    End Sub

#End Region

#Region "Collage options"

    Private Sub Option_Changed(sender As Object, e As EventArgs) Handles _
            nudRows.ValueChanged, nudColumns.ValueChanged, cmbFitMode.SelectedIndexChanged, cmbCellShape.SelectedIndexChanged,
            nudSpacing.ValueChanged, nudMargin.ValueChanged, chkBorder.CheckedChanged, nudBorderThickness.ValueChanged,
            nudOutputWidth.ValueChanged
        If _loadingSettings Then Return
        UpdateControlStates()
        SchedulePreview()
    End Sub

    Private Sub chkAutoGrid_CheckedChanged(sender As Object, e As EventArgs) Handles chkAutoGrid.CheckedChanged
        If _loadingSettings Then Return
        UpdateAutoGrid()
        UpdateControlStates()
        SchedulePreview()
    End Sub

    ''' <summary>When automatic grid is on, sets rows and columns to suit the number of images.</summary>
    Private Sub UpdateAutoGrid()
        If Not chkAutoGrid.Checked Then Return
        Dim grid = CollageRenderer.SuggestGrid(_images.Count)
        SetValue(nudRows, grid.Height)
        SetValue(nudColumns, grid.Width)
    End Sub

    Private Sub cmbSizePreset_SelectedIndexChanged(sender As Object, e As EventArgs) Handles cmbSizePreset.SelectedIndexChanged
        If _syncingPreset OrElse cmbSizePreset.SelectedIndex < 0 Then Return
        Dim width = SizePresetWidths(cmbSizePreset.SelectedIndex)
        If width <= 0 Then Return
        _syncingPreset = True
        Try
            SetValue(nudOutputWidth, width)
        Finally
            _syncingPreset = False
        End Try
    End Sub

    Private Sub nudOutputWidth_ValueChanged(sender As Object, e As EventArgs) Handles nudOutputWidth.ValueChanged
        If Not _loadingSettings AndAlso Not _syncingPreset Then SyncPresetFromWidth()
    End Sub

    ''' <summary>Shows the preset that matches the typed width, or "Custom".</summary>
    Private Sub SyncPresetFromWidth()
        If cmbSizePreset.Items.Count = 0 Then Return
        _syncingPreset = True
        Try
            cmbSizePreset.SelectedIndex = Math.Max(0, Array.IndexOf(SizePresetWidths, CInt(nudOutputWidth.Value)))
        Finally
            _syncingPreset = False
        End Try
    End Sub

    Private Sub btnBorderColor_Click(sender As Object, e As EventArgs) Handles btnBorderColor.Click
        If PickColor(_borderColor) Then
            ShowColor(btnBorderColor, _borderColor)
            SchedulePreview()
        End If
    End Sub

    Private Sub btnBackgroundColor_Click(sender As Object, e As EventArgs) Handles btnBackgroundColor.Click
        If PickColor(_backgroundColor) Then
            ShowColor(btnBackgroundColor, _backgroundColor)
            SchedulePreview()
        End If
    End Sub

    Private Function PickColor(ByRef color As Color) As Boolean
        colorPicker.Color = color
        If colorPicker.ShowDialog(Me) <> DialogResult.OK Then Return False
        color = colorPicker.Color
        Return True
    End Function

    ''' <summary>Paints a button with a color and writes the color's name on it in a readable text color.</summary>
    Private Shared Sub ShowColor(button As Button, color As Color)
        button.BackColor = color
        button.Text = If(color.IsNamedColor, color.Name, String.Format("#{0:X2}{1:X2}{2:X2}", color.R, color.G, color.B))
        Dim brightness = 0.299 * color.R + 0.587 * color.G + 0.114 * color.B
        button.ForeColor = If(brightness > 150, Color.Black, Color.White)
    End Sub

    ''' <summary>Reads the current options from the controls.</summary>
    Private Function BuildOptions() As CollageOptions
        Dim aspect = CellShapeAspects(Math.Max(0, cmbCellShape.SelectedIndex))
        If aspect <= 0 Then
            ' "Match first image"
            aspect = If(_images.Count > 0, _images(0).OriginalSize.Width / _images(0).OriginalSize.Height, 1.0)
        End If
        Return New CollageOptions With {
            .Rows = CInt(nudRows.Value),
            .Columns = CInt(nudColumns.Value),
            .Mode = If(cmbFitMode.SelectedIndex = 1, FitMode.Fit, FitMode.Fill),
            .CellAspect = aspect,
            .OutputWidth = CInt(nudOutputWidth.Value),
            .Spacing = CInt(nudSpacing.Value),
            .Margin = CInt(nudMargin.Value),
            .BorderEnabled = chkBorder.Checked,
            .BorderThickness = CInt(nudBorderThickness.Value),
            .BorderColor = _borderColor,
            .BackgroundColor = _backgroundColor
        }
    End Function

    Private Sub UpdateControlStates()
        Dim idle = Not _busy
        Dim hasImages = _images.Count > 0
        ' Read the selection as plain numbers. SelectedIndices.Contains(i) looks up Items(i) and throws when i is past
        ' the end of the list, which happens while the list is being filled.
        Dim selected = lvImages.SelectedIndices.Cast(Of Integer)().ToList()
        Dim selectedCount = selected.Count

        btnAddImages.Enabled = idle
        btnRemove.Enabled = idle AndAlso selectedCount > 0
        btnClear.Enabled = idle AndAlso hasImages
        btnMoveUp.Enabled = idle AndAlso selectedCount > 0 AndAlso Not selected.Contains(0)
        btnMoveDown.Enabled = idle AndAlso selectedCount > 0 AndAlso Not selected.Contains(lvImages.Items.Count - 1)
        btnSave.Enabled = idle AndAlso hasImages
        pnlSettings.Enabled = idle

        nudRows.Enabled = Not chkAutoGrid.Checked
        nudColumns.Enabled = Not chkAutoGrid.Checked
        lblRows.Enabled = Not chkAutoGrid.Checked
        lblColumns.Enabled = Not chkAutoGrid.Checked
        nudBorderThickness.Enabled = chkBorder.Checked
        btnBorderColor.Enabled = chkBorder.Checked
        lblBorderThickness.Enabled = chkBorder.Checked
        lblBorderColor.Enabled = chkBorder.Checked

        lblEmpty.Visible = Not hasImages
        picPreview.Visible = hasImages
    End Sub

#End Region

#Region "Live preview"

    ''' <summary>Redraws the preview shortly after the last change, so dragging a value doesn't redraw it dozens of times.</summary>
    Private Sub SchedulePreview()
        previewTimer.Stop()
        previewTimer.Start()
    End Sub

    Private Sub previewTimer_Tick(sender As Object, e As EventArgs) Handles previewTimer.Tick
        previewTimer.Stop()
        RenderPreview()
    End Sub

    Private Sub pnlPreview_Resize(sender As Object, e As EventArgs) Handles pnlPreview.Resize
        If IsHandleCreated Then SchedulePreview()
    End Sub

    Private Sub RenderPreview()
        previewTimer.Stop()
        ' While saving, the preview images may be in use on the background thread.
        If _busy Then Return

        If _images.Count = 0 Then
            SetPreviewImage(Nothing)
            lblCanvasInfo.Text = ""
            SetStatus("Add images to start.", NormalStatusColor)
            Return
        End If

        Dim options = BuildOptions()
        Dim problem = CollageRenderer.Validate(options)
        If problem IsNot Nothing Then
            SetPreviewImage(Nothing)
            lblCanvasInfo.Text = ""
            SetStatus(problem, ErrorStatusColor)
            Return
        End If

        Dim canvas = CollageRenderer.ComputeLayout(options, 1.0).CanvasSize
        Dim box = picPreview.ClientSize
        If box.Width < 10 OrElse box.Height < 10 Then Return
        Dim scale = Math.Min(1.0, Math.Min(box.Width / canvas.Width, box.Height / canvas.Height))

        Try
            SetPreviewImage(CollageRenderer.Render(options, scale, _images.Count, Function(i) _images(i).Preview, False))
        Catch ex As Exception When TypeOf ex Is OutOfMemoryException OrElse TypeOf ex Is ExternalException OrElse TypeOf ex Is ArgumentException
            SetPreviewImage(Nothing)
            SetStatus("The preview could not be drawn: " & ex.Message, ErrorStatusColor)
            Return
        End Try

        lblCanvasInfo.Text = String.Format("Saved size: {0:N0} × {1:N0} px", canvas.Width, canvas.Height)
        ShowLayoutStatus(options)
    End Sub

    Private Sub ShowLayoutStatus(options As CollageOptions)
        Dim cells = options.CellCount
        Dim summary = String.Format("{0} image{1}  ·  {2} × {3} grid", _images.Count, If(_images.Count = 1, "", "s"),
                                    options.Rows, options.Columns)
        If _images.Count > cells Then
            SetStatus(String.Format("{0}  ·  Only the first {1} images fit. Add more rows or columns to include the other {2}.",
                                    summary, cells, _images.Count - cells), WarningStatusColor)
        ElseIf _images.Count < cells Then
            Dim empty = cells - _images.Count
            SetStatus(String.Format("{0}  ·  {1} empty cell{2}", summary, empty, If(empty = 1, "", "s")), NormalStatusColor)
        Else
            SetStatus(summary, NormalStatusColor)
        End If
    End Sub

    Private Sub SetPreviewImage(image As Image)
        Dim old = picPreview.Image
        picPreview.Image = image
        old?.Dispose()
    End Sub

    Private Sub SetStatus(text As String, color As Color)
        lblStatus.Text = text
        lblStatus.ForeColor = color
    End Sub

#End Region

#Region "Saving"

    Private Async Sub btnSave_Click(sender As Object, e As EventArgs) Handles btnSave.Click
        If _busy Then Return
        If _images.Count = 0 Then
            MessageBox.Show("Please add at least one image.", "Save collage", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        Dim options = BuildOptions()
        Dim problem = CollageRenderer.Validate(options)
        If problem IsNot Nothing Then
            MessageBox.Show(problem, "Save collage", MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        If _images.Count > options.CellCount Then
            Dim answer = MessageBox.Show(
                String.Format("Only the first {0} of your {1} images fit in a {2} × {3} grid." & vbCrLf & vbCrLf & "Save anyway?",
                              options.CellCount, _images.Count, options.Rows, options.Columns),
                "Save collage", MessageBoxButtons.YesNo, MessageBoxIcon.Question)
            If answer <> DialogResult.Yes Then Return
        End If

        If Directory.Exists(My.Settings.LastSaveFolder) Then
            saveCollageDialog.InitialDirectory = My.Settings.LastSaveFolder
        End If
        If saveCollageDialog.ShowDialog(Me) <> DialogResult.OK Then Return

        Dim filePath = saveCollageDialog.FileName
        If Not {".jpg", ".jpeg", ".png", ".bmp"}.Contains(Path.GetExtension(filePath).ToLowerInvariant()) Then
            filePath &= ".jpg"
        End If
        My.Settings.LastSaveFolder = Path.GetDirectoryName(filePath)

        Dim items = _images.ToList()
        Dim quality = CInt(nudJpegQuality.Value)
        Dim unreadable As New List(Of String)
        Dim saveError As Exception = Nothing

        _busy = True
        UseWaitCursor = True
        UpdateControlStates()
        SetStatus("Saving the collage…", NormalStatusColor)
        Try
            ' Full-size photos are loaded one at a time in the background, drawn, and released straight away.
            Await Task.Run(
                Sub()
                    Using collage = CollageRenderer.Render(options, 1.0, items.Count,
                                                           Function(i) LoadFullSizeImage(items(i), unreadable), True)
                        CollageRenderer.Save(collage, filePath, quality)
                    End Using
                End Sub)
        Catch ex As Exception
            saveError = ex
        Finally
            _busy = False
            UseWaitCursor = False
            UpdateControlStates()
        End Try
        RenderPreview()

        If saveError IsNot Nothing Then
            Dim reason = If(TypeOf saveError Is ExternalException,
                            "Windows could not write the file. Check that the folder exists, that you are allowed to save there, and that the file is not open in another program.",
                            saveError.Message)
            MessageBox.Show("The collage could not be saved." & vbCrLf & vbCrLf & reason, "Save collage",
                            MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If

        Dim message = "Collage saved successfully!" & vbCrLf & vbCrLf & filePath
        If unreadable.Count > 0 Then
            message &= vbCrLf & vbCrLf & "These images were moved, deleted or changed since they were added, so a lower-quality copy was used:" &
                       vbCrLf & String.Join(vbCrLf, unreadable.Take(10))
        End If
        message &= vbCrLf & vbCrLf & "Open the folder that contains it?"
        If MessageBox.Show(message, "Success", MessageBoxButtons.YesNo, MessageBoxIcon.Information) = DialogResult.Yes Then
            Try
                Process.Start("explorer.exe", String.Format("/select,""{0}""", filePath))
            Catch ex As Exception When TypeOf ex Is ComponentModel.Win32Exception OrElse TypeOf ex Is IOException
                ' Not being able to open Explorer isn't worth an error message; the file is saved.
            End Try
        End If
    End Sub

    ''' <summary>
    ''' Loads the original photo for saving. If it can no longer be read, falls back to a copy of the
    ''' preview image so the collage can still be saved. Runs on a background thread.
    ''' </summary>
    Private Shared Function LoadFullSizeImage(item As CollageImage, unreadable As List(Of String)) As Image
        Try
            Return item.LoadFullImage()
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException OrElse
                                   TypeOf ex Is InvalidDataException
            unreadable.Add(item.FileName)
            Return New Bitmap(item.Preview)
        End Try
    End Function

#End Region

#Region "Keyboard shortcuts"

    Private Sub Form1_KeyDown(sender As Object, e As KeyEventArgs) Handles MyBase.KeyDown
        If e.Control AndAlso e.KeyCode = Keys.O Then
            btnAddImages.PerformClick()
            e.SuppressKeyPress = True
        ElseIf e.Control AndAlso e.KeyCode = Keys.S Then
            btnSave.PerformClick()
            e.SuppressKeyPress = True
        End If
    End Sub

#End Region

#Region "Helpers"

    Private Shared Sub SetValue(control As NumericUpDown, value As Integer)
        control.Value = Math.Max(control.Minimum, Math.Min(control.Maximum, CDec(value)))
    End Sub

    Private Shared Function ClampIndex(index As Integer, combo As ComboBox) As Integer
        Return If(index >= 0 AndAlso index < combo.Items.Count, index, 0)
    End Function

#End Region

End Class
