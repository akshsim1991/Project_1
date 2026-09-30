Imports System.Drawing.Drawing2D
Imports System.Drawing.Imaging
Imports System.IO

''' <summary>
''' One picture added to the collage. Keeps only small copies in memory (for the live preview and the list
''' thumbnail); the full-size photo is re-read from disk when the collage is saved. Files are never locked.
''' </summary>
Public NotInheritable Class CollageImage
    Implements IDisposable

    ''' <summary>Longest side of the in-memory copy used for the live preview.</summary>
    Private Const PreviewMaxSide As Integer = 800
    Private Const ExifOrientationId As Integer = &H112

    Public Shared ReadOnly ThumbnailSize As New Size(56, 56)
    Public Shared ReadOnly ThumbnailBackground As Color = Color.FromArgb(30, 30, 30)

    Public ReadOnly Property FilePath As String
    ''' <summary>Unique key, used for the thumbnail in the ListView's ImageList.</summary>
    Public ReadOnly Property Key As String = Guid.NewGuid().ToString("N")
    ''' <summary>Pixel size of the original photo, after applying its camera rotation.</summary>
    Public ReadOnly Property OriginalSize As Size
    Public ReadOnly Property Preview As Bitmap
    Public ReadOnly Property Thumbnail As Bitmap

    Private Sub New(filePath As String, originalSize As Size, preview As Bitmap, thumbnail As Bitmap)
        Me.FilePath = filePath
        Me.OriginalSize = originalSize
        Me.Preview = preview
        Me.Thumbnail = thumbnail
    End Sub

    Public ReadOnly Property FileName As String
        Get
            Return Path.GetFileName(FilePath)
        End Get
    End Property

    ''' <summary>Reads an image file and prepares its preview and thumbnail. Throws if the file can't be used.</summary>
    Public Shared Function Load(filePath As String) As CollageImage
        Using full = ReadImage(filePath)
            Dim preview = ResizeToFit(full, PreviewMaxSide)
            Try
                Dim thumbnail = MakeThumbnail(preview)
                Return New CollageImage(filePath, full.Size, preview, thumbnail)
            Catch
                preview.Dispose()
                Throw
            End Try
        End Using
    End Function

    ''' <summary>Reads the full-size photo from disk. The caller must dispose it.</summary>
    Public Function LoadFullImage() As Image
        Return ReadImage(FilePath)
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        Preview.Dispose()
        Thumbnail.Dispose()
    End Sub

    ''' <summary>
    ''' Loads an image from a copy of the file's bytes so the file stays unlocked, and turns it upright
    ''' according to its EXIF orientation (photos from phones are often stored sideways).
    ''' </summary>
    Private Shared Function ReadImage(filePath As String) As Image
        Dim bytes = File.ReadAllBytes(filePath)
        Dim img As Image
        Try
            ' GDI+ needs the stream for the image's whole lifetime; a MemoryStream holds no OS resources,
            ' so it is simply left for the garbage collector together with the image.
            img = Image.FromStream(New MemoryStream(bytes), True, True)
        Catch ex As ArgumentException
            Throw New InvalidDataException("This is not a valid or supported image file.", ex)
        Catch ex As OutOfMemoryException
            Throw New InvalidDataException("The image is damaged or too large to open.", ex)
        End Try

        Try
            ApplyExifOrientation(img)
            Return img
        Catch
            img.Dispose()
            Throw
        End Try
    End Function

    Private Shared Sub ApplyExifOrientation(img As Image)
        If Array.IndexOf(img.PropertyIdList, ExifOrientationId) < 0 Then Return
        Dim value = img.GetPropertyItem(ExifOrientationId).Value
        If value Is Nothing OrElse value.Length = 0 Then Return
        Dim orientation = If(value.Length >= 2, BitConverter.ToUInt16(value, 0), CUShort(value(0)))

        Dim rotation As RotateFlipType
        Select Case orientation
            Case 2 : rotation = RotateFlipType.RotateNoneFlipX
            Case 3 : rotation = RotateFlipType.Rotate180FlipNone
            Case 4 : rotation = RotateFlipType.Rotate180FlipX
            Case 5 : rotation = RotateFlipType.Rotate90FlipX
            Case 6 : rotation = RotateFlipType.Rotate90FlipNone
            Case 7 : rotation = RotateFlipType.Rotate270FlipX
            Case 8 : rotation = RotateFlipType.Rotate270FlipNone
            Case Else : Return
        End Select
        img.RotateFlip(rotation)
        ' The pixels are now upright; drop the tag so nothing rotates them a second time.
        img.RemovePropertyItem(ExifOrientationId)
    End Sub

    Private Shared Function ResizeToFit(source As Image, maxSide As Integer) As Bitmap
        Dim ratio = Math.Min(1.0, maxSide / Math.Max(source.Width, source.Height))
        Dim width = Math.Max(1, CInt(Math.Round(source.Width * ratio)))
        Dim height = Math.Max(1, CInt(Math.Round(source.Height * ratio)))
        Dim result As New Bitmap(width, height, PixelFormat.Format32bppPArgb)
        Using g = Graphics.FromImage(result), attributes As New ImageAttributes()
            g.InterpolationMode = InterpolationMode.HighQualityBicubic
            g.PixelOffsetMode = PixelOffsetMode.HighQuality
            attributes.SetWrapMode(WrapMode.TileFlipXY)
            g.DrawImage(source, New Rectangle(0, 0, width, height), 0, 0, source.Width, source.Height, GraphicsUnit.Pixel, attributes)
        End Using
        Return result
    End Function

    ''' <summary>A square thumbnail with the whole picture centred on a dark background.</summary>
    Private Shared Function MakeThumbnail(source As Image) As Bitmap
        Dim result As New Bitmap(ThumbnailSize.Width, ThumbnailSize.Height, PixelFormat.Format32bppArgb)
        Using g = Graphics.FromImage(result)
            g.Clear(ThumbnailBackground)
            g.InterpolationMode = InterpolationMode.HighQualityBicubic
            g.PixelOffsetMode = PixelOffsetMode.HighQuality
            Dim ratio = Math.Min(ThumbnailSize.Width / source.Width, ThumbnailSize.Height / source.Height)
            Dim width = Math.Max(1, CInt(Math.Round(source.Width * ratio)))
            Dim height = Math.Max(1, CInt(Math.Round(source.Height * ratio)))
            g.DrawImage(source, New Rectangle((ThumbnailSize.Width - width) \ 2, (ThumbnailSize.Height - height) \ 2, width, height))
        End Using
        Return result
    End Function
End Class
