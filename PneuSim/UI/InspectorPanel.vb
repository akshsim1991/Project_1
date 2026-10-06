''' <summary>
''' State inspector: live values of the component or tube right-clicked during the simulation,
''' with sliders for the settings that can be changed while it runs.
''' </summary>
Public Class InspectorPanel
    Inherits UserControl

    Private ReadOnly _title As New Label() With {.Dock = DockStyle.Top, .Height = 24, .Font = New Font("Segoe UI", 9, FontStyle.Bold), .Padding = New Padding(4, 4, 0, 0)}
    Private ReadOnly _list As New ListView() With {.Dock = DockStyle.Fill, .View = View.Details, .FullRowSelect = True, .HeaderStyle = ColumnHeaderStyle.Nonclickable}
    Private ReadOnly _knobs As New FlowLayoutPanel() With {.Dock = DockStyle.Right, .Width = 320, .FlowDirection = FlowDirection.TopDown, .WrapContents = False, .AutoScroll = True}
    Private _target As Object
    Private _updating As Boolean

    ''' <summary>A live setting was changed (the simulation should re-solve).</summary>
    Public Event SettingChanged As EventHandler
    ''' <summary>A slider was released (the change should be kept as an undo step).</summary>
    Public Event SettingCommitted As EventHandler

    Public Sub New()
        _list.Columns.Add("Quantity", 170)
        _list.Columns.Add("Value", 260)
        Controls.Add(_list)
        Controls.Add(_knobs)
        Controls.Add(_title)
        ShowHint()
    End Sub

    Private Sub ShowHint()
        _title.Text = "State inspector"
        _list.Items.Clear()
        _list.Items.Add(New ListViewItem({"Right-click a component or tube", "during the simulation to see its live values here."}))
        _knobs.Controls.Clear()
    End Sub

    ''' <summary>Extra text after the title (e.g. the troubleshooting counters).</summary>
    Public Property Note As String = ""

    ''' <summary>The component (or tube) shown.</summary>
    Public Property Target As Object
        Get
            Return _target
        End Get
        Set(value As Object)
            _target = value
            If value Is Nothing Then ShowHint() : Return
            _title.Text = "Inspecting: " & TroubleshootExercise.NameOf_(value) & If(String.IsNullOrEmpty(Note), "", "      —  " & Note)
            BuildKnobs()
            RefreshValues()
        End Set
    End Property

    ''' <summary>Updates the live values (called while the simulation runs).</summary>
    Public Sub RefreshValues()
        If _target Is Nothing Then Return
        Dim values = LiveTuning.Inspect(_target)
        _list.BeginUpdate()
        If _list.Items.Count <> values.Count Then
            _list.Items.Clear()
            For Each v In values
                _list.Items.Add(New ListViewItem({v.Name, v.Value}))
            Next
        Else
            For i = 0 To values.Count - 1
                _list.Items(i).SubItems(0).Text = values(i).Name
                If _list.Items(i).SubItems(1).Text <> values(i).Value Then _list.Items(i).SubItems(1).Text = values(i).Value
            Next
        End If
        _list.EndUpdate()
    End Sub

    Private Sub BuildKnobs()
        _knobs.Controls.Clear()
        Dim el = TryCast(_target, CircuitElement)
        If el Is Nothing Then Return
        Dim knobs = LiveTuning.KnobsFor(el)
        If knobs.Count = 0 Then
            _knobs.Controls.Add(New Label() With {.Text = "No settings to change while running.", .AutoSize = True, .Padding = New Padding(4, 8, 0, 0)})
            Return
        End If
        _knobs.Controls.Add(New Label() With {.Text = "Change while it runs:", .AutoSize = True, .Font = New Font("Segoe UI", 8.5F, FontStyle.Bold), .Padding = New Padding(4, 6, 0, 0)})
        For Each kn In knobs
            Dim caption As New Label() With {.AutoSize = True, .Padding = New Padding(4, 4, 0, 0)}
            Dim bar As New TrackBar() With {.Minimum = 0, .Maximum = 200, .TickFrequency = 20, .Width = 290, .Height = 32}
            Dim knob = kn
            Dim show = Sub() caption.Text = $"{knob.Caption}: {LiveTuning.GetValue(el, knob):0.##}"
            _updating = True
            bar.Value = CInt(Math.Max(0, Math.Min(200, (LiveTuning.GetValue(el, knob) - knob.Min) / (knob.Max - knob.Min) * 200)))
            _updating = False
            show()
            AddHandler bar.Scroll, Sub()
                                       If _updating Then Return
                                       LiveTuning.SetValue(el, knob, knob.Min + (knob.Max - knob.Min) * bar.Value / 200.0)
                                       show()
                                       RaiseEvent SettingChanged(Me, EventArgs.Empty)
                                   End Sub
            AddHandler bar.MouseUp, Sub() RaiseEvent SettingCommitted(Me, EventArgs.Empty)
            AddHandler bar.KeyUp, Sub() RaiseEvent SettingCommitted(Me, EventArgs.Empty)
            _knobs.Controls.Add(caption)
            _knobs.Controls.Add(bar)
        Next
    End Sub
End Class
