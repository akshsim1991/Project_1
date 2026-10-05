Imports System.ComponentModel

''' <summary>Compressed air source.</summary>
Public Class AirSupply
    Inherits CircuitElement

    Private _pressure As Double = 6

    Public Sub New()
        AddPort("1", 20, 0, 0, -1)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "AirSupply"
    Public Overrides ReadOnly Property DisplayName As String = "Compressed air supply"

    <Category("Supply"), DisplayName("Pressure (bar)"), Description("Operating pressure delivered by the supply.")>
    Public Property Pressure As Double
        Get
            Return _pressure
        End Get
        Set(value As Double)
            _pressure = Math.Max(0.5, Math.Min(16, value))
        End Set
    End Property

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(4, 0, 32, 44)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(36, 24)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As Graphics, r As RenderContext)
        Dim pen = r.PenFor(Ports(0))
        g.DrawLine(pen, 20, 0, 20, 18)
        g.FillEllipse(r.BodyBrush, 8, 18, 24, 24)
        g.DrawEllipse(r.Line, 8, 18, 24, 24)
        Using b As New SolidBrush(If(r.Simulating, RenderContext.PressureColor, Color.Black))
            g.FillPolygon(b, {New PointF(20, 22), New PointF(14, 32), New PointF(26, 32)})
        End Using
        g.DrawString($"{Pressure:0.#} bar", r.SmallFont, r.TextBrush, 34, 30)
    End Sub

    Public Overrides Sub AddTerminals(sim As Simulator)
        sim.AddSupply(Ports(0), Pressure)
    End Sub
End Class

''' <summary>Pressure gauge showing the pressure at its connection.</summary>
Public Class PressureGauge
    Inherits CircuitElement

    Public Sub New()
        AddPort("1", 20, 50, 0, 1)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "PressureGauge"
    Public Overrides ReadOnly Property DisplayName As String = "Pressure gauge"

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

    Public Overrides Sub DrawSymbol(g As Graphics, r As RenderContext)
        Dim p = Ports(0)
        g.DrawLine(r.PenFor(p), 20, 38, 20, 50)
        g.FillEllipse(r.BodyBrush, 2, 2, 36, 36)
        g.DrawEllipse(r.Line, 2, 2, 36, 36)
        ' Needle: -135 deg at 0 bar to +135 deg at 10 bar.
        Dim angle = (-135 + 270 * Math.Min(1, p.Pressure / 10)) * Math.PI / 180
        Dim tip As New PointF(CSng(20 + 14 * Math.Sin(angle)), CSng(20 - 14 * Math.Cos(angle)))
        Symbols.Arrow(g, If(r.Simulating AndAlso p.IsPressurized, r.Pressure, r.Line), New PointF(20, 20), tip, 5)
        If r.Simulating Then
            g.DrawString($"{p.Pressure:0.0} bar", r.SmallFont, r.TextBrush, 40, 26)
        End If
    End Sub
End Class
