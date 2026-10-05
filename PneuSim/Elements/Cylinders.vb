Imports System.ComponentModel

''' <summary>Common behaviour and drawing of pneumatic cylinders.</summary>
Public MustInherit Class CylinderBase
    Inherits CircuitElement

    ' Body geometry (local coordinates). Ports sit 10 px below the body.
    Protected Const BodyLeft As Single = 0, BodyRight As Single = 120
    Protected Const BodyTop As Single = 0, BodyBottom As Single = 30
    Protected Const PistonWidth As Single = 8
    Protected Const PistonTravel As Single = 92
    Protected Const RodLength As Single = 112

    Private _strokeTime As Double = 1.0
    Private _position As Double

    <Category("Cylinder"), DisplayName("Stroke time (s)"),
     Description("Time for a full stroke when air can flow freely. Flow control valves make it slower.")>
    Public Property StrokeTime As Double
        Get
            Return _strokeTime
        End Get
        Set(value As Double)
            _strokeTime = Math.Max(0.05, Math.Min(60, value))
        End Set
    End Property

    <Category("Cylinder"), DisplayName("Stroke (mm)"), Description("Stroke length, shown in the displacement-step diagram.")>
    Public Property StrokeLength As Integer = 100

    <Category("Position marks"), DisplayName("Retracted mark"),
     Description("Name of the limit valve operated when the piston rod is fully retracted, e.g. 1S1.")>
    Public Property RetractedMark As String = ""

    <Category("Position marks"), DisplayName("Extended mark"),
     Description("Name of the limit valve operated when the piston rod is fully extended, e.g. 1S2.")>
    Public Property ExtendedMark As String = ""

    ''' <summary>Piston position, 0 = retracted, 1 = extended.</summary>
    <Browsable(False)> Public ReadOnly Property Position As Double
        Get
            Return _position
        End Get
    End Property

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
    End Sub

    Protected Sub Move(direction As Integer, flowFactor As Double, dt As Double)
        _position += direction * flowFactor * dt / _strokeTime
        _position = Math.Max(0, Math.Min(1, _position))
    End Sub

    Protected Sub DrawBody(g As Graphics, r As RenderContext)
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
    End Sub

    Private Sub DrawMark(g As Graphics, r As RenderContext, x As Single, mark As String)
        If String.IsNullOrWhiteSpace(mark) Then Return
        Using p As New Pen(Color.FromArgb(160, 40, 40), 1.4F)
            g.DrawLine(p, x, BodyTop + 4, x, BodyTop - 6)
        End Using
        Dim sz = g.MeasureString(mark, r.SmallFont)
        g.DrawString(mark, r.SmallFont, r.MarkBrush, x - sz.Width / 2, BodyTop - 6 - sz.Height)
    End Sub

    Protected Sub DrawPortStub(g As Graphics, r As RenderContext, p As Port)
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

    Public Overrides Sub DrawSymbol(g As Graphics, r As RenderContext)
        DrawBody(g, r)
        Dim px = PistonX + PistonWidth
        Symbols.Spring(g, r.Thin, px + 2, BodyRight - 2, (BodyTop + BodyBottom) / 2, 10)
        DrawPortStub(g, r, Ports(0))
    End Sub

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        Dim p = Ports(0)
        Select Case p.State
            Case PortState.Pressurized
                If p.Pressure > 1 Then Move(+1, p.Factor, dt)
            Case PortState.Exhausted
                Move(-1, p.Factor, dt)
        End Select
    End Sub
End Class

''' <summary>Double-acting cylinder.</summary>
Public Class DoubleActingCylinder
    Inherits CylinderBase

    ''' <summary>Annular (rod side) area relative to the full piston area.</summary>
    Private Const RodSideArea As Double = 0.7

    Public Sub New()
        AddPort("1", 10, BodyBottom + 10, 0, 1, vents:=True)
        AddPort("2", 110, BodyBottom + 10, 0, 1, vents:=True)
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "DoubleActingCylinder"
    Public Overrides ReadOnly Property DisplayName As String = "Double-acting cylinder"

    Public Overrides Sub DrawSymbol(g As Graphics, r As RenderContext)
        DrawBody(g, r)
        DrawPortStub(g, r, Ports(0))
        DrawPortStub(g, r, Ports(1))
    End Sub

    Public Overrides Sub UpdateDynamics(sim As Simulator, dt As Double)
        Dim capSide = Ports(0), rodSide = Ports(1)
        Dim force = capSide.Pressure - rodSide.Pressure * RodSideArea
        If force > 0.2 Then
            Move(+1, Speed(capSide, rodSide), dt)
        ElseIf force < -0.2 Then
            Move(-1, Speed(rodSide, capSide), dt)
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
