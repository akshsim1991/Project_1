Imports System.ComponentModel

''' <summary>Common behaviour and drawing of cylinders, including the realistic physics model.</summary>
Public MustInherit Class CylinderBase
    Inherits CircuitElement

    ' Body geometry (local coordinates). Ports sit 10 px below the body.
    Protected Const BodyLeft As Single = 0, BodyRight As Single = 120
    Protected Const BodyTop As Single = 0, BodyBottom As Single = 30
    Protected Const PistonWidth As Single = 8
    Protected Const PistonTravel As Single = 92
    Protected Const RodLength As Single = 112

    ''' <summary>Atmospheric pressure, bar absolute.</summary>
    Public Const Atm As Double = 1.013
    ''' <summary>Sonic conductance of a typical valve and tubing, normal litres / (s·bar).</summary>
    Public Const ValveConductance As Double = 0.35
    Private Const CriticalRatio As Double = 0.3
    Private Const PhysicsDt As Double = 0.0002

    Private _strokeTime As Double = 1.0
    Private _position As Double

    ' Realistic-mode state.
    Private _physicsReady As Boolean
    Private _velocity As Double          ' m/s
    Private _airCap As Double            ' normal litres in the cap-side chamber
    Private _airRod As Double            ' normal litres in the rod-side chamber
    Private _lastDirection As Integer

    <Category("Cylinder"), DisplayName("Stroke time (s)"),
     Description("Ideal mode: time for a full stroke when air can flow freely. Flow control valves make it slower. (Realistic mode calculates the speed from bore, load and flow.)")>
    Public Property StrokeTime As Double
        Get
            Return _strokeTime
        End Get
        Set(value As Double)
            _strokeTime = Math.Max(0.05, Math.Min(60, value))
        End Set
    End Property

    <Category("Cylinder"), DisplayName("Stroke (mm)"), Description("Stroke length (5 to 5000 mm).")>
    Public Property StrokeLength As Integer
        Get
            Return _strokeLength
        End Get
        Set(value As Integer)
            _strokeLength = Math.Max(5, Math.Min(5000, value))
        End Set
    End Property
    Private _strokeLength As Integer = 100

    <Category("Physical data (realistic mode)"), DisplayName("Bore (mm)"), Description("Piston diameter (4 to 500 mm).")>
    Public Property BoreMm As Double
        Get
            Return _boreMm
        End Get
        Set(value As Double)
            _boreMm = Math.Max(4, Math.Min(500, value))
        End Set
    End Property
    Private _boreMm As Double = 32

    <Category("Physical data (realistic mode)"), DisplayName("Rod diameter (mm)"), Description("Piston rod diameter; at most 90 % of the bore is used.")>
    Public Property RodMm As Double
        Get
            Return _rodMm
        End Get
        Set(value As Double)
            _rodMm = Math.Max(1, Math.Min(400, value))
        End Set
    End Property
    Private _rodMm As Double = 12

    <Category("Physical data (realistic mode)"), DisplayName("Moving load (kg)"), Description("Mass moved by the piston rod.")>
    Public Property LoadMassKg As Double
        Get
            Return _loadMassKg
        End Get
        Set(value As Double)
            _loadMassKg = Math.Max(0, Math.Min(100000, value))
        End Set
    End Property
    Private _loadMassKg As Double = 2

    <Category("Physical data (realistic mode)"), DisplayName("Load force (N)"),
     Description("External force against extension (e.g. a pressing force or a weight on a vertical cylinder). Negative values help extension.")>
    Public Property LoadForceN As Double
        Get
            Return _loadForceN
        End Get
        Set(value As Double)
            _loadForceN = Math.Max(-1000000, Math.Min(1000000, value))
        End Set
    End Property
    Private _loadForceN As Double

    <Category("Physical data (realistic mode)"), DisplayName("Friction (N)")>
    Public Property FrictionN As Double
        Get
            Return _frictionN
        End Get
        Set(value As Double)
            _frictionN = Math.Max(0, Math.Min(100000, value))
        End Set
    End Property
    Private _frictionN As Double = 25

    <Category("Physical data (realistic mode)"), DisplayName("End-position cushioning"),
     Description("Slows the piston down over the last few millimetres of the stroke.")>
    Public Property Cushioning As Boolean = True

    <Category("Position marks"), DisplayName("Retracted mark"),
     Description("Name of the limit valve or sensor operated when the piston rod is fully retracted, e.g. 1S1.")>
    Public Property RetractedMark As String = ""

    <Category("Position marks"), DisplayName("Extended mark"),
     Description("Name of the limit valve or sensor operated when the piston rod is fully extended, e.g. 1S2.")>
    Public Property ExtendedMark As String = ""

    ''' <summary>Piston position, 0 = retracted, 1 = extended.</summary>
    <Browsable(False)> Public ReadOnly Property Position As Double
        Get
            Return _position
        End Get
    End Property

    ''' <summary>Piston speed in m/s (positive = extending).</summary>
    <Browsable(False)> Public ReadOnly Property Velocity As Double
        Get
            Return _velocity
        End Get
    End Property

    ''' <summary>Cap-side chamber pressure, bar gauge (realistic mode).</summary>
    <Browsable(False)> Public ReadOnly Property CapPressure As Double
        Get
            Return If(_physicsReady, Math.Max(0, ChamberAbs(_airCap, CapVolume()) - Atm), Ports(0).Pressure)
        End Get
    End Property

    ''' <summary>Rod-side chamber pressure, bar gauge (realistic mode).</summary>
    <Browsable(False)> Public ReadOnly Property RodPressure As Double
        Get
            If Not HasRodPort Then Return 0
            Return If(_physicsReady, Math.Max(0, ChamberAbs(_airRod, RodVolume()) - Atm), Ports(1).Pressure)
        End Get
    End Property

    ''' <summary>Number of completed strokes (extend + retract) since the simulation started.</summary>
    <Browsable(False)> Public Property CompletedCycles As Integer

    Protected MustOverride ReadOnly Property HasRodPort As Boolean
    Protected Overridable ReadOnly Property SpringForce(x As Double) As Double
        Get
            Return 0
        End Get
    End Property

    Public Overrides Function ShowProperty(name As String) As Boolean
        Dim hydraulic = Ports(0).Kind = PortKind.Hydraulic
        If hydraulic Then
            ' Hydraulic speed comes from the pump flow; mass and cushioning are not modelled.
            If name = NameOf(StrokeTime) OrElse name = NameOf(LoadMassKg) OrElse name = NameOf(Cushioning) Then Return False
        End If
        If TypeOf Me Is SemiRotaryActuator Then
            ' A swivel drive has an angle, not a stroke and a bore.
            Select Case name
                Case NameOf(StrokeLength), NameOf(BoreMm), NameOf(RodMm), NameOf(LoadMassKg), NameOf(LoadForceN), NameOf(FrictionN), NameOf(Cushioning)
                    Return False
            End Select
        End If
        Return True
    End Function

    Public Function IsAtMark(mark As String) As Boolean
        If String.Equals(mark, RetractedMark, StringComparison.OrdinalIgnoreCase) AndAlso _position <= 0.005 Then Return True
        If String.Equals(mark, ExtendedMark, StringComparison.OrdinalIgnoreCase) AndAlso _position >= 0.995 Then Return True
        Return False
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return RectangleF.FromLTRB(BodyLeft, BodyTop, PistonTravel + PistonWidth + 4 + RodLength + 4, BodyBottom + 10)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(BodyLeft, BodyTop - 2)
        End Get
    End Property

    Protected ReadOnly Property PistonX As Single
        Get
            Return CSng(4 + _position * (PistonTravel - 4))
        End Get
    End Property

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _position = 0
        _velocity = 0
        _physicsReady = False
        _lastDirection = 0
        CompletedCycles = 0
    End Sub

    ''' <summary>Moves the piston at <paramref name="speed"/> m/s (used by the hydraulic model).</summary>
    Protected Sub MoveAtSpeed(speed As Double, dt As Double)
        Dim length = Math.Max(0.001, StrokeLength / 1000.0)
        Dim before = _position
        _position = Math.Max(0, Math.Min(1, _position + speed * dt / length))
        _velocity = (_position - before) * length / Math.Max(dt, 0.000001)
        TrackCycles()
    End Sub

    ''' <summary>Ideal-mode movement at a speed set by the stroke time and the flow factor.</summary>
    Protected Sub Move(direction As Integer, flowFactor As Double, dt As Double, Optional sim As Simulator = Nothing)
        Dim before = _position
        _position += direction * flowFactor * dt / _strokeTime
        _position = Math.Max(0, Math.Min(1, _position))
        _velocity = (_position - before) * StrokeLength / 1000 / Math.Max(dt, 0.000001)
        If sim IsNot Nothing Then
            ' Free air needed to fill the swept volume at supply pressure.
            Dim swept = Math.Abs(_position - before) * StrokeLength / 1000
            Dim area = If(direction > 0, CapArea(), RodArea())
            Dim p = If(direction > 0, Ports(0).Pressure, If(HasRodPort, Ports(1).Pressure, 0))
            If direction > 0 OrElse HasRodPort Then sim.AddAirConsumption(area * swept * 1000 * (p + Atm) / Atm)
        End If
        TrackCycles()
    End Sub

    Private Sub TrackCycles()
        If _position >= 0.995 Then _lastDirection = 1
        If _position <= 0.005 AndAlso _lastDirection = 1 Then
            _lastDirection = 0
            CompletedCycles += 1
        End If
    End Sub

    ' ---------------------------------------------------------------- realistic physics

    Protected Function CapArea() As Double
        Return Math.PI / 4 * (BoreMm / 1000) ^ 2
    End Function

    Protected Function RodArea() As Double
        Return Math.PI / 4 * ((BoreMm / 1000) ^ 2 - (Math.Min(RodMm, BoreMm * 0.9) / 1000) ^ 2)
    End Function

    Private Function Travel() As Double
        Return _position * StrokeLength / 1000
    End Function

    ''' <summary>Chamber volumes in litres, including dead volume of ports and tubing.</summary>
    Private Function CapVolume() As Double
        Return (CapArea() * (0.006 + Travel()) + 0.000015) * 1000
    End Function

    Private Function RodVolume() As Double
        Return (RodArea() * (0.006 + StrokeLength / 1000 - Travel()) + 0.000015) * 1000
    End Function

    Private Shared Function ChamberAbs(air As Double, volume As Double) As Double
        Return air * Atm / volume
    End Function

    ''' <summary>ISO 6358 flow (normal litres per second) through conductance <paramref name="c"/>.</summary>
    Private Shared Function Flow(c As Double, upAbs As Double, downAbs As Double) As Double
        If upAbs <= downAbs OrElse c <= 0 Then Return 0
        Dim ratio = downAbs / upAbs
        Dim psi = If(ratio <= CriticalRatio, 1.0, Math.Sqrt(Math.Max(0, 1 - ((ratio - CriticalRatio) / (1 - CriticalRatio)) ^ 2)))
        Return c * upAbs * psi
    End Function

    ''' <summary>Net flow into a chamber through its port; free air drawn from the supply is reported.</summary>
    Private Function ChamberFlow(port As Port, chamberAbs As Double, outletCushion As Boolean, sim As Simulator) As Double
        Dim c = ValveConductance * Math.Max(port.Factor, 0)
        Select Case port.State
            Case PortState.Pressurized
                Dim q = Flow(c, port.SupplyPressure + Atm, chamberAbs)
                sim.AddAirConsumption(q * PhysicsDt)
                Return q
            Case PortState.Exhausted
                If outletCushion Then c *= 0.08
                Return -Flow(c, chamberAbs, Atm)
            Case Else
                Return 0
        End Select
    End Function

    ''' <summary>Fills the chambers with the pressures the circuit has at rest (called at reset).</summary>
    Public Sub InitPhysics()
        ' Start from the line pressures the circuit has at the moment.
        _airCap = (Ports(0).SupplyPressure * If(Ports(0).State = PortState.Pressurized, 1, 0) + Atm) * CapVolume() / Atm
        _airRod = If(HasRodPort, (Ports(1).SupplyPressure * If(Ports(1).State = PortState.Pressurized, 1, 0) + Atm), Atm) * RodVolume() / Atm
        _physicsReady = True
    End Sub

    Protected Sub PhysicsStep(sim As Simulator, dt As Double)
        If Not _physicsReady Then InitPhysics()
        Dim length = Math.Max(0.005, StrokeLength / 1000.0)
        Dim cushion = If(Cushioning, Math.Min(0.015, length * 0.2), 0)
        Dim mass = 0.3 + Math.Max(0, LoadMassKg)
        Dim steps = Math.Max(1, CInt(Math.Ceiling(dt / PhysicsDt)))
        Dim h = dt / steps
        For i = 1 To steps
            Dim x_ = Travel()
            Dim pCap = ChamberAbs(_airCap, CapVolume())
            Dim pRod = If(HasRodPort, ChamberAbs(_airRod, RodVolume()), Atm)
            ' Air flows.
            Dim nearEnd = x_ > length - cushion AndAlso _velocity > 0
            Dim nearStart = x_ < cushion AndAlso _velocity < 0
            _airCap += ChamberFlow(Ports(0), pCap, nearStart, sim) * h
            If HasRodPort Then _airRod += ChamberFlow(Ports(1), pRod, nearEnd, sim) * h
            _airCap = Math.Max(_airCap, 0.0001)
            _airRod = Math.Max(_airRod, 0.0001)
            ' Forces (N): pressures in bar gauge * 1e5 Pa * area.
            Dim force = (pCap - Atm) * 100000.0 * CapArea() - If(HasRodPort, (pRod - Atm) * 100000.0 * RodArea(), 0) -
                        LoadForceN - SpringForce(x_) - 60 * _velocity
            Dim friction = Math.Max(0, FrictionN)
            Dim accel As Double
            If Math.Abs(_velocity) < 0.0005 Then
                accel = If(Math.Abs(force) <= friction, 0, (force - Math.Sign(force) * friction) / mass)
                If accel = 0 Then _velocity = 0
            Else
                accel = (force - Math.Sign(_velocity) * friction) / mass
            End If
            _velocity += accel * h
            Dim nx = x_ + _velocity * h
            If nx <= 0 Then nx = 0 : If _velocity < 0 Then _velocity = 0
            If nx >= length Then nx = length : If _velocity > 0 Then _velocity = 0
            _position = nx / length
            If Double.IsNaN(_position) OrElse Double.IsNaN(_velocity) Then
                ' Should never happen with validated inputs; keep the model usable instead of failing.
                _position = 0 : _velocity = 0
                InitPhysics()
                Exit For
            End If
        Next
        TrackCycles()
    End Sub

    ''' <summary>Gauge pressure of the chamber behind the given port (realistic mode), or -1.</summary>
    Public Function ChamberPressureAt(p As Port) As Double
        If Not _physicsReady Then Return -1
        If p Is Ports(0) Then Return CapPressure
        If HasRodPort AndAlso p Is Ports(1) Then Return RodPressure
        Return -1
    End Function

    ' ---------------------------------------------------------------- drawing

    Protected Sub DrawBody(g As DrawSurface, r As RenderContext)
        Dim cy = (BodyTop + BodyBottom) / 2
        g.FillRectangle(r.BodyBrush, BodyLeft, BodyTop, BodyRight - BodyLeft, BodyBottom - BodyTop)
        g.DrawRectangle(r.Line, BodyLeft, BodyTop, BodyRight - BodyLeft, BodyBottom - BodyTop)

        Dim px = PistonX
        Using b As New SolidBrush(Color.FromArgb(70, 70, 70))
            g.FillRectangle(b, px, BodyTop + 1.5F, PistonWidth, BodyBottom - BodyTop - 3)
        End Using
        Dim rodStart = px + PistonWidth
        Dim tip = rodStart + RodLength
        g.FillRectangle(r.BodyBrush, rodStart, cy - 3, tip - rodStart, 6)
        g.DrawRectangle(r.Line, rodStart, cy - 3, tip - rodStart, 6)
        ' Redraw the cylinder end cap over the rod.
        g.DrawLine(r.Line, BodyRight, BodyTop, BodyRight, BodyBottom)

        DrawMark(g, r, 4 + PistonWidth + RodLength, RetractedMark)
        DrawMark(g, r, PistonTravel + PistonWidth + RodLength, ExtendedMark)
        If r.Simulating AndAlso _physicsReady Then
            g.DrawString($"{CapPressure:0.0}", r.SmallFont, r.TextBrush, 2, BodyBottom - 12)
            If HasRodPort Then g.DrawString($"{RodPressure:0.0}", r.SmallFont, r.TextBrush, BodyRight - 20, BodyBottom - 12)
        End If
    End Sub

    Private Sub DrawMark(g As DrawSurface, r As RenderContext, x_ As Single, mark As String)
        If String.IsNullOrWhiteSpace(mark) Then Return
        Using p As New Pen(Color.FromArgb(160, 40, 40), 1.4F)
            g.DrawLine(p, x_, BodyTop + 4, x_, BodyTop - 6)
        End Using
        Dim sz = g.MeasureString(mark, r.SmallFont)
        g.DrawString(mark, r.SmallFont, r.MarkBrush, x_ - sz.Width / 2, BodyTop - 6 - sz.Height)
    End Sub

    Protected Sub DrawPortStub(g As DrawSurface, r As RenderContext, p As Port)
        g.DrawLine(r.PenFor(p), p.Local.X, BodyBottom, p.Local.X, p.Local.Y)
    End Sub
End Class

''' <summary>Single-acting cylinder with spring return.</summary>
Public Class SingleActingCylinder
    Inherits CylinderBase

    Public Sub New()
        AddPort("1", 10, BodyBottom + 10, 0, 1, vents:=True)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "SingleActingCylinder"
    Public Overrides ReadOnly Property DisplayName As String = "Single-acting cylinder"

    <Category("Physical data (realistic mode)"), DisplayName("Spring force (N)"), Description("Return spring force when retracted; it rises by 50 % at full stroke.")>
    Public Property SpringPreloadN As Double
        Get
            Return _springPreloadN
        End Get
        Set(value As Double)
            _springPreloadN = Math.Max(0, Math.Min(100000, value))
        End Set
    End Property
    Private _springPreloadN As Double = 40

    Protected Overrides ReadOnly Property HasRodPort As Boolean = False

    Protected Overrides ReadOnly Property SpringForce(x As Double) As Double
        Get
            Return SpringPreloadN * (1 + 0.5 * x / Math.Max(0.001, StrokeLength / 1000.0))
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        DrawBody(g, r)
        Dim px = PistonX + PistonWidth
        Symbols.Spring(g, r.Thin, px + 2, BodyRight - 2, (BodyTop + BodyBottom) / 2, 10)
        DrawPortStub(g, r, Ports(0))
    End Sub

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        If sim.RealPhysics Then PhysicsStep(sim, dt) : Return
        Dim p = Ports(0)
        Select Case p.State
            Case PortState.Pressurized
                If p.Pressure > 1 Then Move(+1, p.Factor, dt, sim)
            Case PortState.Exhausted
                Move(-1, p.Factor, dt)
        End Select
    End Sub
End Class

''' <summary>Double-acting cylinder.</summary>
Public Class DoubleActingCylinder
    Inherits CylinderBase

    ''' <summary>Annular (rod side) area relative to the full piston area, ideal mode.</summary>
    Private Const RodSideArea As Double = 0.7

    Public Sub New()
        AddPort("1", 10, BodyBottom + 10, 0, 1, vents:=True)
        AddPort("2", 110, BodyBottom + 10, 0, 1, vents:=True)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "DoubleActingCylinder"
    Public Overrides ReadOnly Property DisplayName As String = "Double-acting cylinder"

    Protected Overrides ReadOnly Property HasRodPort As Boolean = True

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        DrawBody(g, r)
        DrawPortStub(g, r, Ports(0))
        DrawPortStub(g, r, Ports(1))
    End Sub

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        If sim.RealPhysics Then PhysicsStep(sim, dt) : Return
        Dim capSide = Ports(0), rodSide = Ports(1)
        Dim force = capSide.Pressure - rodSide.Pressure * RodSideArea
        If force > 0.2 Then
            Move(+1, Speed(capSide, rodSide), dt, sim)
        ElseIf force < -0.2 Then
            Move(-1, Speed(rodSide, capSide), dt, sim)
        End If
    End Sub

    ''' <summary>Speed factor when <paramref name="drive"/> pushes and <paramref name="outlet"/> must let air out.</summary>
    Private Shared Function Speed(drive As Port, outlet As Port) As Double
        Dim inflow = If(drive.State = PortState.Pressurized, drive.Factor, 0.0)
        Select Case outlet.State
            Case PortState.Exhausted
                Return Math.Min(inflow, outlet.Factor)
            Case PortState.Pressurized
                ' Both chambers pressurized: the larger piston area wins, slowly.
                Return inflow * 0.3
            Case Else
                ' Outlet air is trapped: the piston cannot move.
                Return 0
        End Select
    End Function
End Class
