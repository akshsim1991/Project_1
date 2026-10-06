''' <summary>Lets the user choose a fault for a component or tube (to insert it, or as a diagnosis).</summary>
Public Class FaultDialog
    Inherits Form

    Private ReadOnly _list As New ListBox() With {.Dock = DockStyle.Fill, .IntegralHeight = False}
    Private ReadOnly _hidden As New CheckBox() With {.Text = "Hidden: a troubleshooting exercise (students must find it; the file does not show it)", .AutoSize = True}
    Private ReadOnly _kinds As New List(Of FaultKind)

    ''' <param name="target">Component or tube.</param>
    ''' <param name="diagnosis">True: the student names the fault they found (no 'hidden' option).</param>
    Public Sub New(target As Object, diagnosis As Boolean)
        Text = If(diagnosis, "What is wrong with it?", "Put a fault into " & TroubleshootExercise.NameOf_(target))
        Size = New Size(520, 330)
        StartPosition = FormStartPosition.CenterParent
        FormBorderStyle = FormBorderStyle.FixedDialog
        MinimizeBox = False : MaximizeBox = False
        Font = New Font("Segoe UI", 9)
        Dim head As New Label() With {.Dock = DockStyle.Top, .Height = 40, .Padding = New Padding(6),
            .Text = If(diagnosis, $"You think the fault is in {TroubleshootExercise.NameOf_(target)}. What kind of fault is it?",
                       "Choose a fault. The circuit will behave as a real one with this fault.")}
        If diagnosis Then
            _list.Items.Add("I am not sure what kind of fault (only the part counts)") : _kinds.Add(FaultKind.None)
        Else
            _list.Items.Add("No fault (repair it)") : _kinds.Add(FaultKind.None)
        End If
        For Each k In TroubleshootExercise.PossibleFaultsOf(target)
            _list.Items.Add(TroubleshootExercise.DescribeFault(target, k))
            _kinds.Add(k)
        Next
        Dim current = TroubleshootExercise.FaultOf(target)
        _list.SelectedIndex = If(diagnosis, 0, Math.Max(0, _kinds.IndexOf(current)))
        If TypeOf target Is CircuitElement Then _hidden.Checked = DirectCast(target, CircuitElement).FaultHidden
        If TypeOf target Is Tube Then _hidden.Checked = DirectCast(target, Tube).FaultHidden
        Dim bottom As New FlowLayoutPanel() With {.Dock = DockStyle.Bottom, .Height = If(diagnosis, 40, 70), .FlowDirection = FlowDirection.LeftToRight, .Padding = New Padding(6)}
        If Not diagnosis Then bottom.Controls.Add(_hidden) : bottom.SetFlowBreak(_hidden, True)
        Dim ok As New Button() With {.Text = "OK", .DialogResult = DialogResult.OK, .Width = 90}
        Dim cancel As New Button() With {.Text = "Cancel", .DialogResult = DialogResult.Cancel, .Width = 90}
        bottom.Controls.Add(ok) : bottom.Controls.Add(cancel)
        AcceptButton = ok : CancelButton = cancel
        AddHandler _list.DoubleClick, Sub() If _list.SelectedIndex >= 0 Then DialogResult = DialogResult.OK
        Controls.Add(_list)
        Controls.Add(bottom)
        Controls.Add(head)
        Theme.Apply(Me)
    End Sub

    Public ReadOnly Property SelectedKind As FaultKind
        Get
            Return If(_list.SelectedIndex < 0, FaultKind.None, _kinds(_list.SelectedIndex))
        End Get
    End Property

    Public ReadOnly Property Hidden As Boolean
        Get
            Return _hidden.Checked
        End Get
    End Property
End Class
