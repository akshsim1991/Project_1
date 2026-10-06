''' <summary>Bottom panel for faults and troubleshooting exercises.</summary>
Public Class TroubleshootPanel
    Inherits UserControl

    Private ReadOnly _status As New Label() With {.Dock = DockStyle.Top, .Height = 24, .Font = New Font("Segoe UI", 9, FontStyle.Bold), .Padding = New Padding(4, 4, 0, 0)}
    Private ReadOnly _messages As New TextBox() With {.Dock = DockStyle.Fill, .Multiline = True, .ReadOnly = True, .ScrollBars = ScrollBars.Vertical, .WordWrap = True}
    Private ReadOnly _log As New ListBox() With {.Dock = DockStyle.Right, .Width = 360, .IntegralHeight = False, .HorizontalScrollbar = True}
    Private ReadOnly _buttons As New Dictionary(Of String, Button)

    ''' <summary>A button was clicked: "add", "clear", "start", "hint", "found", "giveup", "report".</summary>
    Public Event Command As EventHandler(Of String)

    Public Sub New()
        Dim bar As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 32, .WrapContents = False}
        AddButton(bar, "start", "New exercise", "Hides one random fault in the circuit. Start the simulation, operate and measure (right-click) to find it.")
        AddButton(bar, "hint", "Hint", "Each hint tells you more: first the area, then the neighbours, then the cause. Hints cost points.")
        AddButton(bar, "found", "I found it…", "Then click the component or tube you think is faulty.")
        AddButton(bar, "giveup", "Show fault", "Ends the exercise and shows where the fault was.")
        AddButton(bar, "report", "Report…", "Saves the diagnosis log and score as a text file.")
        bar.Controls.Add(New Label() With {.Text = "  |  ", .AutoSize = True, .Padding = New Padding(0, 7, 0, 0)})
        AddButton(bar, "add", "Add fault…", "Puts a fault into the selected component or tube. Select a component or tube in edit mode first.")
        AddButton(bar, "clear", "Repair all", "Removes every fault from the project.")
        Controls.Add(_messages)
        Controls.Add(_log)
        Controls.Add(_status)
        Controls.Add(bar)
        ShowExercise(Nothing)
        _messages.Text = "Troubleshooting practice:" & vbCrLf &
            "• 'New exercise' hides a fault in the circuit. Run it, operate it, right-click parts to measure them, then say where the fault is." & vbCrLf &
            "• Teachers: select a part, 'Add fault…', tick 'Hidden' and save the file for the students." & vbCrLf &
            "• Right-clicking a part during the simulation counts as one check; few checks and no hints give the best score."
    End Sub

    Private Sub AddButton(bar As FlowLayoutPanel, key As String, text As String, tip As String)
        Dim b As New Button() With {.Text = text, .AutoSize = True, .Height = 26}
        AddHandler b.Click, Sub() RaiseEvent Command(Me, key)
        Dim tt As New ToolTip()
        tt.SetToolTip(b, tip)
        bar.Controls.Add(b)
        _buttons(key) = b
    End Sub

    ''' <summary>Shows the state of an exercise (Nothing: none running).</summary>
    Public Sub ShowExercise(ex As TroubleshootExercise)
        _status.Text = If(ex Is Nothing, "No exercise running.", ex.StatusText())
        Dim running = ex IsNot Nothing AndAlso Not ex.IsOver
        _buttons("hint").Enabled = running
        _buttons("found").Enabled = running
        _buttons("giveup").Enabled = running
        _buttons("report").Enabled = ex IsNot Nothing
        If ex Is Nothing Then
            _log.Items.Clear()
        ElseIf _log.Items.Count <> ex.Log.Count Then
            _log.BeginUpdate()
            _log.Items.Clear()
            For Each l In ex.Log
                _log.Items.Add(l)
            Next
            _log.TopIndex = Math.Max(0, _log.Items.Count - 1)
            _log.EndUpdate()
        End If
    End Sub

    Public Sub AddMessage(text As String)
        _messages.AppendText(If(_messages.TextLength > 0, vbCrLf & vbCrLf, "") & text)
    End Sub
End Class
