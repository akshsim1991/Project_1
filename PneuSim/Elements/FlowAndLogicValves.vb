Imports System.ComponentModel

''' <summary>Base for the two logic valves: two inputs (1) on the sides, output (2) on top.</summary>
Public MustInherit Class LogicValve
    Inherits CircuitElement

    ''' <summary>0: output joined to the left input, 1: joined to the right input.</summary>
    Protected Side As Integer

    ''' <summary>Which input (0 = 1a, 1 = 1b) the output is joined to at the moment.</summary>
    <ComponentModel.Browsable(False)> Public ReadOnly Property ConnectedInput As Integer
        Get
            Return Side
        End Get
    End Property

    Protected Sub New()
        AddPort("1a", 0, 20, -1, 0)
        AddPort("1b", 80, 20, 1, 0)
        AddPort("2", 40, 0, 0, -1)
    End Sub

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 80, 30)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(50, 9)
        End Get
    End Property

    Public Overrides Sub ResetSim()
        MyBase.ResetSim()
        Side = 0
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        sim.AddEdge(Ports(Side), Ports(2))
    End Sub

    Protected Sub DrawFrame(g As DrawSurface, r As RenderContext)
        g.DrawLine(r.PenFor(Ports(0)), 0, 20, 10, 20)
        g.DrawLine(r.PenFor(Ports(1)), 70, 20, 80, 20)
        g.DrawLine(r.PenFor(Ports(2)), 40, 0, 40, 10)
        g.FillRectangle(r.BodyBrush, 10, 10, 60, 20)
        g.DrawRectangle(r.Line, 10, 10, 60, 20)
    End Sub
End Class

''' <summary>Shuttle valve: the output takes the higher of the two inputs (OR function).</summary>
Public Class ShuttleValve
    Inherits LogicValve

    Public Overrides ReadOnly Property TypeName As String = "ShuttleValve"
    Public Overrides ReadOnly Property DisplayName As String = "Shuttle valve (OR)"

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        DrawFrame(g, r)
        ' Seats at both ends and the ball resting against the side with lower pressure.
        g.DrawLine(r.Line, 20, 13, 16, 20) : g.DrawLine(r.Line, 16, 20, 20, 27)
        g.DrawLine(r.Line, 60, 13, 64, 20) : g.DrawLine(r.Line, 64, 20, 60, 27)
        Dim ballX = If(Side = 0, 50, 21)
        Using b As New SolidBrush(Color.FromArgb(70, 70, 70))
            g.FillEllipse(b, ballX, 14, 10, 12)
        End Using
        g.DrawLine(r.PenFor(Ports(Side)), If(Side = 0, 16, 64), 20, 40, 20)
        g.DrawLine(r.PenFor(Ports(Side)), 40, 20, 40, 10)
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim pa = Ports(0).Pressure, pb = Ports(1).Pressure
        Dim newSide = If(pa > pb + 0.01, 0, If(pb > pa + 0.01, 1, Side))
        If newSide = Side Then Return False
        Side = newSide
        Return True
    End Function
End Class

''' <summary>Two-pressure valve: the output takes the lower of the two inputs (AND function).</summary>
Public Class TwoPressureValve
    Inherits LogicValve

    Public Overrides ReadOnly Property TypeName As String = "TwoPressureValve"
    Public Overrides ReadOnly Property DisplayName As String = "Two-pressure valve (AND)"

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        DrawFrame(g, r)
        ' Spool with a seat at each end; the spool is pushed towards the lower pressure side.
        Dim offset = If(Side = 0, -5, 5)
        g.DrawRectangle(r.Line, 20 + offset, 14, 6, 12)
        g.DrawRectangle(r.Line, 54 + offset, 14, 6, 12)
        g.DrawLine(r.Line, 26 + offset, 20, 54 + offset, 20)
        Dim pen = r.PenFor(Ports(2))
        g.DrawLine(pen, 40, 10, 40, 20)
    End Sub

    Public Overrides Function UpdateLogic(sim As Simulator) As Boolean
        Dim pa = Ports(0).Pressure, pb = Ports(1).Pressure
        Dim newSide = If(pa < pb - 0.01, 0, If(pb < pa - 0.01, 1, Side))
        If newSide = Side Then Return False
        Side = newSide
        Return True
    End Function
End Class

''' <summary>Adjustable throttle, optionally with a parallel check valve (one-way flow control valve).</summary>
Public Class FlowControlValve
    Inherits CircuitElement

    Private _opening As Integer = 30
    Private _hasCheck As Boolean = True

    Public Sub New()
        AddPort("1", 0, 30, -1, 0)
        AddPort("2", 80, 30, 1, 0)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "FlowControlValve"

    Public Overrides ReadOnly Property DisplayName As String
        Get
            Return If(Hydraulic, If(_hasCheck, "Hydraulic one-way flow control valve", "Hydraulic throttle valve"),
                      If(_hasCheck, "One-way flow control valve", "Flow control valve"))
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


    <Category("Flow control"), DisplayName("Opening (%)"),
     Description("Throttle opening. 100 % is unrestricted flow; smaller values slow down the cylinder.")>
    Public Property OpeningPercent As Integer
        Get
            Return _opening
        End Get
        Set(value As Integer)
            _opening = Math.Max(1, Math.Min(100, value))
        End Set
    End Property

    <Category("Flow control"), DisplayName("Check valve"),
     Description("True: free flow from 1 to 2, throttled from 2 to 1. False: throttled in both directions.")>
    Public Property HasCheckValve As Boolean
        Get
            Return _hasCheck
        End Get
        Set(value As Boolean)
            _hasCheck = value
        End Set
    End Property

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, If(_hasCheck, 0, 18), 80, If(_hasCheck, 42, 24))
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(0, If(_hasCheck, 0, 18))
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim pen = r.PenFor(Ports(0))
        Dim pen2 = r.PenFor(Ports(1))
        g.DrawLine(pen, 0, 30, 32, 30)
        g.DrawLine(pen2, 48, 30, 80, 30)
        ' Throttle: two arcs forming a restriction.
        g.DrawArc(r.Line, 30, 21, 20, 8, 200, 140)
        g.DrawArc(r.Line, 30, 31, 20, 8, 20, 140)
        g.DrawLine(If(Ports(0).IsPressurized AndAlso r.Simulating, r.Pressure, r.Thin), 32, 30, 48, 30)
        ' Adjustment arrow across the throttle.
        Symbols.Arrow(g, r.Thin, New PointF(30, 40), New PointF(52, 20), 5)
        If _hasCheck Then
            ' Bypass with check valve: free flow from 1 (left) to 2 (right).
            g.DrawLines(pen, {New PointF(16, 30), New PointF(16, 8), New PointF(34, 8)})
            g.DrawLines(pen2, {New PointF(48, 8), New PointF(64, 8), New PointF(64, 30)})
            g.DrawLine(pen, 34, 8, 38, 8)
            g.DrawLine(r.Line, 44, 2, 38, 8) : g.DrawLine(r.Line, 38, 8, 44, 14)
            g.FillEllipse(r.BodyBrush, 40, 4, 8, 8)
            g.DrawEllipse(r.Line, 40, 4, 8, 8)
        End If
        g.DrawString($"{_opening}%", r.SmallFont, r.TextBrush, 52, 32)
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        Dim f = _opening / 100.0
        If _hasCheck Then
            sim.AddEdge(Ports(0), Ports(1), 1, f)
        Else
            sim.AddEdge(Ports(0), Ports(1), f, f)
        End If
    End Sub
End Class
