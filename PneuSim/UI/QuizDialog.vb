Imports System.Text

''' <summary>Practice quiz (instant feedback) or timed exam (score at the end).</summary>
Public Class QuizDialog
    Inherits Form

    Private ReadOnly _exam As Boolean
    Private ReadOnly _questions As List(Of QuizQuestion)
    Private ReadOnly _answers As Integer()
    Private _index As Integer
    Private _secondsLeft As Integer
    Private _checked As Boolean
    Private _score As Integer

    Private ReadOnly _header As New Label() With {.Dock = DockStyle.Top, .Height = 30, .Font = New Font("Segoe UI", 10, FontStyle.Bold), .Padding = New Padding(10, 6, 0, 0)}
    Private ReadOnly _picture As New PictureBox() With {.Dock = DockStyle.Top, .Height = 110, .SizeMode = PictureBoxSizeMode.CenterImage, .BackColor = Color.White}
    Private ReadOnly _question As New Label() With {.Dock = DockStyle.Top, .Height = 60, .Font = New Font("Segoe UI", 11), .Padding = New Padding(10, 8, 10, 0)}
    Private ReadOnly _optionsPanel As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 130, .FlowDirection = FlowDirection.TopDown, .Padding = New Padding(14, 0, 0, 0)}
    Private ReadOnly _options As New List(Of RadioButton)
    Private ReadOnly _feedback As New Label() With {.Dock = DockStyle.Fill, .Font = New Font("Segoe UI", 9.5F), .Padding = New Padding(12, 6, 12, 0)}
    Private ReadOnly _next As New Button() With {.Text = "Check", .Width = 110, .Height = 32}
    Private ReadOnly _back As New Button() With {.Text = "Back", .Width = 90, .Height = 32}
    Private ReadOnly _timer As New Timer() With {.Interval = 1000}

    Public Sub New(exam As Boolean)
        _exam = exam
        _questions = QuestionBank.Pick(If(exam, 20, 10))
        _answers = Enumerable.Repeat(-1, _questions.Count).ToArray()
        _secondsLeft = 15 * 60
        Text = If(exam, "Exam: pneumatics, electro-pneumatics and hydraulics", "Practice quiz")
        Font = New Font("Segoe UI", 9)
        Size = New Size(700, 560)
        StartPosition = FormStartPosition.CenterParent
        ShowInTaskbar = False
        For i = 0 To 3
            Dim rb As New RadioButton() With {.AutoSize = True, .Font = New Font("Segoe UI", 10), .Margin = New Padding(3, 4, 3, 4)}
            _options.Add(rb)
            _optionsPanel.Controls.Add(rb)
        Next
        Dim bottom As New FlowLayoutPanel() With {.Dock = DockStyle.Bottom, .Height = 46, .FlowDirection = FlowDirection.RightToLeft, .Padding = New Padding(8)}
        bottom.Controls.Add(_next)
        If exam Then bottom.Controls.Add(_back)
        Controls.Add(_feedback)
        Controls.Add(_optionsPanel)
        Controls.Add(_question)
        Controls.Add(_picture)
        Controls.Add(_header)
        Controls.Add(bottom)
        AcceptButton = _next
        AddHandler _next.Click, AddressOf OnNext
        AddHandler _back.Click, AddressOf OnBack
        KeyPreview = True
        AddHandler KeyDown, Sub(s, e)
                                If e.KeyCode = Keys.Escape Then Close()
                            End Sub
        AddHandler _timer.Tick, AddressOf OnTimer
        If exam Then _timer.Start()
        ShowQuestion()
    End Sub

    Private Sub ShowQuestion()
        Dim q = _questions(_index)
        _checked = False
        _header.Text = $"Question {_index + 1} of {_questions.Count}   ({q.Topic})" & If(_exam, $"      Time left: {_secondsLeft \ 60}:{_secondsLeft Mod 60:00}", $"      Score: {_score}")
        _question.Text = q.Text
        If q.SymbolPreset IsNot Nothing Then
            _picture.Image = Library.RenderThumbnail(q.SymbolPreset.Factory.Invoke(), 240, 100)
            _picture.Visible = True
        Else
            _picture.Visible = False
        End If
        For i = 0 To 3
            _options(i).Visible = i < q.Options.Length
            If i < q.Options.Length Then _options(i).Text = q.Options(i)
            ' In the exam, coming back to a question shows the answer given before.
            _options(i).Checked = _exam AndAlso _answers(_index) = i
            _options(i).ForeColor = If(AppSettings.DarkMode, Theme.DarkFore, SystemColors.ControlText)
            _options(i).Enabled = True
        Next
        _feedback.Text = ""
        _next.Text = If(_exam, If(_index = _questions.Count - 1, "Finish", "Next"), "Check")
        _back.Enabled = _index > 0
    End Sub

    Private Sub OnBack(sender As Object, e As EventArgs)
        If _index = 0 Then Return
        _answers(_index) = Chosen()
        _index -= 1
        ShowQuestion()
    End Sub

    Protected Overrides Sub OnFormClosed(e As FormClosedEventArgs)
        ' Stop the exam clock when the window closes (also when it is closed early).
        _timer.Stop()
        _timer.Dispose()
        MyBase.OnFormClosed(e)
    End Sub

    Private Function Chosen() As Integer
        Return _options.FindIndex(Function(o) o.Checked)
    End Function

    Private Sub OnNext(sender As Object, e As EventArgs)
        Dim q = _questions(_index)
        If Not _exam AndAlso Not _checked Then
            Dim a = Chosen()
            If a < 0 Then _feedback.Text = "Choose an answer first." : Return
            _answers(_index) = a
            _checked = True
            If a = q.Correct Then _score += 1
            _options(q.Correct).ForeColor = Color.DarkGreen
            If a <> q.Correct Then _options(a).ForeColor = Color.Firebrick
            For Each o In _options : o.Enabled = False : Next
            _feedback.ForeColor = If(a = q.Correct, Color.DarkGreen, Color.Firebrick)
            _feedback.Text = If(a = q.Correct, "Correct! ", "Not quite. ") & q.Explanation
            _next.Text = If(_index = _questions.Count - 1, "Finish", "Next")
            Return
        End If
        If _exam Then _answers(_index) = Chosen()
        If _index < _questions.Count - 1 Then
            _index += 1
            ShowQuestion()
        Else
            Finish()
        End If
    End Sub

    Private Sub OnTimer(sender As Object, e As EventArgs)
        _secondsLeft -= 1
        If _secondsLeft <= 0 Then
            _answers(_index) = Chosen()
            Finish()
            Return
        End If
        _header.Text = $"Question {_index + 1} of {_questions.Count}   ({_questions(_index).Topic})      Time left: {_secondsLeft \ 60}:{_secondsLeft Mod 60:00}"
    End Sub

    Private Sub Finish()
        _timer.Stop()
        Dim correct = Enumerable.Range(0, _questions.Count).Count(Function(i) _answers(i) = _questions(i).Correct)
        Dim pct = 100.0 * correct / _questions.Count
        Dim grade = If(pct >= 90, "Excellent", If(pct >= 75, "Very good", If(pct >= 60, "Good", If(pct >= 40, "Pass", "Needs more practice"))))
        Dim sb As New StringBuilder()
        sb.AppendLine($"{If(_exam, "Exam", "Quiz")} result: {correct} of {_questions.Count} correct ({pct:0} %) — {grade}")
        sb.AppendLine($"Date: {Date.Now:dd-MM-yyyy HH:mm}")
        sb.AppendLine()
        Dim wrong = Enumerable.Range(0, _questions.Count).Where(Function(i) _answers(i) <> _questions(i).Correct).ToList()
        If wrong.Count > 0 Then
            sb.AppendLine("Review:")
            For Each i In wrong
                Dim q = _questions(i)
                sb.AppendLine($"Q{i + 1}. {q.Text}{If(q.SymbolPreset IsNot Nothing, " [symbol]", "")}")
                sb.AppendLine($"   Your answer: {If(_answers(i) < 0, "(none)", q.Options(_answers(i)))}")
                sb.AppendLine($"   Correct: {q.Options(q.Correct)} — {q.Explanation}")
            Next
        End If
        Controls.Clear()
        Dim box As New TextBox() With {.Multiline = True, .ReadOnly = True, .Dock = DockStyle.Fill, .ScrollBars = ScrollBars.Vertical,
                                       .Font = New Font("Consolas", 9.5F), .BackColor = Color.White, .Text = sb.ToString().Replace(vbLf, vbCrLf).Replace(vbCr & vbCrLf, vbCrLf)}
        Dim bottom As New FlowLayoutPanel() With {.Dock = DockStyle.Bottom, .Height = 46, .FlowDirection = FlowDirection.RightToLeft, .Padding = New Padding(8)}
        Dim close As New Button() With {.Text = "Close", .Width = 90, .Height = 30, .DialogResult = DialogResult.OK}
        Dim save As New Button() With {.Text = "Save result...", .Width = 110, .Height = 30}
        AddHandler save.Click, Sub()
                                   Using dlg As New SaveFileDialog() With {.Filter = "Text file (*.txt)|*.txt", .FileName = "pneusim-result.txt"}
                                       If dlg.ShowDialog(Me) = DialogResult.OK Then IO.File.WriteAllText(dlg.FileName, box.Text)
                                   End Using
                               End Sub
        bottom.Controls.AddRange({close, save})
        Controls.Add(box)
        Controls.Add(bottom)
        AcceptButton = close
    End Sub
End Class
