''' <summary>Main window: running the simulation and recording it.</summary>
Partial Public Class MainForm

    Private Const TickMs = 20
    Private Const SubSteps = 4

    Private ReadOnly _timer As New Timer() With {.Interval = TickMs}
    Private _simulator As Simulator
    Private _running As Boolean
    Private _paused As Boolean
    Private _realPhysics As Boolean
    Private _tickCount As Integer

    Private _gif As GifRecorder
    Private _gifBounds As RectangleF
    Private _gifPage As Circuit
    Private ReadOnly _gifCanvas As New CircuitCanvas()

    Private Sub OnStart(sender As Object, e As EventArgs)
        If _running AndAlso Not _paused Then Return
        If Not _running Then
            If _project.AllElements().Count() = 0 Then
                _statusMessage.Text = "Place some components first, or load one of the examples."
                Return
            End If
            _running = True
            _canvas.PlacingPreset = Nothing
            _canvas.ClearSelection()
            _project.UpdateCrossReferences()
            _simulator = New Simulator(_project.SimulationCircuit()) With {.RealPhysics = _realPhysics}
            _canvas.Simulator = _simulator
            _canvas.Simulating = True
            _simulator.Reset()
            _diagram.Simulator = _simulator
            _plotter.Simulator = _simulator
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
        If _gif IsNot Nothing Then FinishRecording()
        If Not _running Then Return
        _running = False
        _paused = False
        For Each el In _project.AllElements()
            el.ResetSim()
        Next
        _canvas.Simulating = False
        _canvas.Invalidate()
        _diagram.Invalidate()
        _plotter.RefreshPlot()
        _statusMessage.Text = ""
        _statusMessage.ForeColor = SystemColors.ControlText
        UpdateUiState()
    End Sub

    Private Sub OnToggleReal(sender As Object, e As EventArgs)
        _realPhysics = Not _realPhysics
        AppSettings.RealPhysics = _realPhysics
        AppSettings.Save()
        If _running Then
            StopSimulation()
            OnStart(Nothing, EventArgs.Empty)
        End If
        _statusMessage.Text = If(_realPhysics,
            "Realistic physics: pressures build up, cylinders move according to bore, load and friction, air consumption is measured.",
            "Ideal mode: pressures switch instantly and cylinders move at their set stroke time.")
        UpdateUiState()
    End Sub

    Private Sub OnTick(sender As Object, e As EventArgs)
        Dim speed = {0.1, 0.25, 0.5, 1.0, 2.0, 4.0}(Math.Max(0, _speedBox.SelectedIndex))
        Dim dt = TickMs / 1000.0 * speed / SubSteps
        For i = 1 To SubSteps
            _simulator.Step(dt)
        Next
        _tickCount += 1
        ShowSimulationStatus()
        _canvas.Invalidate()
        _diagram.Invalidate()
        If _tickCount Mod 5 = 0 Then _plotter.RefreshPlot()
        If _gif IsNot Nothing AndAlso _tickCount Mod 3 = 0 Then CaptureFrame()
    End Sub

    Private Sub OnElementOperated(sender As Object, e As EventArgs)
        If Not _running Then Return
        _simulator.RunLogic()
        ShowSimulationStatus()
        _canvas.Invalidate()
    End Sub

    Private Sub ShowSimulationStatus()
        _statusMode.Text = $"Simulating{If(_realPhysics, " (realistic)", "")}   t = {_simulator.Time:0.00} s{If(_paused, "  (paused)", "")}" &
                           If(_gif IsNot Nothing, $"   ● REC {_gif.FrameCount}", "")
        If _simulator.Warnings.Count > 0 Then
            _statusMessage.Text = "Warning: " & _simulator.Warnings(0)
            _statusMessage.ForeColor = Color.DarkRed
        Else
            If _statusMessage.ForeColor = Color.DarkRed Then _statusMessage.ForeColor = If(AppSettings.DarkMode, Color.Gainsboro, SystemColors.ControlText)
            Dim cost = _simulator.CostSummary(_project.Info)
            If cost <> "" Then _statusMessage.Text = cost
        End If
    End Sub

    ' ----------------------------------------------------------------- GIF recording

    Private Sub OnToggleRecord(sender As Object, e As EventArgs)
        If _gif IsNot Nothing Then
            FinishRecording()
            Return
        End If
        If CurrentCircuit.Elements.Count = 0 Then Return
        _gifPage = CurrentCircuit
        _gifBounds = _gifPage.Bounds()
        _gifBounds.Inflate(30, 30)
        Dim scale = Math.Min(1.0F, 900.0F / Math.Max(1, _gifBounds.Width))
        _gif = New GifRecorder(CInt(_gifBounds.Width * scale), CInt(_gifBounds.Height * scale), delayCs:=6)
        _statusMessage.Text = "Recording: start the simulation and operate the circuit. Click 'Record GIF' again to save."
        If Not _running Then OnStart(Nothing, EventArgs.Empty)
        UpdateUiState()
    End Sub

    Private Sub CaptureFrame()
        Using bmp As New Bitmap(CInt(_gifBounds.Width), CInt(_gifBounds.Height))
            Using g = Graphics.FromImage(bmp)
                g.Clear(Color.White)
                g.TranslateTransform(-_gifBounds.Left, -_gifBounds.Top)
                _gifCanvas.Circuit = _gifPage
                _gifCanvas.Simulating = True
                _gifCanvas.PaintTo(g)
            End Using
            If Not _gif.AddFrame(bmp) Then FinishRecording()
        End Using
    End Sub

    Private Sub FinishRecording()
        Dim rec = _gif
        _gif = Nothing
        UpdateUiState()
        If rec Is Nothing OrElse rec.FrameCount = 0 Then Return
        Using dlg As New SaveFileDialog() With {.Filter = "Animated GIF (*.gif)|*.gif", .FileName = SafeName(_project.Info.Title) & ".gif"}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            rec.Save(dlg.FileName)
            _statusMessage.Text = $"Saved {rec.FrameCount} frames to {dlg.FileName}"
        End Using
    End Sub
End Class
