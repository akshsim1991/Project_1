Imports System.Drawing.Imaging
Imports System.IO
Imports System.Runtime.InteropServices

''' <summary>Records frames and writes an animated GIF (GIF89a, looping, fixed 256-colour palette).</summary>
Public Class GifRecorder
    Private Const MaxFrames As Integer = 900
    Private ReadOnly _frames As New List(Of Byte())
    Private ReadOnly _palette As Byte() = BuildPalette()

    Public ReadOnly Property Width As Integer
    Public ReadOnly Property Height As Integer
    ''' <summary>Delay between frames in hundredths of a second.</summary>
    Public ReadOnly Property DelayCs As Integer

    Public Sub New(width As Integer, height As Integer, Optional delayCs As Integer = 8)
        Me.Width = width
        Me.Height = height
        Me.DelayCs = delayCs
    End Sub

    Public ReadOnly Property FrameCount As Integer
        Get
            Return _frames.Count
        End Get
    End Property

    ''' <summary>6 x 7 x 6 colour cube plus greys.</summary>
    Private Shared Function BuildPalette() As Byte()
        Dim p(256 * 3 - 1) As Byte
        Dim i = 0
        For r = 0 To 5
            For gr = 0 To 6
                For b = 0 To 5
                    p(i * 3) = CByte(r * 51) : p(i * 3 + 1) = CByte(gr * 255 \ 6) : p(i * 3 + 2) = CByte(b * 51)
                    i += 1
                Next
            Next
        Next
        While i < 256
            Dim v = CByte((i - 252) * 64 + 32)
            p(i * 3) = v : p(i * 3 + 1) = v : p(i * 3 + 2) = v
            i += 1
        End While
        Return p
    End Function

    Private Shared Function IndexOf(r As Integer, gr As Integer, b As Integer) As Byte
        Return CByte(((r + 25) \ 51) * 42 + ((gr * 6 + 127) \ 255) * 6 + (b + 25) \ 51)
    End Function

    Public Function AddFrame(bmp As Bitmap) As Boolean
        If _frames.Count >= MaxFrames Then Return False
        Dim rect As New Rectangle(0, 0, Width, Height)
        Using frame As New Bitmap(Width, Height, PixelFormat.Format24bppRgb)
            Using g = Graphics.FromImage(frame)
                g.Clear(Color.White)
                g.DrawImage(bmp, 0, 0, Width, Height)
            End Using
            Dim data = frame.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb)
            Dim raw(data.Stride * Height - 1) As Byte
            Marshal.Copy(data.Scan0, raw, 0, raw.Length)
            frame.UnlockBits(data)
            Dim idx(Width * Height - 1) As Byte
            For y = 0 To Height - 1
                Dim row = y * data.Stride
                For x = 0 To Width - 1
                    Dim o = row + x * 3
                    idx(y * Width + x) = IndexOf(raw(o + 2), raw(o + 1), raw(o))
                Next
            Next
            _frames.Add(idx)
        End Using
        Return True
    End Function

    Public Sub Save(path As String)
        Using fs As New FileStream(path, FileMode.Create), w As New BinaryWriter(fs)
            w.Write(Text.Encoding.ASCII.GetBytes("GIF89a"))
            w.Write(CUShort(Width)) : w.Write(CUShort(Height))
            w.Write(CByte(&HF7)) : w.Write(CByte(0)) : w.Write(CByte(0)) ' global colour table, 256 colours
            w.Write(_palette)
            ' Loop forever.
            w.Write(New Byte() {&H21, &HFF, &HB})
            w.Write(Text.Encoding.ASCII.GetBytes("NETSCAPE2.0"))
            w.Write(New Byte() {3, 1, 0, 0, 0})
            For Each f In _frames
                w.Write(New Byte() {&H21, &HF9, 4, 0}) : w.Write(CUShort(DelayCs)) : w.Write(New Byte() {0, 0})
                w.Write(CByte(&H2C)) : w.Write(CUShort(0)) : w.Write(CUShort(0)) : w.Write(CUShort(Width)) : w.Write(CUShort(Height)) : w.Write(CByte(0))
                w.Write(CByte(8))
                Dim lzw = Compress(f)
                Dim pos = 0
                While pos < lzw.Length
                    Dim n = Math.Min(255, lzw.Length - pos)
                    w.Write(CByte(n))
                    w.Write(lzw, pos, n)
                    pos += n
                End While
                w.Write(CByte(0))
            Next
            w.Write(CByte(&H3B))
        End Using
    End Sub

    ''' <summary>GIF LZW compression with 8-bit pixels.</summary>
    Private Shared Function Compress(pixels As Byte()) As Byte()
        Const clearCode = 256, endCode = 257
        Dim output As New List(Of Byte)
        Dim bitBuffer = 0, bitCount = 0
        Dim codeSize = 9
        Dim emit = Sub(code As Integer)
                       bitBuffer = bitBuffer Or (code << bitCount)
                       bitCount += codeSize
                       While bitCount >= 8
                           output.Add(CByte(bitBuffer And &HFF))
                           bitBuffer >>= 8
                           bitCount -= 8
                       End While
                   End Sub
        Dim dict As New Dictionary(Of Integer, Integer)
        Dim nextCode = 258
        emit(clearCode)
        Dim prefix As Integer = pixels(0)
        For i = 1 To pixels.Length - 1
            Dim k As Integer = pixels(i)
            Dim key = (prefix << 8) Or k
            Dim found As Integer
            If dict.TryGetValue(key, found) Then
                prefix = found
            Else
                emit(prefix)
                If nextCode < 4096 Then
                    dict(key) = nextCode
                    nextCode += 1
                    If nextCode > (1 << codeSize) AndAlso codeSize < 12 Then codeSize += 1
                Else
                    emit(clearCode)
                    dict.Clear()
                    nextCode = 258
                    codeSize = 9
                End If
                prefix = k
            End If
        Next
        emit(prefix)
        emit(endCode)
        If bitCount > 0 Then output.Add(CByte(bitBuffer And &HFF))
        Return output.ToArray()
    End Function
End Class
