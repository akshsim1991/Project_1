''' <summary>Main window: faults and troubleshooting exercises, state inspector, replay and parameter sweep.</summary>
Partial Public Class MainForm

    Private ReadOnly _inspector As New InspectorPanel() With {.Dock = DockStyle.Fill}
    Private ReadOnly _trouble As New TroubleshootPanel() With {.Dock = DockStyle.Fill}
    Private _exercise As TroubleshootExercise
    Private ReadOnly _rnd As New Random()
    Private _recorder As SimulationRecorder
    Private _btnBack, _btnStepFwd, _btnPrevEvent, _btnNextEvent As ToolStripButton
    Private _timeline As TrackBar
    Private _timelineHost As ToolStripControlHost
    Private _updatingTimeline As Boolean

    ''' <summary>Called from the constructor: hooks up the panels and canvas events.</summary>
    Private Sub InitTroubleshooting()
        AddHandler _canvas.InspectRequested, AddressOf OnInspect
        AddHandler _canvas.TargetPicked, AddressOf OnTargetPicked
        AddHandler _inspector.SettingChanged, Sub()
                                                  If _running Then _simulator.RunLogic()
                                                  _canvas.Invalidate()
                                              End Sub
        AddHandler _inspector.SettingCommitted, Sub() OnCircuitModified()
        AddHandler _trouble.Command, AddressOf OnTroubleCommand
    End Sub

    Private Function BuildTroubleshootMenu() As ToolStripMenuItem
        Dim m As New ToolStripMenuItem("&Troubleshooting")
        m.DropDownItems.Add(Item("&New exercise (hidden fault)", Sub() OnTroubleCommand(Nothing, "start")))
        m.DropDownItems.Add(Item("&Hint", Sub() OnTroubleCommand(Nothing, "hint"), Keys.Control Or Keys.H))
        m.DropDownItems.Add(Item("I &found the fault…", Sub() OnTroubleCommand(Nothing, "found")))
        m.DropDownItems.Add(Item("&Show the fault", Sub() OnTroubleCommand(Nothing, "giveup")))
        m.DropDownItems.Add(New ToolStripSeparator())
        m.DropDownItems.Add(Item("&Put a fault into the selection…", Sub() OnTroubleCommand(Nothing, "add")))
        m.DropDownItems.Add(Item("&Repair all faults", Sub() OnTroubleCommand(Nothing, "clear")))
        Return m
    End Function

    ' ----------------------------------------------------------------- inspector

    Private Sub OnInspect(sender As Object, target As Object)
        _inspector.Target = target
        If _exercise IsNot Nothing AndAlso Not _exercise.IsOver AndAlso _exercise.RecordCheck(target) Then
            _trouble.ShowExercise(_exercise)
            _statusMessage.Text = $"Check {_exercise.Checks.Count}: {TroubleshootExercise.NameOf_(target)} (see the Inspector tab)."
        End If
        _inspector.Note = If(_exercise IsNot Nothing AndAlso Not _exercise.IsOver,
                             $"Troubleshooting: check {_exercise.Checks.Count}, {_exercise.HintsUsed} hints", "")
        ShowBottomTab("Inspector")
    End Sub

    Private Sub ClearInspector()
        _inspector.Target = Nothing
        _canvas.InspectedObject = Nothing
    End Sub

    ' ----------------------------------------------------------------- troubleshooting

    Private Sub OnTroubleCommand(sender As Object, cmd As String)
        Select Case cmd
            Case "start" : StartExercise()
            Case "hint"
                If _exercise Is Nothing OrElse _exercise.IsOver Then Return
                _trouble.AddMessage("Hint: " & _exercise.NextHint(_project))
            Case "found"
                If _exercise Is Nothing OrElse _exercise.IsOver Then Return
                _canvas.PickingTarget = True
                _canvas.Cursor = Cursors.Hand
                _statusMessage.Text = "Click the component or tube you think is faulty (Esc to cancel)."
                ShowBottomTab("Troubleshoot")
            Case "giveup"
                If _exercise Is Nothing OrElse _exercise.IsOver Then Return
                _trouble.AddMessage(_exercise.GiveUp(_project))
                MarkDirty()
            Case "report" : SaveExerciseReport()
            Case "add" : AddFaultToSelection()
            Case "clear"
                If _project.Pages.Sum(Function(p) p.Circuit.FaultCount()) = 0 Then
                    _statusMessage.Text = "There are no faults in the project."
                    Return
                End If
                TroubleshootExercise.RemoveAllFaults(_project)
                _exercise = Nothing
                _trouble.AddMessage("All faults repaired.")
                AfterFaultChange()
        End Select
        _trouble.ShowExercise(_exercise)
        _canvas.Invalidate()
    End Sub

    Private Sub StartExercise()
        If _running Then StopSimulation()
        Dim ex = TroubleshootExercise.StartRandom(_project, _rnd)
        If ex Is Nothing Then
            _trouble.AddMessage("This circuit has nothing that can fail. Load an example or draw a circuit first.")
            Return
        End If
        _exercise = ex
        AfterFaultChange()
        _trouble.AddMessage("A fault is now hidden somewhere in the circuit. Start the simulation, operate the circuit and right-click components or tubes to measure them. " &
                            "When you know where the fault is, click 'I found it…'.")
        ShowBottomTab("Troubleshoot")
        _trouble.ShowExercise(_exercise)
    End Sub

    ''' <summary>Starts an exercise for a file that contains a hidden fault (made by a teacher).</summary>
    Private Sub CheckForHiddenFault()
        If TroubleshootExercise.FindHidden(_project) Is Nothing Then
            _exercise = Nothing
        Else
            _exercise = New TroubleshootExercise()
            _trouble.AddMessage("This circuit contains a hidden fault. Find it! Run the simulation, measure with right-clicks, then click 'I found it…'.")
            ShowBottomTab("Troubleshoot")
        End If
        _trouble.ShowExercise(_exercise)
    End Sub

    Private Sub OnTargetPicked(sender As Object, target As Object)
        If _exercise Is Nothing OrElse _exercise.IsOver Then Return
        Using dlg As New FaultDialog(target, diagnosis:=True)
            If dlg.ShowDialog(Me) <> DialogResult.OK Then _statusMessage.Text = "" : Return
            Dim r = _exercise.Guess(_project, target, dlg.SelectedKind)
            _trouble.AddMessage(r.Message)
            If r.Correct Then
                _trouble.AddMessage($"Score: {_exercise.Score()} / 100 ({_exercise.Checks.Count} checks, {_exercise.HintsUsed} hints, {_exercise.WrongGuesses} wrong guesses). " &
                                    "Use 'Repair all' to fix the circuit, or start a new exercise.")
                MarkDirty()
            End If
            _statusMessage.Text = r.Message
        End Using
        _trouble.ShowExercise(_exercise)
        _canvas.Invalidate()
    End Sub

    Private Sub AddFaultToSelection()
        If _running Then _statusMessage.Text = "Stop the simulation first (F11) to put in a fault." : Return
        Dim target As Object = If(CObj(_canvas.SelectedElement), _canvas.SelectedTube)
        If target Is Nothing Then
            _statusMessage.Text = "Select a component or a tube first, then click 'Add fault…'."
            ShowBottomTab("Troubleshoot")
            Return
        End If
        If TroubleshootExercise.PossibleFaultsOf(target).Length = 0 Then
            _statusMessage.Text = $"{TroubleshootExercise.NameOf_(target)} cannot have a fault."
            Return
        End If
        Using dlg As New FaultDialog(target, diagnosis:=False)
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            TroubleshootExercise.SetFault(target, dlg.SelectedKind, dlg.Hidden)
            If dlg.Hidden AndAlso dlg.SelectedKind <> FaultKind.None Then
                ' One hidden fault per exercise: make every other fault visible.
                For Each pg In _project.Pages
                    For Each e In pg.Circuit.Elements.Where(Function(x) x IsNot target)
                        e.FaultHidden = False
                    Next
                    For Each t In pg.Circuit.Tubes.Where(Function(x) x IsNot target)
                        t.FaultHidden = False
                    Next
                Next
                _trouble.AddMessage($"Hidden fault put into {TroubleshootExercise.NameOf_(target)}. Save the file and give it to your students: it opens as an exercise.")
            ElseIf dlg.SelectedKind <> FaultKind.None Then
                _trouble.AddMessage($"{TroubleshootExercise.NameOf_(target)}: {TroubleshootExercise.DescribeFault(target, dlg.SelectedKind)}. Run the simulation to see what it does.")
            End If
        End Using
        AfterFaultChange()
    End Sub

    Private Sub AfterFaultChange()
        _canvas.Invalidate()
        OnCircuitModified()
    End Sub

    Private Sub SaveExerciseReport()
        If _exercise Is Nothing Then Return
        Using dlg As New SaveFileDialog() With {.Filter = "Text file (*.txt)|*.txt", .FileName = SafeName(_project.Info.Title) & " troubleshooting.txt"}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            IO.File.WriteAllText(dlg.FileName, _exercise.Report(_project.Info.Title), System.Text.Encoding.UTF8)
            _statusMessage.Text = "Saved " & dlg.FileName
        End Using
    End Sub

    ' ----------------------------------------------------------------- replay

    Private Sub AddReplayButtons(bar As ToolStrip)
        _btnBack = Button("Step back", Icons.StepIcon(False, False), Sub() ReplayStep(-1), iconOnly:=True)
        _btnStepFwd = Button("Step forward", Icons.StepIcon(True, False), Sub() ReplayStep(+1), iconOnly:=True)
        _btnPrevEvent = Button("Back to the previous event", Icons.StepIcon(False, True), Sub() ReplayEvent(-1), iconOnly:=True)
        _btnNextEvent = Button("Forward to the next event (a valve, relay or cylinder changes)", Icons.StepIcon(True, True), Sub() ReplayEvent(+1), iconOnly:=True)
        _timeline = New TrackBar() With {.Minimum = 0, .Maximum = 0, .TickStyle = TickStyle.None, .AutoSize = False, .Height = 22, .Width = 150}
        _timelineHost = New ToolStripControlHost(_timeline) With {.ToolTipText = "Replay: drag to go back in time"}
        AddHandler _timeline.Scroll, Sub()
                                         If _updatingTimeline OrElse _recorder Is Nothing Then Return
                                         PauseForReplay()
                                         _recorder.Restore(_timeline.Value)
                                         AfterReplayMove()
                                     End Sub
        bar.Items.AddRange({_btnPrevEvent, _btnBack, _btnStepFwd, _btnNextEvent, _timelineHost})
    End Sub

    Private Sub PauseForReplay()
        If _running AndAlso Not _paused Then
            _paused = True
            _timer.Stop()
        End If
    End Sub

    ''' <summary>One frame back, or one tick forward.</summary>
    Private Sub ReplayStep(direction As Integer)
        If Not _running OrElse _recorder Is Nothing Then Return
        PauseForReplay()
        If direction < 0 Then
            _recorder.Restore(_recorder.Index - 1)
        ElseIf _recorder.IsRewound Then
            _recorder.Restore(_recorder.Index + 1)
        Else
            AdvanceSimulation(SimulationRecorder.Interval)
        End If
        AfterReplayMove()
    End Sub

    ''' <summary>Jumps to the previous or next moment where something switched.</summary>
    Private Sub ReplayEvent(direction As Integer)
        If Not _running OrElse _recorder Is Nothing Then Return
        PauseForReplay()
        If direction < 0 Then
            Dim i = _recorder.PreviousEventFrame()
            _recorder.Restore(If(i < 0, 0, i))
        Else
            Dim i = _recorder.NextEventFrame()
            If i >= 0 Then
                _recorder.Restore(i)
            Else
                If _recorder.IsRewound Then _recorder.Restore(_recorder.Count - 1)
                Dim before = _simulator.DiscreteState()
                Dim limit = _simulator.Time + 30
                Do
                    AdvanceSimulation(SimulationRecorder.Interval)
                Loop While _simulator.DiscreteState() = before AndAlso _simulator.Time < limit
                If _simulator.DiscreteState() = before Then _statusMessage.Text = "Nothing switched in the next 30 s."
            End If
        End If
        AfterReplayMove()
    End Sub

    ''' <summary>Runs the simulation forward by some seconds and records it.</summary>
    Private Sub AdvanceSimulation(seconds As Double)
        Dim dt = TickMs / 1000.0 / SubSteps
        Dim n = Math.Max(1, CInt(Math.Round(seconds / dt)))
        For i = 1 To n
            _simulator.Step(dt)
            _recorder.Record()
        Next
    End Sub

    Private Sub AfterReplayMove()
        _canvas.Invalidate()
        _diagram.Invalidate()
        _plotter.RefreshPlot()
        _inspector.RefreshValues()
        UpdateUiState()
        UpdateReplayUi()
    End Sub

    Private Sub UpdateReplayUi()
        If _timeline Is Nothing Then Return
        Dim has = _running AndAlso _recorder IsNot Nothing AndAlso _recorder.Count > 0
        For Each b In {_btnBack, _btnStepFwd, _btnPrevEvent, _btnNextEvent}
            b.Enabled = _running
        Next
        _timelineHost.Enabled = has
        _updatingTimeline = True
        If has Then
            _timeline.Maximum = Math.Max(0, _recorder.Count - 1)
            _timeline.Value = Math.Max(0, Math.Min(_timeline.Maximum, _recorder.Index))
        Else
            _timeline.Maximum = 0
        End If
        _updatingTimeline = False
    End Sub

    ' ----------------------------------------------------------------- parameter sweep

    Private Sub OnSweep(sender As Object, e As EventArgs)
        If _project.AllElements().Count() = 0 Then _statusMessage.Text = "Draw or load a circuit first." : Return
        If _running Then StopSimulation()
        Using dlg As New SweepDialog(_project, _realPhysics)
            dlg.ShowDialog(Me)
        End Using
    End Sub
End Class
