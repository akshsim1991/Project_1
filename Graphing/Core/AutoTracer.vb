''' <summary>An image as a flat array of 32-bit ARGB pixels (row by row).</summary>
Public NotInheritable Class PixelBuffer
    Public ReadOnly Property Width As Integer
    Public ReadOnly Property Height As Integer
    Public ReadOnly Property Pixels As Integer()

    Public Sub New(width As Integer, height As Integer, pixels As Integer())
        If pixels.Length <> width * height Then Throw New ArgumentException("The pixel array does not match the size.", NameOf(pixels))
        Me.Width = width
        Me.Height = height
        Me.Pixels = pixels
    End Sub

    Public Function GetArgb(x As Integer, y As Integer) As Integer
        Return Pixels(y * Width + x)
    End Function

    Public Function Contains(x As Integer, y As Integer) As Boolean
        Return x >= 0 AndAlso y >= 0 AndAlso x < Width AndAlso y < Height
    End Function
End Class

''' <summary>Settings for automatic tracing.</summary>
Public Class AutoTraceOptions
    ''' <summary>How far (0–442, distance in RGB space) a pixel's color may be from the curve color and still count.</summary>
    Public Property ColorTolerance As Integer = 60
    ''' <summary>Horizontal distance between extracted points, in image pixels.</summary>
    Public Property StepPixels As Integer = 5
    ''' <summary>How far the curve may jump vertically between neighbouring columns before tracing stops, in pixels.</summary>
    Public Property MaxJumpPixels As Integer = 40
    ''' <summary>How many pixels of gap (e.g. a dashed line or a label over the curve) tracing will bridge.</summary>
    Public Property MaxGapPixels As Integer = 30
End Class

''' <summary>
''' Follows a curve of one color left and right from a starting point, one column at a time, and returns points
''' along its centre line. Designed for curves that are functions of X (one Y per X), which is how most
''' research plots are drawn.
''' </summary>
Public NotInheritable Class AutoTracer
    Private Sub New()
    End Sub

    ''' <summary>
    ''' Chooses the color to trace from a click: the clicked pixel, or, if the click landed on light background,
    ''' the most strongly colored pixel within a few pixels of it.
    ''' </summary>
    Public Shared Function PickCurveColor(image As PixelBuffer, x As Integer, y As Integer) As Integer?
        If Not image.Contains(x, y) Then Return Nothing
        Dim clicked = image.GetArgb(x, y)
        If Not IsBackgroundLike(clicked) Then Return clicked
        Dim best As Integer? = Nothing
        Dim bestScore = -1.0
        For dy = -5 To 5
            For dx = -5 To 5
                If Not image.Contains(x + dx, y + dy) Then Continue For
                Dim c = image.GetArgb(x + dx, y + dy)
                If IsBackgroundLike(c) Then Continue For
                ' Prefer pixels that are dark or saturated, and close to the click.
                Dim score = Ink(c) - Math.Sqrt(dx * dx + dy * dy) * 4
                If score > bestScore Then
                    bestScore = score
                    best = c
                End If
            Next
        Next
        Return best
    End Function

    ''' <summary>
    ''' Traces the curve of <paramref name="curveArgb"/> that passes near (<paramref name="startX"/>, <paramref name="startY"/>),
    ''' limited to the rectangle <paramref name="bounds"/> (left, top, right, bottom; inclusive). Returns image-pixel points
    ''' ordered left to right, or an empty list if no curve of that color is near the start point.
    ''' </summary>
    Public Shared Function Trace(image As PixelBuffer, startX As Integer, startY As Integer, curveArgb As Integer,
                                 options As AutoTraceOptions, Optional bounds As (Left As Integer, Top As Integer, Right As Integer, Bottom As Integer)? = Nothing) As List(Of PointD)
        Dim area As (Left As Integer, Top As Integer, Right As Integer, Bottom As Integer) =
            If(bounds.HasValue, bounds.Value, (0, 0, image.Width - 1, image.Height - 1))
        Dim left = Math.Max(0, area.Left), right = Math.Min(image.Width - 1, area.Right)
        Dim top = Math.Max(0, area.Top), bottom = Math.Min(image.Height - 1, area.Bottom)
        Dim result As New List(Of PointD)
        If startX < left OrElse startX > right Then Return result

        Dim stepPx = Math.Max(1, options.StepPixels)
        Dim tolerance2 = CLng(options.ColorTolerance) * options.ColorTolerance

        ' Find the run of curve pixels in the start column that is closest to the click.
        Dim startCentre = NearestRunCentre(image, startX, startY, top, bottom, curveArgb, tolerance2, 12)
        If Not startCentre.HasValue Then Return result

        Dim rightPoints = Follow(image, startX, startCentre.Value, stepPx, right, top, bottom, curveArgb, tolerance2, options)
        Dim leftPoints = Follow(image, startX, startCentre.Value, -stepPx, left, top, bottom, curveArgb, tolerance2, options)
        leftPoints.Reverse()
        result.AddRange(leftPoints)
        result.Add(New PointD(startX, startCentre.Value))
        result.AddRange(rightPoints)
        Return result
    End Function

    Private Shared Function Follow(image As PixelBuffer, startX As Integer, startY As Double, stepPx As Integer, limitX As Integer,
                                   top As Integer, bottom As Integer, curveArgb As Integer, tolerance2 As Long,
                                   options As AutoTraceOptions) As List(Of PointD)
        Dim points As New List(Of PointD)
        Dim previousY = startY
        Dim gap = 0
        Dim x = startX + stepPx
        While If(stepPx > 0, x <= limitX, x >= limitX)
            ' Allow a bigger jump after a gap, because the curve may have moved while hidden.
            Dim allowed = options.MaxJumpPixels + gap
            Dim centre = NearestRunCentre(image, x, previousY, top, bottom, curveArgb, tolerance2, allowed)
            If centre.HasValue Then
                points.Add(New PointD(x, centre.Value))
                previousY = centre.Value
                gap = 0
            Else
                gap += Math.Abs(stepPx)
                If gap > options.MaxGapPixels Then Exit While
            End If
            x += stepPx
        End While
        Return points
    End Function

    ''' <summary>The centre of the run of matching pixels in column <paramref name="x"/> nearest to <paramref name="nearY"/>, within <paramref name="maxDistance"/>.</summary>
    Private Shared Function NearestRunCentre(image As PixelBuffer, x As Integer, nearY As Double, top As Integer, bottom As Integer,
                                             curveArgb As Integer, tolerance2 As Long, maxDistance As Double) As Double?
        Dim best As Double? = Nothing
        Dim bestDistance = Double.MaxValue
        Dim y = Math.Max(top, CInt(Math.Floor(nearY - maxDistance)))
        Dim lastY = Math.Min(bottom, CInt(Math.Ceiling(nearY + maxDistance)))
        While y <= lastY
            If Matches(image.GetArgb(x, y), curveArgb, tolerance2) Then
                Dim runStart = y
                While y + 1 <= bottom AndAlso Matches(image.GetArgb(x, y + 1), curveArgb, tolerance2)
                    y += 1
                End While
                ' Extend upwards too in case the run started above the search window.
                Dim runTop = runStart
                While runTop - 1 >= top AndAlso Matches(image.GetArgb(x, runTop - 1), curveArgb, tolerance2)
                    runTop -= 1
                End While
                Dim centre = (runTop + y) / 2.0
                Dim distance = Math.Abs(centre - nearY)
                If distance <= maxDistance AndAlso distance < bestDistance Then
                    bestDistance = distance
                    best = centre
                End If
            End If
            y += 1
        End While
        Return best
    End Function

    Public Shared Function ColorDistance(a As Integer, b As Integer) As Double
        Return Math.Sqrt(ColorDistance2(a, b))
    End Function

    Private Shared Function Matches(pixel As Integer, target As Integer, tolerance2 As Long) As Boolean
        Return ColorDistance2(pixel, target) <= tolerance2
    End Function

    Private Shared Function ColorDistance2(a As Integer, b As Integer) As Long
        Dim dr = ((a >> 16) And &HFF) - ((b >> 16) And &HFF)
        Dim dg = ((a >> 8) And &HFF) - ((b >> 8) And &HFF)
        Dim db = (a And &HFF) - (b And &HFF)
        Return CLng(dr) * dr + CLng(dg) * dg + CLng(db) * db
    End Function

    Private Shared Function IsBackgroundLike(argb As Integer) As Boolean
        Return Ink(argb) < 40
    End Function

    ''' <summary>How much a pixel stands out from white paper: darkness plus saturation, 0–255-ish.</summary>
    Private Shared Function Ink(argb As Integer) As Double
        Dim r = (argb >> 16) And &HFF, g = (argb >> 8) And &HFF, b = argb And &HFF
        Dim max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b))
        Return (255 - max) + (max - min)
    End Function
End Class
