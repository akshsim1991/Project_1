Imports System.Drawing.Drawing2D
Imports System.Drawing.Imaging
Imports System.IO

''' <summary>How an image is placed inside its grid cell.</summary>
Public Enum FitMode
    ''' <summary>Scale the image to cover the whole cell, cropping the edges that stick out.</summary>
    Fill = 0
    ''' <summary>Scale the image so all of it is visible; the background shows around it.</summary>
    Fit = 1
End Enum

''' <summary>Every setting that affects how a collage looks.</summary>
Public Class CollageOptions
    Public Property Rows As Integer = 2
    Public Property Columns As Integer = 2
    Public Property Mode As FitMode = FitMode.Fill
    ''' <summary>Cell width divided by cell height.</summary>
    Public Property CellAspect As Double = 1.0
    Public Property OutputWidth As Integer = 2000
    Public Property Spacing As Integer = 10
    Public Property Margin As Integer = 10
    Public Property BorderEnabled As Boolean
    Public Property BorderThickness As Integer = 4
    Public Property BorderColor As Color = Color.Black
    Public Property BackgroundColor As Color = Color.White

    Public ReadOnly Property CellCount As Integer
        Get
            Return Math.Max(1, Rows) * Math.Max(1, Columns)
        End Get
    End Property
End Class

''' <summary>The computed position of every cell for a given set of options.</summary>
Public Class CollageLayout
    Public Property CanvasSize As Size
    Public Property BorderSize As Integer
    Public ReadOnly Property Cells As New List(Of Rectangle)
End Class

''' <summary>Lays out and draws collages. Contains no UI code.</summary>
Public NotInheritable Class CollageRenderer

    ''' <summary>Largest width or height of a saved collage, in pixels.</summary>
    Public Const MaxSide As Integer = 20000
    ''' <summary>Largest total pixel count of a saved collage (about 300 MB in memory).</summary>
    Public Const MaxPixels As Long = 100000000L
    ''' <summary>Smallest useful size of the picture area inside a cell, in pixels.</summary>
    Private Const MinImageArea As Integer = 8

    Private Sub New()
    End Sub

    ''' <summary>Suggests a grid (Width = columns, Height = rows) that fits the given number of images.</summary>
    Public Shared Function SuggestGrid(imageCount As Integer) As Size
        If imageCount <= 1 Then Return New Size(1, 1)
        Dim columns = CInt(Math.Ceiling(Math.Sqrt(imageCount)))
        Dim rows = CInt(Math.Ceiling(imageCount / columns))
        Return New Size(columns, rows)
    End Function

    ''' <summary>
    ''' Checks whether a full-size collage can be made with these options.
    ''' Returns a message explaining the problem, or Nothing when everything is fine.
    ''' </summary>
    Public Shared Function Validate(options As CollageOptions) As String
        Dim columns = Math.Max(1, options.Columns)
        Dim border = If(options.BorderEnabled, Math.Max(1, options.BorderThickness), 0)
        Dim cellWidth = (options.OutputWidth - 2 * options.Margin - (columns - 1) * options.Spacing) \ columns
        If cellWidth - 2 * border < MinImageArea Then
            Return String.Format("The gap, margin and border are too large for a {0} px wide collage with {1} columns. " &
                                 "Reduce them, use fewer columns, or increase the width.", options.OutputWidth, columns)
        End If

        Dim canvas = ComputeLayout(options, 1.0).CanvasSize
        If canvas.Width > MaxSide OrElse canvas.Height > MaxSide OrElse CLng(canvas.Width) * canvas.Height > MaxPixels Then
            Return String.Format("The collage would be {0:N0} × {1:N0} px, which is too large. " &
                                 "Reduce the width, the number of rows, or pick a wider cell shape.", canvas.Width, canvas.Height)
        End If
        Return Nothing
    End Function

    ''' <summary>
    ''' Computes the canvas size and cell rectangles. <paramref name="scale"/> shrinks everything
    ''' proportionally, which is used to draw the on-screen preview.
    ''' </summary>
    Public Shared Function ComputeLayout(options As CollageOptions, scale As Double) As CollageLayout
        Dim columns = Math.Max(1, options.Columns)
        Dim rows = Math.Max(1, options.Rows)
        Dim margin = ScaleValue(options.Margin, scale, 0)
        Dim spacing = ScaleValue(options.Spacing, scale, 0)
        Dim border = If(options.BorderEnabled, ScaleValue(options.BorderThickness, scale, 1), 0)
        Dim width = Math.Max(1, CInt(Math.Round(options.OutputWidth * scale)))
        Dim aspect = If(options.CellAspect > 0, options.CellAspect, 1.0)

        Dim cellWidth = Math.Max(1, (width - 2 * margin - (columns - 1) * spacing) \ columns)
        Dim cellHeight = Math.Max(1, CInt(Math.Round(cellWidth / aspect)))

        ' Integer division can leave a few spare pixels; split them evenly between the left and right margins
        ' so the canvas is exactly the requested width.
        Dim usedWidth = 2 * margin + columns * cellWidth + (columns - 1) * spacing
        Dim left = margin + Math.Max(0, width - usedWidth) \ 2

        Dim layout As New CollageLayout With {
            .CanvasSize = New Size(Math.Max(width, usedWidth), 2 * margin + rows * cellHeight + (rows - 1) * spacing),
            .BorderSize = border
        }
        For row = 0 To rows - 1
            For column = 0 To columns - 1
                layout.Cells.Add(New Rectangle(left + column * (cellWidth + spacing),
                                               margin + row * (cellHeight + spacing),
                                               cellWidth, cellHeight))
            Next
        Next
        Return layout
    End Function

    ''' <summary>
    ''' Draws a collage. Images are requested one at a time through <paramref name="getImage"/>, so the
    ''' caller can load full-size photos from disk only while they are being drawn.
    ''' </summary>
    ''' <param name="disposeAfterDraw">True to dispose each image as soon as it has been drawn.</param>
    Public Shared Function Render(options As CollageOptions, scale As Double, imageCount As Integer,
                                  getImage As Func(Of Integer, Image), disposeAfterDraw As Boolean) As Bitmap
        Dim layout = ComputeLayout(options, scale)
        Dim canvas As New Bitmap(layout.CanvasSize.Width, layout.CanvasSize.Height, PixelFormat.Format24bppRgb)
        Try
            Using g = Graphics.FromImage(canvas), attributes As New ImageAttributes()
                g.Clear(Color.FromArgb(255, options.BackgroundColor))
                g.InterpolationMode = InterpolationMode.HighQualityBicubic
                g.PixelOffsetMode = PixelOffsetMode.HighQuality
                g.CompositingQuality = CompositingQuality.HighQuality
                g.SmoothingMode = SmoothingMode.HighQuality
                ' Stops a faint semi-transparent line appearing along the edges of scaled images.
                attributes.SetWrapMode(WrapMode.TileFlipXY)

                Dim count = Math.Min(imageCount, layout.Cells.Count)
                For i = 0 To count - 1
                    Dim img = getImage(i)
                    Try
                        DrawCell(g, img, layout.Cells(i), options, layout.BorderSize, attributes)
                    Finally
                        If disposeAfterDraw Then img.Dispose()
                    End Try
                Next
            End Using
            Return canvas
        Catch
            canvas.Dispose()
            Throw
        End Try
    End Function

    ''' <summary>Saves the collage in the format that matches the file extension (.jpg, .png or .bmp).</summary>
    Public Shared Sub Save(collage As Bitmap, filePath As String, jpegQuality As Integer)
        Select Case Path.GetExtension(filePath).ToLowerInvariant()
            Case ".png"
                collage.Save(filePath, ImageFormat.Png)
            Case ".bmp"
                collage.Save(filePath, ImageFormat.Bmp)
            Case Else
                Dim jpegCodec = ImageCodecInfo.GetImageEncoders().First(Function(c) c.FormatID = ImageFormat.Jpeg.Guid)
                Using parameters As New EncoderParameters(1)
                    parameters.Param(0) = New EncoderParameter(System.Drawing.Imaging.Encoder.Quality,
                                                               CLng(Math.Max(1, Math.Min(100, jpegQuality))))
                    collage.Save(filePath, jpegCodec, parameters)
                End Using
        End Select
    End Sub

    Private Shared Sub DrawCell(g As Graphics, img As Image, cell As Rectangle, options As CollageOptions,
                                border As Integer, attributes As ImageAttributes)
        Dim available = Rectangle.Inflate(cell, -border, -border)
        If available.Width < 1 OrElse available.Height < 1 OrElse img.Width < 1 OrElse img.Height < 1 Then Return

        Dim destination As Rectangle
        Dim source As RectangleF
        If options.Mode = FitMode.Fill Then
            destination = available
            source = CropToAspect(img.Size, available.Width / available.Height)
        Else
            destination = FitInside(img.Size, available)
            source = New RectangleF(0, 0, img.Width, img.Height)
        End If

        If border > 0 Then
            Using brush As New SolidBrush(options.BorderColor)
                g.FillRectangle(brush, Rectangle.Inflate(destination, border, border))
            End Using
        End If
        g.DrawImage(img, destination, source.X, source.Y, source.Width, source.Height, GraphicsUnit.Pixel, attributes)
    End Sub

    ''' <summary>The centred part of an image that has the given aspect ratio.</summary>
    Private Shared Function CropToAspect(imageSize As Size, aspect As Double) As RectangleF
        Dim imageAspect = imageSize.Width / imageSize.Height
        If imageAspect > aspect Then
            Dim width = CSng(imageSize.Height * aspect)
            Return New RectangleF((imageSize.Width - width) / 2.0F, 0, width, imageSize.Height)
        Else
            Dim height = CSng(imageSize.Width / aspect)
            Return New RectangleF(0, (imageSize.Height - height) / 2.0F, imageSize.Width, height)
        End If
    End Function

    ''' <summary>The largest rectangle with the image's shape that fits, centred, inside <paramref name="area"/>.</summary>
    Private Shared Function FitInside(imageSize As Size, area As Rectangle) As Rectangle
        Dim ratio = Math.Min(area.Width / imageSize.Width, area.Height / imageSize.Height)
        Dim width = Math.Max(1, CInt(Math.Round(imageSize.Width * ratio)))
        Dim height = Math.Max(1, CInt(Math.Round(imageSize.Height * ratio)))
        Return New Rectangle(area.X + (area.Width - width) \ 2, area.Y + (area.Height - height) \ 2, width, height)
    End Function

    Private Shared Function ScaleValue(value As Integer, scale As Double, minimum As Integer) As Integer
        If value <= 0 Then Return minimum
        Return Math.Max(minimum, CInt(Math.Round(value * scale)))
    End Function
End Class
