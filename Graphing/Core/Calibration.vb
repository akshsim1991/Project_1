Imports System.Text.Json.Serialization

''' <summary>A point with double-precision coordinates (image pixels or data values).</summary>
Public Structure PointD
    Public Property X As Double
    Public Property Y As Double

    Public Sub New(x As Double, y As Double)
        Me.X = x
        Me.Y = y
    End Sub

    <JsonIgnore>
    Public ReadOnly Property IsValid As Boolean
        Get
            Return Not (Double.IsNaN(X) OrElse Double.IsNaN(Y) OrElse Double.IsInfinity(X) OrElse Double.IsInfinity(Y))
        End Get
    End Property

    Public Function DistanceTo(other As PointD) As Double
        Dim dx = X - other.X, dy = Y - other.Y
        Return Math.Sqrt(dx * dx + dy * dy)
    End Function

    Public Overrides Function ToString() As String
        Return String.Format(Globalization.CultureInfo.InvariantCulture, "({0:0.###}, {1:0.###})", X, Y)
    End Function
End Structure

''' <summary>One reference point used for calibration: where it is on the image and what value it represents.</summary>
Public Class AxisReference
    ''' <summary>Position on the image, in image pixels. Nothing until the user picks it.</summary>
    Public Property Pixel As PointD?
    ''' <summary>The axis value at that position. Nothing until the user types it.</summary>
    Public Property Value As Double?

    Public Function Clone() As AxisReference
        Return New AxisReference With {.Pixel = Pixel, .Value = Value}
    End Function
End Class

''' <summary>
''' Converts between image pixels and graph values using two reference points on each axis.
''' X1 and X2 are two known values on the X axis, Y1 and Y2 two known values on the Y axis. Because the
''' directions of both axes are measured from the image, a scan that is slightly rotated or skewed is handled
''' correctly, and the axes do not need to start at zero. Either axis can be logarithmic.
''' </summary>
Public Class Calibration
    Public Property X1 As New AxisReference
    Public Property X2 As New AxisReference
    Public Property Y1 As New AxisReference
    Public Property Y2 As New AxisReference
    Public Property LogX As Boolean
    Public Property LogY As Boolean

    ' Solved transform: pixel = Origin + tx(x) * UnitX + ty(y) * UnitY, where tx/ty are identity or log10.
    Private Structure Solution
        Public Origin As PointD
        Public UnitX As PointD
        Public UnitY As PointD
        Public Determinant As Double
    End Structure

    ''' <summary>The four references in a fixed order, with their names, for looping in the UI.</summary>
    Public Function References() As IReadOnlyList(Of (Name As String, Reference As AxisReference))
        Return {("X1", X1), ("X2", X2), ("Y1", Y1), ("Y2", Y2)}
    End Function

    Public Function Clone() As Calibration
        Return New Calibration With {.X1 = X1.Clone(), .X2 = X2.Clone(), .Y1 = Y1.Clone(), .Y2 = Y2.Clone(), .LogX = LogX, .LogY = LogY}
    End Function

    ''' <summary>True when the calibration is complete and consistent.</summary>
    <JsonIgnore>
    Public ReadOnly Property IsComplete As Boolean
        Get
            Return Validate() Is Nothing
        End Get
    End Property

    ''' <summary>Returns a message describing what is missing or wrong, or Nothing when the calibration can be used.</summary>
    Public Function Validate() As String
        Dim solution As Solution
        Return TrySolve(solution)
    End Function

    ''' <summary>Converts an image position to graph values. Returns NaN values when the calibration is not usable.</summary>
    Public Function PixelToData(pixel As PointD) As PointD
        Dim s As Solution
        If TrySolve(s) IsNot Nothing Then Return New PointD(Double.NaN, Double.NaN)
        Dim wx = pixel.X - s.Origin.X, wy = pixel.Y - s.Origin.Y
        Dim tx = (wx * s.UnitY.Y - wy * s.UnitY.X) / s.Determinant
        Dim ty = (s.UnitX.X * wy - s.UnitX.Y * wx) / s.Determinant
        Return New PointD(FromAxis(tx, LogX), FromAxis(ty, LogY))
    End Function

    ''' <summary>Converts graph values to an image position. Returns NaN values when that is impossible.</summary>
    Public Function DataToPixel(data As PointD) As PointD
        Dim s As Solution
        If TrySolve(s) IsNot Nothing Then Return New PointD(Double.NaN, Double.NaN)
        Dim tx = ToAxis(data.X, LogX), ty = ToAxis(data.Y, LogY)
        Return New PointD(s.Origin.X + tx * s.UnitX.X + ty * s.UnitY.X,
                          s.Origin.Y + tx * s.UnitX.Y + ty * s.UnitY.Y)
    End Function

    Private Function TrySolve(ByRef s As Solution) As String
        For Each item In References()
            If Not item.Reference.Pixel.HasValue Then Return String.Format("Pick the {0} point on the image.", item.Name)
        Next
        For Each item In References()
            If Not item.Reference.Value.HasValue Then Return String.Format("Type the value of {0}.", item.Name)
            Dim v = item.Reference.Value.Value
            If Double.IsNaN(v) OrElse Double.IsInfinity(v) Then Return String.Format("The value of {0} is not a number.", item.Name)
        Next
        If LogX AndAlso (X1.Value.Value <= 0 OrElse X2.Value.Value <= 0) Then Return "A logarithmic X axis needs X1 and X2 values above 0."
        If LogY AndAlso (Y1.Value.Value <= 0 OrElse Y2.Value.Value <= 0) Then Return "A logarithmic Y axis needs Y1 and Y2 values above 0."

        Dim ax1 = ToAxis(X1.Value.Value, LogX), ax2 = ToAxis(X2.Value.Value, LogX)
        Dim ay1 = ToAxis(Y1.Value.Value, LogY), ay2 = ToAxis(Y2.Value.Value, LogY)
        If ax1 = ax2 Then Return "X1 and X2 must have different values."
        If ay1 = ay2 Then Return "Y1 and Y2 must have different values."

        Dim p1 = X1.Pixel.Value, p2 = X2.Pixel.Value, p3 = Y1.Pixel.Value, p4 = Y2.Pixel.Value
        If p1.DistanceTo(p2) < 2 Then Return "X1 and X2 are at the same place on the image. Pick two points far apart on the X axis."
        If p3.DistanceTo(p4) < 2 Then Return "Y1 and Y2 are at the same place on the image. Pick two points far apart on the Y axis."

        Dim ux = New PointD((p2.X - p1.X) / (ax2 - ax1), (p2.Y - p1.Y) / (ax2 - ax1))
        Dim uy = New PointD((p4.X - p3.X) / (ay2 - ay1), (p4.Y - p3.Y) / (ay2 - ay1))
        Dim det = ux.X * uy.Y - ux.Y * uy.X
        ' The axes must not point in (nearly) the same direction: compare the sine of the angle between them.
        Dim lengths = Math.Sqrt(ux.X * ux.X + ux.Y * ux.Y) * Math.Sqrt(uy.X * uy.X + uy.Y * uy.Y)
        If lengths = 0 OrElse Math.Abs(det) / lengths < 0.05 Then
            Return "The X points and the Y points run in the same direction. Pick X1/X2 along the X axis and Y1/Y2 along the Y axis."
        End If

        ' X1 lies on some line of constant y and Y1 on some line of constant x; solve for the unknown pair.
        Dim dx = p1.X - p3.X, dy = p1.Y - p3.Y
        Dim a = (dx * uy.Y - dy * uy.X) / det         ' = ax1 - (x value of Y1)
        Dim xOfY1 = ax1 - a
        s.UnitX = ux
        s.UnitY = uy
        s.Determinant = det
        s.Origin = New PointD(p3.X - xOfY1 * ux.X - ay1 * uy.X, p3.Y - xOfY1 * ux.Y - ay1 * uy.Y)
        Return Nothing
    End Function

    Private Shared Function ToAxis(value As Double, isLog As Boolean) As Double
        If Not isLog Then Return value
        Return If(value > 0, Math.Log10(value), Double.NaN)
    End Function

    Private Shared Function FromAxis(value As Double, isLog As Boolean) As Double
        Return If(isLog, Math.Pow(10, value), value)
    End Function
End Class
