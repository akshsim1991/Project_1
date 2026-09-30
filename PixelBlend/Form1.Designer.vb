<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form1
    Inherits System.Windows.Forms.Form

    'Form overrides dispose to clean up the component list.
    <System.Diagnostics.DebuggerNonUserCode()> _
    Protected Overrides Sub Dispose(ByVal disposing As Boolean)
        Try
            If disposing AndAlso components IsNot Nothing Then
                components.Dispose()
            End If
        Finally
            MyBase.Dispose(disposing)
        End Try
    End Sub

    'Required by the Windows Form Designer
    Private components As System.ComponentModel.IContainer

    'NOTE: The following procedure is required by the Windows Form Designer
    'It can be modified using the Windows Form Designer.
    'Do not modify it using the code editor.
    <System.Diagnostics.DebuggerStepThrough()> _
    Private Sub InitializeComponent()
        Me.components = New System.ComponentModel.Container()
        Dim resources As System.ComponentModel.ComponentResourceManager = New System.ComponentModel.ComponentResourceManager(GetType(Form1))
        Me.pnlHeader = New System.Windows.Forms.Panel()
        Me.lblTagline = New System.Windows.Forms.Label()
        Me.lblVersion = New System.Windows.Forms.Label()
        Me.lblTitle = New System.Windows.Forms.Label()
        Me.pnlStatus = New System.Windows.Forms.Panel()
        Me.lblStatus = New System.Windows.Forms.Label()
        Me.lblCanvasInfo = New System.Windows.Forms.Label()
        Me.pnlLeft = New System.Windows.Forms.Panel()
        Me.lvImages = New System.Windows.Forms.ListView()
        Me.colImage = CType(New System.Windows.Forms.ColumnHeader(), System.Windows.Forms.ColumnHeader)
        Me.imgThumbs = New System.Windows.Forms.ImageList(Me.components)
        Me.lblDropHint = New System.Windows.Forms.Label()
        Me.pnlImageButtons = New System.Windows.Forms.Panel()
        Me.btnMoveDown = New System.Windows.Forms.Button()
        Me.btnMoveUp = New System.Windows.Forms.Button()
        Me.btnClear = New System.Windows.Forms.Button()
        Me.btnRemove = New System.Windows.Forms.Button()
        Me.btnAddImages = New System.Windows.Forms.Button()
        Me.lblImagesHeader = New System.Windows.Forms.Label()
        Me.pnlRight = New System.Windows.Forms.Panel()
        Me.pnlSettings = New System.Windows.Forms.Panel()
        Me.lblOutputNote = New System.Windows.Forms.Label()
        Me.nudJpegQuality = New System.Windows.Forms.NumericUpDown()
        Me.lblJpegQuality = New System.Windows.Forms.Label()
        Me.nudOutputWidth = New System.Windows.Forms.NumericUpDown()
        Me.lblOutputWidth = New System.Windows.Forms.Label()
        Me.cmbSizePreset = New System.Windows.Forms.ComboBox()
        Me.lblSizePreset = New System.Windows.Forms.Label()
        Me.lblOutputHeader = New System.Windows.Forms.Label()
        Me.btnBorderColor = New System.Windows.Forms.Button()
        Me.lblBorderColor = New System.Windows.Forms.Label()
        Me.nudBorderThickness = New System.Windows.Forms.NumericUpDown()
        Me.lblBorderThickness = New System.Windows.Forms.Label()
        Me.chkBorder = New System.Windows.Forms.CheckBox()
        Me.btnBackgroundColor = New System.Windows.Forms.Button()
        Me.lblBackground = New System.Windows.Forms.Label()
        Me.nudMargin = New System.Windows.Forms.NumericUpDown()
        Me.lblMargin = New System.Windows.Forms.Label()
        Me.nudSpacing = New System.Windows.Forms.NumericUpDown()
        Me.lblSpacing = New System.Windows.Forms.Label()
        Me.lblSpacingHeader = New System.Windows.Forms.Label()
        Me.cmbCellShape = New System.Windows.Forms.ComboBox()
        Me.lblCellShape = New System.Windows.Forms.Label()
        Me.cmbFitMode = New System.Windows.Forms.ComboBox()
        Me.lblFitMode = New System.Windows.Forms.Label()
        Me.nudColumns = New System.Windows.Forms.NumericUpDown()
        Me.lblColumns = New System.Windows.Forms.Label()
        Me.nudRows = New System.Windows.Forms.NumericUpDown()
        Me.lblRows = New System.Windows.Forms.Label()
        Me.chkAutoGrid = New System.Windows.Forms.CheckBox()
        Me.lblLayoutHeader = New System.Windows.Forms.Label()
        Me.pnlSaveArea = New System.Windows.Forms.Panel()
        Me.btnSave = New System.Windows.Forms.Button()
        Me.pnlPreview = New System.Windows.Forms.Panel()
        Me.picPreview = New System.Windows.Forms.PictureBox()
        Me.lblEmpty = New System.Windows.Forms.Label()
        Me.previewTimer = New System.Windows.Forms.Timer(Me.components)
        Me.hints = New System.Windows.Forms.ToolTip(Me.components)
        Me.colorPicker = New System.Windows.Forms.ColorDialog()
        Me.openImagesDialog = New System.Windows.Forms.OpenFileDialog()
        Me.saveCollageDialog = New System.Windows.Forms.SaveFileDialog()
        Me.pnlHeader.SuspendLayout()
        Me.pnlStatus.SuspendLayout()
        Me.pnlLeft.SuspendLayout()
        Me.pnlImageButtons.SuspendLayout()
        Me.pnlRight.SuspendLayout()
        Me.pnlSettings.SuspendLayout()
        CType(Me.nudJpegQuality, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudOutputWidth, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudBorderThickness, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudMargin, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudSpacing, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudColumns, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudRows, System.ComponentModel.ISupportInitialize).BeginInit()
        Me.pnlSaveArea.SuspendLayout()
        Me.pnlPreview.SuspendLayout()
        CType(Me.picPreview, System.ComponentModel.ISupportInitialize).BeginInit()
        Me.SuspendLayout()
        '
        'pnlHeader
        '
        Me.pnlHeader.BackColor = System.Drawing.Color.FromArgb(CType(CType(20, Byte), Integer), CType(CType(20, Byte), Integer), CType(CType(20, Byte), Integer))
        Me.pnlHeader.Controls.Add(Me.lblTagline)
        Me.pnlHeader.Controls.Add(Me.lblVersion)
        Me.pnlHeader.Controls.Add(Me.lblTitle)
        Me.pnlHeader.Dock = System.Windows.Forms.DockStyle.Top
        Me.pnlHeader.Location = New System.Drawing.Point(0, 0)
        Me.pnlHeader.Name = "pnlHeader"
        Me.pnlHeader.Size = New System.Drawing.Size(1184, 52)
        Me.pnlHeader.TabIndex = 0
        '
        'lblTagline
        '
        Me.lblTagline.Anchor = CType((System.Windows.Forms.AnchorStyles.Top Or System.Windows.Forms.AnchorStyles.Right), System.Windows.Forms.AnchorStyles)
        Me.lblTagline.ForeColor = System.Drawing.Color.Gray
        Me.lblTagline.Location = New System.Drawing.Point(724, 16)
        Me.lblTagline.Name = "lblTagline"
        Me.lblTagline.Size = New System.Drawing.Size(448, 20)
        Me.lblTagline.TabIndex = 2
        Me.lblTagline.Text = "Collage Making Software  ·  © 2023 Akshaya Simha"
        Me.lblTagline.TextAlign = System.Drawing.ContentAlignment.MiddleRight
        '
        'lblVersion
        '
        Me.lblVersion.AutoSize = True
        Me.lblVersion.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblVersion.ForeColor = System.Drawing.Color.Red
        Me.lblVersion.Location = New System.Drawing.Point(142, 20)
        Me.lblVersion.Name = "lblVersion"
        Me.lblVersion.Size = New System.Drawing.Size(21, 15)
        Me.lblVersion.TabIndex = 1
        Me.lblVersion.Text = "v2"
        '
        'lblTitle
        '
        Me.lblTitle.AutoSize = True
        Me.lblTitle.Font = New System.Drawing.Font("Segoe UI", 18.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblTitle.ForeColor = System.Drawing.Color.White
        Me.lblTitle.Location = New System.Drawing.Point(10, 7)
        Me.lblTitle.Name = "lblTitle"
        Me.lblTitle.Size = New System.Drawing.Size(134, 32)
        Me.lblTitle.TabIndex = 0
        Me.lblTitle.Text = "PixelBlend"
        '
        'pnlStatus
        '
        Me.pnlStatus.BackColor = System.Drawing.Color.FromArgb(CType(CType(20, Byte), Integer), CType(CType(20, Byte), Integer), CType(CType(20, Byte), Integer))
        Me.pnlStatus.Controls.Add(Me.lblStatus)
        Me.pnlStatus.Controls.Add(Me.lblCanvasInfo)
        Me.pnlStatus.Dock = System.Windows.Forms.DockStyle.Bottom
        Me.pnlStatus.Location = New System.Drawing.Point(0, 733)
        Me.pnlStatus.Name = "pnlStatus"
        Me.pnlStatus.Padding = New System.Windows.Forms.Padding(8, 0, 8, 0)
        Me.pnlStatus.Size = New System.Drawing.Size(1184, 28)
        Me.pnlStatus.TabIndex = 4
        '
        'lblStatus
        '
        Me.lblStatus.AutoEllipsis = True
        Me.lblStatus.Dock = System.Windows.Forms.DockStyle.Fill
        Me.lblStatus.ForeColor = System.Drawing.Color.Gainsboro
        Me.lblStatus.Location = New System.Drawing.Point(8, 0)
        Me.lblStatus.Name = "lblStatus"
        Me.lblStatus.Size = New System.Drawing.Size(908, 28)
        Me.lblStatus.TabIndex = 0
        Me.lblStatus.Text = "Add images to start."
        Me.lblStatus.TextAlign = System.Drawing.ContentAlignment.MiddleLeft
        '
        'lblCanvasInfo
        '
        Me.lblCanvasInfo.Dock = System.Windows.Forms.DockStyle.Right
        Me.lblCanvasInfo.ForeColor = System.Drawing.Color.Gainsboro
        Me.lblCanvasInfo.Location = New System.Drawing.Point(916, 0)
        Me.lblCanvasInfo.Name = "lblCanvasInfo"
        Me.lblCanvasInfo.Size = New System.Drawing.Size(260, 28)
        Me.lblCanvasInfo.TabIndex = 1
        Me.lblCanvasInfo.TextAlign = System.Drawing.ContentAlignment.MiddleRight
        '
        'pnlLeft
        '
        Me.pnlLeft.BackColor = System.Drawing.Color.FromArgb(CType(CType(40, Byte), Integer), CType(CType(40, Byte), Integer), CType(CType(40, Byte), Integer))
        Me.pnlLeft.Controls.Add(Me.lvImages)
        Me.pnlLeft.Controls.Add(Me.lblDropHint)
        Me.pnlLeft.Controls.Add(Me.pnlImageButtons)
        Me.pnlLeft.Controls.Add(Me.lblImagesHeader)
        Me.pnlLeft.Dock = System.Windows.Forms.DockStyle.Left
        Me.pnlLeft.Location = New System.Drawing.Point(0, 52)
        Me.pnlLeft.Name = "pnlLeft"
        Me.pnlLeft.Padding = New System.Windows.Forms.Padding(10)
        Me.pnlLeft.Size = New System.Drawing.Size(300, 681)
        Me.pnlLeft.TabIndex = 1
        '
        'lvImages
        '
        Me.lvImages.BackColor = System.Drawing.Color.FromArgb(CType(CType(30, Byte), Integer), CType(CType(30, Byte), Integer), CType(CType(30, Byte), Integer))
        Me.lvImages.BorderStyle = System.Windows.Forms.BorderStyle.None
        Me.lvImages.Columns.AddRange(New System.Windows.Forms.ColumnHeader() {Me.colImage})
        Me.lvImages.Dock = System.Windows.Forms.DockStyle.Fill
        Me.lvImages.ForeColor = System.Drawing.Color.White
        Me.lvImages.FullRowSelect = True
        Me.lvImages.HeaderStyle = System.Windows.Forms.ColumnHeaderStyle.None
        Me.lvImages.HideSelection = False
        Me.lvImages.Location = New System.Drawing.Point(10, 150)
        Me.lvImages.Name = "lvImages"
        Me.lvImages.ShowItemToolTips = True
        Me.lvImages.Size = New System.Drawing.Size(280, 485)
        Me.lvImages.SmallImageList = Me.imgThumbs
        Me.lvImages.TabIndex = 2
        Me.lvImages.UseCompatibleStateImageBehavior = False
        Me.lvImages.View = System.Windows.Forms.View.Details
        '
        'colImage
        '
        Me.colImage.Text = "Image"
        Me.colImage.Width = 260
        '
        'imgThumbs
        '
        Me.imgThumbs.ColorDepth = System.Windows.Forms.ColorDepth.Depth32Bit
        Me.imgThumbs.ImageSize = New System.Drawing.Size(56, 56)
        Me.imgThumbs.TransparentColor = System.Drawing.Color.Transparent
        '
        'lblDropHint
        '
        Me.lblDropHint.Dock = System.Windows.Forms.DockStyle.Bottom
        Me.lblDropHint.ForeColor = System.Drawing.Color.Gray
        Me.lblDropHint.Location = New System.Drawing.Point(10, 635)
        Me.lblDropHint.Name = "lblDropHint"
        Me.lblDropHint.Size = New System.Drawing.Size(280, 36)
        Me.lblDropHint.TabIndex = 3
        Me.lblDropHint.Text = "Tip: drag photos or folders onto the window." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "Press Delete to remove the selected images."
        Me.lblDropHint.TextAlign = System.Drawing.ContentAlignment.BottomLeft
        '
        'pnlImageButtons
        '
        Me.pnlImageButtons.Controls.Add(Me.btnMoveDown)
        Me.pnlImageButtons.Controls.Add(Me.btnMoveUp)
        Me.pnlImageButtons.Controls.Add(Me.btnClear)
        Me.pnlImageButtons.Controls.Add(Me.btnRemove)
        Me.pnlImageButtons.Controls.Add(Me.btnAddImages)
        Me.pnlImageButtons.Dock = System.Windows.Forms.DockStyle.Top
        Me.pnlImageButtons.Location = New System.Drawing.Point(10, 36)
        Me.pnlImageButtons.Name = "pnlImageButtons"
        Me.pnlImageButtons.Size = New System.Drawing.Size(280, 114)
        Me.pnlImageButtons.TabIndex = 1
        '
        'btnMoveDown
        '
        Me.btnMoveDown.BackColor = System.Drawing.Color.FromArgb(CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer))
        Me.btnMoveDown.FlatAppearance.BorderColor = System.Drawing.Color.FromArgb(CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer))
        Me.btnMoveDown.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.btnMoveDown.ForeColor = System.Drawing.Color.White
        Me.btnMoveDown.Location = New System.Drawing.Point(143, 74)
        Me.btnMoveDown.Name = "btnMoveDown"
        Me.btnMoveDown.Size = New System.Drawing.Size(137, 30)
        Me.btnMoveDown.TabIndex = 4
        Me.btnMoveDown.Text = "▼  Move Down"
        Me.hints.SetToolTip(Me.btnMoveDown, "Move the selected images later in the collage")
        Me.btnMoveDown.UseVisualStyleBackColor = False
        '
        'btnMoveUp
        '
        Me.btnMoveUp.BackColor = System.Drawing.Color.FromArgb(CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer))
        Me.btnMoveUp.FlatAppearance.BorderColor = System.Drawing.Color.FromArgb(CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer))
        Me.btnMoveUp.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.btnMoveUp.ForeColor = System.Drawing.Color.White
        Me.btnMoveUp.Location = New System.Drawing.Point(0, 74)
        Me.btnMoveUp.Name = "btnMoveUp"
        Me.btnMoveUp.Size = New System.Drawing.Size(137, 30)
        Me.btnMoveUp.TabIndex = 3
        Me.btnMoveUp.Text = "▲  Move Up"
        Me.hints.SetToolTip(Me.btnMoveUp, "Move the selected images earlier in the collage")
        Me.btnMoveUp.UseVisualStyleBackColor = False
        '
        'btnClear
        '
        Me.btnClear.BackColor = System.Drawing.Color.FromArgb(CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer))
        Me.btnClear.FlatAppearance.BorderColor = System.Drawing.Color.FromArgb(CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer))
        Me.btnClear.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.btnClear.ForeColor = System.Drawing.Color.White
        Me.btnClear.Location = New System.Drawing.Point(143, 38)
        Me.btnClear.Name = "btnClear"
        Me.btnClear.Size = New System.Drawing.Size(137, 30)
        Me.btnClear.TabIndex = 2
        Me.btnClear.Text = "Clear All"
        Me.btnClear.UseVisualStyleBackColor = False
        '
        'btnRemove
        '
        Me.btnRemove.BackColor = System.Drawing.Color.FromArgb(CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer))
        Me.btnRemove.FlatAppearance.BorderColor = System.Drawing.Color.FromArgb(CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer))
        Me.btnRemove.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.btnRemove.ForeColor = System.Drawing.Color.White
        Me.btnRemove.Location = New System.Drawing.Point(0, 38)
        Me.btnRemove.Name = "btnRemove"
        Me.btnRemove.Size = New System.Drawing.Size(137, 30)
        Me.btnRemove.TabIndex = 1
        Me.btnRemove.Text = "Remove"
        Me.hints.SetToolTip(Me.btnRemove, "Remove the selected images (Delete)")
        Me.btnRemove.UseVisualStyleBackColor = False
        '
        'btnAddImages
        '
        Me.btnAddImages.BackColor = System.Drawing.Color.FromArgb(CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer), CType(CType(60, Byte), Integer))
        Me.btnAddImages.FlatAppearance.BorderColor = System.Drawing.Color.FromArgb(CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer))
        Me.btnAddImages.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.btnAddImages.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.btnAddImages.ForeColor = System.Drawing.Color.White
        Me.btnAddImages.Location = New System.Drawing.Point(0, 0)
        Me.btnAddImages.Name = "btnAddImages"
        Me.btnAddImages.Size = New System.Drawing.Size(280, 32)
        Me.btnAddImages.TabIndex = 0
        Me.btnAddImages.Text = "+  Add Images…"
        Me.hints.SetToolTip(Me.btnAddImages, "Add JPG, PNG, BMP, GIF or TIFF images (Ctrl+O)")
        Me.btnAddImages.UseVisualStyleBackColor = False
        '
        'lblImagesHeader
        '
        Me.lblImagesHeader.Dock = System.Windows.Forms.DockStyle.Top
        Me.lblImagesHeader.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblImagesHeader.ForeColor = System.Drawing.Color.Silver
        Me.lblImagesHeader.Location = New System.Drawing.Point(10, 10)
        Me.lblImagesHeader.Name = "lblImagesHeader"
        Me.lblImagesHeader.Size = New System.Drawing.Size(280, 26)
        Me.lblImagesHeader.TabIndex = 0
        Me.lblImagesHeader.Text = "IMAGES"
        '
        'pnlRight
        '
        Me.pnlRight.BackColor = System.Drawing.Color.FromArgb(CType(CType(40, Byte), Integer), CType(CType(40, Byte), Integer), CType(CType(40, Byte), Integer))
        Me.pnlRight.Controls.Add(Me.pnlSettings)
        Me.pnlRight.Controls.Add(Me.pnlSaveArea)
        Me.pnlRight.Dock = System.Windows.Forms.DockStyle.Right
        Me.pnlRight.Location = New System.Drawing.Point(884, 52)
        Me.pnlRight.Name = "pnlRight"
        Me.pnlRight.Padding = New System.Windows.Forms.Padding(10)
        Me.pnlRight.Size = New System.Drawing.Size(300, 681)
        Me.pnlRight.TabIndex = 3
        '
        'pnlSettings
        '
        Me.pnlSettings.AutoScroll = True
        Me.pnlSettings.Controls.Add(Me.lblOutputNote)
        Me.pnlSettings.Controls.Add(Me.nudJpegQuality)
        Me.pnlSettings.Controls.Add(Me.lblJpegQuality)
        Me.pnlSettings.Controls.Add(Me.nudOutputWidth)
        Me.pnlSettings.Controls.Add(Me.lblOutputWidth)
        Me.pnlSettings.Controls.Add(Me.cmbSizePreset)
        Me.pnlSettings.Controls.Add(Me.lblSizePreset)
        Me.pnlSettings.Controls.Add(Me.lblOutputHeader)
        Me.pnlSettings.Controls.Add(Me.btnBorderColor)
        Me.pnlSettings.Controls.Add(Me.lblBorderColor)
        Me.pnlSettings.Controls.Add(Me.nudBorderThickness)
        Me.pnlSettings.Controls.Add(Me.lblBorderThickness)
        Me.pnlSettings.Controls.Add(Me.chkBorder)
        Me.pnlSettings.Controls.Add(Me.btnBackgroundColor)
        Me.pnlSettings.Controls.Add(Me.lblBackground)
        Me.pnlSettings.Controls.Add(Me.nudMargin)
        Me.pnlSettings.Controls.Add(Me.lblMargin)
        Me.pnlSettings.Controls.Add(Me.nudSpacing)
        Me.pnlSettings.Controls.Add(Me.lblSpacing)
        Me.pnlSettings.Controls.Add(Me.lblSpacingHeader)
        Me.pnlSettings.Controls.Add(Me.cmbCellShape)
        Me.pnlSettings.Controls.Add(Me.lblCellShape)
        Me.pnlSettings.Controls.Add(Me.cmbFitMode)
        Me.pnlSettings.Controls.Add(Me.lblFitMode)
        Me.pnlSettings.Controls.Add(Me.nudColumns)
        Me.pnlSettings.Controls.Add(Me.lblColumns)
        Me.pnlSettings.Controls.Add(Me.nudRows)
        Me.pnlSettings.Controls.Add(Me.lblRows)
        Me.pnlSettings.Controls.Add(Me.chkAutoGrid)
        Me.pnlSettings.Controls.Add(Me.lblLayoutHeader)
        Me.pnlSettings.Dock = System.Windows.Forms.DockStyle.Fill
        Me.pnlSettings.Location = New System.Drawing.Point(10, 10)
        Me.pnlSettings.Name = "pnlSettings"
        Me.pnlSettings.Size = New System.Drawing.Size(280, 609)
        Me.pnlSettings.TabIndex = 0
        '
        'lblOutputNote
        '
        Me.lblOutputNote.ForeColor = System.Drawing.Color.Gray
        Me.lblOutputNote.Location = New System.Drawing.Point(0, 510)
        Me.lblOutputNote.Name = "lblOutputNote"
        Me.lblOutputNote.Size = New System.Drawing.Size(260, 36)
        Me.lblOutputNote.TabIndex = 29
        Me.lblOutputNote.Text = "The height is worked out from the grid and the cell shape. JPEG quality only applies to .jpg files."
        '
        'nudJpegQuality
        '
        Me.nudJpegQuality.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.nudJpegQuality.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle
        Me.nudJpegQuality.ForeColor = System.Drawing.Color.White
        Me.nudJpegQuality.Location = New System.Drawing.Point(120, 478)
        Me.nudJpegQuality.Minimum = New Decimal(New Integer() {10, 0, 0, 0})
        Me.nudJpegQuality.Name = "nudJpegQuality"
        Me.nudJpegQuality.Size = New System.Drawing.Size(140, 23)
        Me.nudJpegQuality.TabIndex = 28
        Me.hints.SetToolTip(Me.nudJpegQuality, "Higher means better quality and a bigger file. 85–95 is a good choice.")
        Me.nudJpegQuality.Value = New Decimal(New Integer() {90, 0, 0, 0})
        '
        'lblJpegQuality
        '
        Me.lblJpegQuality.AutoSize = True
        Me.lblJpegQuality.ForeColor = System.Drawing.Color.White
        Me.lblJpegQuality.Location = New System.Drawing.Point(0, 480)
        Me.lblJpegQuality.Name = "lblJpegQuality"
        Me.lblJpegQuality.Size = New System.Drawing.Size(76, 15)
        Me.lblJpegQuality.TabIndex = 27
        Me.lblJpegQuality.Text = "JPEG quality"
        '
        'nudOutputWidth
        '
        Me.nudOutputWidth.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.nudOutputWidth.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle
        Me.nudOutputWidth.ForeColor = System.Drawing.Color.White
        Me.nudOutputWidth.Increment = New Decimal(New Integer() {100, 0, 0, 0})
        Me.nudOutputWidth.Location = New System.Drawing.Point(120, 448)
        Me.nudOutputWidth.Maximum = New Decimal(New Integer() {10000, 0, 0, 0})
        Me.nudOutputWidth.Minimum = New Decimal(New Integer() {200, 0, 0, 0})
        Me.nudOutputWidth.Name = "nudOutputWidth"
        Me.nudOutputWidth.Size = New System.Drawing.Size(140, 23)
        Me.nudOutputWidth.TabIndex = 26
        Me.nudOutputWidth.ThousandsSeparator = True
        Me.hints.SetToolTip(Me.nudOutputWidth, "Width of the saved collage in pixels")
        Me.nudOutputWidth.Value = New Decimal(New Integer() {2000, 0, 0, 0})
        '
        'lblOutputWidth
        '
        Me.lblOutputWidth.AutoSize = True
        Me.lblOutputWidth.ForeColor = System.Drawing.Color.White
        Me.lblOutputWidth.Location = New System.Drawing.Point(0, 450)
        Me.lblOutputWidth.Name = "lblOutputWidth"
        Me.lblOutputWidth.Size = New System.Drawing.Size(63, 15)
        Me.lblOutputWidth.TabIndex = 25
        Me.lblOutputWidth.Text = "Width (px)"
        '
        'cmbSizePreset
        '
        Me.cmbSizePreset.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.cmbSizePreset.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList
        Me.cmbSizePreset.DropDownWidth = 200
        Me.cmbSizePreset.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.cmbSizePreset.ForeColor = System.Drawing.Color.White
        Me.cmbSizePreset.FormattingEnabled = True
        Me.cmbSizePreset.Location = New System.Drawing.Point(120, 417)
        Me.cmbSizePreset.Name = "cmbSizePreset"
        Me.cmbSizePreset.Size = New System.Drawing.Size(140, 23)
        Me.cmbSizePreset.TabIndex = 24
        '
        'lblSizePreset
        '
        Me.lblSizePreset.AutoSize = True
        Me.lblSizePreset.ForeColor = System.Drawing.Color.White
        Me.lblSizePreset.Location = New System.Drawing.Point(0, 420)
        Me.lblSizePreset.Name = "lblSizePreset"
        Me.lblSizePreset.Size = New System.Drawing.Size(62, 15)
        Me.lblSizePreset.TabIndex = 23
        Me.lblSizePreset.Text = "Size preset"
        '
        'lblOutputHeader
        '
        Me.lblOutputHeader.AutoSize = True
        Me.lblOutputHeader.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblOutputHeader.ForeColor = System.Drawing.Color.Silver
        Me.lblOutputHeader.Location = New System.Drawing.Point(0, 392)
        Me.lblOutputHeader.Name = "lblOutputHeader"
        Me.lblOutputHeader.Size = New System.Drawing.Size(55, 15)
        Me.lblOutputHeader.TabIndex = 22
        Me.lblOutputHeader.Text = "OUTPUT"
        '
        'btnBorderColor
        '
        Me.btnBorderColor.BackColor = System.Drawing.Color.Black
        Me.btnBorderColor.FlatAppearance.BorderColor = System.Drawing.Color.FromArgb(CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer))
        Me.btnBorderColor.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.btnBorderColor.ForeColor = System.Drawing.Color.White
        Me.btnBorderColor.Location = New System.Drawing.Point(120, 351)
        Me.btnBorderColor.Name = "btnBorderColor"
        Me.btnBorderColor.Size = New System.Drawing.Size(140, 26)
        Me.btnBorderColor.TabIndex = 21
        Me.btnBorderColor.Text = "Black"
        Me.hints.SetToolTip(Me.btnBorderColor, "Click to choose the border color")
        Me.btnBorderColor.UseVisualStyleBackColor = False
        '
        'lblBorderColor
        '
        Me.lblBorderColor.AutoSize = True
        Me.lblBorderColor.ForeColor = System.Drawing.Color.White
        Me.lblBorderColor.Location = New System.Drawing.Point(0, 356)
        Me.lblBorderColor.Name = "lblBorderColor"
        Me.lblBorderColor.Size = New System.Drawing.Size(73, 15)
        Me.lblBorderColor.TabIndex = 20
        Me.lblBorderColor.Text = "Border color"
        '
        'nudBorderThickness
        '
        Me.nudBorderThickness.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.nudBorderThickness.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle
        Me.nudBorderThickness.ForeColor = System.Drawing.Color.White
        Me.nudBorderThickness.Location = New System.Drawing.Point(120, 321)
        Me.nudBorderThickness.Minimum = New Decimal(New Integer() {1, 0, 0, 0})
        Me.nudBorderThickness.Name = "nudBorderThickness"
        Me.nudBorderThickness.Size = New System.Drawing.Size(140, 23)
        Me.nudBorderThickness.TabIndex = 19
        Me.hints.SetToolTip(Me.nudBorderThickness, "Border thickness in pixels (on the saved collage)")
        Me.nudBorderThickness.Value = New Decimal(New Integer() {4, 0, 0, 0})
        '
        'lblBorderThickness
        '
        Me.lblBorderThickness.AutoSize = True
        Me.lblBorderThickness.ForeColor = System.Drawing.Color.White
        Me.lblBorderThickness.Location = New System.Drawing.Point(0, 323)
        Me.lblBorderThickness.Name = "lblBorderThickness"
        Me.lblBorderThickness.Size = New System.Drawing.Size(96, 15)
        Me.lblBorderThickness.TabIndex = 18
        Me.lblBorderThickness.Text = "Border thickness"
        '
        'chkBorder
        '
        Me.chkBorder.AutoSize = True
        Me.chkBorder.ForeColor = System.Drawing.Color.White
        Me.chkBorder.Location = New System.Drawing.Point(0, 294)
        Me.chkBorder.Name = "chkBorder"
        Me.chkBorder.Size = New System.Drawing.Size(177, 19)
        Me.chkBorder.TabIndex = 17
        Me.chkBorder.Text = "Show borders around images"
        Me.chkBorder.UseVisualStyleBackColor = True
        '
        'btnBackgroundColor
        '
        Me.btnBackgroundColor.BackColor = System.Drawing.Color.White
        Me.btnBackgroundColor.FlatAppearance.BorderColor = System.Drawing.Color.FromArgb(CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer), CType(CType(80, Byte), Integer))
        Me.btnBackgroundColor.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.btnBackgroundColor.ForeColor = System.Drawing.Color.Black
        Me.btnBackgroundColor.Location = New System.Drawing.Point(120, 261)
        Me.btnBackgroundColor.Name = "btnBackgroundColor"
        Me.btnBackgroundColor.Size = New System.Drawing.Size(140, 26)
        Me.btnBackgroundColor.TabIndex = 16
        Me.btnBackgroundColor.Text = "White"
        Me.hints.SetToolTip(Me.btnBackgroundColor, "Click to choose the color behind and between the images")
        Me.btnBackgroundColor.UseVisualStyleBackColor = False
        '
        'lblBackground
        '
        Me.lblBackground.AutoSize = True
        Me.lblBackground.ForeColor = System.Drawing.Color.White
        Me.lblBackground.Location = New System.Drawing.Point(0, 266)
        Me.lblBackground.Name = "lblBackground"
        Me.lblBackground.Size = New System.Drawing.Size(71, 15)
        Me.lblBackground.TabIndex = 15
        Me.lblBackground.Text = "Background"
        '
        'nudMargin
        '
        Me.nudMargin.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.nudMargin.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle
        Me.nudMargin.ForeColor = System.Drawing.Color.White
        Me.nudMargin.Location = New System.Drawing.Point(120, 231)
        Me.nudMargin.Maximum = New Decimal(New Integer() {500, 0, 0, 0})
        Me.nudMargin.Name = "nudMargin"
        Me.nudMargin.Size = New System.Drawing.Size(140, 23)
        Me.nudMargin.TabIndex = 14
        Me.hints.SetToolTip(Me.nudMargin, "Space around the outside of the collage, in pixels")
        Me.nudMargin.Value = New Decimal(New Integer() {10, 0, 0, 0})
        '
        'lblMargin
        '
        Me.lblMargin.AutoSize = True
        Me.lblMargin.ForeColor = System.Drawing.Color.White
        Me.lblMargin.Location = New System.Drawing.Point(0, 233)
        Me.lblMargin.Name = "lblMargin"
        Me.lblMargin.Size = New System.Drawing.Size(79, 15)
        Me.lblMargin.TabIndex = 13
        Me.lblMargin.Text = "Outer margin"
        '
        'nudSpacing
        '
        Me.nudSpacing.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.nudSpacing.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle
        Me.nudSpacing.ForeColor = System.Drawing.Color.White
        Me.nudSpacing.Location = New System.Drawing.Point(120, 201)
        Me.nudSpacing.Maximum = New Decimal(New Integer() {500, 0, 0, 0})
        Me.nudSpacing.Name = "nudSpacing"
        Me.nudSpacing.Size = New System.Drawing.Size(140, 23)
        Me.nudSpacing.TabIndex = 12
        Me.hints.SetToolTip(Me.nudSpacing, "Space between neighbouring images, in pixels")
        Me.nudSpacing.Value = New Decimal(New Integer() {10, 0, 0, 0})
        '
        'lblSpacing
        '
        Me.lblSpacing.AutoSize = True
        Me.lblSpacing.ForeColor = System.Drawing.Color.White
        Me.lblSpacing.Location = New System.Drawing.Point(0, 203)
        Me.lblSpacing.Name = "lblSpacing"
        Me.lblSpacing.Size = New System.Drawing.Size(114, 15)
        Me.lblSpacing.TabIndex = 11
        Me.lblSpacing.Text = "Gap between images"
        '
        'lblSpacingHeader
        '
        Me.lblSpacingHeader.AutoSize = True
        Me.lblSpacingHeader.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblSpacingHeader.ForeColor = System.Drawing.Color.Silver
        Me.lblSpacingHeader.Location = New System.Drawing.Point(0, 176)
        Me.lblSpacingHeader.Name = "lblSpacingHeader"
        Me.lblSpacingHeader.Size = New System.Drawing.Size(155, 15)
        Me.lblSpacingHeader.TabIndex = 10
        Me.lblSpacingHeader.Text = "SPACING AND BORDERS"
        '
        'cmbCellShape
        '
        Me.cmbCellShape.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.cmbCellShape.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList
        Me.cmbCellShape.DropDownWidth = 200
        Me.cmbCellShape.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.cmbCellShape.ForeColor = System.Drawing.Color.White
        Me.cmbCellShape.FormattingEnabled = True
        Me.cmbCellShape.Location = New System.Drawing.Point(120, 138)
        Me.cmbCellShape.Name = "cmbCellShape"
        Me.cmbCellShape.Size = New System.Drawing.Size(140, 23)
        Me.cmbCellShape.TabIndex = 9
        Me.hints.SetToolTip(Me.cmbCellShape, "The shape of every cell in the grid")
        '
        'lblCellShape
        '
        Me.lblCellShape.AutoSize = True
        Me.lblCellShape.ForeColor = System.Drawing.Color.White
        Me.lblCellShape.Location = New System.Drawing.Point(0, 141)
        Me.lblCellShape.Name = "lblCellShape"
        Me.lblCellShape.Size = New System.Drawing.Size(62, 15)
        Me.lblCellShape.TabIndex = 8
        Me.lblCellShape.Text = "Cell shape"
        '
        'cmbFitMode
        '
        Me.cmbFitMode.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.cmbFitMode.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList
        Me.cmbFitMode.DropDownWidth = 200
        Me.cmbFitMode.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.cmbFitMode.ForeColor = System.Drawing.Color.White
        Me.cmbFitMode.FormattingEnabled = True
        Me.cmbFitMode.Location = New System.Drawing.Point(120, 108)
        Me.cmbFitMode.Name = "cmbFitMode"
        Me.cmbFitMode.Size = New System.Drawing.Size(140, 23)
        Me.cmbFitMode.TabIndex = 7
        Me.hints.SetToolTip(Me.cmbFitMode, "Fill: every cell is completely covered (edges may be cropped)." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "Fit: the whole image is shown (the background may show around it).")
        '
        'lblFitMode
        '
        Me.lblFitMode.AutoSize = True
        Me.lblFitMode.ForeColor = System.Drawing.Color.White
        Me.lblFitMode.Location = New System.Drawing.Point(0, 111)
        Me.lblFitMode.Name = "lblFitMode"
        Me.lblFitMode.Size = New System.Drawing.Size(54, 15)
        Me.lblFitMode.TabIndex = 6
        Me.lblFitMode.Text = "Image fit"
        '
        'nudColumns
        '
        Me.nudColumns.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.nudColumns.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle
        Me.nudColumns.ForeColor = System.Drawing.Color.White
        Me.nudColumns.Location = New System.Drawing.Point(120, 78)
        Me.nudColumns.Maximum = New Decimal(New Integer() {50, 0, 0, 0})
        Me.nudColumns.Minimum = New Decimal(New Integer() {1, 0, 0, 0})
        Me.nudColumns.Name = "nudColumns"
        Me.nudColumns.Size = New System.Drawing.Size(140, 23)
        Me.nudColumns.TabIndex = 5
        Me.nudColumns.Value = New Decimal(New Integer() {2, 0, 0, 0})
        '
        'lblColumns
        '
        Me.lblColumns.AutoSize = True
        Me.lblColumns.ForeColor = System.Drawing.Color.White
        Me.lblColumns.Location = New System.Drawing.Point(0, 80)
        Me.lblColumns.Name = "lblColumns"
        Me.lblColumns.Size = New System.Drawing.Size(55, 15)
        Me.lblColumns.TabIndex = 4
        Me.lblColumns.Text = "Columns"
        '
        'nudRows
        '
        Me.nudRows.BackColor = System.Drawing.Color.FromArgb(CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer), CType(CType(55, Byte), Integer))
        Me.nudRows.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle
        Me.nudRows.ForeColor = System.Drawing.Color.White
        Me.nudRows.Location = New System.Drawing.Point(120, 48)
        Me.nudRows.Maximum = New Decimal(New Integer() {50, 0, 0, 0})
        Me.nudRows.Minimum = New Decimal(New Integer() {1, 0, 0, 0})
        Me.nudRows.Name = "nudRows"
        Me.nudRows.Size = New System.Drawing.Size(140, 23)
        Me.nudRows.TabIndex = 3
        Me.nudRows.Value = New Decimal(New Integer() {2, 0, 0, 0})
        '
        'lblRows
        '
        Me.lblRows.AutoSize = True
        Me.lblRows.ForeColor = System.Drawing.Color.White
        Me.lblRows.Location = New System.Drawing.Point(0, 50)
        Me.lblRows.Name = "lblRows"
        Me.lblRows.Size = New System.Drawing.Size(35, 15)
        Me.lblRows.TabIndex = 2
        Me.lblRows.Text = "Rows"
        '
        'chkAutoGrid
        '
        Me.chkAutoGrid.AutoSize = True
        Me.chkAutoGrid.Checked = True
        Me.chkAutoGrid.CheckState = System.Windows.Forms.CheckState.Checked
        Me.chkAutoGrid.ForeColor = System.Drawing.Color.White
        Me.chkAutoGrid.Location = New System.Drawing.Point(0, 22)
        Me.chkAutoGrid.Name = "chkAutoGrid"
        Me.chkAutoGrid.Size = New System.Drawing.Size(222, 19)
        Me.chkAutoGrid.TabIndex = 1
        Me.chkAutoGrid.Text = "Choose rows and columns for me"
        Me.hints.SetToolTip(Me.chkAutoGrid, "Pick a grid that fits the number of images. Untick to set rows and columns yourself.")
        Me.chkAutoGrid.UseVisualStyleBackColor = True
        '
        'lblLayoutHeader
        '
        Me.lblLayoutHeader.AutoSize = True
        Me.lblLayoutHeader.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblLayoutHeader.ForeColor = System.Drawing.Color.Silver
        Me.lblLayoutHeader.Location = New System.Drawing.Point(0, 0)
        Me.lblLayoutHeader.Name = "lblLayoutHeader"
        Me.lblLayoutHeader.Size = New System.Drawing.Size(52, 15)
        Me.lblLayoutHeader.TabIndex = 0
        Me.lblLayoutHeader.Text = "LAYOUT"
        '
        'pnlSaveArea
        '
        Me.pnlSaveArea.Controls.Add(Me.btnSave)
        Me.pnlSaveArea.Dock = System.Windows.Forms.DockStyle.Bottom
        Me.pnlSaveArea.Location = New System.Drawing.Point(10, 619)
        Me.pnlSaveArea.Name = "pnlSaveArea"
        Me.pnlSaveArea.Padding = New System.Windows.Forms.Padding(0, 10, 0, 0)
        Me.pnlSaveArea.Size = New System.Drawing.Size(280, 52)
        Me.pnlSaveArea.TabIndex = 1
        '
        'btnSave
        '
        Me.btnSave.BackColor = System.Drawing.Color.FromArgb(CType(CType(0, Byte), Integer), CType(CType(120, Byte), Integer), CType(CType(212, Byte), Integer))
        Me.btnSave.Dock = System.Windows.Forms.DockStyle.Fill
        Me.btnSave.FlatAppearance.BorderSize = 0
        Me.btnSave.FlatStyle = System.Windows.Forms.FlatStyle.Flat
        Me.btnSave.Font = New System.Drawing.Font("Segoe UI", 10.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.btnSave.ForeColor = System.Drawing.Color.White
        Me.btnSave.Location = New System.Drawing.Point(0, 10)
        Me.btnSave.Name = "btnSave"
        Me.btnSave.Size = New System.Drawing.Size(280, 42)
        Me.btnSave.TabIndex = 0
        Me.btnSave.Text = "Save Collage…"
        Me.hints.SetToolTip(Me.btnSave, "Save the full-size collage as JPG, PNG or BMP (Ctrl+S)")
        Me.btnSave.UseVisualStyleBackColor = False
        '
        'pnlPreview
        '
        Me.pnlPreview.BackColor = System.Drawing.Color.FromArgb(CType(CType(28, Byte), Integer), CType(CType(28, Byte), Integer), CType(CType(28, Byte), Integer))
        Me.pnlPreview.Controls.Add(Me.picPreview)
        Me.pnlPreview.Controls.Add(Me.lblEmpty)
        Me.pnlPreview.Dock = System.Windows.Forms.DockStyle.Fill
        Me.pnlPreview.Location = New System.Drawing.Point(300, 52)
        Me.pnlPreview.Name = "pnlPreview"
        Me.pnlPreview.Padding = New System.Windows.Forms.Padding(16)
        Me.pnlPreview.Size = New System.Drawing.Size(584, 681)
        Me.pnlPreview.TabIndex = 2
        '
        'picPreview
        '
        Me.picPreview.Dock = System.Windows.Forms.DockStyle.Fill
        Me.picPreview.Location = New System.Drawing.Point(16, 16)
        Me.picPreview.Name = "picPreview"
        Me.picPreview.Size = New System.Drawing.Size(552, 649)
        Me.picPreview.SizeMode = System.Windows.Forms.PictureBoxSizeMode.Zoom
        Me.picPreview.TabIndex = 0
        Me.picPreview.TabStop = False
        Me.picPreview.Visible = False
        '
        'lblEmpty
        '
        Me.lblEmpty.Dock = System.Windows.Forms.DockStyle.Fill
        Me.lblEmpty.Font = New System.Drawing.Font("Segoe UI", 12.0!, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblEmpty.ForeColor = System.Drawing.Color.Gray
        Me.lblEmpty.Location = New System.Drawing.Point(16, 16)
        Me.lblEmpty.Name = "lblEmpty"
        Me.lblEmpty.Size = New System.Drawing.Size(552, 649)
        Me.lblEmpty.TabIndex = 1
        Me.lblEmpty.Text = "Your collage preview will appear here." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "Click ""Add Images…"" or drag photos onto the window."
        Me.lblEmpty.TextAlign = System.Drawing.ContentAlignment.MiddleCenter
        '
        'previewTimer
        '
        Me.previewTimer.Interval = 150
        '
        'colorPicker
        '
        Me.colorPicker.AnyColor = True
        Me.colorPicker.FullOpen = True
        '
        'openImagesDialog
        '
        Me.openImagesDialog.Filter = "Images|*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff"
        Me.openImagesDialog.Multiselect = True
        Me.openImagesDialog.Title = "Add images to the collage"
        '
        'saveCollageDialog
        '
        Me.saveCollageDialog.DefaultExt = "jpg"
        Me.saveCollageDialog.FileName = "PixelBlend collage"
        Me.saveCollageDialog.Filter = "JPEG image (*.jpg)|*.jpg;*.jpeg|PNG image (*.png)|*.png|Bitmap image (*.bmp)|*.bmp"
        Me.saveCollageDialog.Title = "Save collage"
        '
        'Form1
        '
        Me.AutoScaleDimensions = New System.Drawing.SizeF(7.0!, 15.0!)
        Me.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font
        Me.BackColor = System.Drawing.Color.FromArgb(CType(CType(32, Byte), Integer), CType(CType(32, Byte), Integer), CType(CType(32, Byte), Integer))
        Me.ClientSize = New System.Drawing.Size(1184, 761)
        Me.Controls.Add(Me.pnlPreview)
        Me.Controls.Add(Me.pnlRight)
        Me.Controls.Add(Me.pnlLeft)
        Me.Controls.Add(Me.pnlStatus)
        Me.Controls.Add(Me.pnlHeader)
        Me.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.ForeColor = System.Drawing.Color.White
        Me.Icon = CType(resources.GetObject("$this.Icon"), System.Drawing.Icon)
        Me.KeyPreview = True
        Me.MinimumSize = New System.Drawing.Size(1000, 640)
        Me.Name = "Form1"
        Me.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen
        Me.Text = "PixelBlend — Collage Maker"
        Me.pnlHeader.ResumeLayout(False)
        Me.pnlHeader.PerformLayout()
        Me.pnlStatus.ResumeLayout(False)
        Me.pnlLeft.ResumeLayout(False)
        Me.pnlImageButtons.ResumeLayout(False)
        Me.pnlRight.ResumeLayout(False)
        Me.pnlSettings.ResumeLayout(False)
        Me.pnlSettings.PerformLayout()
        CType(Me.nudJpegQuality, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudOutputWidth, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudBorderThickness, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudMargin, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudSpacing, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudColumns, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudRows, System.ComponentModel.ISupportInitialize).EndInit()
        Me.pnlSaveArea.ResumeLayout(False)
        Me.pnlPreview.ResumeLayout(False)
        CType(Me.picPreview, System.ComponentModel.ISupportInitialize).EndInit()
        Me.ResumeLayout(False)

    End Sub

    Friend WithEvents pnlHeader As Panel
    Friend WithEvents lblTagline As Label
    Friend WithEvents lblVersion As Label
    Friend WithEvents lblTitle As Label
    Friend WithEvents pnlStatus As Panel
    Friend WithEvents lblStatus As Label
    Friend WithEvents lblCanvasInfo As Label
    Friend WithEvents pnlLeft As Panel
    Friend WithEvents lvImages As ListView
    Friend WithEvents colImage As ColumnHeader
    Friend WithEvents imgThumbs As ImageList
    Friend WithEvents lblDropHint As Label
    Friend WithEvents pnlImageButtons As Panel
    Friend WithEvents btnMoveDown As Button
    Friend WithEvents btnMoveUp As Button
    Friend WithEvents btnClear As Button
    Friend WithEvents btnRemove As Button
    Friend WithEvents btnAddImages As Button
    Friend WithEvents lblImagesHeader As Label
    Friend WithEvents pnlRight As Panel
    Friend WithEvents pnlSettings As Panel
    Friend WithEvents lblOutputNote As Label
    Friend WithEvents nudJpegQuality As NumericUpDown
    Friend WithEvents lblJpegQuality As Label
    Friend WithEvents nudOutputWidth As NumericUpDown
    Friend WithEvents lblOutputWidth As Label
    Friend WithEvents cmbSizePreset As ComboBox
    Friend WithEvents lblSizePreset As Label
    Friend WithEvents lblOutputHeader As Label
    Friend WithEvents btnBorderColor As Button
    Friend WithEvents lblBorderColor As Label
    Friend WithEvents nudBorderThickness As NumericUpDown
    Friend WithEvents lblBorderThickness As Label
    Friend WithEvents chkBorder As CheckBox
    Friend WithEvents btnBackgroundColor As Button
    Friend WithEvents lblBackground As Label
    Friend WithEvents nudMargin As NumericUpDown
    Friend WithEvents lblMargin As Label
    Friend WithEvents nudSpacing As NumericUpDown
    Friend WithEvents lblSpacing As Label
    Friend WithEvents lblSpacingHeader As Label
    Friend WithEvents cmbCellShape As ComboBox
    Friend WithEvents lblCellShape As Label
    Friend WithEvents cmbFitMode As ComboBox
    Friend WithEvents lblFitMode As Label
    Friend WithEvents nudColumns As NumericUpDown
    Friend WithEvents lblColumns As Label
    Friend WithEvents nudRows As NumericUpDown
    Friend WithEvents lblRows As Label
    Friend WithEvents chkAutoGrid As CheckBox
    Friend WithEvents lblLayoutHeader As Label
    Friend WithEvents pnlSaveArea As Panel
    Friend WithEvents btnSave As Button
    Friend WithEvents pnlPreview As Panel
    Friend WithEvents picPreview As PictureBox
    Friend WithEvents lblEmpty As Label
    Friend WithEvents previewTimer As Timer
    Friend WithEvents hints As ToolTip
    Friend WithEvents colorPicker As ColorDialog
    Friend WithEvents openImagesDialog As OpenFileDialog
    Friend WithEvents saveCollageDialog As SaveFileDialog
End Class
