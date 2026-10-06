Imports System.ComponentModel
Imports System.Drawing.Design

''' <summary>Non-return valve: free flow from 1 to 2, blocked from 2 to 1.</summary>
Public Class CheckValve
    Inherits CircuitElement

    Public Sub New()
        AddPort("1", 0, 20, -1, 0)
        AddPort("2", 60, 20, 1, 0)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "CheckValve"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return If(Hydraulic, "Hydraulic check valve", "Check valve")
        End Get
    End Property

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

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 8, 60, 24)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 0, 20, 24, 20)
        g.DrawLine(r.PenFor(Ports(1)), 36, 20, 60, 20)
        ' Seat (V with its point upstream) and ball: flow from 1 lifts the ball off the seat.
        g.DrawLine(r.Line, 30, 12, 24, 20) : g.DrawLine(r.Line, 24, 20, 30, 28)
        g.FillEllipse(r.BodyBrush, 26, 14, 12, 12)
        g.DrawEllipse(r.Line, 26, 14, 12, 12)
    End Sub

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.Leak, FaultKind.Blocked}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Select Case kind
            Case FaultKind.Leak : Return "Seat damaged: it also lets flow through in the blocked direction"
            Case FaultKind.Blocked : Return "Stuck closed: nothing flows through it"
        End Select
        Return MyBase.FaultDescription(kind)
    End Function

    Public Overrides Sub AddEdges(sim As Simulator)
        Select Case Fault
            Case FaultKind.Blocked
            Case FaultKind.Leak : sim.AddEdge(Ports(0), Ports(1), 1, 0.4)
            Case Else : sim.AddEdge(Ports(0), Ports(1), 1, 0)
        End Select
    End Sub
End Class

''' <summary>Quick exhaust valve: feeds 1 to 2; when 1 is vented, 2 exhausts straight through 3.</summary>
Public Class QuickExhaustValve
    Inherits CircuitElement

    Private _feeding As Boolean

    ''' <summary>True while air flows 1 to 2 (the disc closes the exhaust).</summary>
    <Browsable(False)> Public ReadOnly Property Feeding As Boolean
        Get
            Return _feeding
        End Get
    End Property

    Public Sub New()
        AddPort("1", 0, 30, -1, 0)
        AddPort("2", 40, 0, 0, -1)
        AddPort("3", 40, 60, 0, 1, vents:=True)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "QuickExhaustValve"
    Public Overrides ReadOnly Property DisplayName As String = "Quick exhaust valve"

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 70, 60)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(56, 22)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 0, 30, 20, 30)
        g.DrawLine(r.PenFor(Ports(1)), 40, 0, 40, 18)
        g.DrawLine(r.PenFor(Ports(2)), 40, 42, 40, 60)
        g.FillRectangle(r.BodyBrush, 20, 18, 34, 24)
        g.DrawRectangle(r.Line, 20, 18, 34, 24)
        ' Disc: against the exhaust seat while feeding, against the inlet seat while exhausting.
        Using b As New SolidBrush(Color.FromArgb(70, 70, 70))
            If _feeding Then g.FillRectangle(b, 34, 36, 12, 4) Else g.FillRectangle(b, 22, 24, 4, 12)
        End Using
        If Ports(2).ConnectionCount = 0 Then Symbols.Exhaust(g, r.Line, New PointF(40, 60), New PointF(0, 1))
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _feeding = False
    End Sub

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.Blocked}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.Blocked, "Exhaust port blocked: the air cannot get out here", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Sub AddEdges(sim As Simulator)
        If _feeding Then
            sim.AddEdge(Ports(0), Ports(1), 1, 0)
        ElseIf Fault <> FaultKind.Blocked Then
            sim.AddEdge(Ports(1), Ports(2), 1, 0)
        End If
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim feeding = Ports(0).State = PortState.Pressurized
        If feeding = _feeding Then Return False
        _feeding = feeding
        Return True
    End Function
End Class

<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum RegulatorStyle
    <Description("Pressure regulator")> Regulator
    <Description("Service unit (filter, regulator, gauge)")> ServiceUnit
End Enum

''' <summary>Pressure regulator or service unit: output pressure limited to the setting.</summary>
Public Class PressureRegulator
    Inherits CircuitElement

    Private _setting As Double = 4

    Public Sub New()
        AddPort("1", 0, 30, -1, 0)
        AddPort("2", 100, 30, 1, 0)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "PressureRegulator"

    Public Overrides Function ShowProperty(name As String) As Boolean
        If name = NameOf(Style) Then Return Not Hydraulic
        Return True
    End Function

    Public Overrides ReadOnly Property DisplayName As String
        Get
            If Hydraulic Then Return "Pressure reducing valve"
            Return If(Style = RegulatorStyle.ServiceUnit, "Service unit", "Pressure regulator")
        End Get
    End Property

    <Category("Regulator"), DisplayName("Output pressure (bar)"), Description("Pressure delivered at port 2.")>
    Public Property Setting As Double
        Get
            Return _setting
        End Get
        Set(value As Double)
            _setting = Math.Max(0.2, Math.Min(400, value))
        End Set
    End Property

    <Category("Medium"), DisplayName("Hydraulic"), Description("True: fitted in a hydraulic (oil) line. False: compressed air (pressure regulator).")>
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


    <Category("Regulator"), DisplayName("Symbol"), Description("Plain regulator, or a service unit with filter and gauge.")>
    Public Property Style As RegulatorStyle = RegulatorStyle.Regulator

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 100, 56)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim pin = r.PenFor(Ports(0)), pout = r.PenFor(Ports(1))
        If Style = RegulatorStyle.ServiceUnit AndAlso Not Hydraulic Then
            g.DrawLine(pin, 0, 30, 14, 30)
            g.DrawLine(pout, 86, 30, 100, 30)
            Using dash As New Pen(Color.Black, 1) With {.DashStyle = Drawing2D.DashStyle.Dash}
                g.DrawRectangle(dash, 14, 6, 72, 44)
            End Using
            ' Filter: diamond with a dashed element.
            Dim fx = 30.0F
            g.DrawPolygon(r.Line, {New PointF(fx, 18), New PointF(fx + 12, 30), New PointF(fx, 42), New PointF(fx - 12, 30)})
            Using dash As New Pen(Color.Black, 1) With {.DashStyle = Drawing2D.DashStyle.Dash}
                g.DrawLine(dash, fx, 19, fx, 41)
            End Using
            g.DrawLine(pin, 14, 30, fx - 12, 30)
            g.DrawLine(pin, fx + 12, 30, 52, 30)
            DrawRegulatorBox(g, r, 52, 22, 16)
            g.DrawLine(pout, 68, 30, 86, 30)
            ' Gauge.
            g.DrawLine(r.Thin, 78, 30, 78, 22)
            g.FillEllipse(r.BodyBrush, 72, 10, 12, 12)
            g.DrawEllipse(r.Thin, 72, 10, 12, 12)
            g.DrawLine(r.Thin, 78, 16, 81, 12)
        Else
            g.DrawLine(pin, 0, 30, 34, 30)
            g.DrawLine(pout, 66, 30, 100, 30)
            DrawRegulatorBox(g, r, 34, 14, 32)
        End If
        Dim txt = If(r.Simulating, $"{Ports(1).Pressure:0.0} bar", $"{_setting:0.#} bar")
        g.DrawString(txt, r.SmallFont, r.TextBrush, 36, 44)
    End Sub

    Private Sub DrawRegulatorBox(g As DrawSurface, r As RenderContext, x As Single, y As Single, size As Single)
        g.FillRectangle(r.BodyBrush, x, y, size, size)
        g.DrawRectangle(r.Line, x, y, size, size)
        Dim cy = y + size / 2
        Symbols.Arrow(g, If(r.Simulating AndAlso Ports(1).IsPressurized, r.Pressure, r.Line),
                      New PointF(x + 3, cy + 3), New PointF(x + size - 3, cy + 3), 4)
        ' Adjustable spring on top.
        Symbols.Spring(g, r.Thin, x + size / 2 - 1, x + size / 2 + 1, y - 4, 3)
        g.DrawLine(r.Thin, x + 2, y - 2, x + size - 2, y - 10)
    End Sub

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.LowOutput, FaultKind.Blocked}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Select Case kind
            Case FaultKind.LowOutput : Return If(Hydraulic, "Reducing valve faulty: the output pressure is far too low", "Regulator diaphragm torn: the output pressure is far too low")
            Case FaultKind.Blocked : Return If(Style = RegulatorStyle.ServiceUnit AndAlso Not Hydraulic, "Filter element clogged: very little air gets through", "Clogged: very little flow gets through")
        End Select
        Return MyBase.FaultDescription(kind)
    End Function

    ''' <summary>Output pressure including a fault.</summary>
    Private ReadOnly Property EffectiveSetting As Double
        Get
            Return If(Fault = FaultKind.LowOutput, Math.Max(0.2, _setting * 0.25), _setting)
        End Get
    End Property

    Public Overrides Sub AddEdges(sim As Simulator)
        Dim cap = If(Fault = FaultKind.Blocked, 0.12, 1)
        sim.AddEdge(Ports(0), Ports(1), cap, cap, EffectiveSetting)
    End Sub
End Class

''' <summary>Silencer: lets air escape quietly; acts as an open exhaust.</summary>
Public Class Silencer
    Inherits CircuitElement

    Public Sub New()
        AddPort("1", 20, 0, 0, -1)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Silencer"
    Public Overrides ReadOnly Property DisplayName As String = "Silencer"

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(8, 0, 24, 30)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.Line, 20, 0, 20, 8)
        g.DrawPolygon(r.Line, {New PointF(20, 8), New PointF(10, 22), New PointF(30, 22)})
        g.DrawRectangle(r.Line, 10, 22, 20, 6)
        For hx = 13 To 27 Step 4
            g.DrawLine(r.Thin, hx, 22, hx, 28)
        Next
    End Sub

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.Blocked}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.Blocked, "Silencer clogged with oil and dirt: the air escapes very slowly", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Sub AddTerminals(sim As Simulator)
        sim.AddExhaust(Ports(0), If(Fault = FaultKind.Blocked, 0.08, 1))
    End Sub
End Class

''' <summary>Double-acting semi-rotary actuator (swivel drive), 0 to 180 degrees.</summary>
Public Class SemiRotaryActuator
    Inherits DoubleActingCylinder

    Public Overrides ReadOnly Property TypeName As String = "SemiRotaryActuator"
    Public Overrides ReadOnly Property DisplayName As String = "Semi-rotary actuator"

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, -6, 120, 46)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        g.FillRectangle(r.BodyBrush, 30, -4, 60, 34)
        g.DrawRectangle(r.Line, 30, -4, 60, 34)
        g.DrawLine(r.PenFor(Ports(0)), 10, 40, 10, 13)
        g.DrawLine(r.PenFor(Ports(0)), 10, 13, 30, 13)
        g.DrawLine(r.PenFor(Ports(1)), 110, 40, 110, 13)
        g.DrawLine(r.PenFor(Ports(1)), 110, 13, 90, 13)
        ' Shaft with a pointer turning through 180 degrees.
        g.DrawArc(r.Thin, 46, -1, 28, 28, 180, 180)
        Dim a = Math.PI * (1 - Position)
        Dim tip As New PointF(CSng(60 + 13 * Math.Cos(a)), CSng(13 - 13 * Math.Sin(a)))
        Using p As New Pen(Color.FromArgb(70, 70, 70), 3)
            g.DrawLine(p, 60, 13, tip.X, tip.Y)
        End Using
        g.FillEllipse(Brushes.Black, 57, 10, 6, 6)
        g.DrawString($"{Position * 180:0}°", r.SmallFont, r.TextBrush, 50, 16)
        If Not String.IsNullOrWhiteSpace(RetractedMark) Then g.DrawString(RetractedMark, r.SmallFont, r.MarkBrush, 31, -16)
        If Not String.IsNullOrWhiteSpace(ExtendedMark) Then g.DrawString(ExtendedMark, r.SmallFont, r.MarkBrush, 70, -16)
    End Sub
End Class

''' <summary>Air motor: turns while its port is pressurized; speed follows the available flow.</summary>
Public Class AirMotor
    Inherits CircuitElement

    Private _angle As Double
    Private _rpm As Double

    Public Sub New()
        AddPort("1", 20, 50, 0, 1)
    End Sub

    <Browsable(False)> Public ReadOnly Property Rpm As Double
        Get
            Return _rpm
        End Get
    End Property

    Public Overrides ReadOnly Property TypeName As String = "AirMotor"
    Public Overrides ReadOnly Property DisplayName As String = "Air motor"

    <Category("Motor"), DisplayName("Speed (rpm)"), Description("Speed at full flow. Shown slowed down in the animation.")>
    Public Property NominalSpeed As Double
        Get
            Return _nominalSpeed
        End Get
        Set(value As Double)
            _nominalSpeed = Math.Max(0, Math.Min(30000, value))
        End Set
    End Property
    Private _nominalSpeed As Double = 60

    <Category("Motor"), DisplayName("Air consumption (NL/min)"), Description("Free air used at full speed and 6 bar; counted in the air consumption and running cost.")>
    Public Property AirConsumptionNlMin As Double
        Get
            Return _consumption
        End Get
        Set(value As Double)
            _consumption = Math.Max(0, Math.Min(100000, value))
        End Set
    End Property
    Private _consumption As Double = 150

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(2, 2, 36, 48)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(40, 14)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim p = Ports(0)
        g.DrawLine(r.PenFor(p), 20, 38, 20, 50)
        g.FillEllipse(r.BodyBrush, 2, 2, 36, 36)
        g.DrawEllipse(r.Line, 2, 2, 36, 36)
        Symbols.EnergyTriangle(g, r, {New PointF(20, 36), New PointF(14, 27), New PointF(26, 27)}, False, r.Simulating AndAlso p.IsPressurized)
        Dim a = _angle * Math.PI / 180
        g.DrawLine(r.Thin, 20, 20, CSng(20 + 12 * Math.Cos(a)), CSng(20 + 12 * Math.Sin(a)))
        g.DrawArc(r.Thin, -4, -4, 48, 48, 200, 40)
        If r.Simulating Then g.DrawString($"{_rpm:0} rpm", r.SmallFont, r.TextBrush, 40, 26)
    End Sub

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        _angle = 0
        _rpm = 0
    End Sub

    Public Overrides Function PossibleFaults() As FaultKind()
        Return {FaultKind.Jammed}
    End Function

    Public Overrides Function FaultDescription(kind As FaultKind) As String
        Return If(kind = FaultKind.Jammed, "Motor seized: the vanes are stuck", MyBase.FaultDescription(kind))
    End Function

    Public Overrides Function InspectValues() As List(Of (Name As String, Value As String))
        Dim list = MyBase.InspectValues()
        list.Add(("Speed", $"{_rpm:0} rpm"))
        Return list
    End Function

    ''' <summary>Free air flowing through the motor now, NL/min.</summary>
    Public Function ConsumptionNow() As Double
        Dim p = Ports(0)
        If p.State <> PortState.Pressurized OrElse p.Pressure <= 0.2 OrElse Fault = FaultKind.Jammed Then Return 0
        Return _consumption * p.Factor * (p.Pressure + CylinderBase.Atm) / (6 + CylinderBase.Atm)
    End Function

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        Dim p = Ports(0)
        _rpm = If(p.State = PortState.Pressurized AndAlso Fault <> FaultKind.Jammed, NominalSpeed * p.Factor * Math.Min(1, p.Pressure / 6), 0)
        _angle = (_angle + _rpm * 6 * dt) Mod 360
        If p.State = PortState.Pressurized AndAlso p.Pressure > 0.2 AndAlso Fault <> FaultKind.Jammed Then
            ' Air flows through the motor to its exhaust; consumption rises with pressure.
            sim.AddAirConsumption(_consumption * p.Factor * (p.Pressure + CylinderBase.Atm) / (6 + CylinderBase.Atm) * dt / 60)
        End If
    End Sub
End Class

''' <summary>Branch point for tubes or wires.</summary>
Public Class Junction
    Inherits CircuitElement

    Private _electric As Boolean

    Public Sub New()
        AddPort("1", 0, 0, 0, 0)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "Junction"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Select Case Medium
                Case PortKind.Electric : Return "Wire junction / bend point"
                Case PortKind.Hydraulic : Return "Hydraulic junction / bend point"
                Case Else : Return "Tube junction / bend point"
            End Select
        End Get
    End Property

    ''' <summary>Kept for files from PneuSim 2.0; use <see cref="Medium"/>.</summary>
    <Browsable(False)> Public Property IsElectric As Boolean
        Get
            Return _electric
        End Get
        Set(value As Boolean)
            Medium = If(value, PortKind.Electric, PortKind.Pneumatic)
        End Set
    End Property

    <Category("Junction"), DisplayName("Joins"), Description("Tubes (pneumatic), wires (electric) or hydraulic lines.")>
    Public Property Medium As PortKind
        Get
            Return Ports(0).Kind
        End Get
        Set(value As PortKind)
            _electric = value = PortKind.Electric
            Ports(0).Kind = value
        End Set
    End Property

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(-5, -5, 10, 10)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(6, 0)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim p = Ports(0)
        Dim c = If(r.Simulating AndAlso p.IsPressurized, RenderContext.ActiveColor(p.Kind), Color.Black)
        If p.ConnectionCount >= 3 Then
            Using b As New SolidBrush(c)
                g.FillEllipse(b, -3.5F, -3.5F, 7, 7)
            End Using
        ElseIf r.Interactive AndAlso Not r.Simulating Then
            ' A bend point: only visible while editing.
            Using pen As New Pen(Color.FromArgb(150, 150, 150))
                g.DrawEllipse(pen, -2.5F, -2.5F, 5, 5)
            End Using
        End If
    End Sub
End Class

''' <summary>Free text placed on the drawing (titles, notes, explanations).</summary>
Public Class TextNote
    Inherits CircuitElement

    Private Shared ReadOnly Measure As Graphics = Graphics.FromImage(New Bitmap(1, 1))
    Private _text As String = "Text"
    Private _size As Single = 10

    Public Overrides ReadOnly Property TypeName As String = "TextNote"
    Public Overrides ReadOnly Property DisplayName As String = "Text"

    <Category("Text"), DisplayName("Content"), Description("The text to show. Use Shift+Enter or the ... button for several lines."),
     Editor("System.ComponentModel.Design.MultilineStringEditor, System.Design", GetType(UITypeEditor))>
    Public Property Text As String
        Get
            Return _text
        End Get
        Set(value As String)
            _text = If(value, "")
        End Set
    End Property

    <Category("Text"), DisplayName("Font size")>
    Public Property FontSize As Single
        Get
            Return _size
        End Get
        Set(value As Single)
            _size = Math.Max(6, Math.Min(48, value))
        End Set
    End Property

    <Category("Text"), DisplayName("Bold")>
    Public Property Bold As Boolean

    Private Function MakeFont() As Font
        Return New Font("Segoe UI", _size, If(Bold, FontStyle.Bold, FontStyle.Regular))
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Using f = MakeFont()
                SyncLock Measure
                    Dim sz = Measure.MeasureString(If(_text.Length = 0, " ", _text), f)
                    Return New RectangleF(0, 0, Math.Max(10, sz.Width), Math.Max(10, sz.Height))
                End SyncLock
            End Using
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Using f = MakeFont()
            g.DrawString(_text, f, Brushes.Black, 0, 0)
        End Using
    End Sub
End Class
