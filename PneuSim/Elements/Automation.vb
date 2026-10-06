Imports System.ComponentModel

''' <summary>
''' Pressure switch (pneumatic-electric converter): switches when the pressure at its port reaches
''' the setting. Contacts with 'Operated by: Pressure switch' and its label as Reference follow it,
''' and so do roller-type valves and sensors that name it. A negative setting makes a vacuum switch.
''' </summary>
Public Class PressureSwitch
    Inherits CircuitElement
    Implements ISignalSource

    Private _setting As Double = 4
    Private _on As Boolean

    ''' <summary>Pressure the switch falls back below before it switches off again (bar).</summary>
    Public Const Hysteresis As Double = 0.2

    Public Sub New()
        AddPort("1", 20, 60, 0, 1)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "PressureSwitch"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return If(_setting < 0, "Vacuum switch", "Pressure switch")
        End Get
    End Property

    <Category("Pressure switch"), DisplayName("Switching pressure (bar)"),
     Description("The switch operates when the pressure reaches this value. Use a negative value (e.g. -0.5) for a vacuum switch.")>
    Public Property Setting As Double
        Get
            Return _setting
        End Get
        Set(value As Double)
            _setting = Math.Max(-0.95, Math.Min(400, value))
        End Set
    End Property

    <Category("Medium"), DisplayName("Hydraulic"), Description("True: fitted in a hydraulic (oil) line. False: compressed air.")>
    Public Property Hydraulic As Boolean
        Get
            Return Ports(0).Kind = PortKind.Hydraulic
        End Get
        Set(value As Boolean)
            Ports(0).Kind = If(value, PortKind.Hydraulic, PortKind.Pneumatic)
        End Set
    End Property

    ''' <summary>True while the switch is operated.</summary>
    <Browsable(False)> Public ReadOnly Property IsOn As Boolean
        Get
            Return _on
        End Get
    End Property

    Public Function IsSignalOn(name As String) As Boolean Implements ISignalSource.IsSignalOn
        Return _on AndAlso String.Equals(Label?.Trim(), name, StringComparison.OrdinalIgnoreCase)
    End Function

    Public Function SignalNames() As IEnumerable(Of String) Implements ISignalSource.SignalNames
        Return If(String.IsNullOrWhiteSpace(Label), Array.Empty(Of String)(), {Label.Trim()})
    End Function

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.StuckNormal, FaultKind.StuckOperated}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Select Case kind
            Case FaultKind.StuckNormal : Return "Pressure switch does not switch (sensing port blocked or switch broken)"
            Case FaultKind.StuckOperated : Return "Pressure switch always switched (stuck)"
        End Select
        Return MyBase.FaultDescription(kind)
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Switching pressure", $"{_setting:0.0#} bar"))
        list.Add(("Switch", If(_on, "operated", "at rest")))
        Return list
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 4, 44, 56)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(46, 18)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim p = Ports(0)
        g.DrawLine(r.PenFor(p), 20, 60, 20, 44)
        ' Pressure sensing part: pilot line with energy triangle into the switch box.
        Using d As New Pen(If(r.Simulating AndAlso p.IsPressurized, RenderContext.ActiveColor(p.Kind), Color.Black), 1.2F) With {.DashStyle = Drawing2D.DashStyle.Dash}
            g.DrawLine(d, 20, 44, 20, 36)
        End Using
        Symbols.EnergyTriangle(g, r, {New PointF(20, 30), New PointF(16, 36), New PointF(24, 36)}, Hydraulic, r.Simulating AndAlso p.IsPressurized)
        g.FillRectangle(r.BodyBrush, 4, 8, 32, 22)
        g.DrawRectangle(r.Line, 4, 8, 32, 22)
        ' Electrical contact inside the box.
        Dim on_ = r.Simulating AndAlso _on
        g.DrawLine(r.Thin, 10, 24, 16, 24)
        g.DrawLine(If(on_, r.Energized, r.Line), 16, 24, 28, If(on_, 24, 16))
        g.DrawLine(r.Thin, 28, 24, 32, 24)
        Dim txt = If(r.Simulating, If(Hydraulic, $"{p.Pressure:0} bar", $"{p.Pressure:0.0} bar"), $"{_setting:0.0#} bar")
        g.DrawString(txt, r.SmallFont, r.TextBrush, 26, 42)
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _on = False
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim p = Ports(0).Pressure
        Dim on_ As Boolean
        If _setting >= 0 Then
            on_ = If(_on, p >= _setting - Hysteresis, p >= _setting - 0.000001)
        Else
            on_ = If(_on, p <= _setting + Hysteresis, p <= _setting + 0.000001)
        End If
        If Fault = FaultKind.StuckNormal Then on_ = False
        If Fault = FaultKind.StuckOperated Then on_ = True
        If on_ = _on Then Return False
        _on = on_
        Return True
    End Function
End Class

''' <summary>Manual shut-off (ball) valve: click it during the simulation to open or close it.</summary>
Public Class ShutOffValve
    Inherits CircuitElement

    Private _open As Boolean = True

    Public Sub New()
        AddPort("1", 0, 30, -1, 0)
        AddPort("2", 60, 30, 1, 0)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "ShutOffValve"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return If(Hydraulic, "Hydraulic shut-off valve", "Shut-off valve")
        End Get
    End Property

    <Category("Valve"), DisplayName("Open at start"), Description("Position of the handle when the simulation starts.")>
    Public Property OpenAtStart As Boolean = True

    <Category("Medium"), DisplayName("Hydraulic"), Description("True: fitted in a hydraulic (oil) line. False: compressed air.")>
    Public Property Hydraulic As Boolean
        Get
            Return Ports(0).Kind = PortKind.Hydraulic
        End Get
        Set(value As Boolean)
            For Each p In Ports
                p.Kind = If(value, PortKind.Hydraulic, PortKind.Pneumatic)
            Next
        End Set
    End Property

    ''' <summary>True while the valve is open.</summary>
    <Browsable(False)> Public ReadOnly Property IsOpen As Boolean
        Get
            Return _open AndAlso Fault <> FaultKind.Blocked
        End Get
    End Property

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.Blocked}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.Blocked, "Ball seized: the valve stays closed whatever the handle shows", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Handle", If(_open, "open", "closed")))
        Return list
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 4, 60, 48)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim open = If(r.Simulating, _open, OpenAtStart)
        Dim active = r.Simulating AndAlso IsOpen
        g.DrawLine(r.PenFor(Ports(0)), 0, 30, 18, 30)
        g.DrawLine(If(active, r.PenFor(Ports(0)), r.PenFor(Ports(1))), 42, 30, 60, 30)
        ' ISO symbol: two triangles meeting at their tips; filled when closed.
        Dim left = {New PointF(18, 22), New PointF(30, 30), New PointF(18, 38)}
        Dim right = {New PointF(42, 22), New PointF(30, 30), New PointF(42, 38)}
        If open Then
            g.FillPolygon(r.BodyBrush, left) : g.FillPolygon(r.BodyBrush, right)
        Else
            g.FillPolygon(Brushes.Black, left) : g.FillPolygon(Brushes.Black, right)
        End If
        g.DrawPolygon(r.Line, left) : g.DrawPolygon(r.Line, right)
        ' Hand lever: along the pipe when open, across it when closed.
        g.DrawLine(r.Thin, 30, 30, 30, 18)
        If open Then
            g.DrawLine(r.Line, 30, 14, 46, 14)
        Else
            g.DrawLine(r.Line, 30, 6, 30, 18)
        End If
        If r.Simulating Then g.DrawString(If(open, "open", "closed"), r.SmallFont, r.TextBrush, 18, 40)
    End Sub

    <Browsable(False)> Public Overrides ReadOnly Property IsManuallyOperated As Boolean
        Get
            Return True
        End Get
    End Property

    Public Overrides Sub OnSimMouseDown(local As PointF)
        _open = Not _open
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _open = OpenAtStart
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        If IsOpen Then sim.AddEdge(Ports(0), Ports(1))
    End Sub
End Class

''' <summary>
''' Vacuum generator (ejector): compressed air at port 1 blows through a nozzle and sucks air out of
''' the vacuum port V. The air leaves through the built-in silencer.
''' </summary>
Public Class VacuumGenerator
    Inherits CircuitElement

    Public Sub New()
        AddPort("1", 20, 60, 0, 1)
        AddPort("V", 20, 0, 0, -1)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "VacuumGenerator"
    Public Overrides ReadOnly Property DisplayName As String = "Vacuum generator (ejector)"

    <Category("Vacuum generator"), DisplayName("Vacuum (bar)"), Description("Vacuum reached with a closed suction line, e.g. -0.85 bar.")>
    Public Property VacuumLevel As Double
        Get
            Return _level
        End Get
        Set(value As Double)
            _level = Math.Max(-0.95, Math.Min(-0.1, value))
        End Set
    End Property
    Private _level As Double = -0.85

    <Category("Vacuum generator"), DisplayName("Air consumption (NL/min)"), Description("Free air blown through the nozzle while it works (at 6 bar).")>
    Public Property AirConsumptionNlMin As Double
        Get
            Return _consumption
        End Get
        Set(value As Double)
            _consumption = Math.Max(0, Math.Min(10000, value))
        End Set
    End Property
    Private _consumption As Double = 60

    ''' <summary>True while compressed air drives the nozzle (and it is not clogged).</summary>
    <Browsable(False)> Public ReadOnly Property Working As Boolean
        Get
            Return Ports(0).State = PortState.Pressurized AndAlso Ports(0).Pressure >= 2 AndAlso Fault <> FaultKind.Blocked
        End Get
    End Property

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.Blocked}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.Blocked, "Nozzle clogged: no vacuum is produced", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Nozzle", If(Working, "blowing (making vacuum)", "off")))
        Return list
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 64, 60)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(44, 14)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim on_ = r.Simulating AndAlso Working
        g.DrawLine(r.PenFor(Ports(0)), 20, 60, 20, 42)
        g.DrawLine(r.Line, 20, 0, 20, 18)
        g.FillRectangle(r.BodyBrush, 4, 18, 32, 24)
        g.DrawRectangle(r.Line, 4, 18, 32, 24)
        ' Nozzle (converging) and diffuser, with the suction port between them.
        Dim pen = If(on_, r.Pressure, r.Line)
        g.DrawLine(pen, 20, 42, 20, 34)
        g.DrawLines(r.Thin, {New PointF(12, 38), New PointF(18, 32), New PointF(22, 32), New PointF(28, 38)})
        g.DrawLines(r.Thin, {New PointF(18, 24), New PointF(14, 20)})
        g.DrawLines(r.Thin, {New PointF(22, 24), New PointF(26, 20)})
        ' Built-in silencer on the exhaust.
        g.DrawLine(r.Line, 36, 30, 44, 30)
        g.DrawRectangle(r.Line, 44, 26, 8, 8)
        For hx = 46 To 50 Step 2
            g.DrawLine(r.Thin, hx, 26, hx, 34)
        Next
        If r.Simulating Then g.DrawString(If(Working, $"{Ports(1).Pressure:0.00} bar", "off"), r.SmallFont, r.TextBrush, 24, 46)
    End Sub

    ''' <summary>Free air blown through the nozzle now, NL/min.</summary>
    Public Function ConsumptionNow() As Double
        If Not Working Then Return 0
        Return _consumption * (Ports(0).Pressure + CylinderBase.Atm) / (6 + CylinderBase.Atm)
    End Function

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        sim.AddAirConsumption(ConsumptionNow() * dt / 60)
    End Sub
End Class

''' <summary>Suction cup: holds the workpiece while there is enough vacuum. Click it to put a workpiece under it or take it away.</summary>
Public Class SuctionCup
    Inherits CircuitElement

    Private _part As Boolean = True

    ''' <summary>Vacuum needed to hold the workpiece (bar).</summary>
    Public Const HoldVacuum As Double = -0.3

    Public Sub New()
        AddPort("1", 20, 0, 0, -1)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "SuctionCup"
    Public Overrides ReadOnly Property DisplayName As String = "Suction cup"

    <Category("Suction cup"), DisplayName("Diameter (mm)"), Description("Cup diameter; sets the holding force.")>
    Public Property DiameterMm As Double
        Get
            Return _diameter
        End Get
        Set(value As Double)
            _diameter = Math.Max(5, Math.Min(500, value))
        End Set
    End Property
    Private _diameter As Double = 40

    <Category("Suction cup"), DisplayName("Workpiece at start"), Description("True: a workpiece lies under the cup when the simulation starts.")>
    Public Property WorkpieceAtStart As Boolean = True

    ''' <summary>True while a workpiece is under the cup.</summary>
    <Browsable(False)> Public ReadOnly Property WorkpiecePresent As Boolean
        Get
            Return _part
        End Get
    End Property

    ''' <summary>True if no air can get into the cup (a workpiece seals it and the cup is not torn).</summary>
    <Browsable(False)> Public ReadOnly Property Sealed As Boolean
        Get
            Return _part AndAlso Fault <> FaultKind.Leak
        End Get
    End Property

    ''' <summary>True while the workpiece is held.</summary>
    <Browsable(False)> Public ReadOnly Property Holding As Boolean
        Get
            Return Sealed AndAlso Ports(0).Pressure <= HoldVacuum
        End Get
    End Property

    ''' <summary>Holding force (N) from the vacuum.</summary>
    Public Function HoldingForce() As Double
        Return Math.Max(0, -Ports(0).Pressure) * 100000.0 * Math.PI / 4 * (_diameter / 1000) ^ 2
    End Function

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.Leak}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.Leak, "Cup lip torn: air leaks in, so it cannot hold", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Workpiece", If(_part, If(Holding, "held", "lying under the cup, not held"), "none")))
        list.Add(("Holding force", $"{HoldingForce():0} N"))
        Return list
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 44, 54)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(40, 22)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim vac = r.Simulating AndAlso Ports(0).Pressure < -0.05
        Using pen As New Pen(If(vac, RenderContext.VacuumColor, Color.Black), 1.6F)
            g.DrawLine(pen, 20, 0, 20, 18)
        End Using
        ' Bellows cup: a trapezoid opening downwards.
        g.FillPolygon(r.BodyBrush, {New PointF(14, 18), New PointF(26, 18), New PointF(36, 32), New PointF(4, 32)})
        g.DrawPolygon(r.Line, {New PointF(14, 18), New PointF(26, 18), New PointF(36, 32), New PointF(4, 32)})
        Dim part = If(r.Simulating, _part, WorkpieceAtStart)
        If part Then
            Dim held = r.Simulating AndAlso Holding
            Dim y = If(held OrElse Not r.Simulating, 33.0F, 37.0F)
            Using b As New SolidBrush(If(held, Color.FromArgb(120, 160, 90), Color.FromArgb(170, 170, 170)))
                g.FillRectangle(b, 0, y, 40, 8)
            End Using
            g.DrawRectangle(r.Thin, 0, y, 40, 8)
        End If
        If r.Simulating Then g.DrawString(If(Holding, "held", If(_part, "not held", "no part")), r.SmallFont, r.TextBrush, 0, 44)
    End Sub

    <Browsable(False)> Public Overrides ReadOnly Property IsManuallyOperated As Boolean
        Get
            Return True
        End Get
    End Property

    Public Overrides Sub OnSimMouseDown(local As PointF)
        _part = Not _part
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _part = WorkpieceAtStart
    End Sub
End Class

''' <summary>Double-acting parallel gripper: air at port 1 closes the jaws, air at port 2 opens them.</summary>
Public Class Gripper
    Inherits DoubleActingCylinder

    Public Sub New()
        StrokeLength = 10
        StrokeTime = 0.25
        BoreMm = 16
        RodMm = 6
        LoadMassKg = 0.1
        FrictionN = 10
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Gripper"
    Public Overrides ReadOnly Property DisplayName As String = "Parallel gripper (double-acting)"

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return RectangleF.FromLTRB(0, -16, 150, 40)
        End Get
    End Property

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Insert(0, ("Jaws", If(Position >= 0.995, "closed", If(Position <= 0.005, "open", "moving"))))
        Return list
    End Function

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.FillRectangle(r.BodyBrush, 0, 0, 120, 30)
        g.DrawRectangle(r.Line, 0, 0, 120, 30)
        g.DrawString("gripper", r.SmallFont, r.TextBrush, 40, 9)
        DrawPortStub(g, r, Ports(0))
        DrawPortStub(g, r, Ports(1))
        ' Two jaws moving towards each other as the piston "extends".
        Dim gap = CSng(22 * (1 - Position))
        Using b As New SolidBrush(Color.FromArgb(70, 70, 70))
            g.FillRectangle(b, 120, 15 - gap - 8, 26, 8)
            g.FillRectangle(b, 120, 15 + gap, 26, 8)
        End Using
        g.DrawLine(r.Thin, 140, 15 - gap, 140, 15 + gap)
        If Not String.IsNullOrWhiteSpace(RetractedMark) Then g.DrawString(RetractedMark, r.SmallFont, r.MarkBrush, 2, -14)
        If Not String.IsNullOrWhiteSpace(ExtendedMark) Then g.DrawString(ExtendedMark, r.SmallFont, r.MarkBrush, 80, -14)
        g.DrawString("1 close   2 open", r.SmallFont, r.TextBrush, 22, 31)
    End Sub
End Class

''' <summary>
''' Air compressor with its pressure switch: charges the air receivers connected to it, starting
''' at the cut-in pressure and stopping at the cut-out pressure. Without a receiver it works as
''' a plain supply. Click it to switch the main switch on or off.
''' </summary>
Public Class Compressor
    Inherits CircuitElement

    Private _switchedOn As Boolean = True
    Private _running As Boolean

    Public Sub New()
        AddPort("1", 30, 0, 0, -1)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Compressor"
    Public Overrides ReadOnly Property DisplayName As String = "Compressor"

    <Category("Compressor"), DisplayName("Delivery (NL/min)"), Description("Free air delivered while running.")>
    Public Property DeliveryNlMin As Double
        Get
            Return _delivery
        End Get
        Set(value As Double)
            _delivery = Math.Max(1, Math.Min(100000, value))
        End Set
    End Property
    Private _delivery As Double = 250

    <Category("Compressor"), DisplayName("Cut-out pressure (bar)"), Description("The compressor stops when the receiver reaches this pressure.")>
    Public Property CutOutPressure As Double
        Get
            Return _cutOut
        End Get
        Set(value As Double)
            _cutOut = Math.Max(1, Math.Min(16, value))
        End Set
    End Property
    Private _cutOut As Double = 8

    <Category("Compressor"), DisplayName("Cut-in pressure (bar)"), Description("The compressor starts again when the receiver falls to this pressure.")>
    Public Property CutInPressure As Double
        Get
            Return Math.Min(_cutIn, _cutOut - 0.2)
        End Get
        Set(value As Double)
            _cutIn = Math.Max(0.5, Math.Min(15.8, value))
        End Set
    End Property
    Private _cutIn As Double = 6.5

    ''' <summary>Set by the simulator: true if the compressor charges a receiver (it is then not a supply itself).</summary>
    <Browsable(False)> Public Property ChargesReceiver As Boolean

    ''' <summary>True while the motor runs.</summary>
    <Browsable(False)> Public ReadOnly Property Running As Boolean
        Get
            Return _running
        End Get
    End Property

    ''' <summary>Delivery including a worn-compressor fault (NL/min).</summary>
    <Browsable(False)> Public ReadOnly Property EffectiveDelivery As Double
        Get
            Return If(Fault = FaultKind.LowOutput, _delivery * 0.3, _delivery)
        End Get
    End Property

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.LowOutput}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.LowOutput, "Compressor worn (valves or rings): it delivers far less air", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Main switch", If(_switchedOn, "on", "off")))
        list.Add(("Motor", If(_running, "running", "stopped")))
        list.Add(("Delivery", If(_running, $"{EffectiveDelivery:0} NL/min", "0 NL/min")))
        Return list
    End Function

    ''' <summary>Called by the simulator with the lowest and highest pressure of the receivers it charges.</summary>
    Friend Sub UpdatePressureSwitch(lowest As Double, highest As Double)
        If Not _switchedOn Then
            _running = False
        ElseIf highest >= _cutOut Then
            _running = False
        ElseIf lowest <= CutInPressure Then
            _running = True
        End If
    End Sub

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(-10, 0, 80, 50)
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
        Symbols.EnergyTriangle(g, r, {New PointF(30, 16), New PointF(25, 24), New PointF(35, 24)}, False, r.Simulating AndAlso _running)
        g.DrawLine(r.Line, 16, 28, 6, 28)
        g.DrawEllipse(r.Line, -10, 20, 16, 16)
        g.DrawString("M", r.SmallFont, r.TextBrush, -6, 22)
        Dim txt = $"{_cutIn:0.#}–{_cutOut:0.#} bar"
        If r.Simulating Then txt = If(Not _switchedOn, "switched off", If(_running, "running", "stopped (full)"))
        g.DrawString(txt, r.SmallFont, r.TextBrush, 46, 34)
    End Sub

    <Browsable(False)> Public Overrides ReadOnly Property IsManuallyOperated As Boolean
        Get
            Return True
        End Get
    End Property

    Public Overrides Sub OnSimMouseDown(local As PointF)
        _switchedOn = Not _switchedOn
        If Not _switchedOn Then _running = False
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _switchedOn = True
        _running = True
    End Sub

    Public Overrides Sub AddTerminals(sim As Simulator)
        ' On its own the compressor feeds the circuit directly at the cut-out pressure.
        If Not ChargesReceiver AndAlso _switchedOn Then
            _running = True
            sim.AddSupply(Ports(0), If(Fault = FaultKind.LowOutput, _cutOut * 0.5, _cutOut))
        End If
    End Sub
End Class

''' <summary>Air receiver (tank): stores compressed air. Its pressure falls as air is used and rises while a compressor charges it.</summary>
Public Class AirReceiver
    Inherits CircuitElement

    Private _pressure As Double

    Public Sub New()
        AddPort("1", 0, 30, -1, 0)
        AddPort("2", 100, 30, 1, 0)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "AirReceiver"
    Public Overrides ReadOnly Property DisplayName As String = "Air receiver"

    <Category("Receiver"), DisplayName("Volume (litres)")>
    Public Property VolumeLitres As Double
        Get
            Return _volume
        End Get
        Set(value As Double)
            _volume = Math.Max(0.1, Math.Min(100000, value))
        End Set
    End Property
    Private _volume As Double = 20

    <Category("Receiver"), DisplayName("Pressure at start (bar)"), Description("Use 0 to watch the compressor fill an empty receiver.")>
    Public Property InitialPressure As Double
        Get
            Return _initial
        End Get
        Set(value As Double)
            _initial = Math.Max(0, Math.Min(16, value))
        End Set
    End Property
    Private _initial As Double = 6

    ''' <summary>Pressure in the receiver now (bar gauge).</summary>
    <Browsable(False)> Public ReadOnly Property Pressure As Double
        Get
            Return _pressure
        End Get
    End Property

    ''' <summary>Adds (positive) or removes (negative) free air, normal litres.</summary>
    Friend Sub Exchange(normalLitres As Double)
        _pressure = Math.Max(0, Math.Min(40, _pressure + normalLitres * CylinderBase.Atm / _volume))
    End Sub

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.Leak}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.Leak, "Drain valve left open: air keeps escaping from the receiver", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Receiver pressure", $"{_pressure:0.00} bar"))
        list.Add(("Stored free air", $"{(_pressure + CylinderBase.Atm) / CylinderBase.Atm * _volume:0} NL"))
        Return list
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 10, 100, 40)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(20, 12)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 0, 30, 20, 30)
        g.DrawLine(r.PenFor(Ports(1)), 80, 30, 100, 30)
        ' Horizontal tank with rounded ends.
        g.FillEllipse(r.BodyBrush, 20, 18, 16, 24)
        g.FillEllipse(r.BodyBrush, 64, 18, 16, 24)
        g.FillRectangle(r.BodyBrush, 28, 18, 44, 24)
        g.DrawArc(r.Line, 20, 18, 16, 24, 90, 180)
        g.DrawArc(r.Line, 64, 18, 16, 24, 270, 180)
        g.DrawLine(r.Line, 28, 18, 72, 18)
        g.DrawLine(r.Line, 28, 42, 72, 42)
        Dim txt = If(r.Simulating, $"{_pressure:0.0} bar", $"{_volume:0.#} l")
        g.DrawString(txt, r.SmallFont, r.TextBrush, 38, 23)
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _pressure = _initial
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        sim.AddEdge(Ports(0), Ports(1))
    End Sub

    Public Overrides Sub AddTerminals(sim As Simulator)
        If _pressure > 0.05 Then
            sim.AddSupply(Ports(0), _pressure)
            sim.AddSupply(Ports(1), _pressure)
        End If
    End Sub
End Class

''' <summary>Flow meter fitted in a line: shows the flow (free air or oil) and its direction.</summary>
Public Class FlowMeter
    Inherits CircuitElement

    Public Sub New()
        AddPort("1", 0, 30, -1, 0)
        AddPort("2", 80, 30, 1, 0)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "FlowMeter"
    Public Overrides ReadOnly Property DisplayName As String = "Flow meter"

    <Category("Medium"), DisplayName("Hydraulic"), Description("True: fitted in a hydraulic (oil) line, shows l/min. False: compressed air, shows free air in NL/min.")>
    Public Property Hydraulic As Boolean
        Get
            Return Ports(0).Kind = PortKind.Hydraulic
        End Get
        Set(value As Boolean)
            For Each p In Ports
                p.Kind = If(value, PortKind.Hydraulic, PortKind.Pneumatic)
            Next
        End Set
    End Property

    ''' <summary>Measured flow from port 1 to port 2 (negative: from 2 to 1), NL/min or l/min.</summary>
    <Browsable(False)> Public Property Flow As Double

    ''' <summary>Unit of the reading.</summary>
    <Browsable(False)> Public ReadOnly Property Unit As String
        Get
            Return If(Hydraulic, "l/min", "NL/min")
        End Get
    End Property

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Flow", $"{Math.Abs(Flow):0.0} {Unit}" & If(Math.Abs(Flow) < 0.05, "", If(Flow > 0, " from 1 to 2", " from 2 to 1"))))
        Return list
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 10, 80, 40)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(22, 14)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 0, 30, 26, 30)
        g.DrawLine(r.PenFor(Ports(1)), 54, 30, 80, 30)
        g.FillEllipse(r.BodyBrush, 26, 16, 28, 28)
        g.DrawEllipse(r.Line, 26, 16, 28, 28)
        ' ISO flow meter: an arrow across a circle.
        Symbols.Arrow(g, r.Line, New PointF(32, 38), New PointF(48, 22), 5)
        If r.Simulating Then
            g.DrawString($"{Math.Abs(Flow):0.0} {Unit}", r.SmallFont, r.TextBrush, 22, 44)
            If Math.Abs(Flow) >= 0.05 Then
                Dim pen = If(Hydraulic, r.HydraulicPressure, r.Pressure)
                If Flow > 0 Then
                    Symbols.Arrow(g, pen, New PointF(4, 22), New PointF(22, 22), 5)
                Else
                    Symbols.Arrow(g, pen, New PointF(76, 22), New PointF(58, 22), 5)
                End If
            End If
        End If
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        Flow = 0
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        sim.AddEdge(Ports(0), Ports(1))
    End Sub
End Class

''' <summary>Force sensor (load cell) on a cylinder's piston rod: shows the force the cylinder pushes or pulls with.</summary>
Public Class ForceSensor
    Inherits CircuitElement

    Public Overrides ReadOnly Property TypeName As String = "ForceSensor"
    Public Overrides ReadOnly Property DisplayName As String = "Force sensor"

    <Category("Force sensor"), DisplayName("Cylinder"), Description("Label of the cylinder whose rod force is measured, e.g. 1A.")>
    Public Property Cylinder As String = "1A"

    ''' <summary>Measured force (N), positive = pushing (extending).</summary>
    <Browsable(False)> Public Property Force As Double

    ''' <summary>The cylinder this sensor is mounted on, or Nothing.</summary>
    Public Function FindCylinder(elements As IEnumerable(Of CircuitElement)) As CylinderBase
        Return elements.OfType(Of CylinderBase)().FirstOrDefault(Function(c) String.Equals(c.Label?.Trim(), Cylinder?.Trim(), StringComparison.OrdinalIgnoreCase))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Force", $"{Force:0} N ({If(Force >= 0, "pushing", "pulling")})"))
        Return list
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 70, 40)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(0, 0)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.FillRectangle(r.BodyBrush, 0, 6, 70, 30)
        g.DrawRectangle(r.Line, 0, 6, 70, 30)
        ' Load cell: two arrows pressing on a bar.
        g.DrawLine(r.Line, 8, 21, 22, 21)
        Symbols.Arrow(g, r.Thin, New PointF(15, 10), New PointF(15, 19), 4)
        Symbols.Arrow(g, r.Thin, New PointF(15, 32), New PointF(15, 23), 4)
        g.DrawString(If(r.Simulating, $"{Force:0} N", $"F {Cylinder}"), r.Font, r.TextBrush, 26, 13)
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        Force = 0
    End Sub
End Class
