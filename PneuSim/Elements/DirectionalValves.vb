Imports System.ComponentModel

''' <summary>How the valve is switched into its left (a) position.</summary>
<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum ValveActuator
    <Description("Push button")> PushButton
    <Description("Selector switch (detented)")> Selector
    <Description("Roller lever")> RollerLever
    <Description("Pneumatic pilot")> Pilot
    <Description("Pneumatic pilot with time delay")> DelayedPilot
    <Description("Solenoid")> Solenoid
    <Description("Idle-return roller (one direction only)")> IdleReturnRoller
End Enum

''' <summary>
''' The right-hand actuator. Two-position valves: what returns the valve to normal.
''' Three-position valves: the actuator for the right (b) position; the valve is always spring centred.
''' </summary>
<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum ValveReturn
    <Description("Spring")> Spring
    <Description("Pneumatic pilot")> Pilot
    <Description("Solenoid")> Solenoid
End Enum

''' <summary>Centre position of a 5/3-way valve.</summary>
<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum CentrePosition
    <Description("Closed centre")> Closed
    <Description("Exhaust centre")> Exhausted
    <Description("Pressure centre")> Pressurized
End Enum

''' <summary>
''' Directional control valve with two or three positions, drawn to ISO 1219.
''' Box order, left to right: position 1 (a), position 0 (normal), and for 3-position valves
''' position 2 (b). Working ports stay fixed under the normal box; the boxes slide so the
''' active box sits over the ports.
''' </summary>
Public MustInherit Class DirectionalValve
    Inherits CircuitElement

    Protected Const BoxTop As Single = 10
    Protected Const BoxBottom As Single = 50
    Private Const SpringW As Single = 14
    Private Const RestIndex As Integer = 1

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
    Private _manualRight As Boolean
    Private _detentOn As Boolean
    Private _timer As Double
    Private _pulse As Double
    Private _markBefore As Boolean = True

    ''' <summary>How long an idle-return roller stays operated while the cam passes over it (s).</summary>
    Public Const IdleRollerPulse As Double = 0.15

    Protected MustOverride ReadOnly Property BoxWidth As Single
    Protected MustOverride Function WorkingPorts() As WorkingPort()
    ''' <summary>Passages (from, to) open in the given position; the arrow points to "to".</summary>
    Protected MustOverride Function Flows(position As Integer) As String()()
    Protected MustOverride Function ClosedPorts(position As Integer) As String()
    Protected MustOverride ReadOnly Property LeftPilotName As String
    Protected MustOverride ReadOnly Property RightPilotName As String

    Protected Overridable ReadOnly Property PositionCount As Integer
        Get
            Return 2
        End Get
    End Property

    ''' <summary>Width reserved at each end for the actuator (and centring spring).</summary>
    Protected ReadOnly Property ActW As Single
        Get
            Return If(PositionCount = 3, 20 + SpringW, 20)
        End Get
    End Property

    <Category("Actuation"), DisplayName("Operated by"), Description("How the valve is switched into position a (left box).")>
    Public Property Actuator As ValveActuator
        Get
            Return _actuator
        End Get
        Set(value As ValveActuator)
            _actuator = value
            RebuildPorts()
        End Set
    End Property

    <Category("Actuation"), DisplayName("Return / right actuator"),
     Description("2-position valves: spring return, or a second pilot / solenoid (memory valve). 3-position valves: actuator for position b; the valve is spring centred.")>
    Public Property ReturnType As ValveReturn
        Get
            Return _return
        End Get
        Set(value As ValveReturn)
            ' A spring-centred 3-position valve needs an actuator on both sides to reach position b.
            If PositionCount = 3 AndAlso value = ValveReturn.Spring Then
                value = If(_actuator = ValveActuator.Solenoid, ValveReturn.Solenoid, ValveReturn.Pilot)
            End If
            _return = value
            RebuildPorts()
        End Set
    End Property

    <Category("Actuation"), DisplayName("Solenoid (left)"), Description("Label of the solenoid coil that operates position a, e.g. 1M1.")>
    Public Property SolenoidLabel As String = "1M1"

    <Category("Actuation"), DisplayName("Solenoid (right)"), Description("Label of the solenoid coil on the right side, e.g. 1M2.")>
    Public Property ReturnSolenoidLabel As String = "1M2"

    <Category("Actuation"), DisplayName("Roller mark"),
     Description("For roller lever valves: the cylinder position mark that operates the roller, e.g. 1S2.")>
    Public Property TriggerMark As String = ""

    <Category("Actuation"), DisplayName("Switching pressure (bar)"),
     Description("Pilot pressure at which the valve switches. Raise it to make a pressure sequence valve (most meaningful in realistic mode).")>
    Public Property SwitchingPressure As Double
        Get
            Return _switchPressure
        End Get
        Set(value As Double)
            _switchPressure = Math.Max(0.2, Math.Min(16, value))
        End Set
    End Property
    Private _switchPressure As Double = Simulator.PilotThreshold

    <Category("Actuation"), DisplayName("Delay (s)"), Description("For time delay valves: how long the pilot signal must be present.")>
    Public Property DelaySeconds As Double
        Get
            Return _delay
        End Get
        Set(value As Double)
            _delay = Math.Max(0, Math.Min(600, value))
        End Set
    End Property

    Public Overrides Function ShowProperty(name As String) As Boolean
        Select Case name
            Case NameOf(SolenoidLabel) : Return _actuator = ValveActuator.Solenoid
            Case NameOf(ReturnSolenoidLabel) : Return _return = ValveReturn.Solenoid
            Case NameOf(TriggerMark) : Return IsRollerOperated
            Case NameOf(DelaySeconds) : Return _actuator = ValveActuator.DelayedPilot
            Case NameOf(SwitchingPressure) : Return _actuator = ValveActuator.Pilot OrElse _actuator = ValveActuator.DelayedPilot OrElse _return = ValveReturn.Pilot
        End Select
        Return True
    End Function

    ''' <summary>True for roller lever valves, including idle-return rollers.</summary>
    <Browsable(False)> Public ReadOnly Property IsRollerOperated As Boolean
        Get
            Return _actuator = ValveActuator.RollerLever OrElse _actuator = ValveActuator.IdleReturnRoller
        End Get
    End Property

    ''' <summary>True if the valve has a solenoid on either side.</summary>
    <Browsable(False)> Public ReadOnly Property HasSolenoid As Boolean
        Get
            Return _actuator = ValveActuator.Solenoid OrElse _return = ValveReturn.Solenoid
        End Get
    End Property

    Public Overrides Function PossibleFaults() As FaultKind()
        Dim list As New List(Of FaultKind) From {FaultKind.StuckNormal, FaultKind.StuckOperated}
        If HasSolenoid Then list.Add(FaultKind.BurntCoil)
        Return list.ToArray()
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Select Case kind
            Case FaultKind.StuckNormal : Return "Spool stuck in the normal position (dirt or varnish)"
            Case FaultKind.StuckOperated : Return If(PositionCount = 3, "Spool stuck in position a", "Spool stuck in the switched position")
            Case FaultKind.BurntCoil : Return "Solenoid coil on the valve burnt out (the manual override still works)"
        End Select
        Return MyBase.FaultDescription(kind)
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Switching position", If(_state = 0, If(PositionCount = 3, "centre (normal)", "normal (rest)"), If(_state = 1, "a (left box)", "b (right box)"))))
        If _actuator = ValveActuator.DelayedPilot Then list.Add(("Delay timer", $"{Math.Min(_timer, _delay):0.00} of {_delay:0.0#} s"))
        Return list
    End Function

    Public Overrides Sub AfterStateRestored()
        RebuildPorts()
    End Sub

    ''' <summary>Passages (from, to) open in a position: 0 normal, 1 = a, 2 = b.</summary>
    Public Function PassagesIn(position As Integer) As String()()
        Return Flows(position)
    End Function

    <Browsable(False)> Public ReadOnly Property Positions As Integer
        Get
            Return PositionCount
        End Get
    End Property

    ''' <summary>Name of the pilot port on the left (a) or right (b) side.</summary>
    Public Function PilotPortName(leftSide As Boolean) As String
        Return If(leftSide, LeftPilotName, RightPilotName)
    End Function

    ''' <summary>True for two-position valves that stay where they are when both signals go (impulse / memory valves).</summary>
    <Browsable(False)> Public ReadOnly Property IsMemoryValve As Boolean
        Get
            Return PositionCount = 2 AndAlso _return <> ValveReturn.Spring
        End Get
    End Property

    ''' <summary>Working port positions inside one box: name, offset from the box's left edge, top or bottom.</summary>
    Public Function WorkingPortLayout() As (Name As String, Offset As Single, Top As Boolean, Vents As Boolean)()
        Return WorkingPorts().Select(Function(w) (w.Name, w.Offset, w.Top, w.Vents)).ToArray()
    End Function

    <Browsable(False)> Public ReadOnly Property BoxSize As Single
        Get
            Return BoxWidth
        End Get
    End Property

    ''' <summary>Names of the working ports (not pilots).</summary>
    Public Function WorkingPortNames() As String()
        Return WorkingPorts().Select(Function(w) w.Name).ToArray()
    End Function

    ''' <summary>True if a solenoid coil with this label operates the valve.</summary>
    Public Function UsesSolenoid(coilLabel As String) As Boolean
        Dim same = Function(a As String) String.Equals(a?.Trim(), coilLabel?.Trim(), StringComparison.OrdinalIgnoreCase)
        Return (_actuator = ValveActuator.Solenoid AndAlso same(SolenoidLabel)) OrElse
               (_return = ValveReturn.Solenoid AndAlso same(ReturnSolenoidLabel))
    End Function

    ''' <summary>0 = normal position, 1 = position a (left), 2 = position b (right, 3-position valves).</summary>
    <Browsable(False)> Public ReadOnly Property State As Integer
        Get
            Return _state
        End Get
    End Property

    ''' <summary>Value plotted in the diagram: 1 = a, 0 = normal (2-position) or b (3-position), 0.5 = centre.</summary>
    <Browsable(False)> Public ReadOnly Property DiagramValue As Double
        Get
            If PositionCount = 2 Then Return _state
            Return If(_state = 1, 1.0, If(_state = 2, 0.0, 0.5))
        End Get
    End Property

    Private Function BoxIndex(position As Integer) As Integer
        Return If(position = 1, 0, If(position = 0, 1, 2))
    End Function

    Private Function PositionAt(index As Integer) As Integer
        Return If(index = 0, 1, If(index = 1, 0, 2))
    End Function

    Private ReadOnly Property Shift As Single
        Get
            Return (RestIndex - BoxIndex(_state)) * BoxWidth
        End Get
    End Property

    Private ReadOnly Property TotalWidth As Single
        Get
            Return 2 * ActW + PositionCount * BoxWidth
        End Get
    End Property

    Protected Sub RebuildPorts()
        Dim s = Shift
        Dim wanted As New List(Of Port)
        For Each wp In WorkingPorts()
            wanted.Add(MakePort(wp.Name, ActW + RestIndex * BoxWidth + wp.Offset, If(wp.Top, 0F, 60.0F), 0, If(wp.Top, -1.0F, 1.0F), wp.Vents))
        Next
        If HasLeftPilot Then wanted.Add(MakePort(LeftPilotName, s, 30, -1, 0, False))
        If _return = ValveReturn.Pilot Then wanted.Add(MakePort(RightPilotName, TotalWidth + s, 30, 1, 0, False))
        Ports.Clear()
        Ports.AddRange(wanted)
    End Sub

    ''' <summary>Reuses an existing port with the same name so tubes stay attached.</summary>
    Private Function MakePort(name As String, x As Single, y As Single, dx As Single, dy As Single, vents As Boolean) As Port
        Dim p = If(GetPort(name), New Port(Me, name))
        p.Local = New PointF(x, y)
        p.Direction = New PointF(dx, dy)
        p.VentsWhenOpen = vents
        p.Kind = Medium
        Return p
    End Function

    ''' <summary>Pneumatic, or hydraulic for hydraulic valves.</summary>
    Protected Overridable ReadOnly Property Medium As PortKind
        Get
            Return PortKind.Pneumatic
        End Get
    End Property

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
            Dim s = Shift
            Return RectangleF.FromLTRB(s, 0, TotalWidth + s, 60)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(ActW, 78)
        End Get
    End Property

    ' ---------------------------------------------------------------- drawing

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim s = Shift
        Dim wps = WorkingPorts()

        ' Port stubs from the fixed connection points to the box edge.
        For Each wp In wps
            Dim p = GetPort(wp.Name)
            Dim x = ActW + RestIndex * BoxWidth + wp.Offset
            g.DrawLine(r.PenFor(p), x, If(wp.Top, 0, 60), x, If(wp.Top, BoxTop, BoxBottom))
            If wp.Vents AndAlso p.ConnectionCount = 0 Then
                Symbols.Exhaust(g, r.Line, New PointF(x, 60), New PointF(0, 1))
            End If
        Next

        For index = 0 To PositionCount - 1
            Dim position = PositionAt(index)
            Dim boxX = ActW + s + index * BoxWidth
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

        Dim leftEdge = ActW + s
        Dim rightEdge = ActW + PositionCount * BoxWidth + s
        Dim cy = (BoxTop + BoxBottom) / 2
        If PositionCount = 3 Then
            Symbols.Spring(g, r.Line, leftEdge - SpringW, leftEdge, cy + 10, 5)
            Symbols.Spring(g, r.Line, rightEdge, rightEdge + SpringW, cy + 10, 5)
        End If
        Dim inset = If(PositionCount = 3, SpringW, 0F)
        DrawActuator(g, r, LeftKind(), leftEdge - inset, -1, _state = 1, LeftPilotName, SolenoidLabel)
        Dim rightKind = RightKindForDrawing()
        If rightKind IsNot Nothing Then
            DrawActuator(g, r, rightKind, rightEdge + inset, +1, _state = If(PositionCount = 3, 2, 0) AndAlso r.Simulating AndAlso RightActive, RightPilotName, ReturnSolenoidLabel)
        End If
        If PositionCount = 3 Then
            ' Lines joining the actuators to the outer boxes across the centring springs.
            g.DrawLine(r.Thin, leftEdge - SpringW, cy, leftEdge, cy)
            g.DrawLine(r.Thin, rightEdge, cy, rightEdge + SpringW, cy)
        End If
    End Sub

    ' Last computed right-hand actuation, used only for drawing.
    Private _rightActive As Boolean
    Private ReadOnly Property RightActive As Boolean
        Get
            Return _rightActive
        End Get
    End Property

    Private Function LeftKind() As String
        Return _actuator.ToString()
    End Function

    Private Function RightKindForDrawing() As String
        If _return = ValveReturn.Spring Then Return If(PositionCount = 3, Nothing, "Spring")
        Return _return.ToString()
    End Function

    Private Shared Function FindWorking(wps As WorkingPort(), name As String) As WorkingPort
        Return wps.First(Function(w) w.Name = name)
    End Function

    Private Shared Function BoxPoint(boxX As Single, wp As WorkingPort, inset As Single) As PointF
        Return New PointF(boxX + wp.Offset, If(wp.Top, BoxTop + inset, BoxBottom - inset))
    End Function

    ''' <summary>
    ''' Draws an actuator symbol attached at x = <paramref name="edge"/>, extending outwards
    ''' (to the left when <paramref name="dir"/> is -1, to the right when +1).
    ''' </summary>
    Private Sub DrawActuator(g As DrawSurface, r As RenderContext, kind As String, edge As Single, dir As Integer,
                             operated As Boolean, pilotName As String, solenoid As String)
        Dim cy = (BoxTop + BoxBottom) / 2
        Dim x = Function(t As Single) edge + dir * t
        Dim pressedPen = If(r.Simulating AndAlso operated, r.Pressure, r.Line)
        Select Case kind
            Case "Spring"
                Symbols.Spring(g, r.Line, edge, x(18), cy, 7)
            Case "PushButton"
                g.DrawLine(r.Line, edge, cy, x(12), cy)
                g.DrawLine(pressedPen, x(12), cy - 8, x(12), cy + 8)
                If dir < 0 Then
                    g.DrawArc(pressedPen, x(18), cy - 8, 12, 16, 90, 180)
                Else
                    g.DrawArc(pressedPen, x(6), cy - 8, 12, 16, 270, 180)
                End If
            Case "Selector"
                g.DrawLine(r.Line, edge, cy, x(8), cy)
                g.DrawLine(pressedPen, x(8), cy, x(18), cy - 12)
                g.DrawLine(r.Thin, x(4), cy + 6, x(16), cy + 6)
                For Each t In {6.0F, 14.0F}
                    g.DrawLine(r.Thin, x(t), cy + 6, x(t + 2), cy + 3)
                    g.DrawLine(r.Thin, x(t + 2), cy + 3, x(t + 4), cy + 6)
                Next
            Case "RollerLever", "IdleReturnRoller"
                g.DrawLine(r.Line, edge, cy, x(10), cy)
                If kind = "IdleReturnRoller" Then
                    ' Hinged lever: it only operates the valve when the cam comes from one side.
                    g.DrawLine(r.Line, x(10), cy, x(14), cy - 6)
                    g.FillEllipse(Brushes.Black, x(10) - 1.5F, cy - 1.5F, 3, 3)
                    Dim cxi = x(14)
                    g.FillEllipse(r.BodyBrush, cxi - 4, cy - 14, 9, 9)
                    g.DrawEllipse(pressedPen, cxi - 4, cy - 14, 9, 9)
                    If Not String.IsNullOrWhiteSpace(TriggerMark) Then
                        Dim szi = g.MeasureString(TriggerMark, r.SmallFont)
                        g.DrawString(TriggerMark, r.SmallFont, r.MarkBrush, cxi - szi.Width / 2, cy - 16 - szi.Height)
                    End If
                    Exit Select
                End If
                Dim cx = x(14)
                g.FillEllipse(r.BodyBrush, cx - 5, cy - 5, 10, 10)
                g.DrawEllipse(pressedPen, cx - 5, cy - 5, 10, 10)
                If Not String.IsNullOrWhiteSpace(TriggerMark) Then
                    Dim sz = g.MeasureString(TriggerMark, r.SmallFont)
                    g.DrawString(TriggerMark, r.SmallFont, r.MarkBrush, cx - sz.Width / 2, cy - 8 - sz.Height)
                End If
            Case "Pilot", "DelayedPilot"
                Dim port = GetPort(pilotName)
                If Math.Abs(_switchPressure - Simulator.PilotThreshold) > 0.01 Then
                    ' Pressure sequence valve: adjustable spring and the set pressure.
                    Symbols.Spring(g, r.Thin, x(18), x(6), cy - 9, 3)
                    g.DrawString($"{_switchPressure:0.#} bar", r.SmallFont, r.TextBrush, Math.Min(x(22), x(0)), cy - 26)
                End If
                g.DrawLine(r.DashedFor(port), x(20), cy, x(6), cy)
                Symbols.EnergyTriangle(g, r, {New PointF(edge, cy), New PointF(x(7), cy - 4), New PointF(x(7), cy + 4)},
                                       Medium = PortKind.Hydraulic, r.Simulating AndAlso port IsNot Nothing AndAlso port.IsPressurized)
                If kind = "DelayedPilot" Then
                    Dim cx = x(11)
                    g.FillEllipse(r.BodyBrush, cx - 6, cy - 22, 12, 12)
                    g.DrawEllipse(r.Thin, cx - 6, cy - 22, 12, 12)
                    g.DrawLine(r.Thin, cx, cy - 16, cx, cy - 20)
                    g.DrawLine(r.Thin, cx, cy - 16, cx + 3, cy - 16)
                    Dim txt = If(r.Simulating, $"{Math.Min(_timer, _delay):0.0}/{_delay:0.0} s", $"t = {_delay:0.0#} s")
                    g.DrawString(txt, r.SmallFont, r.TextBrush, edge - 4, cy - 34)
                End If
            Case "Solenoid"
                Dim x1 = Math.Min(x(4), x(14)), x2 = Math.Max(x(4), x(14))
                Using fill As New SolidBrush(If(r.Simulating AndAlso operated, Color.FromArgb(255, 205, 205), Color.White))
                    g.FillRectangle(fill, x1, cy - 9, x2 - x1, 18)
                End Using
                g.DrawRectangle(If(r.Simulating AndAlso operated, r.Energized, r.Line), x1, cy - 9, x2 - x1, 18)
                g.DrawLine(r.Line, x1, cy + 9, x2, cy - 9)
                g.DrawLine(r.Line, edge, cy, x(4), cy)
                If Not String.IsNullOrWhiteSpace(solenoid) Then
                    Dim sz = g.MeasureString(solenoid, r.SmallFont)
                    ' Below the solenoid and away from the valve body, so it never sits on a tube.
                    Dim tx = If(dir < 0, edge - 2 - sz.Width, edge + 2)
                    g.DrawString(solenoid, r.SmallFont, r.TextBrush, tx, cy + 11)
                End If
        End Select
    End Sub

    ' ---------------------------------------------------------------- simulation

    <Browsable(False)> Public Overrides ReadOnly Property IsManuallyOperated As Boolean
        Get
            ' Solenoid valves have a manual override button.
            Return _actuator = ValveActuator.PushButton OrElse _actuator = ValveActuator.Selector OrElse
                   _actuator = ValveActuator.Solenoid OrElse _return = ValveReturn.Solenoid
        End Get
    End Property

    ''' <summary>
    ''' A click on the right half of a valve with a right-hand solenoid operates that solenoid's
    ''' manual override; any other click operates the left actuator.
    ''' </summary>
    Public Overrides Sub OnSimMouseDown(local As PointF)
        Dim rightHalf = local.X > Shift + TotalWidth / 2
        If rightHalf AndAlso _return = ValveReturn.Solenoid Then
            _manualRight = True
            Return
        End If
        If _actuator = ValveActuator.PushButton OrElse _actuator = ValveActuator.Solenoid Then _manualPressed = True
        If _actuator = ValveActuator.Selector Then _detentOn = Not _detentOn
    End Sub

    Public Overrides Sub OnSimMouseUp()
        _manualPressed = False
        _manualRight = False
    End Sub

    Public Overrides Sub ResetSim()
        SetState(0)
        MyBase.ResetSim()
        _manualPressed = False
        _manualRight = False
        _detentOn = False
        _rightActive = False
        _timer = 0
        _pulse = 0
        ' A cylinder already standing at the mark when the simulation starts does not trip the roller.
        _markBefore = True
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        For Each fl In Flows(_state)
            sim.AddEdge(GetPort(fl(0)), GetPort(fl(1)))
        Next
    End Sub

    Private Function PilotOn(name As String) As Boolean
        Dim p = GetPort(name)
        Return p IsNot Nothing AndAlso p.Pressure >= _switchPressure - 0.000001
    End Function

    ''' <summary>Current signals on the left (a) and right (b) side, ignoring manual buttons.</summary>
    Public Function ActiveSignals(sim As Simulator) As Boolean()
        Dim leftOn As Boolean
        Select Case _actuator
            Case ValveActuator.RollerLever : leftOn = sim.IsMarkActive(TriggerMark)
            Case ValveActuator.IdleReturnRoller : leftOn = _pulse > 0
            Case ValveActuator.Pilot, ValveActuator.DelayedPilot : leftOn = PilotOn(LeftPilotName)
            Case ValveActuator.Solenoid : leftOn = sim.IsSolenoidActive(SolenoidLabel)
        End Select
        Dim rightOn = (_return = ValveReturn.Pilot AndAlso PilotOn(RightPilotName)) OrElse
                      (_return = ValveReturn.Solenoid AndAlso sim.IsSolenoidActive(ReturnSolenoidLabel))
        Return {leftOn, rightOn}
    End Function

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        ' A burnt-out solenoid no longer moves the spool; the manual override still does.
        Dim coilOk = Fault <> FaultKind.BurntCoil
        Dim leftActive As Boolean
        Select Case _actuator
            Case ValveActuator.PushButton : leftActive = _manualPressed
            Case ValveActuator.Selector : leftActive = _detentOn
            Case ValveActuator.RollerLever : leftActive = sim.IsMarkActive(TriggerMark)
            Case ValveActuator.IdleReturnRoller : leftActive = _pulse > 0
            Case ValveActuator.Pilot : leftActive = PilotOn(LeftPilotName)
            Case ValveActuator.DelayedPilot : leftActive = PilotOn(LeftPilotName) AndAlso _timer >= _delay - 0.000001
            Case ValveActuator.Solenoid : leftActive = _manualPressed OrElse (coilOk AndAlso sim.IsSolenoidActive(SolenoidLabel))
        End Select

        Dim rightActive As Boolean
        Select Case _return
            Case ValveReturn.Pilot : rightActive = PilotOn(RightPilotName)
            Case ValveReturn.Solenoid : rightActive = _manualRight OrElse (coilOk AndAlso sim.IsSolenoidActive(ReturnSolenoidLabel))
        End Select
        _rightActive = rightActive

        Dim newState = _state
        If PositionCount = 3 Then
            newState = If(leftActive AndAlso Not rightActive, 1, If(rightActive AndAlso Not leftActive, 2, 0))
        ElseIf _return = ValveReturn.Spring Then
            newState = If(leftActive, 1, 0)
        Else
            If leftActive AndAlso Not rightActive Then newState = 1
            If rightActive AndAlso Not leftActive Then newState = 0
        End If
        If Fault = FaultKind.StuckNormal Then newState = 0
        If Fault = FaultKind.StuckOperated Then newState = 1
        If newState = _state Then Return False
        SetState(newState)
        Return True
    End Function

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        If _actuator = ValveActuator.DelayedPilot Then
            If PilotOn(LeftPilotName) Then _timer += dt Else _timer = 0
        End If
        If _actuator = ValveActuator.IdleReturnRoller Then
            ' The cam trips the hinged lever only when it arrives at the mark; the lever folds
            ' away while the cylinder stays there and when it moves back.
            Dim markNow = sim.IsMarkActive(TriggerMark)
            If markNow AndAlso Not _markBefore Then
                _pulse = IdleRollerPulse
            Else
                _pulse = Math.Max(0, _pulse - dt)
            End If
            _markBefore = markNow
        End If
    End Sub

    Protected Function ActuationText() As String
        Dim a As String
        Select Case _actuator
            Case ValveActuator.PushButton : a = "push button"
            Case ValveActuator.Selector : a = "selector switch"
            Case ValveActuator.RollerLever : a = "roller lever"
            Case ValveActuator.IdleReturnRoller : a = "idle-return roller"
            Case ValveActuator.Pilot : a = "pneumatic pilot"
            Case ValveActuator.Solenoid : a = "solenoid"
            Case Else : a = "time delay"
        End Select
        If PositionCount = 3 Then
            Return a & If(_return = ValveReturn.Spring, ", spring centred", $" / {_return.ToString().ToLowerInvariant()}, spring centred")
        End If
        Select Case _return
            Case ValveReturn.Spring : Return a & ", spring return"
            Case ValveReturn.Pilot : Return a & ", pilot return"
            Case Else : Return a & ", solenoid return"
        End Select
    End Function
End Class

''' <summary>2/2-way valve (on/off).</summary>
Public Class Valve22
    Inherits DirectionalValve

    Public Sub New()
        RebuildPorts()
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Valve22"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return $"2/2-way valve ({If(NormallyOpen, "NO", "NC")}), {ActuationText()}"
        End Get
    End Property

    <Category("Valve"), DisplayName("Normally open"), Description("False: closed at rest (NC). True: open at rest (NO).")>
    Public Property NormallyOpen As Boolean

    Protected Overrides ReadOnly Property BoxWidth As Single = 40
    Protected Overrides ReadOnly Property LeftPilotName As String = "12"
    Protected Overrides ReadOnly Property RightPilotName As String = "10"

    Protected Overrides Function WorkingPorts() As WorkingPort()
        Return {New WorkingPort("2", 20, True), New WorkingPort("1", 20, False)}
    End Function

    Protected Overrides Function Flows(position As Integer) As String()()
        Return If((position = 1) Xor NormallyOpen, {New String() {"1", "2"}}, Array.Empty(Of String())())
    End Function

    Protected Overrides Function ClosedPorts(position As Integer) As String()
        Return If((position = 1) Xor NormallyOpen, Array.Empty(Of String)(), {"1", "2"})
    End Function
End Class

''' <summary>3/2-way directional control valve.</summary>
Public Class Valve32
    Inherits DirectionalValve

    Public Sub New()
        RebuildPorts()
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Valve32"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return $"3/2-way valve ({If(NormallyOpen, "NO", "NC")}), {ActuationText()}"
        End Get
    End Property

    <Category("Valve"), DisplayName("Normally open"), Description("False: 1 is closed at rest (NC). True: 1 is connected to 2 at rest (NO).")>
    Public Property NormallyOpen As Boolean

    Protected Overrides ReadOnly Property BoxWidth As Single = 40
    Protected Overrides ReadOnly Property LeftPilotName As String = "12"
    Protected Overrides ReadOnly Property RightPilotName As String = "10"

    Protected Overrides Function WorkingPorts() As WorkingPort()
        Return {New WorkingPort("2", 10, True), New WorkingPort("1", 10, False), New WorkingPort("3", 30, False, vents:=True)}
    End Function

    Protected Overrides Function Flows(position As Integer) As String()()
        Dim open = (position = 1) Xor NormallyOpen
        Return If(open, {New String() {"1", "2"}}, {New String() {"2", "3"}})
    End Function

    Protected Overrides Function ClosedPorts(position As Integer) As String()
        Dim open = (position = 1) Xor NormallyOpen
        Return If(open, {"3"}, {"1"})
    End Function
End Class

''' <summary>4/2-way directional control valve (one common exhaust).</summary>
Public Class Valve42
    Inherits DirectionalValve

    Public Sub New()
        RebuildPorts()
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Valve42"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return $"4/2-way valve, {ActuationText()}"
        End Get
    End Property

    Protected Overrides ReadOnly Property BoxWidth As Single = 40
    Protected Overrides ReadOnly Property LeftPilotName As String = "14"
    Protected Overrides ReadOnly Property RightPilotName As String = "12"

    Protected Overrides Function WorkingPorts() As WorkingPort()
        Return {New WorkingPort("4", 10, True), New WorkingPort("2", 30, True),
                New WorkingPort("1", 10, False), New WorkingPort("3", 30, False, vents:=True)}
    End Function

    Protected Overrides Function Flows(position As Integer) As String()()
        If position = 1 Then Return {New String() {"1", "4"}, New String() {"2", "3"}}
        Return {New String() {"1", "2"}, New String() {"4", "3"}}
    End Function

    Protected Overrides Function ClosedPorts(position As Integer) As String()
        Return Array.Empty(Of String)()
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
        Return Array.Empty(Of String)()
    End Function
End Class

''' <summary>5/3-way directional control valve, spring centred.</summary>
Public Class Valve53
    Inherits DirectionalValve

    Public Sub New()
        Actuator = ValveActuator.Pilot
        ReturnType = ValveReturn.Pilot
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Valve53"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return $"5/3-way valve ({DescribeCentre()}), {ActuationText()}"
        End Get
    End Property

    <Category("Valve"), DisplayName("Centre position"),
     Description("Closed: all ports blocked (cylinder stops and holds). Exhausted: 2 and 4 vented (cylinder free). Pressurized: 1 to 2 and 4.")>
    Public Property Centre As CentrePosition = CentrePosition.Closed

    Private Function DescribeCentre() As String
        Select Case Centre
            Case CentrePosition.Exhausted : Return "exhaust centre"
            Case CentrePosition.Pressurized : Return "pressure centre"
            Case Else : Return "closed centre"
        End Select
    End Function

    Protected Overrides ReadOnly Property PositionCount As Integer
        Get
            Return 3
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
        Select Case position
            Case 1 : Return {New String() {"1", "4"}, New String() {"2", "3"}}
            Case 2 : Return {New String() {"1", "2"}, New String() {"4", "5"}}
        End Select
        Select Case Centre
            Case CentrePosition.Exhausted : Return {New String() {"4", "5"}, New String() {"2", "3"}}
            Case CentrePosition.Pressurized : Return {New String() {"1", "4"}, New String() {"1", "2"}}
            Case Else : Return Array.Empty(Of String())()
        End Select
    End Function

    Protected Overrides Function ClosedPorts(position As Integer) As String()
        If position <> 0 Then Return Array.Empty(Of String)()
        Select Case Centre
            Case CentrePosition.Exhausted : Return {"1"}
            Case CentrePosition.Pressurized : Return {"5", "3"}
            Case Else : Return {"4", "2", "5", "1", "3"}
        End Select
    End Function
End Class
