''' <summary>Lesson list with instructions, starting circuit and automatic checking.</summary>
Public Class LessonsPanel
    Inherits UserControl

    Private ReadOnly _list As New ListBox() With {.Dock = DockStyle.Left, .Width = 220, .IntegralHeight = False}
    Private ReadOnly _text As New TextBox() With {.Dock = DockStyle.Fill, .Multiline = True, .ReadOnly = True, .ScrollBars = ScrollBars.Vertical,
                                                  .Font = New Font("Segoe UI", 9.5F), .BackColor = Color.White}
    Private ReadOnly _result As New Label() With {.Dock = DockStyle.Bottom, .Height = 48, .Padding = New Padding(6, 4, 6, 0), .Font = New Font("Segoe UI", 9.5F, FontStyle.Bold)}
    Private ReadOnly _done As New HashSet(Of String)

    ''' <summary>Asks the main window to load a starting circuit.</summary>
    Public Event LoadCircuit As EventHandler(Of Circuit)
    ''' <summary>Supplies the circuit to check (all pages combined).</summary>
    Public Property CurrentCircuit As Func(Of Circuit)

    Public Sub New()
        For Each l In Lessons.All
            _list.Items.Add(l)
        Next
        Dim buttons As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 34, .WrapContents = False}
        Dim load As New Button() With {.Text = "Load starting parts", .AutoSize = True}
        Dim check As New Button() With {.Text = "Check my solution", .AutoSize = True}
        Dim hint As New Button() With {.Text = "Hint", .AutoSize = True}
        buttons.Controls.AddRange({load, check, hint})
        Dim right As New Panel() With {.Dock = DockStyle.Fill}
        right.Controls.Add(_text)
        right.Controls.Add(_result)
        right.Controls.Add(buttons)
        Controls.Add(right)
        Controls.Add(_list)
        AddHandler _list.SelectedIndexChanged, Sub() ShowLesson()
        AddHandler load.Click, Sub()
                                   Dim l = Current()
                                   If l Is Nothing Then Return
                                   RaiseEvent LoadCircuit(Me, If(l.Start Is Nothing, New Circuit(), l.Start.Invoke()))
                               End Sub
        AddHandler check.Click, Sub() CheckLesson()
        AddHandler hint.Click, Sub()
                                   Dim l = Current()
                                   If l IsNot Nothing Then _result.ForeColor = Color.DarkBlue : _result.Text = "Hint: " & l.Hint
                               End Sub
        AddHandler _list.DrawItem, AddressOf DrawItem
        _list.DrawMode = DrawMode.OwnerDrawFixed
        _list.ItemHeight = 22
        _list.SelectedIndex = 0
    End Sub

    Private Function Current() As Lesson
        Return TryCast(_list.SelectedItem, Lesson)
    End Function

    Private Sub ShowLesson()
        Dim l = Current()
        If l Is Nothing Then Return
        _text.Text = l.Title & vbCrLf & vbCrLf & "TASK" & vbCrLf & l.Goal & vbCrLf & vbCrLf &
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
            _result.ForeColor = If(r.Passed, Color.DarkGreen, Color.Firebrick)
            _result.Text = If(r.Passed, "✔ ", "✘ ") & r.Feedback
            If r.Passed Then _done.Add(l.Title) : _list.Invalidate()
        Catch ex As Exception
            _result.ForeColor = Color.Firebrick
            _result.Text = "Could not test the circuit: " & ex.Message
        End Try
    End Sub

    Private Sub DrawItem(sender As Object, e As DrawItemEventArgs)
        If e.Index < 0 Then Return
        e.DrawBackground()
        Dim l = DirectCast(_list.Items(e.Index), Lesson)
        Dim mark = If(_done.Contains(l.Title), "✔ ", "   ")
        Using b As New SolidBrush(If((e.State And DrawItemState.Selected) <> 0, SystemColors.HighlightText, If(_done.Contains(l.Title), Color.DarkGreen, SystemColors.ControlText)))
            e.Graphics.DrawString(mark & l.Title, e.Font, b, e.Bounds.X + 2, e.Bounds.Y + 3)
        End Using
    End Sub
End Class
