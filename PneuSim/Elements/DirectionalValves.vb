Imports System.ComponentModel

''' <summary>How the valve is switched into its actuated (left) position.</summary>
Public Enum ValveActuator
    <Description("Push button")> PushButton
    <Description("Selector switch (detented)")> Selector
    <Description("Roller lever")> RollerLever
    <Description("Pneumatic pilot")> Pilot
    <Description("Pneumatic pilot with time delay")> DelayedPilot
End Enum

''' <summary>How the valve returns to its normal (right) position.</summary>
Public Enum ValveReturn
    Spring
    Pilot
End Enum

''' <summary>
''' Two-position directional control valve drawn to ISO 1219. The right box is the normal
''' position (state 0), the left box the actuated position (state 1). Working ports stay fixed;
''' the boxes slide so the active box sits over the ports.
''' </summary>
Public MustInherit Class DirectionalValve
    Inherits CircuitElement

    Protected Const ActW As Single = 20
    Protected Const BoxTop As Single = 10
    Protected Const BoxBottom As Single = 50

    Protected Structure WorkingPort
        Public Name As String
        Public Offset As Single
        Public Top As Boolean
        Public Vents As Boolean
        Public Sub New(name As String, offset As Single, top As Boolean, Optional vents As Boolean = False)
            Me.Name = name : Me.Offset = offset : Me.Top = top : Me.Vents = vents
        End Sub
    End Structure

    Private _actuator As ValveActuator = ValveActuator.PushButton
    Private _return As ValveReturn = ValveReturn.Spring
    Private _delay As Double = 2.0
    Private _state As Integer
    Private _manualPressed As Boolean
    Private _detentOn As Boolean
    Private _timer As Double

    Protected MustOverride ReadOnly Property BoxWidth As Single
    Protected MustOverride Function WorkingPorts() As WorkingPort()
    ''' <summary>Passages (from, to) open in the given position; the arrow points to "to".</summary>
    Protected MustOverride Function Flows(position As Integer) As String()()
    Protected MustOverride Function ClosedPorts(position As Integer) As String()
    Protected MustOverride ReadOnly Property LeftPilotName As String
    Protected MustOverride ReadOnly Property RightPilotName As String

    <Category("Actuation"), DisplayName("Actuation"), Description("How the valve is switched.")>
    Public Property Actuator As ValveActuator
        Get
            Return _actuator
        End Get
        Set(value As ValveActuator)
            _actuator = value
            RebuildPorts()
        End Set
    End Property

    <Category("Actuation"), DisplayName("Return"), Description("Spring return, or a second pilot signal (memory valve).")>
    Public Property ReturnType As ValveReturn
        Get
            Return _return
        End Get
        Set(value As ValveReturn)
            _return = value
            RebuildPorts()
        End Set
    End Property

    <Category("Actuation"), DisplayName("Roller mark"),
     Description("For roller lever valves: the cylinder position mark that operates the roller, e.g. 1S2.")>
    Public Property TriggerMark As String = ""

    <Category("Actuation"), DisplayName("Delay (s)"), Description("For time delay valves: how long the pilot signal must be present.")>
    Public Property DelaySeconds As Double
        Get
            Return _delay
        End Get
        Set(value As Double)
            _delay = Math.Max(0, Math.Min(600, value))
        End Set
    End Property

    ''' <summary>0 = normal position, 1 = actuated.</summary>
    <Browsable(False)> Public ReadOnly Property State As Integer
        Get
            Return _state
        End Get
    End Property

    Protected Sub RebuildPorts()
        Dim shift = _state * BoxWidth
        Dim wanted As New List(Of Port)
        For Each wp In WorkingPorts()
            wanted.Add(MakePort(wp.Name, ActW + BoxWidth + wp.Offset, If(wp.Top, 0F, 60.0F), 0, If(wp.Top, -1.0F, 1.0F), wp.Vents))
        Next
        If HasLeftPilot Then wanted.Add(MakePort(LeftPilotName, shift, 30, -1, 0, False))
        If _return = ValveReturn.Pilot Then
            wanted.Add(MakePort(RightPilotName, 2 * ActW + 2 * BoxWidth + shift, 30, 1, 0, False))
        End If
        Ports.Clear()
        Ports.AddRange(wanted)
    End Sub

    ''' <summary>Reuses an existing port with the same name so tubes stay attached.</summary>
    Private Function MakePort(name As String, x As Single, y As Single, dx As Single, dy As Single, vents As Boolean) As Port
        Dim p = If(GetPort(name), New Port(Me, name))
        p.Local = New PointF(x, y)
        p.Direction = New PointF(dx, dy)
        p.VentsWhenOpen = vents
        Return p
    End Function

    Private ReadOnly Property HasLeftPilot As Boolean
        Get
            Return _actuator = ValveActuator.Pilot OrElse _actuator = ValveActuator.DelayedPilot
        End Get
    End Property

    Private Sub SetState(value As Integer)
        If value = _state Then Return
        _state = value
        RebuildPorts()
    End Sub

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Dim shift = _state * BoxWidth
            Return RectangleF.FromLTRB(shift, 0, 2 * ActW + 2 * BoxWidth + shift, 60)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(ActW, 78)
        End Get
    End Property

    ' ---------------------------------------------------------------- drawing

    Public Overrides Sub DrawSymbol(g As Graphics, r As RenderContext)
        Dim shift = _state * BoxWidth
        Dim wps = WorkingPorts()

        ' Port stubs from the fixed connection points to the box edge.
        For Each wp In wps
            Dim p = GetPort(wp.Name)
            Dim x = ActW + BoxWidth + wp.Offset
            g.DrawLine(r.PenFor(p), x, If(wp.Top, 0, 60), x, If(wp.Top, BoxTop, BoxBottom))
            If wp.Vents AndAlso p.ConnectionCount = 0 Then
                Symbols.Exhaust(g, r.Line, New PointF(x, 60), New PointF(0, 1))
            End If
        Next

        For position = 0 To 1
            Dim boxX = ActW + shift + If(position = 1, 0, BoxWidth)
            g.FillRectangle(r.BodyBrush, boxX, BoxTop, BoxWidth, BoxBottom - BoxTop)
            g.DrawRectangle(r.Line, boxX, BoxTop, BoxWidth, BoxBottom - BoxTop)
            Dim active = position = _state
            For Each fl In Flows(position)
                Dim a = FindWorking(wps, fl(0)), b = FindWorking(wps, fl(1))
                Dim pen = If(active, r.PenFor(GetPort(fl(0))), r.Line)
                Symbols.Arrow(g, pen, BoxPoint(boxX, a, 2), BoxPoint(boxX, b, 2))
            Next
            For Each name In ClosedPorts(position)
                Dim wp = FindWorking(wps, name)
                Symbols.Blocked(g, r.Line, BoxPoint(boxX, wp, 0), New PointF(0, If(wp.Top, 1.0F, -1.0F)))
            Next
        Next

        DrawLeftActuator(g, r, ActW + shift)
        DrawRightReturn(g, r, ActW + 2 * BoxWidth + shift)
    End Sub

    Private Shared Function FindWorking(wps As WorkingPort(), name As String) As WorkingPort
        Return wps.First(Function(w) w.Name = name)
    End Function

    Private Shared Function BoxPoint(boxX As Single, wp As WorkingPort, inset As Single) As PointF
        Return New PointF(boxX + wp.Offset, If(wp.Top, BoxTop + inset, BoxBottom - inset))
    End Function

    Private Sub DrawLeftActuator(g As Graphics, r As RenderContext, edge As Single)
        Dim cy = (BoxTop + BoxBottom) / 2
        Dim pressedPen = If(r.Simulating AndAlso _state = 1, r.Pressure, r.Line)
        Select Case _actuator
            Case ValveActuator.PushButton
                g.DrawLine(r.Line, edge, cy, edge - 12, cy)
                g.DrawLine(pressedPen, edge - 12, cy - 8, edge - 12, cy + 8)
                g.DrawArc(pressedPen, edge - 18, cy - 8, 12, 16, 90, 180)
            Case ValveActuator.Selector
                g.DrawLine(r.Line, edge, cy, edge - 8, cy)
                g.DrawLine(pressedPen, edge - 8, cy, edge - 18, cy - 12)
                ' Detent notches.
                g.DrawLine(r.Thin, edge - 4, cy + 6, edge - 16, cy + 6)
                For Each nx In {edge - 6, edge - 14}
                    g.DrawLine(r.Thin, nx, cy + 6, nx + 2, cy + 3)
                    g.DrawLine(r.Thin, nx + 2, cy + 3, nx + 4, cy + 6)
                Next
            Case ValveActuator.RollerLever
                g.DrawLine(r.Line, edge, cy, edge - 10, cy)
                g.FillEllipse(r.BodyBrush, edge - 19, cy - 5, 10, 10)
                g.DrawEllipse(pressedPen, edge - 19, cy - 5, 10, 10)
                If Not String.IsNullOrWhiteSpace(TriggerMark) Then
                    Dim sz = g.MeasureString(TriggerMark, r.SmallFont)
                    g.DrawString(TriggerMark, r.SmallFont, r.MarkBrush, edge - 14 - sz.Width / 2, cy - 8 - sz.Height)
                End If
            Case ValveActuator.Pilot, ValveActuator.DelayedPilot
                Dim port = GetPort(LeftPilotName)
                g.DrawLine(r.DashedFor(port), edge - ActW, cy, edge - 6, cy)
                Using b As New SolidBrush(If(r.Simulating AndAlso port.IsPressurized, RenderContext.PressureColor, Color.Black))
                    g.FillPolygon(b, {New PointF(edge, cy), New PointF(edge - 7, cy - 4), New PointF(edge - 7, cy + 4)})
                End Using
                If _actuator = ValveActuator.DelayedPilot Then
                    ' Small clock face and the set delay.
                    g.FillEllipse(r.BodyBrush, edge - 17, cy - 22, 12, 12)
                    g.DrawEllipse(r.Thin, edge - 17, cy - 22, 12, 12)
                    g.DrawLine(r.Thin, edge - 11, cy - 16, edge - 11, cy - 20)
                    g.DrawLine(r.Thin, edge - 11, cy - 16, edge - 8, cy - 16)
                    Dim txt = If(r.Simulating, $"{Math.Min(_timer, _delay):0.0}/{_delay:0.0} s", $"t = {_delay:0.0#} s")
                    g.DrawString(txt, r.SmallFont, r.TextBrush, edge - 4, cy - 34)
                End If
        End Select
    End Sub

    Private Sub DrawRightReturn(g As Graphics, r As RenderContext, edge As Single)
        Dim cy = (BoxTop + BoxBottom) / 2
        If _return = ValveReturn.Spring Then
            Symbols.Spring(g, r.Line, edge, edge + ActW - 2, cy, 7)
        Else
            Dim port = GetPort(RightPilotName)
            g.DrawLine(r.DashedFor(port), edge + 6, cy, edge + ActW, cy)
            Using b As New SolidBrush(If(r.Simulating AndAlso port.IsPressurized, RenderContext.PressureColor, Color.Black))
                g.FillPolygon(b, {New PointF(edge, cy), New PointF(edge + 7, cy - 4), New PointF(edge + 7, cy + 4)})
            End Using
        End If
    End Sub

    ' ---------------------------------------------------------------- simulation

    <Browsable(False)> Public Overrides ReadOnly Property IsManuallyOperated As Boolean
        Get
            Return _actuator = ValveActuator.PushButton OrElse _actuator = ValveActuator.Selector
        End Get
    End Property

    Public Overrides Sub OnSimMouseDown()
        If _actuator = ValveActuator.PushButton Then _manualPressed = True
        If _actuator = ValveActuator.Selector Then _detentOn = Not _detentOn
    End Sub

    Public Overrides Sub OnSimMouseUp()
        _manualPressed = False
    End Sub

    Public Overrides Sub ResetSim()
        SetState(0)
        MyBase.ResetSim()
        _manualPressed = False
        _detentOn = False
        _timer = 0
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        For Each fl In Flows(_state)
            sim.AddEdge(GetPort(fl(0)), GetPort(fl(1)))
        Next
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim leftActive As Boolean
        Select Case _actuator
            Case ValveActuator.PushButton : leftActive = _manualPressed
            Case ValveActuator.Selector : leftActive = _detentOn
            Case ValveActuator.RollerLever : leftActive = sim.IsMarkActive(TriggerMark)
            Case ValveActuator.Pilot : leftActive = GetPort(LeftPilotName).Pressure >= Simulator.PilotThreshold
            Case ValveActuator.DelayedPilot
                leftActive = GetPort(LeftPilotName).Pressure >= Simulator.PilotThreshold AndAlso _timer >= _delay - 0.000001
        End Select

        Dim newState = _state
        If _return = ValveReturn.Spring Then
            newState = If(leftActive, 1, 0)
        Else
            Dim rightActive = GetPort(RightPilotName).Pressure >= Simulator.PilotThreshold
            If leftActive AndAlso Not rightActive Then newState = 1
            If rightActive AndAlso Not leftActive Then newState = 0
        End If
        If newState = _state Then Return False
        SetState(newState)
        Return True
    End Function

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        If _actuator = ValveActuator.DelayedPilot Then
            If GetPort(LeftPilotName).Pressure >= Simulator.PilotThreshold Then
                _timer += dt
            Else
                _timer = 0
            End If
        End If
    End Sub

    Protected Function ActuationText() As String
        Dim a As String
        Select Case _actuator
            Case ValveActuator.PushButton : a = "push button"
            Case ValveActuator.Selector : a = "selector switch"
            Case ValveActuator.RollerLever : a = "roller lever"
            Case ValveActuator.Pilot : a = "pneumatic pilot"
            Case Else : a = "time delay"
        End Select
        Return a & If(_return = ValveReturn.Spring, ", spring return", ", pilot return")
    End Function
End Class

''' <summary>3/2-way directional control valve.</summary>
Public Class Valve32
    Inherits DirectionalValve

    Private _normallyOpen As Boolean

    Public Sub New()
        RebuildPorts()
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Valve32"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return $"3/2-way valve ({If(_normallyOpen, "NO", "NC")}), {ActuationText()}"
        End Get
    End Property

    <Category("Valve"), DisplayName("Normally open"), Description("False: 1 is closed at rest (NC). True: 1 is connected to 2 at rest (NO).")>
    Public Property NormallyOpen As Boolean
        Get
            Return _normallyOpen
        End Get
        Set(value As Boolean)
            _normallyOpen = value
        End Set
    End Property

    Protected Overrides ReadOnly Property BoxWidth As Single = 40
    Protected Overrides ReadOnly Property LeftPilotName As String = "12"
    Protected Overrides ReadOnly Property RightPilotName As String = "10"

    Protected Overrides Function WorkingPorts() As WorkingPort()
        Return {New WorkingPort("2", 10, True), New WorkingPort("1", 10, False), New WorkingPort("3", 30, False, vents:=True)}
    End Function

    Protected Overrides Function Flows(position As Integer) As String()()
        Dim open = (position = 1) Xor _normallyOpen
        Return If(open, {New String() {"1", "2"}}, {New String() {"2", "3"}})
    End Function

    Protected Overrides Function ClosedPorts(position As Integer) As String()
        Dim open = (position = 1) Xor _normallyOpen
        Return If(open, {"3"}, {"1"})
    End Function
End Class

''' <summary>5/2-way directional control valve.</summary>
Public Class Valve52
    Inherits DirectionalValve

    Public Sub New()
        RebuildPorts()
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Valve52"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return $"5/2-way valve, {ActuationText()}"
        End Get
    End Property

    Protected Overrides ReadOnly Property BoxWidth As Single = 60
    Protected Overrides ReadOnly Property LeftPilotName As String = "14"
    Protected Overrides ReadOnly Property RightPilotName As String = "12"

    Protected Overrides Function WorkingPorts() As WorkingPort()
        Return {New WorkingPort("4", 20, True), New WorkingPort("2", 40, True),
                New WorkingPort("5", 10, False, vents:=True), New WorkingPort("1", 30, False),
                New WorkingPort("3", 50, False, vents:=True)}
    End Function

    Protected Overrides Function Flows(position As Integer) As String()()
        If position = 1 Then Return {New String() {"1", "4"}, New String() {"2", "3"}}
        Return {New String() {"1", "2"}, New String() {"4", "5"}}
    End Function

    Protected Overrides Function ClosedPorts(position As Integer) As String()
        Return {}
    End Function
End Class
