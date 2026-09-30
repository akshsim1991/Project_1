''' <summary>Chooses round tick values (like 0, 5, 10 or 1, 10, 100) for drawing a calibrated grid.</summary>
Public NotInheritable Class AxisTicks
    Private Sub New()
    End Sub

    ''' <summary>About <paramref name="targetCount"/> round values covering [min, max]. On a log axis, whole powers of ten (and 2× and 5× when there are few decades).</summary>
    Public Shared Function Generate(min As Double, max As Double, targetCount As Integer, isLog As Boolean) As List(Of Double)
        Dim result As New List(Of Double)
        If Double.IsNaN(min) OrElse Double.IsNaN(max) OrElse Double.IsInfinity(min) OrElse Double.IsInfinity(max) Then Return result
        If min > max Then
            Dim t = min : min = max : max = t
        End If
        If isLog Then
            If max <= 0 Then Return result
            If min <= 0 Then min = max / 1.0E+15
            Dim firstDecade = CInt(Math.Floor(Math.Log10(min))), lastDecade = CInt(Math.Ceiling(Math.Log10(max)))
            Dim decades = lastDecade - firstDecade
            If decades > 60 Then Return result
            Dim multipliers = If(decades <= 3, {1.0, 2.0, 5.0}, {1.0})
            For d = firstDecade To lastDecade
                For Each m In multipliers
                    Dim v = m * Math.Pow(10, d)
                    If v >= min AndAlso v <= max Then result.Add(v)
                Next
            Next
            Return result
        End If

        If max = min Then Return New List(Of Double) From {min}
        Dim rawStep = (max - min) / Math.Max(1, targetCount)
        Dim magnitude = Math.Pow(10, Math.Floor(Math.Log10(rawStep)))
        Dim niceStep = magnitude
        For Each factor In {1.0, 2.0, 2.5, 5.0, 10.0}
            niceStep = factor * magnitude
            If niceStep >= rawStep Then Exit For
        Next
        Dim first = Math.Ceiling(min / niceStep) * niceStep
        Dim i = 0
        While True
            Dim v = first + i * niceStep
            If v > max + niceStep * 0.000001 OrElse i > 1000 Then Exit While
            ' Snap values that should be exactly zero but are 1e-17 because of rounding.
            result.Add(If(Math.Abs(v) < niceStep * 0.000000001, 0.0, v))
            i += 1
        End While
        Return result
    End Function
End Class
