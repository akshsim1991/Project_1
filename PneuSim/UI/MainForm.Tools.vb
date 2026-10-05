''' <summary>Main window: checker, explainer, generator, calculators, learning tools and help.</summary>
Partial Public Class MainForm

    Private ReadOnly _checkList As New ListView() With {.View = View.Details, .FullRowSelect = True, .HideSelection = False, .Dock = DockStyle.Fill}
    Private ReadOnly _checkTimer As New Timer() With {.Interval = 700}
    Private ReadOnly _fixCascade As New Button() With {.Text = "Fix: redesign (cascade)", .AutoSize = True, .Enabled = False}
    Private ReadOnly _fixElectro As New Button() With {.Text = "Fix: redesign (electro-pneumatic)", .AutoSize = True, .Enabled = False}
    Private ReadOnly _explainText As New TextBox() With {.Multiline = True, .ReadOnly = True, .ScrollBars = ScrollBars.Vertical, .WordWrap = True,
                                                         .Dock = DockStyle.Fill, .Font = New Font("Consolas", 9)}
    Private ReadOnly _explainOperate As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDownList, .Width = 220}
    Private _issues As New List(Of CheckIssue)
    Private _cutaway As CutawayWindow

    ' ----------------------------------------------------------------- check panel

    Private Function BuildCheckPanel() As Control
        Dim panel As New Panel()
        _checkList.Columns.Add("", 70)
        _checkList.Columns.Add("Problem", 900)
        Dim bar As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 32, .WrapContents = False}
        Dim run As New Button() With {.Text = "Run full check (with a test run)", .AutoSize = True}
        AddHandler run.Click, AddressOf OnFullCheck
        bar.Controls.AddRange({run, _fixCascade, _fixElectro})
        panel.Controls.Add(_checkList)
        panel.Controls.Add(bar)
        AddHandler _checkTimer.Tick, Sub()
                                         _checkTimer.Stop()
                                         RunStaticChecks()
                                     End Sub
        AddHandler _checkList.SelectedIndexChanged, AddressOf OnIssueSelected
        AddHandler _checkList.DoubleClick, AddressOf OnIssueActivated
        AddHandler _fixCascade.Click, Sub() ApplyFix(SelectedIssue()?.Fix)
        AddHandler _fixElectro.Click, Sub()
                                          Dim i = SelectedIssue()
                                          If i IsNot Nothing Then ApplyFix(CircuitAnalysis.ElectroFix(i))
                                      End Sub
        Return panel
    End Function

    ''' <summary>Re-checks the circuit shortly after the user stops editing.</summary>
    Private Sub ScheduleCheck()
        _checkTimer.Stop()
        _checkTimer.Start()
    End Sub

    Private Sub RunStaticChecks()
        If _project Is Nothing Then Return
        Try
            Dim found = CircuitAnalysis.StaticChecks(_project.SimulationCircuit())
            If found.Count = 0 AndAlso _project.AllElements().Any() Then
                found.Add(New CheckIssue With {.Severity = IssueSeverity.Info, .Message = "No problems found while drawing. Click 'Run full check' to test-run the circuit as well."})
            End If
            ShowIssues(found)
            FillOperateList()
        Catch ex As Exception
            ShowIssues(New List(Of CheckIssue) From {New CheckIssue With {.Severity = IssueSeverity.Info, .Message = "The checker could not analyse this circuit: " & ex.Message}})
        End Try
    End Sub

    Private Sub OnFullCheck(sender As Object, e As EventArgs)
        Cursor = Cursors.WaitCursor
        Try
            Dim c = _project.SimulationCircuit()
            Dim issues = CircuitAnalysis.StaticChecks(c)
            If Not issues.Any(Function(i) i.Severity = IssueSeverity.Error AndAlso i.Message.Contains("is empty")) Then
                issues.AddRange(CircuitAnalysis.DynamicCheck(c, _realPhysics))
            End If
            If issues.Count = 0 Then issues.Add(New CheckIssue With {.Severity = IssueSeverity.Info, .Message = "No problems found. The test run completed without warnings."})
            ShowIssues(issues)
            ShowBottomTab("Check circuit")
        Finally
            Cursor = Cursors.Default
        End Try
    End Sub

    Private Sub ShowIssues(issues As List(Of CheckIssue))
        _issues = issues.OrderBy(Function(i) i.Severity).ToList()
        _checkList.BeginUpdate()
        _checkList.Items.Clear()
        For Each i In _issues
            Dim item As New ListViewItem({i.Severity.ToString(), i.Message}) With {.Tag = i}
            item.ForeColor = If(i.Severity = IssueSeverity.Error, Color.Firebrick, If(i.Severity = IssueSeverity.Warning, Color.DarkOrange, If(AppSettings.DarkMode, Color.Gainsboro, Color.DimGray)))
            _checkList.Items.Add(item)
        Next
        _checkList.EndUpdate()
        Dim errors = _issues.Where(Function(i) i.Severity = IssueSeverity.Error).Count()
        For Each p As TabPage In _bottomTabs.TabPages
            If p.Text.StartsWith("Check circuit") Then p.Text = "Check circuit" & If(errors > 0, $" ({errors})", "")
        Next
        OnIssueSelected(Nothing, EventArgs.Empty)
    End Sub

    Private Function SelectedIssue() As CheckIssue
        Return If(_checkList.SelectedItems.Count = 0, Nothing, TryCast(_checkList.SelectedItems(0).Tag, CheckIssue))
    End Function

    Private Sub OnIssueSelected(sender As Object, e As EventArgs)
        Dim i = SelectedIssue()
        _fixCascade.Enabled = i IsNot Nothing AndAlso i.Fix IsNot Nothing AndAlso Not _running
        _fixElectro.Enabled = _fixCascade.Enabled
        If i IsNot Nothing AndAlso i.FixLabel IsNot Nothing Then _fixCascade.Text = "Fix: " & i.FixLabel.Replace("Redesign", "redesign")
    End Sub

    Private Sub OnIssueActivated(sender As Object, e As EventArgs)
        Dim i = SelectedIssue()
        If i Is Nothing OrElse i.Element Is Nothing OrElse _running Then Return
        Dim page = _project.Pages.FindIndex(Function(p) p.Circuit.Elements.Contains(i.Element))
        If page >= 0 Then
            ShowPage(page)
            _canvas.SelectElements({i.Element})
        End If
    End Sub

    Private Sub ApplyFix(fix As Func(Of Circuit))
        If fix Is Nothing OrElse _running Then Return
        If MessageBox.Show("Replace the circuit on this page with a corrected design? (You can undo this.)", AppName,
                           MessageBoxButtons.OKCancel, MessageBoxIcon.Question) <> DialogResult.OK Then Return
        _project.Pages(_pageIndex).Circuit = fix.Invoke()
        ShowPage(_pageIndex)
        FitView()
        OnCircuitModified()
        _statusMessage.Text = "Circuit redesigned without signal overlap. Press Start (F9) to try it."
    End Sub

    ' ----------------------------------------------------------------- explain panel

    Private Function BuildExplainPanel() As Control
        Dim panel As New Panel()
        Dim bar As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 32, .WrapContents = False}
        bar.Controls.Add(New Label() With {.Text = "Operate:", .AutoSize = True, .Padding = New Padding(0, 7, 0, 0)})
        bar.Controls.Add(_explainOperate)
        Dim go As New Button() With {.Text = "Explain my circuit", .AutoSize = True}
        AddHandler go.Click, AddressOf OnExplain
        bar.Controls.Add(go)
        panel.Controls.Add(_explainText)
        panel.Controls.Add(bar)
        AddHandler _explainOperate.DropDown, Sub() FillOperateList()
        Return panel
    End Function

    Private Sub FillOperateList()
        Dim current = TryCast(_explainOperate.SelectedItem, CircuitElement)
        _explainOperate.Items.Clear()
        For Each e In CircuitAnalysis.ManualElements(_project.SimulationCircuit())
            _explainOperate.Items.Add(e)
        Next
        If current IsNot Nothing AndAlso _explainOperate.Items.Contains(current) Then
            _explainOperate.SelectedItem = current
        ElseIf _explainOperate.Items.Count > 0 Then
            _explainOperate.SelectedIndex = 0
        End If
    End Sub

    Private Sub OnExplain(sender As Object, e As EventArgs)
        If _explainOperate.SelectedItem Is Nothing Then FillOperateList()
        Cursor = Cursors.WaitCursor
        Try
            Dim text = CircuitAnalysis.Explain(_project.SimulationCircuit(), TryCast(_explainOperate.SelectedItem, CircuitElement), _realPhysics)
            _explainText.Text = text.Replace(vbLf, vbCrLf).Replace(vbCr & vbCrLf, vbCrLf)
            ShowBottomTab("Explain")
        Catch ex As Exception
            _explainText.Text = "Could not explain this circuit: " & ex.Message
        Finally
            Cursor = Cursors.Default
        End Try
    End Sub

    ' ----------------------------------------------------------------- other tools

    Private Sub OnGenerator(sender As Object, e As EventArgs)
        If _running Then Return
        Using dlg As New GeneratorDialog()
            Theme.Apply(dlg)
            If dlg.ShowDialog(Me) <> DialogResult.OK OrElse dlg.Result Is Nothing Then Return
            If Not ConfirmDiscard() Then Return
            NewProject(New Project(dlg.Result.Circuit, "Generated circuit"))
            FitView()
            _dirty = True
            UpdateTitle()
            _explainText.Text = dlg.Result.Explanation.Replace(vbLf, vbCrLf).Replace(vbCr & vbCrLf, vbCrLf)
            ShowBottomTab("Explain")
            _statusMessage.Text = "Circuit generated. Press Start (F9), then press the start button S1 / 1S0."
        End Using
    End Sub

    Private Sub OnCalculators(sender As Object, e As EventArgs)
        Using dlg As New CalculatorsDialog()
            Theme.Apply(dlg)
            dlg.ShowDialog(Me)
        End Using
    End Sub

    Private Sub OnPartsList(sender As Object, e As EventArgs)
        Using dlg As New PartsListDialog(_project)
            Theme.Apply(dlg)
            dlg.ShowDialog(Me)
            If dlg.PricesChanged Then OnCircuitModified()
        End Using
    End Sub

    Private Sub ShowQuiz(exam As Boolean)
        Using dlg As New QuizDialog(exam)
            Theme.Apply(dlg)
            dlg.ShowDialog(Me)
        End Using
    End Sub

    Private Sub OnCutaway(sender As Object, e As EventArgs)
        If _cutaway Is Nothing OrElse _cutaway.IsDisposed Then
            _cutaway = New CutawayWindow() With {
                .Owner = Me,
                .Location = New Point(Right - 640, Top + 120),
                .ElementSource = Function() If(_canvas.SelectedElement, _canvas.LastHoveredElement),
                .SimulatingSource = Function() _running}
        End If
        _cutaway.Show()
        _cutaway.BringToFront()
    End Sub

    ' ----------------------------------------------------------------- help

    Private Sub OnQuickGuide(sender As Object, e As EventArgs)
        MessageBox.Show(
"BUILDING A CIRCUIT
• Click a component in the library, then click on the drawing (or drag it there). Hover over a symbol for an explanation.
• Drag from one port to another to connect them. Red circles are electrical terminals.
• Drag a box to select several; R rotates, Del deletes, arrow keys nudge, Ctrl+Z / Ctrl+Y undo / redo.
• Ctrl+C, Ctrl+X, Ctrl+V and Ctrl+D copy, cut, paste and duplicate. Drag a tube's middle segment to move it.
• Use several pages (Page menu) and join lines across pages with page connectors of the same name.

SIMULATING
• Start (F9). Pressurized tubes turn blue, live wires red, hydraulic pressure lines orange.
• Click push buttons, switches and pumps; clicking a solenoid valve operates its manual override.
• Realistic physics: pressures build up, cylinders move by bore, load and friction; air consumption and cost are shown.
• Plotter tab: tick any quantity, click for cursor A, right-click for cursor B to measure times.
• Record GIF saves an animation of the running circuit.

TOOLS
• Check my circuit (F6) finds mistakes and signal overlap; Explain (F8) describes the circuit step by step.
• Circuit Generator (Ctrl+G) designs a circuit from a sequence such as A+ B+ B- A-.
• Calculators, parts list with costs, PDF report with title block, SVG and DXF export.
• Learn menu: lessons with automatic checking, practice quiz, timed exam, cutaway view of any component (F7).",
            "PneuSim Quick Guide", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub OnAbout(sender As Object, e As EventArgs)
        MessageBox.Show(
            $"{AppName} {AppVersion}{vbCrLf}Pneumatic, electro-pneumatic and hydraulic circuit design and simulation{vbCrLf}{vbCrLf}" &
            "(c) 2026 Akshaya Simha." & vbCrLf & vbCrLf &
            "Developed in VB.NET initially, and improved it with vibe coding using Claude Pro." & vbCrLf & vbCrLf &
            "Software is used to study pneumatic and hydraulic systems, so that it can be simulated to understand different types of circuits." & vbCrLf & vbCrLf &
            "Symbols follow ISO 1219.",
            "About " & AppName, MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub
End Class
