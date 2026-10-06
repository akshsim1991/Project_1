Imports System.ComponentModel

''' <summary>An element that uses hydraulic flow when it moves (cylinders, motors).</summary>
Public Interface IHydraulicConsumer
    ''' <summary>True if the element would move with the current valve positions.</summary>
    Function WantsFlow() As Boolean
    ''' <summary>Pressure (bar) needed to move the load.</summary>
    ReadOnly Property LoadPressure As Double
End Interface

''' <summary>Fixed-displacement hydraulic pump driven by an electric motor (click to start / stop).</summary>
Public Class HydraulicPump
    Inherits CircuitElement

    Private _running As Boolean = True

    Public Sub New()
        AddPort("P", 30, 0, 0, -1, kind:=PortKind.Hydraulic)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "HydraulicPump"
    Public Overrides ReadOnly Property DisplayName As String = "Hydraulic power unit (pump)"

    <Category("Pump"), DisplayName("Flow (l/min)"), Description("Delivery of the pump; sets the speed of cylinders and motors.")>
    Public Property FlowLpm As Double
        Get
            Return _flowLpm
        End Get
        Set(value As Double)
            _flowLpm = Math.Max(0.1, Math.Min(2000, value))
        End Set
    End Property
    Private _flowLpm As Double = 8

    <Category("Pump"), DisplayName("Maximum pressure (bar)"), Description("Pressure the pump can build up when there is no relief valve.")>
    Public Property MaxPressureBar As Double
        Get
            Return _maxPressure
        End Get
        Set(value As Double)
            _maxPressure = Math.Max(1, Math.Min(700, value))
        End Set
    End Property
    Private _maxPressure As Double = 250

    ''' <summary>Delivery including a worn-pump fault.</summary>
    <Browsable(False)> Public ReadOnly Property EffectiveFlowLpm As Double
        Get
            Return If(Fault = FaultKind.LowOutput, _flowLpm * 0.35, _flowLpm)
        End Get
    End Property

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.LowOutput}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.LowOutput, "Pump worn: it delivers much less oil", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Motor", If(_running, "running", "stopped")))
        list.Add(("Delivery", If(_running, $"{EffectiveFlowLpm:0.0} l/min", "0 l/min")))
        Return list
    End Function

    <Browsable(False)> Public ReadOnly Property Running As Boolean
        Get
            Return _running
        End Get
    End Property

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 70, 66)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(46, 22)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 30, 0, 30, 14)
        g.FillEllipse(r.BodyBrush, 16, 14, 28, 28)
        g.DrawEllipse(r.Line, 16, 14, 28, 28)
        Using b As New SolidBrush(If(r.Simulating AndAlso _running, RenderContext.HydraulicColor, Color.Black))
            g.FillPolygon(b, {New PointF(30, 16), New PointF(25, 24), New PointF(35, 24)})
        End Using
        ' Drive shaft and electric motor.
        g.DrawLine(r.Line, 16, 28, 6, 28)
        g.DrawEllipse(r.Line, -10, 20, 16, 16)
        g.DrawString("M", r.SmallFont, r.TextBrush, -6, 22)
        ' Suction line to the tank.
        g.DrawLine(r.Line, 30, 42, 30, 54)
        g.DrawLines(r.Line, {New PointF(20, 52), New PointF(20, 60), New PointF(40, 60), New PointF(40, 52)})
        g.DrawString($"{FlowLpm:0.#} l/min" & If(r.Simulating AndAlso Not _running, " (off)", ""), r.SmallFont, r.TextBrush, 46, 34)
    End Sub

    <Browsable(False)> Public Overrides ReadOnly Property IsManuallyOperated As Boolean
        Get
            Return True
        End Get
    End Property

    Public Overrides Sub OnSimMouseDown(local As PointF)
        _running = Not _running
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _running = True
    End Sub

    Public Overrides Sub AddTerminals(sim As Simulator)
        If _running Then sim.AddSupply(Ports(0), MaxPressureBar)
    End Sub
End Class

''' <summary>Oil tank (reservoir).</summary>
Public Class HydraulicTank
    Inherits CircuitElement

    Public Sub New()
        AddPort("T", 20, 0, 0, -1, kind:=PortKind.Hydraulic)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "HydraulicTank"
    Public Overrides ReadOnly Property DisplayName As String = "Tank"

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(4, 0, 32, 22)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 20, 0, 20, 16)
        g.DrawLines(r.Line, {New PointF(6, 8), New PointF(6, 20), New PointF(34, 20), New PointF(34, 8)})
    End Sub

    Public Overrides Sub AddTerminals(sim As Simulator)
        sim.AddExhaust(Ports(0))
    End Sub
End Class

''' <summary>Pressure relief valve: limits the system pressure by returning the pump flow to tank.</summary>
Public Class ReliefValve
    Inherits CircuitElement

    Private _setting As Double = 60

    Public Sub New()
        AddPort("P", 20, 0, 0, -1, kind:=PortKind.Hydraulic)
        AddPort("T", 20, 70, 0, 1, kind:=PortKind.Hydraulic)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "ReliefValve"
    Public Overrides ReadOnly Property DisplayName As String = "Pressure relief valve"

    <Category("Relief valve"), DisplayName("Opening pressure (bar)")>
    Public Property Setting As Double
        Get
            Return _setting
        End Get
        Set(value As Double)
            _setting = Math.Max(1, Math.Min(400, value))
        End Set
    End Property

    ''' <summary>Opening pressure including a weak-spring fault.</summary>
    <Browsable(False)> Public ReadOnly Property EffectiveSetting As Double
        Get
            Return If(Fault = FaultKind.LowOutput, Math.Max(1, _setting * 0.4), _setting)
        End Get
    End Property

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.LowOutput}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.LowOutput, "Relief valve opens far too early (weak or broken spring)", MyBase.FaultDescription(kind))
    End Function

    ''' <summary>True while the valve is open (pump flow going to tank).</summary>
    <Browsable(False)> Public ReadOnly Property IsOpen As Boolean
        Get
            Return Ports(0).IsPressurized AndAlso Ports(0).Pressure >= EffectiveSetting - 0.01
        End Get
    End Property

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 50, 70)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(42, 22)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim open = r.Simulating AndAlso IsOpen
        g.DrawLine(r.PenFor(Ports(0)), 20, 0, 20, 20)
        g.DrawLine(If(open, r.HydraulicPressure, r.Line), 20, 50, 20, 70)
        g.FillRectangle(r.BodyBrush, 6, 20, 28, 30)
        g.DrawRectangle(r.Line, 6, 20, 28, 30)
        ' Flow path offset to the side: it closes until the pilot pressure overcomes the spring.
        Symbols.Arrow(g, If(open, r.HydraulicPressure, r.Line), New PointF(If(open, 20, 14), 22), New PointF(If(open, 20, 14), 48), 5)
        Using d As New Pen(Color.Black, 1) With {.DashStyle = Drawing2D.DashStyle.Dash}
            g.DrawLines(d, {New PointF(20, 10), New PointF(2, 10), New PointF(2, 35), New PointF(6, 35)})
        End Using
        Symbols.Spring(g, r.Thin, 34, 46, 35, 4)
        g.DrawLine(r.Thin, 36, 44, 46, 26)
        g.DrawString($"{_setting:0} bar", r.SmallFont, r.TextBrush, 22, 54)
    End Sub
End Class

''' <summary>Hydraulic double-acting cylinder: speed = flow / area, pressure = load / area.</summary>
Public Class HydraulicCylinder
    Inherits DoubleActingCylinder
    Implements IHydraulicConsumer

    Private _direction As Integer

    Public Sub New()
        For Each p In Ports
            p.Kind = PortKind.Hydraulic
            p.VentsWhenOpen = False
        Next
        BoreMm = 40
        RodMm = 22
        StrokeLength = 200
        LoadForceN = 2000
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "HydraulicCylinder"
    Public Overrides ReadOnly Property DisplayName As String = "Hydraulic cylinder"

    Private Function DesiredDirection() As Integer
        Dim cap = Ports(0), rodP = Ports(1)
        If cap.State = PortState.Pressurized AndAlso rodP.State = PortState.Exhausted Then Return If(Position < 1, 1, 0)
        If rodP.State = PortState.Pressurized AndAlso cap.State = PortState.Exhausted Then Return If(Position > 0, -1, 0)
        ' Both sides pressurized: regenerative circuit, extends with the area difference.
        If cap.State = PortState.Pressurized AndAlso rodP.State = PortState.Pressurized Then Return If(Position < 1, 1, 0)
        ' Both sides open to tank and a load pulling the rod out: the load runs away.
        If cap.State = PortState.Exhausted AndAlso rodP.State = PortState.Exhausted AndAlso LoadForceN < 0 Then Return If(Position < 1, 1, 0)
        Return 0
    End Function

    Public Function WantsFlow() As Boolean Implements IHydraulicConsumer.WantsFlow
        If Fault = FaultKind.Jammed Then Return False
        Dim d = DesiredDirection()
        Return d <> 0 AndAlso Not (Ports(0).State = PortState.Exhausted AndAlso Ports(1).State = PortState.Exhausted)
    End Function

    Public ReadOnly Property LoadPressure As Double Implements IHydraulicConsumer.LoadPressure
        Get
            Dim area = If(_direction >= 0, CapArea(), RodArea())
            Dim force = If(_direction >= 0, LoadForceN, -LoadForceN) + EffectiveFrictionN
            Return Math.Max(2, force / area / 100000.0)
        End Get
    End Property

    ''' <summary>Pressure the load produces in the rod-side line when it pulls the rod out (overrunning load).</summary>
    Public ReadOnly Property OverrunPressure As Double
        Get
            Return If(LoadForceN < 0, -LoadForceN / RodArea() / 100000.0, 0)
        End Get
    End Property

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        _direction = DesiredDirection()
        If _direction = 0 Then MoveAtSpeed(0, dt) : Return
        Dim cap = Ports(0), rodP = Ports(1)
        Dim flow = sim.HydraulicFlowShare / 60000.0 ' m³/s
        Dim speed As Double
        If cap.State = PortState.Exhausted AndAlso rodP.State = PortState.Exhausted Then
            speed = 0.5 ' overrunning load falls freely
        ElseIf _direction > 0 AndAlso rodP.State = PortState.Pressurized Then
            speed = flow / Math.Max(CapArea() - RodArea(), 0.00001) * cap.Factor
        ElseIf _direction > 0 Then
            speed = flow / CapArea() * Math.Min(cap.Factor, rodP.Factor)
        Else
            speed = flow / RodArea() * Math.Min(rodP.Factor, cap.Factor)
        End If
        ' Without enough pressure the load cannot be moved.
        Dim supply = Math.Max(cap.Pressure, rodP.Pressure)
        If supply + 0.01 < LoadPressure AndAlso Not (cap.State = PortState.Exhausted AndAlso rodP.State = PortState.Exhausted) Then speed = 0
        MoveAtSpeed(_direction * speed, dt)
    End Sub
End Class

''' <summary>Single-acting hydraulic cylinder (ram): oil extends it, a spring or the load returns it.</summary>
Public Class HydraulicSingleActingCylinder
    Inherits SingleActingCylinder
    Implements IHydraulicConsumer

    ''' <summary>Return speed with the outlet fully open, m/s (set by the spring or load pushing the oil out).</summary>
    Private Const FreeReturnSpeed As Double = 0.15

    Public Sub New()
        Ports(0).Kind = PortKind.Hydraulic
        Ports(0).VentsWhenOpen = False
        BoreMm = 40
        RodMm = 22
        StrokeLength = 200
        SpringPreloadN = 300
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "HydraulicSingleActingCylinder"
    Public Overrides ReadOnly Property DisplayName As String = "Hydraulic single-acting cylinder"

    Public Function WantsFlow() As Boolean Implements IHydraulicConsumer.WantsFlow
        Return Ports(0).State = PortState.Pressurized AndAlso Position < 1 AndAlso Fault <> FaultKind.Jammed
    End Function

    Public ReadOnly Property LoadPressure As Double Implements IHydraulicConsumer.LoadPressure
        Get
            Dim force = LoadForceN + SpringPreloadN * 1.5 + EffectiveFrictionN
            Return Math.Max(2, force / CapArea() / 100000.0)
        End Get
    End Property

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        Dim p = Ports(0)
        Select Case p.State
            Case PortState.Pressurized
                If Position >= 1 OrElse p.Pressure + 0.01 < LoadPressure Then MoveAtSpeed(0, dt) : Return
                MoveAtSpeed(sim.HydraulicFlowShare / 60000.0 / CapArea() * p.Factor, dt)
            Case PortState.Exhausted
                MoveAtSpeed(-FreeReturnSpeed * Math.Max(p.Factor, 0.02), dt)
            Case Else
                MoveAtSpeed(0, dt) ' oil trapped: the ram holds its position
        End Select
    End Sub
End Class

''' <summary>Hydraulic motor (bidirectional): speed = flow / displacement.</summary>
Public Class HydraulicMotor
    Inherits CircuitElement
    Implements IHydraulicConsumer

    Private _angle As Double
    Private _rpm As Double

    Public Sub New()
        AddPort("A", 10, 60, 0, 1, kind:=PortKind.Hydraulic)
        AddPort("B", 50, 60, 0, 1, kind:=PortKind.Hydraulic)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "HydraulicMotor"
    Public Overrides ReadOnly Property DisplayName As String = "Hydraulic motor"

    <Category("Motor"), DisplayName("Displacement (cm³/rev)")>
    Public Property DisplacementCc As Double
        Get
            Return _displacement
        End Get
        Set(value As Double)
            _displacement = Math.Max(0.1, Math.Min(10000, value))
        End Set
    End Property
    Private _displacement As Double = 10

    <Category("Motor"), DisplayName("Load pressure (bar)"), Description("Pressure needed to turn the load (torque / displacement).")>
    Public Property LoadPressureBar As Double
        Get
            Return _loadPressure
        End Get
        Set(value As Double)
            _loadPressure = Math.Max(0, Math.Min(700, value))
        End Set
    End Property
    Private _loadPressure As Double = 20

    Public ReadOnly Property Rpm As Double
        Get
            Return _rpm
        End Get
    End Property

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 60, 60)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(62, 18)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 10, 60, 10, 30)
        g.DrawLine(r.PenFor(Ports(0)), 10, 30, 16, 30)
        g.DrawLine(r.PenFor(Ports(1)), 50, 60, 50, 30)
        g.DrawLine(r.PenFor(Ports(1)), 50, 30, 44, 30)
        g.FillEllipse(r.BodyBrush, 16, 16, 28, 28)
        g.DrawEllipse(r.Line, 16, 16, 28, 28)
        Using b As New SolidBrush(If(r.Simulating AndAlso _rpm <> 0, RenderContext.HydraulicColor, Color.Black))
            g.FillPolygon(b, {New PointF(18, 30), New PointF(25, 26), New PointF(25, 34)})
            g.FillPolygon(b, {New PointF(42, 30), New PointF(35, 26), New PointF(35, 34)})
        End Using
        Dim a = _angle * Math.PI / 180
        g.DrawLine(r.Thin, 30, 30, CSng(30 + 10 * Math.Cos(a)), CSng(30 + 10 * Math.Sin(a)))
        If r.Simulating Then g.DrawString($"{_rpm:0} rpm", r.SmallFont, r.TextBrush, 46, 6)
    End Sub

    Public Function WantsFlow() As Boolean Implements IHydraulicConsumer.WantsFlow
        Dim a = Ports(0), b = Ports(1)
        Return (a.State = PortState.Pressurized AndAlso b.State = PortState.Exhausted) OrElse
               (b.State = PortState.Pressurized AndAlso a.State = PortState.Exhausted)
    End Function

    Public ReadOnly Property LoadPressure As Double Implements IHydraulicConsumer.LoadPressure
        Get
            Return Math.Max(2, LoadPressureBar)
        End Get
    End Property

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _angle = 0
        _rpm = 0
    End Sub

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        Dim a = Ports(0), b = Ports(1)
        Dim dir = If(a.State = PortState.Pressurized AndAlso b.State = PortState.Exhausted, 1,
                     If(b.State = PortState.Pressurized AndAlso a.State = PortState.Exhausted, -1, 0))
        Dim p = Math.Max(a.Pressure, b.Pressure)
        Dim factor = Math.Min(a.Factor, b.Factor)
        _rpm = If(dir <> 0 AndAlso p + 0.01 >= LoadPressure, dir * sim.HydraulicFlowShare * 1000 / Math.Max(0.1, DisplacementCc) * factor, 0)
        _angle = (_angle + _rpm * 6 * dt * 0.2) Mod 360
    End Sub
End Class

''' <summary>Gas-charged accumulator: stores oil under pressure and supplies it when the pump cannot.</summary>
Public Class Accumulator
    Inherits CircuitElement

    Private _stored As Double ' litres of oil

    Public Sub New()
        AddPort("1", 20, 70, 0, 1, kind:=PortKind.Hydraulic)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Accumulator"
    Public Overrides ReadOnly Property DisplayName As String = "Accumulator"

    <Category("Accumulator"), DisplayName("Gas volume (l)")>
    Public Property CapacityL As Double
        Get
            Return _capacity
        End Get
        Set(value As Double)
            _capacity = Math.Max(0.05, Math.Min(500, value))
        End Set
    End Property
    Private _capacity As Double = 1

    <Category("Accumulator"), DisplayName("Pre-charge pressure (bar)")>
    Public Property PrechargeBar As Double
        Get
            Return _precharge
        End Get
        Set(value As Double)
            _precharge = Math.Max(1, Math.Min(400, value))
        End Set
    End Property
    Private _precharge As Double = 30

    <Browsable(False)> Public ReadOnly Property StoredLitres As Double
        Get
            Return _stored
        End Get
    End Property

    ''' <summary>Gas pressure for the stored oil volume (isothermal).</summary>
    Public ReadOnly Property GasPressure As Double
        Get
            Return If(_stored <= 0.0001, 0, PrechargeBar * CapacityL / Math.Max(0.01, CapacityL - _stored))
        End Get
    End Property

    ''' <summary>True while the accumulator feeds the system (no pump pressure on its port).</summary>
    <Browsable(False)> Public Property Discharging As Boolean

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(4, 0, 32, 70)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(38, 18)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 20, 58, 20, 70)
        g.FillEllipse(r.BodyBrush, 6, 4, 28, 54)
        ' Oil level.
        Dim level = CSng(Math.Min(1, _stored / Math.Max(0.01, CapacityL)))
        If level > 0 Then
            Using b As New SolidBrush(Color.FromArgb(120, RenderContext.HydraulicColor))
                g.FillRectangle(b, 9, 56 - 50 * level, 22, 50 * level)
            End Using
        End If
        g.DrawEllipse(r.Line, 6, 4, 28, 54)
        g.DrawLine(r.Line, 6, 31, 34, 31)
        If r.Simulating Then g.DrawString($"{_stored:0.00} l", r.SmallFont, r.TextBrush, 38, 40)
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _stored = 0
        Discharging = False
    End Sub

    Public Overrides Sub AddTerminals(sim As Simulator)
        If Discharging AndAlso _stored > 0.001 Then sim.AddSupply(Ports(0), GasPressure)
    End Sub

    ''' <summary>Called by the simulator: charge from the system, or discharge into it.</summary>
    Friend Sub Exchange(systemPressure As Double, pumpFed As Boolean, flowOutLpm As Double, dt As Double)
        If pumpFed AndAlso systemPressure > PrechargeBar Then
            ' Charge towards the volume matching the system pressure.
            Dim target = CapacityL * (1 - PrechargeBar / systemPressure)
            _stored = Math.Min(target, _stored + 20 / 60.0 * dt)
        ElseIf Not pumpFed Then
            _stored = Math.Max(0, _stored - flowOutLpm / 60.0 * dt)
        End If
    End Sub
End Class

<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum HydraulicCentre
    <Description("Closed centre (all ports blocked)")> Closed
    <Description("Tandem centre (P to T, A and B blocked)")> Tandem
    <Description("Float centre (A and B to T, P blocked)")> Float
    <Description("Open centre (all ports connected)")> Open
End Enum

''' <summary>4/3-way hydraulic directional valve (P, T, A, B), spring centred.</summary>
Public Class HydraulicValve43
    Inherits DirectionalValve

    Public Sub New()
        Actuator = ValveActuator.Solenoid
        ReturnType = ValveReturn.Solenoid
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "HydraulicValve43"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return $"4/3-way hydraulic valve ({Centre.ToString().ToLowerInvariant()} centre), {ActuationText()}"
        End Get
    End Property

    <Category("Valve"), DisplayName("Centre position")>
    Public Property Centre As HydraulicCentre = HydraulicCentre.Tandem

    Protected Overrides ReadOnly Property Medium As PortKind
        Get
            Return PortKind.Hydraulic
        End Get
    End Property

    Protected Overrides ReadOnly Property PositionCount As Integer
        Get
            Return 3
        End Get
    End Property

    Protected Overrides ReadOnly Property BoxWidth As Single = 40
    Protected Overrides ReadOnly Property LeftPilotName As String = "X"
    Protected Overrides ReadOnly Property RightPilotName As String = "Y"

    Protected Overrides Function WorkingPorts() As WorkingPort()
        Return {New WorkingPort("A", 10, True), New WorkingPort("B", 30, True),
                New WorkingPort("P", 10, False), New WorkingPort("T", 30, False)}
    End Function

    Protected Overrides Function Flows(position As Integer) As String()()
        Select Case position
            Case 1 : Return {New String() {"P", "A"}, New String() {"B", "T"}}
            Case 2 : Return {New String() {"P", "B"}, New String() {"A", "T"}}
        End Select
        Select Case Centre
            Case HydraulicCentre.Tandem : Return {New String() {"P", "T"}}
            Case HydraulicCentre.Float : Return {New String() {"A", "T"}, New String() {"B", "T"}}
            Case HydraulicCentre.Open : Return {New String() {"P", "T"}, New String() {"A", "T"}, New String() {"B", "T"}}
            Case Else : Return Array.Empty(Of String())()
        End Select
    End Function

    Protected Overrides Function ClosedPorts(position As Integer) As String()
        If position <> 0 Then Return Array.Empty(Of String)()
        Select Case Centre
            Case HydraulicCentre.Tandem : Return {"A", "B"}
            Case HydraulicCentre.Float : Return {"P"}
            Case HydraulicCentre.Open : Return Array.Empty(Of String)()
            Case Else : Return {"A", "B", "P", "T"}
        End Select
    End Function
End Class

''' <summary>4/2-way hydraulic directional valve (P, T, A, B).</summary>
Public Class HydraulicValve42
    Inherits DirectionalValve

    Public Sub New()
        Actuator = ValveActuator.Solenoid
        RebuildPorts()
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "HydraulicValve42"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return $"4/2-way hydraulic valve, {ActuationText()}"
        End Get
    End Property

    Protected Overrides ReadOnly Property Medium As PortKind
        Get
            Return PortKind.Hydraulic
        End Get
    End Property

    Protected Overrides ReadOnly Property BoxWidth As Single = 40
    Protected Overrides ReadOnly Property LeftPilotName As String = "X"
    Protected Overrides ReadOnly Property RightPilotName As String = "Y"

    Protected Overrides Function WorkingPorts() As WorkingPort()
        Return {New WorkingPort("A", 10, True), New WorkingPort("B", 30, True),
                New WorkingPort("P", 10, False), New WorkingPort("T", 30, False)}
    End Function

    Protected Overrides Function Flows(position As Integer) As String()()
        If position = 1 Then Return {New String() {"P", "A"}, New String() {"B", "T"}}
        Return {New String() {"P", "B"}, New String() {"A", "T"}}
    End Function

    Protected Overrides Function ClosedPorts(position As Integer) As String()
        Return Array.Empty(Of String)()
    End Function
End Class

''' <summary>Pressure-compensated flow control valve: constant flow regardless of load, free return through the check valve.</summary>
Public Class CompensatedFlowControl
    Inherits CircuitElement

    Public Sub New()
        AddPort("1", 0, 30, -1, 0, kind:=PortKind.Hydraulic)
        AddPort("2", 90, 30, 1, 0, kind:=PortKind.Hydraulic)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "CompensatedFlowControl"
    Public Overrides ReadOnly Property DisplayName As String = "Pressure-compensated flow control valve"

    <Category("Flow control"), DisplayName("Flow (l/min)"), Description("Flow passed from 1 to 2, independent of the load pressure.")>
    Public Property FlowLpm As Double
        Get
            Return _flow
        End Get
        Set(value As Double)
            _flow = Math.Max(0.1, Math.Min(2000, value))
        End Set
    End Property
    Private _flow As Double = 4

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 90, 50)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim pen1 = r.PenFor(Ports(0)), pen2 = r.PenFor(Ports(1))
        g.DrawLine(pen1, 0, 30, 20, 30)
        g.DrawLine(pen2, 70, 30, 90, 30)
        g.FillRectangle(r.BodyBrush, 20, 14, 50, 32)
        g.DrawRectangle(r.Line, 20, 14, 50, 32)
        g.DrawArc(r.Line, 36, 23, 18, 6, 200, 140)
        g.DrawArc(r.Line, 36, 31, 18, 6, 20, 140)
        g.DrawLine(r.Line, 20, 30, 36, 30) : g.DrawLine(r.Line, 54, 30, 70, 30)
        Symbols.Arrow(g, r.Thin, New PointF(34, 40), New PointF(56, 18), 5)
        ' Compensator: small arrow showing constant flow direction.
        Symbols.Arrow(g, r.Thin, New PointF(26, 20), New PointF(40, 20), 4)
        ' Check valve bypass (free flow 2 -> 1).
        g.DrawLines(r.Line, {New PointF(10, 30), New PointF(10, 6), New PointF(80, 6), New PointF(80, 30)})
        g.FillEllipse(r.BodyBrush, 41, 1, 8, 8)
        g.DrawEllipse(r.Line, 41, 1, 8, 8)
        g.DrawLine(r.Line, 49, 0, 53, 6) : g.DrawLine(r.Line, 53, 6, 49, 12)
        g.DrawString($"{FlowLpm:0.#} l/min", r.SmallFont, r.TextBrush, 24, 47)
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        Dim f = Math.Min(1, FlowLpm / Math.Max(0.01, sim.HydraulicPumpFlow))
        sim.AddEdge(Ports(0), Ports(1), f, 1)
    End Sub
End Class

''' <summary>
''' Counterbalance (load-holding) valve: free flow 1 to 2; flow 2 to 1 only when the load pressure
''' at 2 reaches the setting or the pilot X is pressurized. Holds a hanging load in place.
''' </summary>
Public Class CounterbalanceValve
    Inherits CircuitElement

    Private _open As Boolean

    ''' <summary>True while oil may flow back from 2 to 1 (load lowered).</summary>
    <Browsable(False)> Public ReadOnly Property IsOpen As Boolean
        Get
            Return _open
        End Get
    End Property

    Public Sub New()
        AddPort("1", 0, 50, -1, 0, kind:=PortKind.Hydraulic)
        AddPort("2", 70, 50, 1, 0, kind:=PortKind.Hydraulic)
        AddPort("X", 35, 0, 0, -1, kind:=PortKind.Hydraulic)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "CounterbalanceValve"
    Public Overrides ReadOnly Property DisplayName As String = "Counterbalance valve"

    <Category("Valve"), DisplayName("Opening pressure (bar)"), Description("Set about 1.3 times the load-induced pressure.")>
    Public Property Setting As Double
        Get
            Return _setting
        End Get
        Set(value As Double)
            _setting = Math.Max(1, Math.Min(400, value))
        End Set
    End Property
    Private _setting As Double = 50

    <Category("Valve"), DisplayName("Pilot pressure to open (bar)")>
    Public Property PilotPressure As Double
        Get
            Return _pilot
        End Get
        Set(value As Double)
            _pilot = Math.Max(1, Math.Min(400, value))
        End Set
    End Property
    Private _pilot As Double = 10

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 70, 66)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 0, 50, 20, 50)
        g.DrawLine(r.PenFor(Ports(1)), 50, 50, 70, 50)
        g.FillRectangle(r.BodyBrush, 20, 20, 30, 40)
        g.DrawRectangle(r.Line, 20, 20, 30, 40)
        Symbols.Arrow(g, If(r.Simulating AndAlso _open, r.HydraulicPressure, r.Line), New PointF(If(_open, 46, 40), 50), New PointF(If(_open, 24, 30), 50), 5)
        Symbols.Spring(g, r.Thin, 50, 60, 30, 3)
        Using d As New Pen(Color.Black, 1) With {.DashStyle = Drawing2D.DashStyle.Dash}
            g.DrawLine(d, 35, 0, 35, 20)
        End Using
        ' Check valve: free flow 1 -> 2 below.
        g.DrawLines(r.Line, {New PointF(10, 50), New PointF(10, 64), New PointF(60, 64), New PointF(60, 50)})
        g.FillEllipse(r.BodyBrush, 33, 60, 8, 8)
        g.DrawEllipse(r.Line, 33, 60, 8, 8)
        g.DrawString($"{Setting:0} bar", r.SmallFont, r.TextBrush, 52, 14)
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _open = False
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim open = Ports(1).Pressure >= Setting - 0.01 OrElse Ports(2).Pressure >= PilotPressure
        If open = _open Then Return False
        _open = open
        Return True
    End Function

    Public Overrides Sub AddEdges(sim As Simulator)
        sim.AddEdge(Ports(0), Ports(1), 1, If(_open, 1, 0))
    End Sub
End Class
