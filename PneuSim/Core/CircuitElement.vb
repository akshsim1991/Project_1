Imports System.ComponentModel
Imports System.Drawing.Drawing2D

''' <summary>Pressure condition of a port after the network has been solved.</summary>
Public Enum PortState
    ''' <summary>Not connected to supply or exhaust: keeps the last (trapped) pressure.</summary>
    Floating
    Pressurized
    Exhausted
End Enum

''' <summary>What a port carries; only ports of the same kind can be connected.</summary>
Public Enum PortKind
    Pneumatic
    Electric
End Enum

''' <summary>A pneumatic or electrical connection point on a circuit element.</summary>
Public Class Port
    Public Sub New(owner As CircuitElement, name As String)
        Me.Owner = owner
        Me.Name = name
    End Sub

    Public ReadOnly Property Owner As CircuitElement
    Public ReadOnly Property Name As String

    ''' <summary>Position in the element's local (unrotated) coordinates.</summary>
    Public Property Local As PointF
    ''' <summary>Unit vector pointing out of the element, in local coordinates.</summary>
    Public Property Direction As PointF
    ''' <summary>True if the port vents to atmosphere when no tube is attached (valve exhausts, cylinder ports).</summary>
    Public Property VentsWhenOpen As Boolean

    Public Property Kind As PortKind = PortKind.Pneumatic

    ''' <summary>True for junction points, whose tubes may leave in any direction.</summary>
    Public ReadOnly Property IsOmnidirectional As Boolean
        Get
            Return Direction.X = 0 AndAlso Direction.Y = 0
        End Get
    End Property

    ''' <summary>Number of tubes attached; maintained by <see cref="Circuit"/>.</summary>
    Public Property ConnectionCount As Integer

    ' --- simulation results ---
    Public Property Pressure As Double
    Public Property State As PortState = PortState.Floating
    ''' <summary>
    ''' Relative flow capacity (0..1) of the path from the supply (pressurized ports)
    ''' or to the exhaust (exhausted ports). Used for cylinder speed.
    ''' </summary>
    Public Property Factor As Double
    Friend Property NodeIndex As Integer

    Public ReadOnly Property IsPressurized As Boolean
        Get
            Return Pressure > 0.5
        End Get
    End Property

    Public Function WorldPos() As PointF
        Return Owner.ToWorld(Local)
    End Function

    Public Function WorldDir() As PointF
        Return Owner.RotateVector(Direction)
    End Function

    Public Sub ResetSim()
        Pressure = 0
        State = PortState.Floating
        Factor = 0
    End Sub
End Class

''' <summary>Base class for every symbol that can be placed in a circuit.</summary>
Public MustInherit Class CircuitElement

    <Browsable(False)> Public Property Id As Integer
    <Browsable(False)> Public Property X As Single
    <Browsable(False)> Public Property Y As Single
    ''' <summary>Rotation in quarter turns (0..3), clockwise.</summary>
    <Browsable(False)> Public Property Rotation As Integer

    <Category("General"), Description("Identifier shown next to the symbol, e.g. 1A, 1V1, 1S2.")>
    Public Property Label As String = ""

    <Browsable(False)> Public ReadOnly Property Ports As New List(Of Port)

    ''' <summary>Name written to circuit files.</summary>
    <Browsable(False)> Public MustOverride ReadOnly Property TypeName As String

    ''' <summary>Human readable name of the symbol.</summary>
    <Browsable(False)> Public MustOverride ReadOnly Property DisplayName As String

    ''' <summary>Area occupied by the symbol in local coordinates.</summary>
    <Browsable(False)> Public MustOverride ReadOnly Property LocalBounds As RectangleF

    ''' <summary>Where the label is drawn, in local coordinates (text is drawn above this point).</summary>
    <Browsable(False)> Public Overridable ReadOnly Property LabelAnchor As PointF
        Get
            Dim b = LocalBounds
            Return New PointF(b.Left, b.Top)
        End Get
    End Property

    Protected Function AddPort(name As String, x As Single, y As Single, dx As Single, dy As Single,
                               Optional vents As Boolean = False, Optional kind As PortKind = PortKind.Pneumatic) As Port
        Dim p As New Port(Me, name) With {
            .Local = New PointF(x, y),
            .Direction = New PointF(dx, dy),
            .VentsWhenOpen = vents,
            .Kind = kind
        }
        Ports.Add(p)
        Return p
    End Function

    Public Function GetPort(name As String) As Port
        Return Ports.FirstOrDefault(Function(p) p.Name = name)
    End Function

    ' ---------------------------------------------------------------- geometry

    Public Function GetMatrix() As Matrix
        Dim m As New Matrix()
        m.Translate(X, Y)
        m.Rotate(90.0F * (Rotation Mod 4))
        Return m
    End Function

    Public Function ToWorld(p As PointF) As PointF
        Dim pts = {p}
        Using m = GetMatrix()
            m.TransformPoints(pts)
        End Using
        Return pts(0)
    End Function

    Public Function RotateVector(v As PointF) As PointF
        Dim pts = {v}
        Using m = GetMatrix()
            m.TransformVectors(pts)
        End Using
        Return New PointF(CSng(Math.Round(pts(0).X, 4)), CSng(Math.Round(pts(0).Y, 4)))
    End Function

    Public Function ToLocal(p As PointF) As PointF
        Dim pts = {p}
        Using m = GetMatrix()
            m.Invert()
            m.TransformPoints(pts)
        End Using
        Return pts(0)
    End Function

    Public Function WorldBounds() As RectangleF
        Dim b = LocalBounds
        Dim pts = {New PointF(b.Left, b.Top), New PointF(b.Right, b.Top),
                   New PointF(b.Right, b.Bottom), New PointF(b.Left, b.Bottom)}
        Using m = GetMatrix()
            m.TransformPoints(pts)
        End Using
        Dim minX = pts.Min(Function(p) p.X), minY = pts.Min(Function(p) p.Y)
        Dim maxX = pts.Max(Function(p) p.X), maxY = pts.Max(Function(p) p.Y)
        Return RectangleF.FromLTRB(minX, minY, maxX, maxY)
    End Function

    Public Overridable Function HitTest(world As PointF) As Boolean
        Dim b = LocalBounds
        b.Inflate(2, 2)
        Return b.Contains(ToLocal(world))
    End Function

    ' ---------------------------------------------------------------- drawing

    ''' <summary>Draws the symbol. The graphics transform is already set to local coordinates.</summary>
    Public MustOverride Sub DrawSymbol(g As Graphics, r As RenderContext)

    ' ---------------------------------------------------------------- simulation

    Public Overridable Sub ResetSim()
        For Each p In Ports
            p.ResetSim()
        Next
    End Sub

    ''' <summary>Adds internal air passages for the current switching state.</summary>
    Public Overridable Sub AddEdges(sim As Simulator)
    End Sub

    ''' <summary>Registers supply ports.</summary>
    Public Overridable Sub AddTerminals(sim As Simulator)
    End Sub

    ''' <summary>Instant switching logic. Returns True if the state changed.</summary>
    Public Overridable Function UpdateLogic(sim As Simulator) As Boolean
        Return False
    End Function

    ''' <summary>Time dependent behaviour (piston movement, timers).</summary>
    Public Overridable Sub UpdateDynamics(sim As Simulator, dt As Double)
    End Sub

    ''' <summary>True if the user can operate this element with the mouse during simulation.</summary>
    <Browsable(False)> Public Overridable ReadOnly Property IsManuallyOperated As Boolean
        Get
            Return False
        End Get
    End Property

    ''' <summary>Mouse pressed on the element during simulation; <paramref name="local"/> is in local coordinates.</summary>
    Public Overridable Sub OnSimMouseDown(local As PointF)
    End Sub

    Public Overridable Sub OnSimMouseUp()
    End Sub

    Public Overrides Function ToString() As String
        Return If(String.IsNullOrEmpty(Label), DisplayName, $"{Label}  ({DisplayName})")
    End Function
End Class
