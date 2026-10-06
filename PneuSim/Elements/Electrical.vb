Imports System.ComponentModel

<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum Polarity
    <Description("+24 V")> Plus24V
    <Description("0 V")> Zero0V
End Enum

''' <summary>Connection to the +24 V or 0 V rail of the control voltage supply.</summary>
Public Class PowerTerminal
    Inherits CircuitElement

    Private _polarity As Polarity = Polarity.Plus24V

    Public Sub New()
        AddPort("1", 20, 30, 0, 1, kind:=PortKind.Electric)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "PowerTerminal"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return If(_polarity = Polarity.Plus24V, "Electrical connection +24 V", "Electrical connection 0 V")
        End Get
    End Property

    <Category("Supply"), DisplayName("Polarity")>
    Public Property Polarity As Polarity
        Get
            Return _polarity
        End Get
        Set(value As Polarity)
            _polarity = value
            Dim p = Ports(0)
            If value = Polarity.Plus24V Then
                p.Local = New PointF(20, 30) : p.Direction = New PointF(0, 1)
            Else
                p.Local = New PointF(20, 0) : p.Direction = New PointF(0, -1)
            End If
        End Set
    End Property

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(8, 0, 34, 30)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(44, 0)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim plus = _polarity = Polarity.Plus24V
        Dim cy = If(plus, 10.0F, 20.0F)
        g.DrawLine(r.PenFor(Ports(0)), 20, If(plus, cy + 5, 0), 20, If(plus, 30, cy - 5))
        Using b As New SolidBrush(If(plus, Color.FromArgb(215, 30, 30), Color.FromArgb(30, 60, 200)))
            g.FillEllipse(b, 15, cy - 5, 10, 10)
        End Using
        g.DrawString(If(plus, "+24V", "0V"), r.Font, r.TextBrush, 26, cy - 7)
    End Sub

    Public Overrides Sub AddTerminals(sim As Simulator)
        If _polarity = Polarity.Plus24V Then sim.AddSupply(Ports(0), Simulator.ControlVoltage) Else sim.AddExhaust(Ports(0))
    End Sub
End Class

''' <summary>What operates an electrical contact.</summary>
<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum ContactOperator
    <Description("Push button")> PushButton
    <Description("Selector switch (detented)")> Selector
    <Description("Relay / timer relay")> Relay
    <Description("Proximity sensor")> ProximitySensor
    <Description("Limit switch (roller)")> LimitSwitch
    <Description("Pressure switch")> PressureSwitch
    <Description("Emergency stop (latching)")> EmergencyStop
End Enum

''' <summary>A switching contact (normally open or normally closed), drawn vertically as in a ladder diagram.</summary>
Public Class ElectricContact
    Inherits CircuitElement

    Private _closed As Boolean
    Private _manualPressed As Boolean
    Private _detentOn As Boolean

    ''' <summary>True while the contact conducts.</summary>
    <Browsable(False)> Public ReadOnly Property IsClosed As Boolean
        Get
            Return _closed
        End Get
    End Property

    Public Sub New()
        AddPort("1", 20, 0, 0, -1, kind:=PortKind.Electric)
        AddPort("2", 20, 60, 0, 1, kind:=PortKind.Electric)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "ElectricContact"

    Public Overrides Function ShowProperty(name As String) As Boolean
        If name = NameOf(Reference) Then Return [Operator] = ContactOperator.Relay OrElse [Operator] = ContactOperator.ProximitySensor OrElse
                                                [Operator] = ContactOperator.LimitSwitch OrElse [Operator] = ContactOperator.PressureSwitch
        Return True
    End Function

    ''' <summary>True for contacts switched by a cylinder position or a pressure (sensors).</summary>
    <Browsable(False)> Public ReadOnly Property IsSensor As Boolean
        Get
            Return [Operator] = ContactOperator.ProximitySensor OrElse [Operator] = ContactOperator.LimitSwitch OrElse [Operator] = ContactOperator.PressureSwitch
        End Get
    End Property

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.ContactOpen, FaultKind.ContactWelded}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Dim welded = kind = FaultKind.ContactWelded
        Select Case [Operator]
            Case ContactOperator.ProximitySensor, ContactOperator.LimitSwitch
                Return If(welded, "Sensor always gives a signal (faulty or set too close)", "Sensor never gives a signal (misaligned, loose or broken)")
            Case ContactOperator.PressureSwitch
                Return If(welded, "Pressure switch contact stuck closed", "Pressure switch contact never closes")
            Case ContactOperator.Relay
                Return If(welded, "Relay contact welded closed", "Relay contact burnt (never closes)")
            Case Else
                Return If(welded, "Switch contact welded closed", "Switch contact dirty or broken (never closes)")
        End Select
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Contact", If(_closed, "closed (conducts)", "open")))
        Return list
    End Function

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Dim op As String
            Select Case [Operator]
                Case ContactOperator.PushButton : op = "Push button"
                Case ContactOperator.Selector : op = "Selector switch"
                Case ContactOperator.Relay : op = "Relay contact"
                Case ContactOperator.ProximitySensor : op = "Proximity sensor"
                Case ContactOperator.PressureSwitch : op = "Pressure switch contact"
                Case ContactOperator.EmergencyStop : op = "Emergency stop"
                Case Else : op = "Limit switch"
            End Select
            Return $"{op} ({If(NormallyClosed, "NC", "NO")})"
        End Get
    End Property

    <Category("Contact"), DisplayName("Normally closed"), Description("False: normally open (NO, make contact). True: normally closed (NC, break contact).")>
    Public Property NormallyClosed As Boolean

    <Category("Contact"), DisplayName("Operated by")>
    Public Property [Operator] As ContactOperator = ContactOperator.PushButton

    <Category("Contact"), DisplayName("Reference"),
     Description("Relay contacts: the relay label (e.g. K1). Sensors and limit switches: the cylinder position mark (e.g. 1B2).")>
    Public Property Reference As String = ""

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 40, 60)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(26, 22)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim closed = If(r.Simulating, _closed, NormallyClosed)
        Dim pTop = Ports(0), pBottom = Ports(1)
        Dim penTop = r.PenFor(pTop), penBottom = r.PenFor(pBottom)
        g.DrawLine(penTop, 20, 0, 20, 18)
        g.DrawLine(penBottom, 20, 42, 20, 60)
        Dim blade = If(closed AndAlso r.Simulating AndAlso pBottom.IsPressurized, r.Energized, r.Line)
        Dim tip As PointF
        If NormallyClosed Then
            g.DrawLine(penTop, 20, 18, 12, 18)
            tip = If(closed, New PointF(10, 16), New PointF(6, 26))
        Else
            tip = If(closed, New PointF(20, 18), New PointF(9, 20))
        End If
        g.DrawLine(blade, New PointF(20, 42), tip)

        ' Mechanical link to the operator.
        Dim mid = Symbols.Lerp(New PointF(20, 42), tip, 0.5F)
        Select Case [Operator]
            Case ContactOperator.PushButton, ContactOperator.Selector, ContactOperator.LimitSwitch, ContactOperator.ProximitySensor,
                 ContactOperator.PressureSwitch, ContactOperator.EmergencyStop
                Using dash As New Pen(Color.Black, 1) With {.DashStyle = Drawing2D.DashStyle.Dash}
                    g.DrawLine(dash, mid.X, mid.Y, 4, mid.Y)
                End Using
        End Select
        Dim y = mid.Y
        Select Case [Operator]
            Case ContactOperator.PushButton
                g.DrawLine(r.Line, 3, y - 6, 3, y + 6)
                g.DrawLine(r.Line, 3, y - 6, 6, y - 6) : g.DrawLine(r.Line, 3, y + 6, 6, y + 6)
            Case ContactOperator.Selector
                g.DrawLine(r.Line, 3, y - 6, 3, y + 6)
                g.DrawLine(r.Line, 3, y - 6, 6, y - 6) : g.DrawLine(r.Line, 3, y + 6, 0, y + 6)
            Case ContactOperator.LimitSwitch
                g.DrawEllipse(r.Line, -2, y - 4, 8, 8)
            Case ContactOperator.ProximitySensor
                g.DrawPolygon(r.Line, {New PointF(3, y - 7), New PointF(9, y), New PointF(3, y + 7), New PointF(-3, y)})
            Case ContactOperator.PressureSwitch
                ' Pressure operated: a pilot line with an energy triangle.
                g.DrawRectangle(r.Line, -6, y - 6, 10, 12)
                Symbols.EnergyTriangle(g, r, {New PointF(4, y), New PointF(-2, y - 4), New PointF(-2, y + 4)}, False, r.Simulating AndAlso _closed <> NormallyClosed)
            Case ContactOperator.EmergencyStop
                ' Mushroom head: latches when pressed, released by turning it.
                Dim head As New List(Of PointF)
                For a = 90 To 270 Step 15
                    head.Add(New PointF(CSng(6 * Math.Cos(a * Math.PI / 180)), CSng(y - 8 * Math.Sin(a * Math.PI / 180))))
                Next
                Using b As New SolidBrush(Color.FromArgb(215, 30, 30))
                    g.FillPolygon(b, head.ToArray())
                End Using
                g.DrawArc(r.Line, -6, y - 8, 12, 16, 90, 180)
                g.DrawLine(r.Line, 0, y - 8, 0, y + 8)
                If r.Simulating AndAlso _detentOn Then g.DrawString("pressed", r.SmallFont, r.MarkBrush, -8, y + 9)
        End Select
        If String.IsNullOrEmpty(Label) AndAlso Not String.IsNullOrWhiteSpace(Reference) Then
            Using f As New Font("Segoe UI", 8.5F, FontStyle.Bold)
                g.DrawString(Reference, f, Brushes.Black, 26, 7)
            End Using
        ElseIf Not String.IsNullOrWhiteSpace(Reference) Then
            g.DrawString(Reference, r.SmallFont, r.MarkBrush, 26, 24)
        End If
        If Not String.IsNullOrEmpty(CrossReference) Then g.DrawString("(" & CrossReference & ")", r.SmallFont, r.MarkBrush, 26, If(String.IsNullOrEmpty(Label), 21, 34))
    End Sub

    <Browsable(False)> Public Overrides ReadOnly Property IsManuallyOperated As Boolean
        Get
            Return [Operator] = ContactOperator.PushButton OrElse [Operator] = ContactOperator.Selector OrElse [Operator] = ContactOperator.EmergencyStop
        End Get
    End Property

    Public Overrides Sub OnSimMouseDown(local As PointF)
        If [Operator] = ContactOperator.PushButton Then _manualPressed = True
        If [Operator] = ContactOperator.Selector OrElse [Operator] = ContactOperator.EmergencyStop Then _detentOn = Not _detentOn
    End Sub

    Public Overrides Sub OnSimMouseUp()
        _manualPressed = False
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _manualPressed = False
        _detentOn = False
        _closed = NormallyClosed
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim actuated As Boolean
        Select Case [Operator]
            Case ContactOperator.PushButton : actuated = _manualPressed
            Case ContactOperator.Selector, ContactOperator.EmergencyStop : actuated = _detentOn
            Case ContactOperator.Relay : actuated = sim.IsRelayActive(Reference)
            Case Else : actuated = sim.IsMarkActive(Reference)
        End Select
        Dim closed = actuated Xor NormallyClosed
        If Fault = FaultKind.ContactOpen Then closed = False
        If Fault = FaultKind.ContactWelded Then closed = True
        If closed = _closed Then Return False
        _closed = closed
        Return True
    End Function

    Public Overrides Sub AddEdges(sim As Simulator)
        If _closed Then sim.AddEdge(Ports(0), Ports(1))
    End Sub
End Class

<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum CoilKind
    <Description("Relay")> Relay
    <Description("Timer relay, on-delay")> OnDelayTimer
    <Description("Timer relay, off-delay")> OffDelayTimer
    <Description("Valve solenoid")> Solenoid
    <Description("Indicator lamp")> Lamp
End Enum

''' <summary>Relay coil, timer relay, valve solenoid or indicator lamp between terminals A1 and A2.</summary>
Public Class ElectricCoil
    Inherits CircuitElement

    Private _kind As CoilKind = CoilKind.Relay
    Private _delay As Double = 2
    Private _energized As Boolean
    Private _active As Boolean
    Private _onTime As Double
    Private _offTime As Double = Double.MaxValue

    Public Sub New()
        AddPort("A1", 20, 0, 0, -1, kind:=PortKind.Electric)
        AddPort("A2", 20, 60, 0, 1, kind:=PortKind.Electric)
        Label = "K1"
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "ElectricCoil"

    Public Overrides Function ShowProperty(name As String) As Boolean
        If name = NameOf(DelaySeconds) Then Return _kind = CoilKind.OnDelayTimer OrElse _kind = CoilKind.OffDelayTimer
        Return True
    End Function

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.BurntCoil}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        If kind <> FaultKind.BurntCoil Then Return MyBase.FaultDescription(kind)
        Select Case _kind
            Case CoilKind.Lamp : Return "Lamp blown"
            Case CoilKind.Solenoid : Return "Solenoid coil burnt out (open circuit)"
            Case Else : Return "Relay coil burnt out (open circuit)"
        End Select
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Voltage across coil", If(_energized, "24 V", If(Ports(0).State = PortState.Pressurized OrElse Ports(1).State = PortState.Pressurized, "24 V on one side only", "0 V"))))
        list.Add((If(_kind = CoilKind.Lamp, "Lamp", "Coil"), If(_active, If(_kind = CoilKind.Lamp, "lit", "active"), "off")))
        If _kind = CoilKind.OnDelayTimer AndAlso _energized AndAlso Not _active Then list.Add(("Timer", $"{_onTime:0.00} of {_delay:0.0#} s"))
        Return list
    End Function

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Select Case _kind
                Case CoilKind.OnDelayTimer : Return "Timer relay (on-delay)"
                Case CoilKind.OffDelayTimer : Return "Timer relay (off-delay)"
                Case CoilKind.Solenoid : Return "Valve solenoid"
                Case CoilKind.Lamp : Return "Indicator lamp"
                Case Else : Return "Relay coil"
            End Select
        End Get
    End Property

    <Category("Coil"), DisplayName("Type"),
     Description("Relays and timers operate contacts with the same label. Solenoids operate valves whose solenoid label matches.")>
    Public Property Kind As CoilKind
        Get
            Return _kind
        End Get
        Set(value As CoilKind)
            _kind = value
        End Set
    End Property

    <Category("Coil"), DisplayName("Delay (s)"), Description("Timer relays: switching delay.")>
    Public Property DelaySeconds As Double
        Get
            Return _delay
        End Get
        Set(value As Double)
            _delay = Math.Max(0, Math.Min(600, value))
        End Set
    End Property

    ''' <summary>True when the coil operates its contacts / valve, or the lamp is lit.</summary>
    <Browsable(False)> Public ReadOnly Property Active As Boolean
        Get
            Return _active
        End Get
    End Property

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(4, 0, 32, 60)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(36, 30)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 20, 0, 20, 18)
        g.DrawLine(r.PenFor(Ports(1)), 20, 42, 20, 60)
        If Not String.IsNullOrEmpty(CrossReference) Then
            ' Contact / valve cross-reference, as on a real ladder diagram.
            g.DrawString(CrossReference.Replace("  ", vbLf), r.SmallFont, r.MarkBrush, 36, 44)
        End If
        Dim on_ = r.Simulating AndAlso _active
        Dim outline = If(r.Simulating AndAlso _energized, r.Energized, r.Line)
        If _kind = CoilKind.Lamp Then
            Using b As New SolidBrush(If(on_, Color.FromArgb(255, 220, 40), Color.White))
                g.FillEllipse(b, 9, 19, 22, 22)
            End Using
            g.DrawEllipse(outline, 9, 19, 22, 22)
            g.DrawLine(r.Line, 12, 22, 28, 38) : g.DrawLine(r.Line, 28, 22, 12, 38)
            g.DrawLine(r.PenFor(Ports(0)), 20, 18, 20, 19)
            Return
        End If
        Using b As New SolidBrush(If(on_, Color.FromArgb(255, 210, 210), Color.White))
            g.FillRectangle(b, 8, 18, 24, 24)
        End Using
        g.DrawRectangle(outline, 8, 18, 24, 24)
        Select Case _kind
            Case CoilKind.Solenoid
                g.DrawLine(r.Line, 8, 42, 32, 18)
            Case CoilKind.OnDelayTimer, CoilKind.OffDelayTimer
                ' Timer marker box on the left of the coil.
                g.FillRectangle(r.BodyBrush, 0, 22, 8, 16)
                g.DrawRectangle(r.Line, 0, 22, 8, 16)
                If _kind = CoilKind.OnDelayTimer Then
                    g.FillRectangle(Brushes.Black, 1, 23, 6, 14)
                Else
                    g.DrawLine(r.Thin, 0, 22, 8, 38) : g.DrawLine(r.Thin, 8, 22, 0, 38)
                End If
                Dim txt = If(r.Simulating AndAlso _kind = CoilKind.OnDelayTimer AndAlso _energized AndAlso Not _active,
                             $"{_onTime:0.0}/{_delay:0.0}s", $"{_delay:0.#}s")
                g.DrawString(txt, r.SmallFont, r.TextBrush, 36, 32)
        End Select
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _energized = False
        _active = False
        _onTime = 0
        _offTime = Double.MaxValue
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim a1 = Ports(0), a2 = Ports(1)
        ' Voltage across the coil; a burnt-out winding carries no current.
        Dim voltage = (a1.State = PortState.Pressurized AndAlso a2.State = PortState.Exhausted) OrElse
                      (a2.State = PortState.Pressurized AndAlso a1.State = PortState.Exhausted)
        _energized = voltage AndAlso Fault <> FaultKind.BurntCoil
        Dim active As Boolean
        Select Case _kind
            Case CoilKind.OnDelayTimer : active = _energized AndAlso _onTime >= _delay - 0.000001
            Case CoilKind.OffDelayTimer : active = _energized OrElse _offTime < _delay
            Case Else : active = _energized
        End Select
        If active = _active Then Return False
        _active = active
        Return True
    End Function

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        If _energized Then
            _onTime += dt
            _offTime = 0
        Else
            _onTime = 0
            If _offTime < Double.MaxValue / 2 Then _offTime += dt
        End If
    End Sub
End Class

''' <summary>
''' Preset counter: counts the pulses on A1/A2; when the count reaches the preset, its contacts
''' (relay contacts with the counter's label) switch. A pulse on R1/R2 resets it.
''' </summary>
Public Class ElectricCounter
    Inherits CircuitElement

    Private _preset As Integer = 3
    Private _count As Integer
    Private _countInput As Boolean
    Private _resetInput As Boolean

    Public Sub New()
        AddPort("A1", 20, 0, 0, -1, kind:=PortKind.Electric)
        AddPort("A2", 20, 60, 0, 1, kind:=PortKind.Electric)
        AddPort("R1", 60, 0, 0, -1, kind:=PortKind.Electric)
        AddPort("R2", 60, 60, 0, 1, kind:=PortKind.Electric)
        Label = "C1"
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "ElectricCounter"
    Public Overrides ReadOnly Property DisplayName As String = "Preset counter"

    <Category("Counter"), DisplayName("Preset"), Description("Number of pulses after which the counter's contacts switch (1 to 9999).")>
    Public Property Preset As Integer
        Get
            Return _preset
        End Get
        Set(value As Integer)
            _preset = Math.Max(1, Math.Min(9999, value))
        End Set
    End Property

    ''' <summary>Pulses counted since the last reset.</summary>
    <Browsable(False)> Public ReadOnly Property Count As Integer
        Get
            Return _count
        End Get
    End Property

    ''' <summary>True when the preset has been reached (the contacts are switched).</summary>
    <Browsable(False)> Public ReadOnly Property Active As Boolean
        Get
            Return _count >= _preset
        End Get
    End Property

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.BurntCoil}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.BurntCoil, "Counting coil burnt out (does not count)", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Count", $"{_count} of {_preset}"))
        list.Add(("Contacts", If(Active, "switched", "at rest")))
        Return list
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(4, 0, 72, 60)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(76, 30)
        End Get
    End Property

    Private Shared Function Energized(a As Port, b As Port) As Boolean
        Return (a.State = PortState.Pressurized AndAlso b.State = PortState.Exhausted) OrElse
               (b.State = PortState.Pressurized AndAlso a.State = PortState.Exhausted)
    End Function

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 20, 0, 20, 18)
        g.DrawLine(r.PenFor(Ports(1)), 20, 42, 20, 60)
        g.DrawLine(r.PenFor(Ports(2)), 60, 0, 60, 18)
        g.DrawLine(r.PenFor(Ports(3)), 60, 42, 60, 60)
        Using b As New SolidBrush(If(r.Simulating AndAlso Active, Color.FromArgb(255, 210, 210), Color.White))
            g.FillRectangle(b, 8, 18, 64, 24)
        End Using
        g.DrawRectangle(If(r.Simulating AndAlso _countInput, r.Energized, r.Line), 8, 18, 64, 24)
        g.DrawLine(r.Thin, 40, 18, 40, 42)
        g.DrawString("+1", r.SmallFont, r.TextBrush, 13, 23)
        g.DrawString("R", r.SmallFont, r.TextBrush, 53, 23)
        g.DrawString(If(r.Simulating, $"{_count}/{_preset}", $"n = {_preset}"), r.SmallFont, r.TextBrush, 76, 40)
        If Not String.IsNullOrEmpty(CrossReference) Then g.DrawString(CrossReference.Replace("  ", vbLf), r.SmallFont, r.MarkBrush, 76, 50)
    End Sub

    <Browsable(False)> Public Overrides ReadOnly Property IsManuallyOperated As Boolean
        Get
            Return True
        End Get
    End Property

    ''' <summary>Clicking the counter during simulation resets it (like the reset key on a real counter).</summary>
    Public Overrides Sub OnSimMouseDown(local As PointF)
        _count = 0
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _count = 0
        _countInput = False
        _resetInput = False
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim before = Active
        Dim countNow = Energized(Ports(0), Ports(1)) AndAlso Fault <> FaultKind.BurntCoil
        Dim resetNow = Energized(Ports(2), Ports(3))
        Dim changed = False
        If resetNow Then
            If _count <> 0 Then _count = 0 : changed = True
        ElseIf countNow AndAlso Not _countInput Then
            _count = Math.Min(_count + 1, 99999)
            changed = True
        End If
        _countInput = countNow
        _resetInput = resetNow
        ' Only a change of the contacts matters to the rest of the circuit.
        Return changed AndAlso before <> Active
    End Function
End Class
