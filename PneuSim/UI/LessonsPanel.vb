''' <summary>Lesson list with instructions, starting circuit and automatic checking.</summary>
Public Class LessonsPanel
    Inherits UserControl

    Private ReadOnly _list As New ListBox() With {.Dock = DockStyle.Fill, .IntegralHeight = False}
    Private ReadOnly _text As New TextBox() With {.Dock = DockStyle.Fill, .Multiline = True, .ReadOnly = True, .ScrollBars = ScrollBars.Vertical,
                                                  .WordWrap = True, .Font = New Font("Segoe UI", 9.5F), .BackColor = Color.White,
                                                  .BorderStyle = BorderStyle.None}
    ''' <summary>Result of the last check; wraps and scrolls, so long feedback is never cut off.</summary>
    Private ReadOnly _result As New TextBox() With {.Dock = DockStyle.Bottom, .Height = 58, .Multiline = True, .ReadOnly = True, .WordWrap = True,
                                                    .ScrollBars = ScrollBars.Vertical, .Font = New Font("Segoe UI", 9.5F, FontStyle.Bold),
                                                    .BorderStyle = BorderStyle.None, .BackColor = Color.White}

    ''' <summary>Asks the main window to load a starting circuit.</summary>
    Public Event LoadCircuit As EventHandler(Of Circuit)

    ''' <summary>Supplies the circuit to check (all pages combined).</summary>
    Public Property CurrentCircuit As Func(Of Circuit)

    Public Sub New()
        For Each l In Lessons.All
            _list.Items.Add(l)
        Next
        ' Left: the lessons and the buttons; right: the task text with the result underneath.
        Dim buttons As New TableLayoutPanel() With {.Dock = DockStyle.Bottom, .Height = 96, .ColumnCount = 1, .RowCount = 3}
        Dim load As New Button() With {.Text = "Load starting parts", .Dock = DockStyle.Fill}
        Dim check As New Button() With {.Text = "Check my solution", .Dock = DockStyle.Fill}
        Dim hint As New Button() With {.Text = "Hint", .Dock = DockStyle.Fill}
        For i = 0 To 2
            buttons.RowStyles.Add(New RowStyle(SizeType.Percent, 33.3F))
        Next
        buttons.Controls.Add(load, 0, 0)
        buttons.Controls.Add(check, 0, 1)
        buttons.Controls.Add(hint, 0, 2)
        Dim left As New Panel() With {.Dock = DockStyle.Left, .Width = 220}
        left.Controls.Add(_list)
        left.Controls.Add(buttons)
        Dim right As New Panel() With {.Dock = DockStyle.Fill, .Padding = New Padding(8, 4, 4, 4)}
        right.Controls.Add(_text)
        right.Controls.Add(_result)
        Controls.Add(right)
        Controls.Add(left)

        AddHandler _list.SelectedIndexChanged, Sub() ShowLesson()
        AddHandler load.Click, Sub()
                                   Dim l = Current()
                                   If l Is Nothing Then Return
                                   RaiseEvent LoadCircuit(Me, If(l.Start Is Nothing, New Circuit(), l.Start.Invoke()))
                               End Sub
        AddHandler check.Click, Sub() CheckLesson()
        AddHandler hint.Click, Sub()
                                   Dim l = Current()
                                   If l IsNot Nothing Then ShowResult("Hint: " & l.Hint, Color.DarkBlue)
                               End Sub
        AddHandler _list.DrawItem, AddressOf DrawItem
        _list.DrawMode = DrawMode.OwnerDrawFixed
        _list.ItemHeight = 22
        _list.SelectedIndex = 0
    End Sub

    Private Function Current() As Lesson
        Return TryCast(_list.SelectedItem, Lesson)
    End Function

    Private Sub ShowResult(text As String, color As Color)
        _result.ForeColor = If(AppSettings.DarkMode, ControlPaint.LightLight(color), color)
        _result.Text = text
    End Sub

    Private Sub ShowLesson()
        Dim l = Current()
        If l Is Nothing Then Return
        _text.Text = l.Title & If(AppSettings.CompletedLessons.Contains(l.Title), "   (passed)", "") & vbCrLf & vbCrLf &
                     "TASK" & vbCrLf & l.Goal & vbCrLf & vbCrLf &
                     If(l.Start Is Nothing, "Start with an empty page (File > New) or press 'Load starting parts' for an empty page.",
                        "Press 'Load starting parts' to get the components, then connect them.") & vbCrLf &
                     "When you think it works, press 'Check my solution'. PneuSim tests your circuit by simulating it."
        _result.Text = ""
    End Sub

    Private Sub CheckLesson()
        Dim l = Current()
        If l Is Nothing OrElse CurrentCircuit Is Nothing Then Return
        Try
            Dim r = l.Check.Invoke(CurrentCircuit.Invoke())
            ShowResult(If(r.Passed, "✔ ", "✘ ") & r.Feedback, If(r.Passed, Color.DarkGreen, Color.Firebrick))
            If r.Passed AndAlso AppSettings.CompletedLessons.Add(l.Title) Then
                AppSettings.Save()
                _list.Invalidate()
            End If
        Catch ex As Exception
            ShowResult("Could not test the circuit: " & ex.Message, Color.Firebrick)
        End Try
    End Sub

    Private Sub DrawItem(sender As Object, e As DrawItemEventArgs)
        If e.Index < 0 Then Return
        e.DrawBackground()
        Dim l = DirectCast(_list.Items(e.Index), Lesson)
        Dim done = AppSettings.CompletedLessons.Contains(l.Title)
        Dim mark = If(done, "✔ ", "   ")
        Using b As New SolidBrush(If((e.State And DrawItemState.Selected) <> 0, SystemColors.HighlightText, If(done, Color.SeaGreen, e.ForeColor)))
            e.Graphics.DrawString(mark & l.Title, e.Font, b, e.Bounds.X + 2, e.Bounds.Y + 3)
        End Using
    End Sub
End Class
