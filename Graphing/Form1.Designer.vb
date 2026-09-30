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
        Me.menuMain = New System.Windows.Forms.MenuStrip()
        Me.mnuFile = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuOpenImage = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuPasteImage = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuSep1 = New System.Windows.Forms.ToolStripSeparator()
        Me.mnuOpenProject = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuSaveProject = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuSaveProjectAs = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuSep2 = New System.Windows.Forms.ToolStripSeparator()
        Me.mnuExportCsv = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuExportExcel = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuSep3 = New System.Windows.Forms.ToolStripSeparator()
        Me.mnuExit = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuEdit = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuUndo = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuRedo = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuSep4 = New System.Windows.Forms.ToolStripSeparator()
        Me.mnuCopyData = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuDeletePoint = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuView = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuZoomIn = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuZoomOut = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuFitWindow = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuSep5 = New System.Windows.Forms.ToolStripSeparator()
        Me.mnuMagnifier = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuGrid = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuLines = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuHelp = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuHowTo = New System.Windows.Forms.ToolStripMenuItem()
        Me.mnuAbout = New System.Windows.Forms.ToolStripMenuItem()
        Me.toolMain = New System.Windows.Forms.ToolStrip()
        Me.tsOpenImage = New System.Windows.Forms.ToolStripButton()
        Me.tsPaste = New System.Windows.Forms.ToolStripButton()
        Me.tsSep6 = New System.Windows.Forms.ToolStripSeparator()
        Me.tsOpenProject = New System.Windows.Forms.ToolStripButton()
        Me.tsSaveProject = New System.Windows.Forms.ToolStripButton()
        Me.tsSep7 = New System.Windows.Forms.ToolStripSeparator()
        Me.tsUndo = New System.Windows.Forms.ToolStripButton()
        Me.tsRedo = New System.Windows.Forms.ToolStripButton()
        Me.tsSep8 = New System.Windows.Forms.ToolStripSeparator()
        Me.tsZoomIn = New System.Windows.Forms.ToolStripButton()
        Me.tsZoomOut = New System.Windows.Forms.ToolStripButton()
        Me.tsFit = New System.Windows.Forms.ToolStripButton()
        Me.tsSep9 = New System.Windows.Forms.ToolStripSeparator()
        Me.tsMagnifier = New System.Windows.Forms.ToolStripButton()
        Me.tsGrid = New System.Windows.Forms.ToolStripButton()
        Me.statusMain = New System.Windows.Forms.StatusStrip()
        Me.lblStatus = New System.Windows.Forms.ToolStripStatusLabel()
        Me.lblZoom = New System.Windows.Forms.ToolStripStatusLabel()
        Me.lblCoords = New System.Windows.Forms.ToolStripStatusLabel()
        Me.canvas = New Graphing.GraphCanvas()
        Me.pnlSide = New System.Windows.Forms.Panel()
        Me.tabsMain = New System.Windows.Forms.TabControl()
        Me.tabCalibrate = New System.Windows.Forms.TabPage()
        Me.tabTrace = New System.Windows.Forms.TabPage()
        Me.tabData = New System.Windows.Forms.TabPage()
        Me.tabAnalyze = New System.Windows.Forms.TabPage()
        Me.lblCalHelp = New System.Windows.Forms.Label()
        Me.lblHdrPoint = New System.Windows.Forms.Label()
        Me.lblHdrPixel = New System.Windows.Forms.Label()
        Me.lblHdrValue = New System.Windows.Forms.Label()
        Me.btnPickX1 = New System.Windows.Forms.Button()
        Me.lblPixelX1 = New System.Windows.Forms.Label()
        Me.txtValueX1 = New System.Windows.Forms.TextBox()
        Me.btnPickX2 = New System.Windows.Forms.Button()
        Me.lblPixelX2 = New System.Windows.Forms.Label()
        Me.txtValueX2 = New System.Windows.Forms.TextBox()
        Me.btnPickY1 = New System.Windows.Forms.Button()
        Me.lblPixelY1 = New System.Windows.Forms.Label()
        Me.txtValueY1 = New System.Windows.Forms.TextBox()
        Me.btnPickY2 = New System.Windows.Forms.Button()
        Me.lblPixelY2 = New System.Windows.Forms.Label()
        Me.txtValueY2 = New System.Windows.Forms.TextBox()
        Me.chkLogX = New System.Windows.Forms.CheckBox()
        Me.chkLogY = New System.Windows.Forms.CheckBox()
        Me.lblCalState = New System.Windows.Forms.Label()
        Me.btnClearCalibration = New System.Windows.Forms.Button()
        Me.lblCalTip = New System.Windows.Forms.Label()
        Me.lblSeriesHeader = New System.Windows.Forms.Label()
        Me.lstSeries = New System.Windows.Forms.ListBox()
        Me.btnAddSeries = New System.Windows.Forms.Button()
        Me.btnRenameSeries = New System.Windows.Forms.Button()
        Me.btnColorSeries = New System.Windows.Forms.Button()
        Me.btnDeleteSeries = New System.Windows.Forms.Button()
        Me.grpTool = New System.Windows.Forms.GroupBox()
        Me.rbPan = New System.Windows.Forms.RadioButton()
        Me.rbAddPoints = New System.Windows.Forms.RadioButton()
        Me.rbDragTrace = New System.Windows.Forms.RadioButton()
        Me.nudDragSpacing = New System.Windows.Forms.NumericUpDown()
        Me.lblDragSpacingUnit = New System.Windows.Forms.Label()
        Me.rbEditPoints = New System.Windows.Forms.RadioButton()
        Me.rbAutoTrace = New System.Windows.Forms.RadioButton()
        Me.grpAuto = New System.Windows.Forms.GroupBox()
        Me.lblTolerance = New System.Windows.Forms.Label()
        Me.nudTolerance = New System.Windows.Forms.NumericUpDown()
        Me.lblAutoStep = New System.Windows.Forms.Label()
        Me.nudAutoStep = New System.Windows.Forms.NumericUpDown()
        Me.lblAutoStepUnit = New System.Windows.Forms.Label()
        Me.lblAutoColorCaption = New System.Windows.Forms.Label()
        Me.pnlAutoColor = New System.Windows.Forms.Panel()
        Me.chkShowLines = New System.Windows.Forms.CheckBox()
        Me.btnClearSeries = New System.Windows.Forms.Button()
        Me.lblTraceTip = New System.Windows.Forms.Label()
        Me.lblDataSeries = New System.Windows.Forms.Label()
        Me.dgvData = New System.Windows.Forms.DataGridView()
        Me.grpClean = New System.Windows.Forms.GroupBox()
        Me.btnSortX = New System.Windows.Forms.Button()
        Me.btnRemoveDuplicates = New System.Windows.Forms.Button()
        Me.lblResampleCount = New System.Windows.Forms.Label()
        Me.nudResampleCount = New System.Windows.Forms.NumericUpDown()
        Me.btnResampleCount = New System.Windows.Forms.Button()
        Me.lblResampleStep = New System.Windows.Forms.Label()
        Me.txtResampleStep = New System.Windows.Forms.TextBox()
        Me.btnResampleStep = New System.Windows.Forms.Button()
        Me.grpExport = New System.Windows.Forms.GroupBox()
        Me.btnCopyData = New System.Windows.Forms.Button()
        Me.btnExportCsv = New System.Windows.Forms.Button()
        Me.btnExportExcel = New System.Windows.Forms.Button()
        Me.lblAnalyzeSeries = New System.Windows.Forms.Label()
        Me.grpStats = New System.Windows.Forms.GroupBox()
        Me.lblStats = New System.Windows.Forms.Label()
        Me.grpInterpolate = New System.Windows.Forms.GroupBox()
        Me.lblInterpolateX = New System.Windows.Forms.Label()
        Me.txtInterpolateX = New System.Windows.Forms.TextBox()
        Me.btnInterpolate = New System.Windows.Forms.Button()
        Me.lblInterpolateResult = New System.Windows.Forms.Label()
        Me.grpFit = New System.Windows.Forms.GroupBox()
        Me.cmbFitKind = New System.Windows.Forms.ComboBox()
        Me.btnFit = New System.Windows.Forms.Button()
        Me.chkShowFit = New System.Windows.Forms.CheckBox()
        Me.txtFitResult = New System.Windows.Forms.TextBox()
        Me.lblAnalyzeTip = New System.Windows.Forms.Label()
        Me.colIndex = New System.Windows.Forms.DataGridViewTextBoxColumn()
        Me.colX = New System.Windows.Forms.DataGridViewTextBoxColumn()
        Me.colY = New System.Windows.Forms.DataGridViewTextBoxColumn()
        Me.dlgOpenImage = New System.Windows.Forms.OpenFileDialog()
        Me.dlgOpenProject = New System.Windows.Forms.OpenFileDialog()
        Me.dlgSaveProject = New System.Windows.Forms.SaveFileDialog()
        Me.dlgExport = New System.Windows.Forms.SaveFileDialog()
        Me.dlgColor = New System.Windows.Forms.ColorDialog()
        Me.hints = New System.Windows.Forms.ToolTip(Me.components)
        Me.errValues = New System.Windows.Forms.ErrorProvider(Me.components)
        Me.menuMain.SuspendLayout()
        Me.toolMain.SuspendLayout()
        Me.statusMain.SuspendLayout()
        Me.pnlSide.SuspendLayout()
        Me.tabsMain.SuspendLayout()
        Me.tabCalibrate.SuspendLayout()
        Me.tabTrace.SuspendLayout()
        Me.tabData.SuspendLayout()
        Me.tabAnalyze.SuspendLayout()
        Me.grpTool.SuspendLayout()
        Me.grpAuto.SuspendLayout()
        Me.pnlAutoColor.SuspendLayout()
        Me.grpClean.SuspendLayout()
        Me.grpExport.SuspendLayout()
        Me.grpStats.SuspendLayout()
        Me.grpInterpolate.SuspendLayout()
        Me.grpFit.SuspendLayout()
        CType(Me.nudDragSpacing, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudTolerance, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudAutoStep, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.dgvData, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.nudResampleCount, System.ComponentModel.ISupportInitialize).BeginInit()
        CType(Me.errValues, System.ComponentModel.ISupportInitialize).BeginInit()
        Me.SuspendLayout()
        '
        'menuMain
        '
        Me.menuMain.Items.AddRange(New System.Windows.Forms.ToolStripItem() {Me.mnuFile, Me.mnuEdit, Me.mnuView, Me.mnuHelp})
        Me.menuMain.Location = New System.Drawing.Point(0, 0)
        Me.menuMain.Name = "menuMain"
        Me.menuMain.Size = New System.Drawing.Size(1280, 24)
        Me.menuMain.TabIndex = 4
        '
        'mnuFile
        '
        Me.mnuFile.DropDownItems.AddRange(New System.Windows.Forms.ToolStripItem() {Me.mnuOpenImage, Me.mnuPasteImage, Me.mnuSep1, Me.mnuOpenProject, Me.mnuSaveProject, Me.mnuSaveProjectAs, Me.mnuSep2, Me.mnuExportCsv, Me.mnuExportExcel, Me.mnuSep3, Me.mnuExit})
        Me.mnuFile.Name = "mnuFile"
        Me.mnuFile.Size = New System.Drawing.Size(40, 22)
        Me.mnuFile.Text = "&File"
        '
        'mnuOpenImage
        '
        Me.mnuOpenImage.Name = "mnuOpenImage"
        Me.mnuOpenImage.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.O), System.Windows.Forms.Keys)
        Me.mnuOpenImage.Size = New System.Drawing.Size(250, 22)
        Me.mnuOpenImage.Text = "&Open Image…"
        '
        'mnuPasteImage
        '
        Me.mnuPasteImage.Name = "mnuPasteImage"
        Me.mnuPasteImage.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.V), System.Windows.Forms.Keys)
        Me.mnuPasteImage.Size = New System.Drawing.Size(250, 22)
        Me.mnuPasteImage.Text = "&Paste Image"
        '
        'mnuSep1
        '
        Me.mnuSep1.Name = "mnuSep1"
        Me.mnuSep1.Size = New System.Drawing.Size(250, 6)
        '
        'mnuOpenProject
        '
        Me.mnuOpenProject.Name = "mnuOpenProject"
        Me.mnuOpenProject.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.Shift Or System.Windows.Forms.Keys.O), System.Windows.Forms.Keys)
        Me.mnuOpenProject.Size = New System.Drawing.Size(250, 22)
        Me.mnuOpenProject.Text = "Open P&roject…"
        '
        'mnuSaveProject
        '
        Me.mnuSaveProject.Name = "mnuSaveProject"
        Me.mnuSaveProject.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.S), System.Windows.Forms.Keys)
        Me.mnuSaveProject.Size = New System.Drawing.Size(250, 22)
        Me.mnuSaveProject.Text = "&Save Project"
        '
        'mnuSaveProjectAs
        '
        Me.mnuSaveProjectAs.Name = "mnuSaveProjectAs"
        Me.mnuSaveProjectAs.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.Shift Or System.Windows.Forms.Keys.S), System.Windows.Forms.Keys)
        Me.mnuSaveProjectAs.Size = New System.Drawing.Size(250, 22)
        Me.mnuSaveProjectAs.Text = "Save Project &As…"
        '
        'mnuSep2
        '
        Me.mnuSep2.Name = "mnuSep2"
        Me.mnuSep2.Size = New System.Drawing.Size(250, 6)
        '
        'mnuExportCsv
        '
        Me.mnuExportCsv.Name = "mnuExportCsv"
        Me.mnuExportCsv.Size = New System.Drawing.Size(250, 22)
        Me.mnuExportCsv.Text = "Export All Curves as &CSV…"
        '
        'mnuExportExcel
        '
        Me.mnuExportExcel.Name = "mnuExportExcel"
        Me.mnuExportExcel.Size = New System.Drawing.Size(250, 22)
        Me.mnuExportExcel.Text = "Export All Curves as &Excel…"
        '
        'mnuSep3
        '
        Me.mnuSep3.Name = "mnuSep3"
        Me.mnuSep3.Size = New System.Drawing.Size(250, 6)
        '
        'mnuExit
        '
        Me.mnuExit.Name = "mnuExit"
        Me.mnuExit.Size = New System.Drawing.Size(250, 22)
        Me.mnuExit.Text = "E&xit"
        '
        'mnuEdit
        '
        Me.mnuEdit.DropDownItems.AddRange(New System.Windows.Forms.ToolStripItem() {Me.mnuUndo, Me.mnuRedo, Me.mnuSep4, Me.mnuCopyData, Me.mnuDeletePoint})
        Me.mnuEdit.Name = "mnuEdit"
        Me.mnuEdit.Size = New System.Drawing.Size(40, 22)
        Me.mnuEdit.Text = "&Edit"
        '
        'mnuUndo
        '
        Me.mnuUndo.Name = "mnuUndo"
        Me.mnuUndo.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.Z), System.Windows.Forms.Keys)
        Me.mnuUndo.Size = New System.Drawing.Size(250, 22)
        Me.mnuUndo.Text = "&Undo"
        '
        'mnuRedo
        '
        Me.mnuRedo.Name = "mnuRedo"
        Me.mnuRedo.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.Y), System.Windows.Forms.Keys)
        Me.mnuRedo.Size = New System.Drawing.Size(250, 22)
        Me.mnuRedo.Text = "&Redo"
        '
        'mnuSep4
        '
        Me.mnuSep4.Name = "mnuSep4"
        Me.mnuSep4.Size = New System.Drawing.Size(250, 6)
        '
        'mnuCopyData
        '
        Me.mnuCopyData.Name = "mnuCopyData"
        Me.mnuCopyData.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.Shift Or System.Windows.Forms.Keys.C), System.Windows.Forms.Keys)
        Me.mnuCopyData.Size = New System.Drawing.Size(250, 22)
        Me.mnuCopyData.Text = "&Copy Curve Data"
        '
        'mnuDeletePoint
        '
        Me.mnuDeletePoint.Name = "mnuDeletePoint"
        Me.mnuDeletePoint.ShortcutKeyDisplayString = "Del"
        Me.mnuDeletePoint.Size = New System.Drawing.Size(250, 22)
        Me.mnuDeletePoint.Text = "&Delete Selected Point"
        '
        'mnuView
        '
        Me.mnuView.DropDownItems.AddRange(New System.Windows.Forms.ToolStripItem() {Me.mnuZoomIn, Me.mnuZoomOut, Me.mnuFitWindow, Me.mnuSep5, Me.mnuMagnifier, Me.mnuGrid, Me.mnuLines})
        Me.mnuView.Name = "mnuView"
        Me.mnuView.Size = New System.Drawing.Size(40, 22)
        Me.mnuView.Text = "&View"
        '
        'mnuZoomIn
        '
        Me.mnuZoomIn.Name = "mnuZoomIn"
        Me.mnuZoomIn.ShortcutKeyDisplayString = "Ctrl++"
        Me.mnuZoomIn.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.Oemplus), System.Windows.Forms.Keys)
        Me.mnuZoomIn.Size = New System.Drawing.Size(250, 22)
        Me.mnuZoomIn.Text = "Zoom &In"
        '
        'mnuZoomOut
        '
        Me.mnuZoomOut.Name = "mnuZoomOut"
        Me.mnuZoomOut.ShortcutKeyDisplayString = "Ctrl+-"
        Me.mnuZoomOut.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.OemMinus), System.Windows.Forms.Keys)
        Me.mnuZoomOut.Size = New System.Drawing.Size(250, 22)
        Me.mnuZoomOut.Text = "Zoom &Out"
        '
        'mnuFitWindow
        '
        Me.mnuFitWindow.Name = "mnuFitWindow"
        Me.mnuFitWindow.ShortcutKeyDisplayString = "Ctrl+0"
        Me.mnuFitWindow.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.D0), System.Windows.Forms.Keys)
        Me.mnuFitWindow.Size = New System.Drawing.Size(250, 22)
        Me.mnuFitWindow.Text = "&Fit to Window"
        '
        'mnuSep5
        '
        Me.mnuSep5.Name = "mnuSep5"
        Me.mnuSep5.Size = New System.Drawing.Size(250, 6)
        '
        'mnuMagnifier
        '
        Me.mnuMagnifier.CheckOnClick = True
        Me.mnuMagnifier.Name = "mnuMagnifier"
        Me.mnuMagnifier.Size = New System.Drawing.Size(250, 22)
        Me.mnuMagnifier.Text = "&Magnifier"
        '
        'mnuGrid
        '
        Me.mnuGrid.CheckOnClick = True
        Me.mnuGrid.Name = "mnuGrid"
        Me.mnuGrid.ShortcutKeys = CType((System.Windows.Forms.Keys.Control Or System.Windows.Forms.Keys.G), System.Windows.Forms.Keys)
        Me.mnuGrid.Size = New System.Drawing.Size(250, 22)
        Me.mnuGrid.Text = "Calibrated &Grid"
        '
        'mnuLines
        '
        Me.mnuLines.CheckOnClick = True
        Me.mnuLines.Name = "mnuLines"
        Me.mnuLines.Size = New System.Drawing.Size(250, 22)
        Me.mnuLines.Text = "Connecting &Lines"
        '
        'mnuHelp
        '
        Me.mnuHelp.DropDownItems.AddRange(New System.Windows.Forms.ToolStripItem() {Me.mnuHowTo, Me.mnuAbout})
        Me.mnuHelp.Name = "mnuHelp"
        Me.mnuHelp.Size = New System.Drawing.Size(40, 22)
        Me.mnuHelp.Text = "&Help"
        '
        'mnuHowTo
        '
        Me.mnuHowTo.Name = "mnuHowTo"
        Me.mnuHowTo.ShortcutKeys = CType((System.Windows.Forms.Keys.F1), System.Windows.Forms.Keys)
        Me.mnuHowTo.Size = New System.Drawing.Size(250, 22)
        Me.mnuHowTo.Text = "&How to Use"
        '
        'mnuAbout
        '
        Me.mnuAbout.Name = "mnuAbout"
        Me.mnuAbout.Size = New System.Drawing.Size(250, 22)
        Me.mnuAbout.Text = "&About Graphing"
        '
        'toolMain
        '
        Me.toolMain.GripStyle = System.Windows.Forms.ToolStripGripStyle.Hidden
        Me.toolMain.Items.AddRange(New System.Windows.Forms.ToolStripItem() {Me.tsOpenImage, Me.tsPaste, Me.tsSep6, Me.tsOpenProject, Me.tsSaveProject, Me.tsSep7, Me.tsUndo, Me.tsRedo, Me.tsSep8, Me.tsZoomIn, Me.tsZoomOut, Me.tsFit, Me.tsSep9, Me.tsMagnifier, Me.tsGrid})
        Me.toolMain.Location = New System.Drawing.Point(0, 24)
        Me.toolMain.Name = "toolMain"
        Me.toolMain.Size = New System.Drawing.Size(1280, 25)
        Me.toolMain.TabIndex = 3
        '
        'tsOpenImage
        '
        Me.tsOpenImage.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsOpenImage.Name = "tsOpenImage"
        Me.tsOpenImage.Size = New System.Drawing.Size(70, 22)
        Me.tsOpenImage.Text = "Open Image"
        Me.tsOpenImage.ToolTipText = "Open a graph image (Ctrl+O)"
        '
        'tsPaste
        '
        Me.tsPaste.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsPaste.Name = "tsPaste"
        Me.tsPaste.Size = New System.Drawing.Size(70, 22)
        Me.tsPaste.Text = "Paste"
        Me.tsPaste.ToolTipText = "Paste a screenshot from the clipboard (Ctrl+V)"
        '
        'tsSep6
        '
        Me.tsSep6.Name = "tsSep6"
        Me.tsSep6.Size = New System.Drawing.Size(6, 25)
        '
        'tsOpenProject
        '
        Me.tsOpenProject.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsOpenProject.Name = "tsOpenProject"
        Me.tsOpenProject.Size = New System.Drawing.Size(70, 22)
        Me.tsOpenProject.Text = "Open Project"
        Me.tsOpenProject.ToolTipText = "Open a saved project (Ctrl+Shift+O)"
        '
        'tsSaveProject
        '
        Me.tsSaveProject.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsSaveProject.Name = "tsSaveProject"
        Me.tsSaveProject.Size = New System.Drawing.Size(70, 22)
        Me.tsSaveProject.Text = "Save Project"
        Me.tsSaveProject.ToolTipText = "Save the project (Ctrl+S)"
        '
        'tsSep7
        '
        Me.tsSep7.Name = "tsSep7"
        Me.tsSep7.Size = New System.Drawing.Size(6, 25)
        '
        'tsUndo
        '
        Me.tsUndo.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsUndo.Name = "tsUndo"
        Me.tsUndo.Size = New System.Drawing.Size(70, 22)
        Me.tsUndo.Text = "Undo"
        Me.tsUndo.ToolTipText = "Undo (Ctrl+Z)"
        '
        'tsRedo
        '
        Me.tsRedo.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsRedo.Name = "tsRedo"
        Me.tsRedo.Size = New System.Drawing.Size(70, 22)
        Me.tsRedo.Text = "Redo"
        Me.tsRedo.ToolTipText = "Redo (Ctrl+Y)"
        '
        'tsSep8
        '
        Me.tsSep8.Name = "tsSep8"
        Me.tsSep8.Size = New System.Drawing.Size(6, 25)
        '
        'tsZoomIn
        '
        Me.tsZoomIn.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsZoomIn.Name = "tsZoomIn"
        Me.tsZoomIn.Size = New System.Drawing.Size(70, 22)
        Me.tsZoomIn.Text = "Zoom +"
        Me.tsZoomIn.ToolTipText = "Zoom in (mouse wheel)"
        '
        'tsZoomOut
        '
        Me.tsZoomOut.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsZoomOut.Name = "tsZoomOut"
        Me.tsZoomOut.Size = New System.Drawing.Size(70, 22)
        Me.tsZoomOut.Text = "Zoom −"
        Me.tsZoomOut.ToolTipText = "Zoom out (mouse wheel)"
        '
        'tsFit
        '
        Me.tsFit.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsFit.Name = "tsFit"
        Me.tsFit.Size = New System.Drawing.Size(70, 22)
        Me.tsFit.Text = "Fit"
        Me.tsFit.ToolTipText = "Fit the image to the window (Ctrl+0)"
        '
        'tsSep9
        '
        Me.tsSep9.Name = "tsSep9"
        Me.tsSep9.Size = New System.Drawing.Size(6, 25)
        '
        'tsMagnifier
        '
        Me.tsMagnifier.CheckOnClick = True
        Me.tsMagnifier.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsMagnifier.Name = "tsMagnifier"
        Me.tsMagnifier.Size = New System.Drawing.Size(70, 22)
        Me.tsMagnifier.Text = "Magnifier"
        Me.tsMagnifier.ToolTipText = "Show a magnifier around the mouse"
        '
        'tsGrid
        '
        Me.tsGrid.CheckOnClick = True
        Me.tsGrid.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text
        Me.tsGrid.Name = "tsGrid"
        Me.tsGrid.Size = New System.Drawing.Size(70, 22)
        Me.tsGrid.Text = "Grid"
        Me.tsGrid.ToolTipText = "Draw a grid from the calibration to check it (Ctrl+G)"
        '
        'statusMain
        '
        Me.statusMain.Items.AddRange(New System.Windows.Forms.ToolStripItem() {Me.lblStatus, Me.lblZoom, Me.lblCoords})
        Me.statusMain.Location = New System.Drawing.Point(0, 778)
        Me.statusMain.Name = "statusMain"
        Me.statusMain.Size = New System.Drawing.Size(1280, 22)
        Me.statusMain.TabIndex = 2
        '
        'lblStatus
        '
        Me.lblStatus.Name = "lblStatus"
        Me.lblStatus.Size = New System.Drawing.Size(1000, 17)
        Me.lblStatus.Spring = True
        Me.lblStatus.Text = "Open a graph image to start."
        Me.lblStatus.TextAlign = System.Drawing.ContentAlignment.MiddleLeft
        '
        'lblZoom
        '
        Me.lblZoom.Name = "lblZoom"
        Me.lblZoom.Size = New System.Drawing.Size(60, 17)
        Me.lblZoom.Text = ""
        '
        'lblCoords
        '
        Me.lblCoords.Name = "lblCoords"
        Me.lblCoords.Size = New System.Drawing.Size(200, 17)
        Me.lblCoords.Text = ""
        '
        'canvas
        '
        Me.canvas.Dock = System.Windows.Forms.DockStyle.Fill
        Me.canvas.Location = New System.Drawing.Point(0, 49)
        Me.canvas.Name = "canvas"
        Me.canvas.Size = New System.Drawing.Size(910, 729)
        Me.canvas.TabIndex = 0
        '
        'pnlSide
        '
        Me.pnlSide.Controls.Add(Me.tabsMain)
        Me.pnlSide.Dock = System.Windows.Forms.DockStyle.Right
        Me.pnlSide.Location = New System.Drawing.Point(910, 49)
        Me.pnlSide.Name = "pnlSide"
        Me.pnlSide.Padding = New System.Windows.Forms.Padding(6)
        Me.pnlSide.Size = New System.Drawing.Size(370, 729)
        Me.pnlSide.TabIndex = 1
        '
        'tabsMain
        '
        Me.tabsMain.Controls.Add(Me.tabCalibrate)
        Me.tabsMain.Controls.Add(Me.tabTrace)
        Me.tabsMain.Controls.Add(Me.tabData)
        Me.tabsMain.Controls.Add(Me.tabAnalyze)
        Me.tabsMain.Dock = System.Windows.Forms.DockStyle.Fill
        Me.tabsMain.Location = New System.Drawing.Point(6, 6)
        Me.tabsMain.Name = "tabsMain"
        Me.tabsMain.Size = New System.Drawing.Size(358, 717)
        Me.tabsMain.TabIndex = 0
        '
        'tabCalibrate
        '
        Me.tabCalibrate.Controls.Add(Me.lblCalHelp)
        Me.tabCalibrate.Controls.Add(Me.lblHdrPoint)
        Me.tabCalibrate.Controls.Add(Me.lblHdrPixel)
        Me.tabCalibrate.Controls.Add(Me.lblHdrValue)
        Me.tabCalibrate.Controls.Add(Me.btnPickX1)
        Me.tabCalibrate.Controls.Add(Me.lblPixelX1)
        Me.tabCalibrate.Controls.Add(Me.txtValueX1)
        Me.tabCalibrate.Controls.Add(Me.btnPickX2)
        Me.tabCalibrate.Controls.Add(Me.lblPixelX2)
        Me.tabCalibrate.Controls.Add(Me.txtValueX2)
        Me.tabCalibrate.Controls.Add(Me.btnPickY1)
        Me.tabCalibrate.Controls.Add(Me.lblPixelY1)
        Me.tabCalibrate.Controls.Add(Me.txtValueY1)
        Me.tabCalibrate.Controls.Add(Me.btnPickY2)
        Me.tabCalibrate.Controls.Add(Me.lblPixelY2)
        Me.tabCalibrate.Controls.Add(Me.txtValueY2)
        Me.tabCalibrate.Controls.Add(Me.chkLogX)
        Me.tabCalibrate.Controls.Add(Me.chkLogY)
        Me.tabCalibrate.Controls.Add(Me.lblCalState)
        Me.tabCalibrate.Controls.Add(Me.btnClearCalibration)
        Me.tabCalibrate.Controls.Add(Me.lblCalTip)
        Me.tabCalibrate.Location = New System.Drawing.Point(4, 24)
        Me.tabCalibrate.Name = "tabCalibrate"
        Me.tabCalibrate.Padding = New System.Windows.Forms.Padding(3)
        Me.tabCalibrate.Size = New System.Drawing.Size(350, 689)
        Me.tabCalibrate.TabIndex = 0
        Me.tabCalibrate.Text = "1. Calibrate"
        Me.tabCalibrate.UseVisualStyleBackColor = True
        '
        'tabTrace
        '
        Me.tabTrace.Controls.Add(Me.lblSeriesHeader)
        Me.tabTrace.Controls.Add(Me.lstSeries)
        Me.tabTrace.Controls.Add(Me.btnAddSeries)
        Me.tabTrace.Controls.Add(Me.btnRenameSeries)
        Me.tabTrace.Controls.Add(Me.btnColorSeries)
        Me.tabTrace.Controls.Add(Me.btnDeleteSeries)
        Me.tabTrace.Controls.Add(Me.grpTool)
        Me.tabTrace.Controls.Add(Me.grpAuto)
        Me.tabTrace.Controls.Add(Me.chkShowLines)
        Me.tabTrace.Controls.Add(Me.btnClearSeries)
        Me.tabTrace.Controls.Add(Me.lblTraceTip)
        Me.tabTrace.Location = New System.Drawing.Point(4, 24)
        Me.tabTrace.Name = "tabTrace"
        Me.tabTrace.Padding = New System.Windows.Forms.Padding(3)
        Me.tabTrace.Size = New System.Drawing.Size(350, 689)
        Me.tabTrace.TabIndex = 1
        Me.tabTrace.Text = "2. Trace"
        Me.tabTrace.UseVisualStyleBackColor = True
        '
        'tabData
        '
        Me.tabData.Controls.Add(Me.lblDataSeries)
        Me.tabData.Controls.Add(Me.dgvData)
        Me.tabData.Controls.Add(Me.grpClean)
        Me.tabData.Controls.Add(Me.grpExport)
        Me.tabData.Location = New System.Drawing.Point(4, 24)
        Me.tabData.Name = "tabData"
        Me.tabData.Padding = New System.Windows.Forms.Padding(3)
        Me.tabData.Size = New System.Drawing.Size(350, 689)
        Me.tabData.TabIndex = 2
        Me.tabData.Text = "3. Data"
        Me.tabData.UseVisualStyleBackColor = True
        '
        'tabAnalyze
        '
        Me.tabAnalyze.Controls.Add(Me.lblAnalyzeSeries)
        Me.tabAnalyze.Controls.Add(Me.grpStats)
        Me.tabAnalyze.Controls.Add(Me.grpInterpolate)
        Me.tabAnalyze.Controls.Add(Me.grpFit)
        Me.tabAnalyze.Controls.Add(Me.lblAnalyzeTip)
        Me.tabAnalyze.Location = New System.Drawing.Point(4, 24)
        Me.tabAnalyze.Name = "tabAnalyze"
        Me.tabAnalyze.Padding = New System.Windows.Forms.Padding(3)
        Me.tabAnalyze.Size = New System.Drawing.Size(350, 689)
        Me.tabAnalyze.TabIndex = 3
        Me.tabAnalyze.Text = "4. Analyze"
        Me.tabAnalyze.UseVisualStyleBackColor = True
        '
        'lblCalHelp
        '
        Me.lblCalHelp.Location = New System.Drawing.Point(8, 8)
        Me.lblCalHelp.Name = "lblCalHelp"
        Me.lblCalHelp.Size = New System.Drawing.Size(334, 64)
        Me.lblCalHelp.TabIndex = 0
        Me.lblCalHelp.Text = "Pick two points on each axis whose values you know (for example two labelled ticks) and type their values." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "The points don't have to be at the origin, and the axes don't have to start at 0."
        '
        'lblHdrPoint
        '
        Me.lblHdrPoint.AutoSize = True
        Me.lblHdrPoint.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblHdrPoint.Location = New System.Drawing.Point(8, 80)
        Me.lblHdrPoint.Name = "lblHdrPoint"
        Me.lblHdrPoint.TabIndex = 1
        Me.lblHdrPoint.Text = "Point"
        '
        'lblHdrPixel
        '
        Me.lblHdrPixel.AutoSize = True
        Me.lblHdrPixel.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblHdrPixel.Location = New System.Drawing.Point(84, 80)
        Me.lblHdrPixel.Name = "lblHdrPixel"
        Me.lblHdrPixel.TabIndex = 2
        Me.lblHdrPixel.Text = "Position on image"
        '
        'lblHdrValue
        '
        Me.lblHdrValue.AutoSize = True
        Me.lblHdrValue.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblHdrValue.Location = New System.Drawing.Point(222, 80)
        Me.lblHdrValue.Name = "lblHdrValue"
        Me.lblHdrValue.TabIndex = 3
        Me.lblHdrValue.Text = "Value"
        '
        'btnPickX1
        '
        Me.btnPickX1.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.btnPickX1.ForeColor = System.Drawing.Color.FromArgb(CType(CType(0, Byte), Integer), CType(CType(110, Byte), Integer), CType(CType(220, Byte), Integer))
        Me.btnPickX1.Location = New System.Drawing.Point(8, 102)
        Me.btnPickX1.Name = "btnPickX1"
        Me.btnPickX1.Size = New System.Drawing.Size(70, 28)
        Me.btnPickX1.TabIndex = 4
        Me.btnPickX1.Text = "Pick X1"
        Me.btnPickX1.UseVisualStyleBackColor = True
        Me.hints.SetToolTip(Me.btnPickX1, "Then click on the image where X1 is")
        '
        'lblPixelX1
        '
        Me.lblPixelX1.ForeColor = System.Drawing.Color.DimGray
        Me.lblPixelX1.Location = New System.Drawing.Point(84, 108)
        Me.lblPixelX1.Name = "lblPixelX1"
        Me.lblPixelX1.Size = New System.Drawing.Size(132, 20)
        Me.lblPixelX1.TabIndex = 5
        Me.lblPixelX1.Text = "not set"
        '
        'txtValueX1
        '
        Me.txtValueX1.Location = New System.Drawing.Point(222, 105)
        Me.txtValueX1.Name = "txtValueX1"
        Me.txtValueX1.Size = New System.Drawing.Size(120, 23)
        Me.txtValueX1.TabIndex = 6
        '
        'btnPickX2
        '
        Me.btnPickX2.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.btnPickX2.ForeColor = System.Drawing.Color.FromArgb(CType(CType(0, Byte), Integer), CType(CType(110, Byte), Integer), CType(CType(220, Byte), Integer))
        Me.btnPickX2.Location = New System.Drawing.Point(8, 138)
        Me.btnPickX2.Name = "btnPickX2"
        Me.btnPickX2.Size = New System.Drawing.Size(70, 28)
        Me.btnPickX2.TabIndex = 7
        Me.btnPickX2.Text = "Pick X2"
        Me.btnPickX2.UseVisualStyleBackColor = True
        Me.hints.SetToolTip(Me.btnPickX2, "Then click on the image where X2 is")
        '
        'lblPixelX2
        '
        Me.lblPixelX2.ForeColor = System.Drawing.Color.DimGray
        Me.lblPixelX2.Location = New System.Drawing.Point(84, 144)
        Me.lblPixelX2.Name = "lblPixelX2"
        Me.lblPixelX2.Size = New System.Drawing.Size(132, 20)
        Me.lblPixelX2.TabIndex = 8
        Me.lblPixelX2.Text = "not set"
        '
        'txtValueX2
        '
        Me.txtValueX2.Location = New System.Drawing.Point(222, 141)
        Me.txtValueX2.Name = "txtValueX2"
        Me.txtValueX2.Size = New System.Drawing.Size(120, 23)
        Me.txtValueX2.TabIndex = 9
        '
        'btnPickY1
        '
        Me.btnPickY1.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.btnPickY1.ForeColor = System.Drawing.Color.FromArgb(CType(CType(0, Byte), Integer), CType(CType(150, Byte), Integer), CType(CType(70, Byte), Integer))
        Me.btnPickY1.Location = New System.Drawing.Point(8, 174)
        Me.btnPickY1.Name = "btnPickY1"
        Me.btnPickY1.Size = New System.Drawing.Size(70, 28)
        Me.btnPickY1.TabIndex = 10
        Me.btnPickY1.Text = "Pick Y1"
        Me.btnPickY1.UseVisualStyleBackColor = True
        Me.hints.SetToolTip(Me.btnPickY1, "Then click on the image where Y1 is")
        '
        'lblPixelY1
        '
        Me.lblPixelY1.ForeColor = System.Drawing.Color.DimGray
        Me.lblPixelY1.Location = New System.Drawing.Point(84, 180)
        Me.lblPixelY1.Name = "lblPixelY1"
        Me.lblPixelY1.Size = New System.Drawing.Size(132, 20)
        Me.lblPixelY1.TabIndex = 11
        Me.lblPixelY1.Text = "not set"
        '
        'txtValueY1
        '
        Me.txtValueY1.Location = New System.Drawing.Point(222, 177)
        Me.txtValueY1.Name = "txtValueY1"
        Me.txtValueY1.Size = New System.Drawing.Size(120, 23)
        Me.txtValueY1.TabIndex = 12
        '
        'btnPickY2
        '
        Me.btnPickY2.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.btnPickY2.ForeColor = System.Drawing.Color.FromArgb(CType(CType(0, Byte), Integer), CType(CType(150, Byte), Integer), CType(CType(70, Byte), Integer))
        Me.btnPickY2.Location = New System.Drawing.Point(8, 210)
        Me.btnPickY2.Name = "btnPickY2"
        Me.btnPickY2.Size = New System.Drawing.Size(70, 28)
        Me.btnPickY2.TabIndex = 13
        Me.btnPickY2.Text = "Pick Y2"
        Me.btnPickY2.UseVisualStyleBackColor = True
        Me.hints.SetToolTip(Me.btnPickY2, "Then click on the image where Y2 is")
        '
        'lblPixelY2
        '
        Me.lblPixelY2.ForeColor = System.Drawing.Color.DimGray
        Me.lblPixelY2.Location = New System.Drawing.Point(84, 216)
        Me.lblPixelY2.Name = "lblPixelY2"
        Me.lblPixelY2.Size = New System.Drawing.Size(132, 20)
        Me.lblPixelY2.TabIndex = 14
        Me.lblPixelY2.Text = "not set"
        '
        'txtValueY2
        '
        Me.txtValueY2.Location = New System.Drawing.Point(222, 213)
        Me.txtValueY2.Name = "txtValueY2"
        Me.txtValueY2.Size = New System.Drawing.Size(120, 23)
        Me.txtValueY2.TabIndex = 15
        '
        'chkLogX
        '
        Me.chkLogX.AutoSize = True
        Me.chkLogX.Location = New System.Drawing.Point(8, 254)
        Me.chkLogX.Name = "chkLogX"
        Me.chkLogX.TabIndex = 16
        Me.chkLogX.Text = "X axis is logarithmic"
        Me.chkLogX.UseVisualStyleBackColor = True
        '
        'chkLogY
        '
        Me.chkLogY.AutoSize = True
        Me.chkLogY.Location = New System.Drawing.Point(8, 280)
        Me.chkLogY.Name = "chkLogY"
        Me.chkLogY.TabIndex = 17
        Me.chkLogY.Text = "Y axis is logarithmic"
        Me.chkLogY.UseVisualStyleBackColor = True
        '
        'lblCalState
        '
        Me.lblCalState.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblCalState.ForeColor = System.Drawing.Color.DarkOrange
        Me.lblCalState.Location = New System.Drawing.Point(8, 312)
        Me.lblCalState.Name = "lblCalState"
        Me.lblCalState.Size = New System.Drawing.Size(334, 58)
        Me.lblCalState.TabIndex = 18
        Me.lblCalState.Text = "Not calibrated yet."
        '
        'btnClearCalibration
        '
        Me.btnClearCalibration.Location = New System.Drawing.Point(8, 376)
        Me.btnClearCalibration.Name = "btnClearCalibration"
        Me.btnClearCalibration.Size = New System.Drawing.Size(150, 28)
        Me.btnClearCalibration.TabIndex = 19
        Me.btnClearCalibration.Text = "Clear calibration"
        Me.btnClearCalibration.UseVisualStyleBackColor = True
        '
        'lblCalTip
        '
        Me.lblCalTip.ForeColor = System.Drawing.Color.DimGray
        Me.lblCalTip.Location = New System.Drawing.Point(8, 418)
        Me.lblCalTip.Name = "lblCalTip"
        Me.lblCalTip.Size = New System.Drawing.Size(334, 120)
        Me.lblCalTip.TabIndex = 20
        Me.lblCalTip.Text = "Tips:" & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "• Zoom with the mouse wheel and use the magnifier to pick exactly." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "• Turn on View ▸ Calibrated Grid to check the calibration: its lines should sit on the graph's own gridlines." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "• Values like 1e-3 or 2.5E6 are fine."
        '
        'lblSeriesHeader
        '
        Me.lblSeriesHeader.AutoSize = True
        Me.lblSeriesHeader.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblSeriesHeader.Location = New System.Drawing.Point(8, 8)
        Me.lblSeriesHeader.Name = "lblSeriesHeader"
        Me.lblSeriesHeader.TabIndex = 0
        Me.lblSeriesHeader.Text = "Curves"
        '
        'lstSeries
        '
        Me.lstSeries.DrawMode = System.Windows.Forms.DrawMode.OwnerDrawFixed
        Me.lstSeries.FormattingEnabled = True
        Me.lstSeries.IntegralHeight = False
        Me.lstSeries.ItemHeight = 22
        Me.lstSeries.Location = New System.Drawing.Point(8, 28)
        Me.lstSeries.Name = "lstSeries"
        Me.lstSeries.Size = New System.Drawing.Size(232, 124)
        Me.lstSeries.TabIndex = 1
        '
        'btnAddSeries
        '
        Me.btnAddSeries.Location = New System.Drawing.Point(248, 28)
        Me.btnAddSeries.Name = "btnAddSeries"
        Me.btnAddSeries.Size = New System.Drawing.Size(94, 28)
        Me.btnAddSeries.TabIndex = 2
        Me.btnAddSeries.Text = "Add"
        Me.btnAddSeries.UseVisualStyleBackColor = True
        '
        'btnRenameSeries
        '
        Me.btnRenameSeries.Location = New System.Drawing.Point(248, 60)
        Me.btnRenameSeries.Name = "btnRenameSeries"
        Me.btnRenameSeries.Size = New System.Drawing.Size(94, 28)
        Me.btnRenameSeries.TabIndex = 3
        Me.btnRenameSeries.Text = "Rename…"
        Me.btnRenameSeries.UseVisualStyleBackColor = True
        '
        'btnColorSeries
        '
        Me.btnColorSeries.Location = New System.Drawing.Point(248, 92)
        Me.btnColorSeries.Name = "btnColorSeries"
        Me.btnColorSeries.Size = New System.Drawing.Size(94, 28)
        Me.btnColorSeries.TabIndex = 4
        Me.btnColorSeries.Text = "Color…"
        Me.btnColorSeries.UseVisualStyleBackColor = True
        '
        'btnDeleteSeries
        '
        Me.btnDeleteSeries.Location = New System.Drawing.Point(248, 124)
        Me.btnDeleteSeries.Name = "btnDeleteSeries"
        Me.btnDeleteSeries.Size = New System.Drawing.Size(94, 28)
        Me.btnDeleteSeries.TabIndex = 5
        Me.btnDeleteSeries.Text = "Delete"
        Me.btnDeleteSeries.UseVisualStyleBackColor = True
        '
        'grpTool
        '
        Me.grpTool.Controls.Add(Me.rbPan)
        Me.grpTool.Controls.Add(Me.rbAddPoints)
        Me.grpTool.Controls.Add(Me.rbDragTrace)
        Me.grpTool.Controls.Add(Me.nudDragSpacing)
        Me.grpTool.Controls.Add(Me.lblDragSpacingUnit)
        Me.grpTool.Controls.Add(Me.rbEditPoints)
        Me.grpTool.Controls.Add(Me.rbAutoTrace)
        Me.grpTool.Location = New System.Drawing.Point(8, 160)
        Me.grpTool.Name = "grpTool"
        Me.grpTool.Size = New System.Drawing.Size(334, 158)
        Me.grpTool.TabIndex = 6
        Me.grpTool.Text = "Tool"
        '
        'rbPan
        '
        Me.rbPan.AutoSize = True
        Me.rbPan.Location = New System.Drawing.Point(10, 22)
        Me.rbPan.Name = "rbPan"
        Me.rbPan.TabIndex = 0
        Me.rbPan.Text = "Move and zoom only"
        Me.rbPan.UseVisualStyleBackColor = True
        '
        'rbAddPoints
        '
        Me.rbAddPoints.AutoSize = True
        Me.rbAddPoints.Checked = True
        Me.rbAddPoints.Location = New System.Drawing.Point(10, 48)
        Me.rbAddPoints.Name = "rbAddPoints"
        Me.rbAddPoints.TabIndex = 1
        Me.rbAddPoints.TabStop = True
        Me.rbAddPoints.Text = "Click to add points"
        Me.rbAddPoints.UseVisualStyleBackColor = True
        '
        'rbDragTrace
        '
        Me.rbDragTrace.AutoSize = True
        Me.rbDragTrace.Location = New System.Drawing.Point(10, 74)
        Me.rbDragTrace.Name = "rbDragTrace"
        Me.rbDragTrace.TabIndex = 2
        Me.rbDragTrace.Text = "Drag to trace, a point every"
        Me.rbDragTrace.UseVisualStyleBackColor = True
        '
        'nudDragSpacing
        '
        Me.nudDragSpacing.Location = New System.Drawing.Point(200, 72)
        Me.nudDragSpacing.Maximum = New Decimal(New Integer() {200, 0, 0, 0})
        Me.nudDragSpacing.Minimum = New Decimal(New Integer() {1, 0, 0, 0})
        Me.nudDragSpacing.Name = "nudDragSpacing"
        Me.nudDragSpacing.Size = New System.Drawing.Size(56, 23)
        Me.nudDragSpacing.TabIndex = 3
        Me.nudDragSpacing.Value = New Decimal(New Integer() {5, 0, 0, 0})
        '
        'lblDragSpacingUnit
        '
        Me.lblDragSpacingUnit.AutoSize = True
        Me.lblDragSpacingUnit.Location = New System.Drawing.Point(262, 76)
        Me.lblDragSpacingUnit.Name = "lblDragSpacingUnit"
        Me.lblDragSpacingUnit.TabIndex = 4
        Me.lblDragSpacingUnit.Text = "px"
        '
        'rbEditPoints
        '
        Me.rbEditPoints.AutoSize = True
        Me.rbEditPoints.Location = New System.Drawing.Point(10, 100)
        Me.rbEditPoints.Name = "rbEditPoints"
        Me.rbEditPoints.TabIndex = 5
        Me.rbEditPoints.Text = "Edit points (drag to move, Del to delete)"
        Me.rbEditPoints.UseVisualStyleBackColor = True
        '
        'rbAutoTrace
        '
        Me.rbAutoTrace.AutoSize = True
        Me.rbAutoTrace.Location = New System.Drawing.Point(10, 126)
        Me.rbAutoTrace.Name = "rbAutoTrace"
        Me.rbAutoTrace.TabIndex = 6
        Me.rbAutoTrace.Text = "Automatic trace (click on a curve)"
        Me.rbAutoTrace.UseVisualStyleBackColor = True
        '
        'grpAuto
        '
        Me.grpAuto.Controls.Add(Me.lblTolerance)
        Me.grpAuto.Controls.Add(Me.nudTolerance)
        Me.grpAuto.Controls.Add(Me.lblAutoStep)
        Me.grpAuto.Controls.Add(Me.nudAutoStep)
        Me.grpAuto.Controls.Add(Me.lblAutoStepUnit)
        Me.grpAuto.Controls.Add(Me.lblAutoColorCaption)
        Me.grpAuto.Controls.Add(Me.pnlAutoColor)
        Me.grpAuto.Location = New System.Drawing.Point(8, 326)
        Me.grpAuto.Name = "grpAuto"
        Me.grpAuto.Size = New System.Drawing.Size(334, 112)
        Me.grpAuto.TabIndex = 7
        Me.grpAuto.Text = "Automatic trace settings"
        '
        'lblTolerance
        '
        Me.lblTolerance.AutoSize = True
        Me.lblTolerance.Location = New System.Drawing.Point(10, 27)
        Me.lblTolerance.Name = "lblTolerance"
        Me.lblTolerance.TabIndex = 0
        Me.lblTolerance.Text = "Color tolerance"
        '
        'nudTolerance
        '
        Me.nudTolerance.Location = New System.Drawing.Point(150, 24)
        Me.nudTolerance.Maximum = New Decimal(New Integer() {200, 0, 0, 0})
        Me.nudTolerance.Minimum = New Decimal(New Integer() {5, 0, 0, 0})
        Me.nudTolerance.Name = "nudTolerance"
        Me.nudTolerance.Size = New System.Drawing.Size(70, 23)
        Me.nudTolerance.TabIndex = 1
        Me.nudTolerance.Value = New Decimal(New Integer() {60, 0, 0, 0})
        Me.hints.SetToolTip(Me.nudTolerance, "How different a pixel's color may be from the curve's color. Raise it for blurry scans, lower it if tracing jumps onto other lines.")
        '
        'lblAutoStep
        '
        Me.lblAutoStep.AutoSize = True
        Me.lblAutoStep.Location = New System.Drawing.Point(10, 56)
        Me.lblAutoStep.Name = "lblAutoStep"
        Me.lblAutoStep.TabIndex = 2
        Me.lblAutoStep.Text = "A point every"
        '
        'nudAutoStep
        '
        Me.nudAutoStep.Location = New System.Drawing.Point(150, 53)
        Me.nudAutoStep.Maximum = New Decimal(New Integer() {100, 0, 0, 0})
        Me.nudAutoStep.Minimum = New Decimal(New Integer() {1, 0, 0, 0})
        Me.nudAutoStep.Name = "nudAutoStep"
        Me.nudAutoStep.Size = New System.Drawing.Size(70, 23)
        Me.nudAutoStep.TabIndex = 3
        Me.nudAutoStep.Value = New Decimal(New Integer() {5, 0, 0, 0})
        Me.hints.SetToolTip(Me.nudAutoStep, "Horizontal distance between the extracted points, in image pixels")
        '
        'lblAutoStepUnit
        '
        Me.lblAutoStepUnit.AutoSize = True
        Me.lblAutoStepUnit.Location = New System.Drawing.Point(226, 56)
        Me.lblAutoStepUnit.Name = "lblAutoStepUnit"
        Me.lblAutoStepUnit.TabIndex = 4
        Me.lblAutoStepUnit.Text = "px"
        '
        'lblAutoColorCaption
        '
        Me.lblAutoColorCaption.AutoSize = True
        Me.lblAutoColorCaption.Location = New System.Drawing.Point(10, 84)
        Me.lblAutoColorCaption.Name = "lblAutoColorCaption"
        Me.lblAutoColorCaption.TabIndex = 5
        Me.lblAutoColorCaption.Text = "Last traced color"
        '
        'pnlAutoColor
        '
        Me.pnlAutoColor.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle
        Me.pnlAutoColor.Location = New System.Drawing.Point(150, 82)
        Me.pnlAutoColor.Name = "pnlAutoColor"
        Me.pnlAutoColor.Size = New System.Drawing.Size(70, 20)
        '
        'chkShowLines
        '
        Me.chkShowLines.AutoSize = True
        Me.chkShowLines.CheckState = System.Windows.Forms.CheckState.Checked
        Me.chkShowLines.Checked = True
        Me.chkShowLines.Location = New System.Drawing.Point(8, 446)
        Me.chkShowLines.Name = "chkShowLines"
        Me.chkShowLines.TabIndex = 8
        Me.chkShowLines.Text = "Show connecting lines"
        Me.chkShowLines.UseVisualStyleBackColor = True
        '
        'btnClearSeries
        '
        Me.btnClearSeries.Location = New System.Drawing.Point(8, 474)
        Me.btnClearSeries.Name = "btnClearSeries"
        Me.btnClearSeries.Size = New System.Drawing.Size(200, 28)
        Me.btnClearSeries.TabIndex = 9
        Me.btnClearSeries.Text = "Clear this curve's points"
        Me.btnClearSeries.UseVisualStyleBackColor = True
        '
        'lblTraceTip
        '
        Me.lblTraceTip.ForeColor = System.Drawing.Color.DimGray
        Me.lblTraceTip.Location = New System.Drawing.Point(8, 514)
        Me.lblTraceTip.Name = "lblTraceTip"
        Me.lblTraceTip.Size = New System.Drawing.Size(334, 100)
        Me.lblTraceTip.TabIndex = 10
        Me.lblTraceTip.Text = "Mouse wheel zooms; right-drag moves the image." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "In Edit mode, arrow keys nudge the selected point (Shift for finer steps) and right-click deletes a point." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "Ctrl+Z undoes any change."
        '
        'lblDataSeries
        '
        Me.lblDataSeries.Anchor = CType(((System.Windows.Forms.AnchorStyles.Top Or System.Windows.Forms.AnchorStyles.Left) Or System.Windows.Forms.AnchorStyles.Right), System.Windows.Forms.AnchorStyles)
        Me.lblDataSeries.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblDataSeries.Location = New System.Drawing.Point(8, 8)
        Me.lblDataSeries.Name = "lblDataSeries"
        Me.lblDataSeries.Size = New System.Drawing.Size(334, 20)
        Me.lblDataSeries.TabIndex = 0
        Me.lblDataSeries.Text = "No curve selected"
        '
        'dgvData
        '
        Me.dgvData.Columns.AddRange(New System.Windows.Forms.DataGridViewColumn() {Me.colIndex, Me.colX, Me.colY})
        Me.dgvData.AllowUserToAddRows = False
        Me.dgvData.AllowUserToDeleteRows = False
        Me.dgvData.AllowUserToResizeRows = False
        Me.dgvData.Anchor = CType((((System.Windows.Forms.AnchorStyles.Top Or System.Windows.Forms.AnchorStyles.Bottom) Or System.Windows.Forms.AnchorStyles.Left) Or System.Windows.Forms.AnchorStyles.Right), System.Windows.Forms.AnchorStyles)
        Me.dgvData.AutoSizeColumnsMode = System.Windows.Forms.DataGridViewAutoSizeColumnsMode.Fill
        Me.dgvData.BackgroundColor = System.Drawing.SystemColors.Window
        Me.dgvData.ColumnHeadersHeightSizeMode = System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode.AutoSize
        Me.dgvData.Location = New System.Drawing.Point(8, 30)
        Me.dgvData.MultiSelect = False
        Me.dgvData.Name = "dgvData"
        Me.dgvData.ReadOnly = True
        Me.dgvData.RowHeadersVisible = False
        Me.dgvData.SelectionMode = System.Windows.Forms.DataGridViewSelectionMode.FullRowSelect
        Me.dgvData.Size = New System.Drawing.Size(334, 380)
        Me.dgvData.TabIndex = 1
        Me.dgvData.VirtualMode = True
        '
        'grpClean
        '
        Me.grpClean.Controls.Add(Me.btnSortX)
        Me.grpClean.Controls.Add(Me.btnRemoveDuplicates)
        Me.grpClean.Controls.Add(Me.lblResampleCount)
        Me.grpClean.Controls.Add(Me.nudResampleCount)
        Me.grpClean.Controls.Add(Me.btnResampleCount)
        Me.grpClean.Controls.Add(Me.lblResampleStep)
        Me.grpClean.Controls.Add(Me.txtResampleStep)
        Me.grpClean.Controls.Add(Me.btnResampleStep)
        Me.grpClean.Anchor = CType(((System.Windows.Forms.AnchorStyles.Bottom Or System.Windows.Forms.AnchorStyles.Left) Or System.Windows.Forms.AnchorStyles.Right), System.Windows.Forms.AnchorStyles)
        Me.grpClean.Location = New System.Drawing.Point(8, 418)
        Me.grpClean.Name = "grpClean"
        Me.grpClean.Size = New System.Drawing.Size(334, 136)
        Me.grpClean.TabIndex = 2
        Me.grpClean.Text = "Clean up this curve"
        '
        'btnSortX
        '
        Me.btnSortX.Location = New System.Drawing.Point(10, 22)
        Me.btnSortX.Name = "btnSortX"
        Me.btnSortX.Size = New System.Drawing.Size(150, 28)
        Me.btnSortX.TabIndex = 0
        Me.btnSortX.Text = "Sort by X"
        Me.btnSortX.UseVisualStyleBackColor = True
        '
        'btnRemoveDuplicates
        '
        Me.btnRemoveDuplicates.Location = New System.Drawing.Point(172, 22)
        Me.btnRemoveDuplicates.Name = "btnRemoveDuplicates"
        Me.btnRemoveDuplicates.Size = New System.Drawing.Size(150, 28)
        Me.btnRemoveDuplicates.TabIndex = 1
        Me.btnRemoveDuplicates.Text = "Remove duplicates"
        Me.btnRemoveDuplicates.UseVisualStyleBackColor = True
        Me.hints.SetToolTip(Me.btnRemoveDuplicates, "Remove points that fall on the same image pixel")
        '
        'lblResampleCount
        '
        Me.lblResampleCount.AutoSize = True
        Me.lblResampleCount.Location = New System.Drawing.Point(10, 64)
        Me.lblResampleCount.Name = "lblResampleCount"
        Me.lblResampleCount.TabIndex = 2
        Me.lblResampleCount.Text = "Resample to"
        '
        'nudResampleCount
        '
        Me.nudResampleCount.Location = New System.Drawing.Point(94, 61)
        Me.nudResampleCount.Maximum = New Decimal(New Integer() {100000, 0, 0, 0})
        Me.nudResampleCount.Minimum = New Decimal(New Integer() {2, 0, 0, 0})
        Me.nudResampleCount.Name = "nudResampleCount"
        Me.nudResampleCount.Size = New System.Drawing.Size(72, 23)
        Me.nudResampleCount.TabIndex = 3
        Me.nudResampleCount.Value = New Decimal(New Integer() {50, 0, 0, 0})
        '
        'btnResampleCount
        '
        Me.btnResampleCount.Location = New System.Drawing.Point(172, 58)
        Me.btnResampleCount.Name = "btnResampleCount"
        Me.btnResampleCount.Size = New System.Drawing.Size(150, 28)
        Me.btnResampleCount.TabIndex = 4
        Me.btnResampleCount.Text = "Resample (points)"
        Me.btnResampleCount.UseVisualStyleBackColor = True
        Me.hints.SetToolTip(Me.btnResampleCount, "Replace the points with this many, evenly spaced along X")
        '
        'lblResampleStep
        '
        Me.lblResampleStep.AutoSize = True
        Me.lblResampleStep.Location = New System.Drawing.Point(10, 100)
        Me.lblResampleStep.Name = "lblResampleStep"
        Me.lblResampleStep.TabIndex = 5
        Me.lblResampleStep.Text = "or every"
        '
        'txtResampleStep
        '
        Me.txtResampleStep.Location = New System.Drawing.Point(94, 97)
        Me.txtResampleStep.Name = "txtResampleStep"
        Me.txtResampleStep.Size = New System.Drawing.Size(72, 23)
        Me.txtResampleStep.TabIndex = 6
        '
        'btnResampleStep
        '
        Me.btnResampleStep.Location = New System.Drawing.Point(172, 94)
        Me.btnResampleStep.Name = "btnResampleStep"
        Me.btnResampleStep.Size = New System.Drawing.Size(150, 28)
        Me.btnResampleStep.TabIndex = 7
        Me.btnResampleStep.Text = "Resample (X step)"
        Me.btnResampleStep.UseVisualStyleBackColor = True
        Me.hints.SetToolTip(Me.btnResampleStep, "Replace the points with one every given X step (linear X axis only)")
        '
        'grpExport
        '
        Me.grpExport.Controls.Add(Me.btnCopyData)
        Me.grpExport.Controls.Add(Me.btnExportCsv)
        Me.grpExport.Controls.Add(Me.btnExportExcel)
        Me.grpExport.Anchor = CType(((System.Windows.Forms.AnchorStyles.Bottom Or System.Windows.Forms.AnchorStyles.Left) Or System.Windows.Forms.AnchorStyles.Right), System.Windows.Forms.AnchorStyles)
        Me.grpExport.Location = New System.Drawing.Point(8, 562)
        Me.grpExport.Name = "grpExport"
        Me.grpExport.Size = New System.Drawing.Size(334, 100)
        Me.grpExport.TabIndex = 3
        Me.grpExport.Text = "Export"
        '
        'btnCopyData
        '
        Me.btnCopyData.Location = New System.Drawing.Point(10, 22)
        Me.btnCopyData.Name = "btnCopyData"
        Me.btnCopyData.Size = New System.Drawing.Size(150, 28)
        Me.btnCopyData.TabIndex = 0
        Me.btnCopyData.Text = "Copy this curve"
        Me.btnCopyData.UseVisualStyleBackColor = True
        Me.hints.SetToolTip(Me.btnCopyData, "Copy X and Y of this curve; paste straight into Excel or Origin")
        '
        'btnExportCsv
        '
        Me.btnExportCsv.Location = New System.Drawing.Point(172, 22)
        Me.btnExportCsv.Name = "btnExportCsv"
        Me.btnExportCsv.Size = New System.Drawing.Size(150, 28)
        Me.btnExportCsv.TabIndex = 1
        Me.btnExportCsv.Text = "All curves to CSV…"
        Me.btnExportCsv.UseVisualStyleBackColor = True
        '
        'btnExportExcel
        '
        Me.btnExportExcel.Location = New System.Drawing.Point(10, 58)
        Me.btnExportExcel.Name = "btnExportExcel"
        Me.btnExportExcel.Size = New System.Drawing.Size(150, 28)
        Me.btnExportExcel.TabIndex = 2
        Me.btnExportExcel.Text = "All curves to Excel…"
        Me.btnExportExcel.UseVisualStyleBackColor = True
        '
        'lblAnalyzeSeries
        '
        Me.lblAnalyzeSeries.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblAnalyzeSeries.Location = New System.Drawing.Point(8, 8)
        Me.lblAnalyzeSeries.Name = "lblAnalyzeSeries"
        Me.lblAnalyzeSeries.Size = New System.Drawing.Size(334, 20)
        Me.lblAnalyzeSeries.TabIndex = 0
        Me.lblAnalyzeSeries.Text = "No curve selected"
        '
        'grpStats
        '
        Me.grpStats.Controls.Add(Me.lblStats)
        Me.grpStats.Location = New System.Drawing.Point(8, 32)
        Me.grpStats.Name = "grpStats"
        Me.grpStats.Size = New System.Drawing.Size(334, 132)
        Me.grpStats.TabIndex = 1
        Me.grpStats.Text = "Statistics"
        '
        'lblStats
        '
        Me.lblStats.Location = New System.Drawing.Point(10, 20)
        Me.lblStats.Name = "lblStats"
        Me.lblStats.Size = New System.Drawing.Size(314, 104)
        Me.lblStats.TabIndex = 0
        Me.lblStats.Text = "—"
        '
        'grpInterpolate
        '
        Me.grpInterpolate.Controls.Add(Me.lblInterpolateX)
        Me.grpInterpolate.Controls.Add(Me.txtInterpolateX)
        Me.grpInterpolate.Controls.Add(Me.btnInterpolate)
        Me.grpInterpolate.Controls.Add(Me.lblInterpolateResult)
        Me.grpInterpolate.Location = New System.Drawing.Point(8, 172)
        Me.grpInterpolate.Name = "grpInterpolate"
        Me.grpInterpolate.Size = New System.Drawing.Size(334, 92)
        Me.grpInterpolate.TabIndex = 2
        Me.grpInterpolate.Text = "Read Y at a given X"
        '
        'lblInterpolateX
        '
        Me.lblInterpolateX.AutoSize = True
        Me.lblInterpolateX.Location = New System.Drawing.Point(10, 28)
        Me.lblInterpolateX.Name = "lblInterpolateX"
        Me.lblInterpolateX.TabIndex = 0
        Me.lblInterpolateX.Text = "X ="
        '
        'txtInterpolateX
        '
        Me.txtInterpolateX.Location = New System.Drawing.Point(42, 25)
        Me.txtInterpolateX.Name = "txtInterpolateX"
        Me.txtInterpolateX.Size = New System.Drawing.Size(110, 23)
        Me.txtInterpolateX.TabIndex = 1
        '
        'btnInterpolate
        '
        Me.btnInterpolate.Location = New System.Drawing.Point(162, 22)
        Me.btnInterpolate.Name = "btnInterpolate"
        Me.btnInterpolate.Size = New System.Drawing.Size(100, 28)
        Me.btnInterpolate.TabIndex = 2
        Me.btnInterpolate.Text = "Read Y"
        Me.btnInterpolate.UseVisualStyleBackColor = True
        '
        'lblInterpolateResult
        '
        Me.lblInterpolateResult.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.lblInterpolateResult.Location = New System.Drawing.Point(10, 60)
        Me.lblInterpolateResult.Name = "lblInterpolateResult"
        Me.lblInterpolateResult.Size = New System.Drawing.Size(314, 22)
        Me.lblInterpolateResult.TabIndex = 3
        Me.lblInterpolateResult.Text = ""
        '
        'grpFit
        '
        Me.grpFit.Controls.Add(Me.cmbFitKind)
        Me.grpFit.Controls.Add(Me.btnFit)
        Me.grpFit.Controls.Add(Me.chkShowFit)
        Me.grpFit.Controls.Add(Me.txtFitResult)
        Me.grpFit.Location = New System.Drawing.Point(8, 272)
        Me.grpFit.Name = "grpFit"
        Me.grpFit.Size = New System.Drawing.Size(334, 250)
        Me.grpFit.TabIndex = 3
        Me.grpFit.Text = "Curve fit"
        '
        'cmbFitKind
        '
        Me.cmbFitKind.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList
        Me.cmbFitKind.FormattingEnabled = True
        Me.cmbFitKind.Location = New System.Drawing.Point(10, 24)
        Me.cmbFitKind.Name = "cmbFitKind"
        Me.cmbFitKind.Size = New System.Drawing.Size(314, 23)
        Me.cmbFitKind.TabIndex = 0
        '
        'btnFit
        '
        Me.btnFit.Location = New System.Drawing.Point(10, 56)
        Me.btnFit.Name = "btnFit"
        Me.btnFit.Size = New System.Drawing.Size(100, 28)
        Me.btnFit.TabIndex = 1
        Me.btnFit.Text = "Fit"
        Me.btnFit.UseVisualStyleBackColor = True
        '
        'chkShowFit
        '
        Me.chkShowFit.AutoSize = True
        Me.chkShowFit.CheckState = System.Windows.Forms.CheckState.Checked
        Me.chkShowFit.Checked = True
        Me.chkShowFit.Location = New System.Drawing.Point(122, 61)
        Me.chkShowFit.Name = "chkShowFit"
        Me.chkShowFit.TabIndex = 2
        Me.chkShowFit.Text = "Show fitted curve on the graph"
        Me.chkShowFit.UseVisualStyleBackColor = True
        '
        'txtFitResult
        '
        Me.txtFitResult.Location = New System.Drawing.Point(10, 92)
        Me.txtFitResult.Multiline = True
        Me.txtFitResult.Name = "txtFitResult"
        Me.txtFitResult.ReadOnly = True
        Me.txtFitResult.ScrollBars = System.Windows.Forms.ScrollBars.Vertical
        Me.txtFitResult.Size = New System.Drawing.Size(314, 146)
        Me.txtFitResult.TabIndex = 3
        '
        'lblAnalyzeTip
        '
        Me.lblAnalyzeTip.ForeColor = System.Drawing.Color.DimGray
        Me.lblAnalyzeTip.Location = New System.Drawing.Point(8, 530)
        Me.lblAnalyzeTip.Name = "lblAnalyzeTip"
        Me.lblAnalyzeTip.Size = New System.Drawing.Size(334, 70)
        Me.lblAnalyzeTip.TabIndex = 4
        Me.lblAnalyzeTip.Text = "Values use straight lines between traced points (on log axes, straight in log scale)." & Global.Microsoft.VisualBasic.ChrW(13) & Global.Microsoft.VisualBasic.ChrW(10) & "Area is between the curve and y = 0, by the trapezoidal rule."
        '
        'colIndex
        '
        Me.colIndex.FillWeight = 22.0!
        Me.colIndex.HeaderText = "#"
        Me.colIndex.Name = "colIndex"
        Me.colIndex.ReadOnly = True
        Me.colIndex.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable
        '
        'colX
        '
        Me.colX.FillWeight = 39.0!
        Me.colX.HeaderText = "X"
        Me.colX.Name = "colX"
        Me.colX.ReadOnly = True
        Me.colX.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable
        '
        'colY
        '
        Me.colY.FillWeight = 39.0!
        Me.colY.HeaderText = "Y"
        Me.colY.Name = "colY"
        Me.colY.ReadOnly = True
        Me.colY.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable
        '
        'dlgOpenImage
        '
        Me.dlgOpenImage.Filter = "Images|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff|All files|*.*"
        Me.dlgOpenImage.Title = "Open a graph image"
        '
        'dlgOpenProject
        '
        Me.dlgOpenProject.Filter = "Graphing projects (*.graphproj)|*.graphproj"
        Me.dlgOpenProject.Title = "Open a project"
        '
        'dlgSaveProject
        '
        Me.dlgSaveProject.DefaultExt = "graphproj"
        Me.dlgSaveProject.Filter = "Graphing projects (*.graphproj)|*.graphproj"
        Me.dlgSaveProject.Title = "Save project"
        '
        'dlgExport
        '
        Me.dlgExport.Title = "Export"
        '
        'dlgColor
        '
        Me.dlgColor.AnyColor = True
        Me.dlgColor.FullOpen = True
        '
        'errValues
        '
        Me.errValues.BlinkStyle = System.Windows.Forms.ErrorBlinkStyle.NeverBlink
        Me.errValues.ContainerControl = Me
        '
        'Form1
        '
        Me.AutoScaleDimensions = New System.Drawing.SizeF(7.0!, 15.0!)
        Me.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font
        Me.ClientSize = New System.Drawing.Size(1280, 800)
        Me.Controls.Add(Me.canvas)
        Me.Controls.Add(Me.pnlSide)
        Me.Controls.Add(Me.statusMain)
        Me.Controls.Add(Me.toolMain)
        Me.Controls.Add(Me.menuMain)
        Me.Font = New System.Drawing.Font("Segoe UI", 9.0!, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, CType(0, Byte))
        Me.Icon = CType(resources.GetObject("$this.Icon"), System.Drawing.Icon)
        Me.KeyPreview = True
        Me.MainMenuStrip = Me.menuMain
        Me.MinimumSize = New System.Drawing.Size(1000, 650)
        Me.Name = "Form1"
        Me.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen
        Me.Text = "Graphing"
        Me.grpFit.ResumeLayout(False)
        Me.grpFit.PerformLayout()
        Me.grpInterpolate.ResumeLayout(False)
        Me.grpInterpolate.PerformLayout()
        Me.grpStats.ResumeLayout(False)
        Me.grpExport.ResumeLayout(False)
        Me.grpClean.ResumeLayout(False)
        Me.grpClean.PerformLayout()
        Me.pnlAutoColor.ResumeLayout(False)
        Me.grpAuto.ResumeLayout(False)
        Me.grpAuto.PerformLayout()
        Me.grpTool.ResumeLayout(False)
        Me.grpTool.PerformLayout()
        Me.tabAnalyze.ResumeLayout(False)
        Me.tabData.ResumeLayout(False)
        Me.tabTrace.ResumeLayout(False)
        Me.tabTrace.PerformLayout()
        Me.tabCalibrate.ResumeLayout(False)
        Me.tabCalibrate.PerformLayout()
        Me.tabsMain.ResumeLayout(False)
        Me.pnlSide.ResumeLayout(False)
        Me.statusMain.ResumeLayout(False)
        Me.statusMain.PerformLayout()
        Me.toolMain.ResumeLayout(False)
        Me.toolMain.PerformLayout()
        Me.menuMain.ResumeLayout(False)
        Me.menuMain.PerformLayout()
        CType(Me.nudDragSpacing, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudTolerance, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudAutoStep, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.dgvData, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.nudResampleCount, System.ComponentModel.ISupportInitialize).EndInit()
        CType(Me.errValues, System.ComponentModel.ISupportInitialize).EndInit()
        Me.ResumeLayout(False)
        Me.PerformLayout()

    End Sub

    Friend WithEvents menuMain As MenuStrip
    Friend WithEvents mnuFile As ToolStripMenuItem
    Friend WithEvents mnuOpenImage As ToolStripMenuItem
    Friend WithEvents mnuPasteImage As ToolStripMenuItem
    Friend WithEvents mnuSep1 As ToolStripSeparator
    Friend WithEvents mnuOpenProject As ToolStripMenuItem
    Friend WithEvents mnuSaveProject As ToolStripMenuItem
    Friend WithEvents mnuSaveProjectAs As ToolStripMenuItem
    Friend WithEvents mnuSep2 As ToolStripSeparator
    Friend WithEvents mnuExportCsv As ToolStripMenuItem
    Friend WithEvents mnuExportExcel As ToolStripMenuItem
    Friend WithEvents mnuSep3 As ToolStripSeparator
    Friend WithEvents mnuExit As ToolStripMenuItem
    Friend WithEvents mnuEdit As ToolStripMenuItem
    Friend WithEvents mnuUndo As ToolStripMenuItem
    Friend WithEvents mnuRedo As ToolStripMenuItem
    Friend WithEvents mnuSep4 As ToolStripSeparator
    Friend WithEvents mnuCopyData As ToolStripMenuItem
    Friend WithEvents mnuDeletePoint As ToolStripMenuItem
    Friend WithEvents mnuView As ToolStripMenuItem
    Friend WithEvents mnuZoomIn As ToolStripMenuItem
    Friend WithEvents mnuZoomOut As ToolStripMenuItem
    Friend WithEvents mnuFitWindow As ToolStripMenuItem
    Friend WithEvents mnuSep5 As ToolStripSeparator
    Friend WithEvents mnuMagnifier As ToolStripMenuItem
    Friend WithEvents mnuGrid As ToolStripMenuItem
    Friend WithEvents mnuLines As ToolStripMenuItem
    Friend WithEvents mnuHelp As ToolStripMenuItem
    Friend WithEvents mnuHowTo As ToolStripMenuItem
    Friend WithEvents mnuAbout As ToolStripMenuItem
    Friend WithEvents toolMain As ToolStrip
    Friend WithEvents tsOpenImage As ToolStripButton
    Friend WithEvents tsPaste As ToolStripButton
    Friend WithEvents tsSep6 As ToolStripSeparator
    Friend WithEvents tsOpenProject As ToolStripButton
    Friend WithEvents tsSaveProject As ToolStripButton
    Friend WithEvents tsSep7 As ToolStripSeparator
    Friend WithEvents tsUndo As ToolStripButton
    Friend WithEvents tsRedo As ToolStripButton
    Friend WithEvents tsSep8 As ToolStripSeparator
    Friend WithEvents tsZoomIn As ToolStripButton
    Friend WithEvents tsZoomOut As ToolStripButton
    Friend WithEvents tsFit As ToolStripButton
    Friend WithEvents tsSep9 As ToolStripSeparator
    Friend WithEvents tsMagnifier As ToolStripButton
    Friend WithEvents tsGrid As ToolStripButton
    Friend WithEvents statusMain As StatusStrip
    Friend WithEvents lblStatus As ToolStripStatusLabel
    Friend WithEvents lblZoom As ToolStripStatusLabel
    Friend WithEvents lblCoords As ToolStripStatusLabel
    Friend WithEvents canvas As Graphing.GraphCanvas
    Friend WithEvents pnlSide As Panel
    Friend WithEvents tabsMain As TabControl
    Friend WithEvents tabCalibrate As TabPage
    Friend WithEvents tabTrace As TabPage
    Friend WithEvents tabData As TabPage
    Friend WithEvents tabAnalyze As TabPage
    Friend WithEvents lblCalHelp As Label
    Friend WithEvents lblHdrPoint As Label
    Friend WithEvents lblHdrPixel As Label
    Friend WithEvents lblHdrValue As Label
    Friend WithEvents btnPickX1 As Button
    Friend WithEvents lblPixelX1 As Label
    Friend WithEvents txtValueX1 As TextBox
    Friend WithEvents btnPickX2 As Button
    Friend WithEvents lblPixelX2 As Label
    Friend WithEvents txtValueX2 As TextBox
    Friend WithEvents btnPickY1 As Button
    Friend WithEvents lblPixelY1 As Label
    Friend WithEvents txtValueY1 As TextBox
    Friend WithEvents btnPickY2 As Button
    Friend WithEvents lblPixelY2 As Label
    Friend WithEvents txtValueY2 As TextBox
    Friend WithEvents chkLogX As CheckBox
    Friend WithEvents chkLogY As CheckBox
    Friend WithEvents lblCalState As Label
    Friend WithEvents btnClearCalibration As Button
    Friend WithEvents lblCalTip As Label
    Friend WithEvents lblSeriesHeader As Label
    Friend WithEvents lstSeries As ListBox
    Friend WithEvents btnAddSeries As Button
    Friend WithEvents btnRenameSeries As Button
    Friend WithEvents btnColorSeries As Button
    Friend WithEvents btnDeleteSeries As Button
    Friend WithEvents grpTool As GroupBox
    Friend WithEvents rbPan As RadioButton
    Friend WithEvents rbAddPoints As RadioButton
    Friend WithEvents rbDragTrace As RadioButton
    Friend WithEvents nudDragSpacing As NumericUpDown
    Friend WithEvents lblDragSpacingUnit As Label
    Friend WithEvents rbEditPoints As RadioButton
    Friend WithEvents rbAutoTrace As RadioButton
    Friend WithEvents grpAuto As GroupBox
    Friend WithEvents lblTolerance As Label
    Friend WithEvents nudTolerance As NumericUpDown
    Friend WithEvents lblAutoStep As Label
    Friend WithEvents nudAutoStep As NumericUpDown
    Friend WithEvents lblAutoStepUnit As Label
    Friend WithEvents lblAutoColorCaption As Label
    Friend WithEvents pnlAutoColor As Panel
    Friend WithEvents chkShowLines As CheckBox
    Friend WithEvents btnClearSeries As Button
    Friend WithEvents lblTraceTip As Label
    Friend WithEvents lblDataSeries As Label
    Friend WithEvents dgvData As DataGridView
    Friend WithEvents grpClean As GroupBox
    Friend WithEvents btnSortX As Button
    Friend WithEvents btnRemoveDuplicates As Button
    Friend WithEvents lblResampleCount As Label
    Friend WithEvents nudResampleCount As NumericUpDown
    Friend WithEvents btnResampleCount As Button
    Friend WithEvents lblResampleStep As Label
    Friend WithEvents txtResampleStep As TextBox
    Friend WithEvents btnResampleStep As Button
    Friend WithEvents grpExport As GroupBox
    Friend WithEvents btnCopyData As Button
    Friend WithEvents btnExportCsv As Button
    Friend WithEvents btnExportExcel As Button
    Friend WithEvents lblAnalyzeSeries As Label
    Friend WithEvents grpStats As GroupBox
    Friend WithEvents lblStats As Label
    Friend WithEvents grpInterpolate As GroupBox
    Friend WithEvents lblInterpolateX As Label
    Friend WithEvents txtInterpolateX As TextBox
    Friend WithEvents btnInterpolate As Button
    Friend WithEvents lblInterpolateResult As Label
    Friend WithEvents grpFit As GroupBox
    Friend WithEvents cmbFitKind As ComboBox
    Friend WithEvents btnFit As Button
    Friend WithEvents chkShowFit As CheckBox
    Friend WithEvents txtFitResult As TextBox
    Friend WithEvents lblAnalyzeTip As Label
    Friend WithEvents colIndex As DataGridViewTextBoxColumn
    Friend WithEvents colX As DataGridViewTextBoxColumn
    Friend WithEvents colY As DataGridViewTextBoxColumn
    Friend WithEvents dlgOpenImage As OpenFileDialog
    Friend WithEvents dlgOpenProject As OpenFileDialog
    Friend WithEvents dlgSaveProject As SaveFileDialog
    Friend WithEvents dlgExport As SaveFileDialog
    Friend WithEvents dlgColor As ColorDialog
    Friend WithEvents hints As ToolTip
    Friend WithEvents errValues As ErrorProvider
End Class
