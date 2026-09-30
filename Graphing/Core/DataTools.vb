Imports System.Globalization

''' <summary>Summary numbers for one curve.</summary>
Public Class SeriesStatistics
    Public Property Count As Integer
    Public Property MinX As Double
    Public Property MaxX As Double
    Public Property MinY As Double
    Public Property MaxY As Double
    ''' <summary>Area between the curve and y = 0 over its X range (trapezoidal rule, points sorted by X).</summary>
    Public Property Area As Double
End Class

''' <summary>Clean-up, interpolation and statistics for traced curves. Works in graph values, not pixels.</summary>
Public NotInheritable Class DataTools
    Private Sub New()
    End Sub

    ''' <summary>Reorders the series so its points run from the smallest to the largest X value.</summary>
    Public Shared Sub SortByX(series As DataSeries, calibration As Calibration)
        series.Points = series.Points.
            Select(Function(p) (Pixel:=p, Data:=calibration.PixelToData(p))).
            OrderBy(Function(t) t.Data.X).
            Select(Function(t) t.Pixel).
            ToList()
    End Sub

    ''' <summary>Removes points that sit on the same image pixel as an earlier point. Returns how many were removed.</summary>
    Public Shared Function RemoveDuplicates(series As DataSeries) As Integer
        Dim seen As New HashSet(Of (Integer, Integer))
        Dim kept As New List(Of PointD)
        For Each p In series.Points
            If seen.Add((CInt(Math.Round(p.X)), CInt(Math.Round(p.Y)))) Then kept.Add(p)
        Next
        Dim removed = series.Points.Count - kept.Count
        series.Points = kept
        Return removed
    End Function

    ''' <summary>
    ''' Replaces the series with <paramref name="count"/> points evenly spaced along X (evenly on a log scale for a
    ''' logarithmic X axis), reading each Y from straight lines between the traced points.
    ''' </summary>
    Public Shared Sub ResampleByCount(series As DataSeries, calibration As Calibration, count As Integer)
        If count < 2 Then Throw New ArgumentOutOfRangeException(NameOf(count), "At least 2 points are needed.")
        Dim curve = AxisCurve(series, calibration)
        If curve.Count < 2 Then Throw New InvalidOperationException("The series needs at least 2 points with different X values.")
        Dim first = curve(0).X, last = curve(curve.Count - 1).X
        Dim xs = Enumerable.Range(0, count).Select(Function(i) first + (last - first) * i / (count - 1))
        ReplaceWithAxisValues(series, calibration, curve, xs)
    End Sub

    ''' <summary>Replaces the series with points every <paramref name="stepX"/> X units (linear X axes only).</summary>
    Public Shared Sub ResampleByStep(series As DataSeries, calibration As Calibration, stepX As Double)
        If calibration.LogX Then Throw New InvalidOperationException("Resampling by a fixed step needs a linear X axis. Use a number of points instead.")
        If Not (stepX > 0) Then Throw New ArgumentOutOfRangeException(NameOf(stepX), "The step must be greater than 0.")
        Dim curve = AxisCurve(series, calibration)
        If curve.Count < 2 Then Throw New InvalidOperationException("The series needs at least 2 points with different X values.")
        Dim first = curve(0).X, last = curve(curve.Count - 1).X
        Dim total = CLng(Math.Floor((last - first) / stepX + 0.000000001)) + 1
        If total > 100000 Then Throw New InvalidOperationException(String.Format("That step would create {0:N0} points. Use a larger step.", total))
        ' Start from a round multiple of the step so values line up nicely (e.g. 0, 0.5, 1.0 ...).
        Dim start = Math.Ceiling(first / stepX - 0.000000001) * stepX
        Dim xs As New List(Of Double)
        Dim x = start
        While x <= last + stepX * 0.000000001
            xs.Add(x)
            x = start + xs.Count * stepX
        End While
        If xs.Count < 1 Then Throw New InvalidOperationException("The step is larger than the X range of the series.")
        ReplaceWithAxisValues(series, calibration, curve, xs)
    End Sub

    ''' <summary>The Y value at <paramref name="x"/>, read from straight lines between the traced points, or Nothing outside the traced range.</summary>
    Public Shared Function InterpolateAt(series As DataSeries, calibration As Calibration, x As Double) As Double?
        If calibration.LogX AndAlso x <= 0 Then Return Nothing
        Dim curve = AxisCurve(series, calibration)
        If curve.Count < 2 Then Return Nothing
        Dim ax = If(calibration.LogX, Math.Log10(x), x)
        Dim ay = InterpolateSorted(curve, ax)
        If Not ay.HasValue Then Return Nothing
        Return If(calibration.LogY, Math.Pow(10, ay.Value), ay.Value)
    End Function

    Public Shared Function Statistics(series As DataSeries, calibration As Calibration) As SeriesStatistics
        Dim data = series.DataPoints(calibration).Where(Function(p) p.IsValid).OrderBy(Function(p) p.X).ToList()
        If data.Count = 0 Then Return New SeriesStatistics()
        Dim area = 0.0
        For i = 1 To data.Count - 1
            area += (data(i).X - data(i - 1).X) * (data(i).Y + data(i - 1).Y) / 2
        Next
        Return New SeriesStatistics With {
            .Count = data.Count,
            .MinX = data.Min(Function(p) p.X), .MaxX = data.Max(Function(p) p.X),
            .MinY = data.Min(Function(p) p.Y), .MaxY = data.Max(Function(p) p.Y),
            .Area = area
        }
    End Function

    ''' <summary>Parses a number typed by the user in either the local format (e.g. 0,5) or the international one (0.5), including 1e-3.</summary>
    Public Shared Function TryParseNumber(text As String, ByRef value As Double) As Boolean
        If String.IsNullOrWhiteSpace(text) Then Return False
        ' No thousands separators: in many locales "1.5" would otherwise be read as 15.
        Dim styles = NumberStyles.Float
        Dim trimmed = text.Trim()
        Return Double.TryParse(trimmed, styles, CultureInfo.CurrentCulture, value) AndAlso Not Double.IsNaN(value) AndAlso Not Double.IsInfinity(value) OrElse
               Double.TryParse(trimmed, styles, CultureInfo.InvariantCulture, value) AndAlso Not Double.IsNaN(value) AndAlso Not Double.IsInfinity(value)
    End Function

    ''' <summary>Formats a value for display with up to 6 significant digits.</summary>
    Public Shared Function FormatNumber(value As Double) As String
        If Double.IsNaN(value) Then Return "—"
        Return value.ToString("G6", CultureInfo.CurrentCulture)
    End Function

    ' The curve in "axis space" (log10 applied on logarithmic axes), sorted by X, without invalid points or repeated X values.
    Private Shared Function AxisCurve(series As DataSeries, calibration As Calibration) As List(Of PointD)
        Dim result As New List(Of PointD)
        For Each p In series.DataPoints(calibration)
            If Not p.IsValid Then Continue For
            If calibration.LogX AndAlso p.X <= 0 Then Continue For
            If calibration.LogY AndAlso p.Y <= 0 Then Continue For
            result.Add(New PointD(If(calibration.LogX, Math.Log10(p.X), p.X), If(calibration.LogY, Math.Log10(p.Y), p.Y)))
        Next
        result.Sort(Function(a, b) a.X.CompareTo(b.X))
        ' Average points that share the same X so interpolation has a single value there.
        Dim merged As New List(Of PointD)
        Dim i = 0
        While i < result.Count
            Dim j = i
            Dim sumY = 0.0
            While j < result.Count AndAlso result(j).X = result(i).X
                sumY += result(j).Y
                j += 1
            End While
            merged.Add(New PointD(result(i).X, sumY / (j - i)))
            i = j
        End While
        Return merged
    End Function

    Private Shared Function InterpolateSorted(curve As List(Of PointD), x As Double) As Double?
        If x < curve(0).X OrElse x > curve(curve.Count - 1).X Then Return Nothing
        ' Binary search for the segment containing x.
        Dim lo = 0, hi = curve.Count - 1
        While hi - lo > 1
            Dim mid = (lo + hi) \ 2
            If curve(mid).X <= x Then lo = mid Else hi = mid
        End While
        Dim a = curve(lo), b = curve(hi)
        If b.X = a.X Then Return a.Y
        Return a.Y + (b.Y - a.Y) * (x - a.X) / (b.X - a.X)
    End Function

    Private Shared Sub ReplaceWithAxisValues(series As DataSeries, calibration As Calibration, curve As List(Of PointD), axisXs As IEnumerable(Of Double))
        Dim points As New List(Of PointD)
        For Each ax In axisXs
            Dim ay = InterpolateSorted(curve, Math.Min(Math.Max(ax, curve(0).X), curve(curve.Count - 1).X))
            If Not ay.HasValue Then Continue For
            Dim data = New PointD(If(calibration.LogX, Math.Pow(10, ax), ax), If(calibration.LogY, Math.Pow(10, ay.Value), ay.Value))
            Dim pixel = calibration.DataToPixel(data)
            If pixel.IsValid Then points.Add(pixel)
        Next
        series.Points = points
    End Sub
End Class
