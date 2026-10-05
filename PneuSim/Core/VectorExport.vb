Imports System.Globalization
Imports System.IO
Imports System.Text

''' <summary>A minimal PDF writer: vector paths and Helvetica text, several pages.</summary>
Public Class PdfDocument
    Private ReadOnly _pages As New List(Of PdfPage)

    Public Function AddPage(width As Single, height As Single) As PdfPage
        Dim p As New PdfPage(width, height)
        _pages.Add(p)
        Return p
    End Function

    Public Sub Save(path As String)
        Dim latin1 = Encoding.GetEncoding(28591)
        Using fs As New FileStream(path, FileMode.Create, FileAccess.Write)
            Dim offsets As New List(Of Long)
            Dim write = Sub(s As String)
                            Dim b = latin1.GetBytes(s)
                            fs.Write(b, 0, b.Length)
                        End Sub
            Dim beginObj = Sub(objNo As Integer)
                               While offsets.Count < objNo : offsets.Add(0) : End While
                               offsets(objNo - 1) = fs.Position
                               write($"{objNo} 0 obj" & vbLf)
                           End Sub
            write("%PDF-1.4" & vbLf & "%" & ChrW(226) & ChrW(227) & ChrW(207) & ChrW(211) & vbLf)
            ' 1 catalog, 2 pages, 3 Helvetica, 4 Helvetica-Bold, then (page, content) pairs.
            Dim n = _pages.Count
            beginObj(1) : write("<< /Type /Catalog /Pages 2 0 R >>" & vbLf & "endobj" & vbLf)
            Dim kids = String.Join(" ", Enumerable.Range(0, n).Select(Function(i) $"{5 + i * 2} 0 R"))
            beginObj(2) : write($"<< /Type /Pages /Kids [{kids}] /Count {n} >>" & vbLf & "endobj" & vbLf)
            beginObj(3) : write("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>" & vbLf & "endobj" & vbLf)
            beginObj(4) : write("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>" & vbLf & "endobj" & vbLf)
            For i = 0 To n - 1
                Dim pg = _pages(i)
                Dim pageNo = 5 + i * 2
                beginObj(pageNo)
                write($"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {F(pg.Width)} {F(pg.Height)}] " &
                      $"/Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents {pageNo + 1} 0 R >>" & vbLf & "endobj" & vbLf)
                Dim content = latin1.GetBytes(pg.Content.ToString())
                beginObj(pageNo + 1)
                write($"<< /Length {content.Length} >>" & vbLf & "stream" & vbLf)
                fs.Write(content, 0, content.Length)
                write(vbLf & "endstream" & vbLf & "endobj" & vbLf)
            Next
            Dim xref = fs.Position
            write($"xref" & vbLf & $"0 {offsets.Count + 1}" & vbLf & "0000000000 65535 f " & vbLf)
            For Each o In offsets
                write(o.ToString("0000000000") & " 00000 n " & vbLf)
            Next
            write($"trailer" & vbLf & $"<< /Size {offsets.Count + 1} /Root 1 0 R >>" & vbLf & "startxref" & vbLf & xref & vbLf & "%%EOF" & vbLf)
        End Using
    End Sub

    Friend Shared Function F(v As Single) As String
        Return v.ToString("0.###", CultureInfo.InvariantCulture)
    End Function
End Class

''' <summary>One PDF page; coordinates have the origin at the top-left, in points.</summary>
Public Class PdfPage
    Public Sub New(width As Single, height As Single)
        Me.Width = width
        Me.Height = height
    End Sub

    Public ReadOnly Property Width As Single
    Public ReadOnly Property Height As Single
    Friend ReadOnly Property Content As New StringBuilder()

    Private Function Y(v As Single) As Single
        Return Height - v
    End Function

    Private Shared Function Rgb(c As Color) As String
        Return $"{PdfDocument.F(c.R / 255.0F)} {PdfDocument.F(c.G / 255.0F)} {PdfDocument.F(c.B / 255.0F)}"
    End Function

    Public Sub Path(points As PointF(), closed As Boolean, stroke As Color, strokeWidth As Single, dashed As Boolean, fill As Color)
        If points.Length < 2 Then Return
        If fill.A = 0 AndAlso stroke.A = 0 Then Return
        Dim sb = Content
        If stroke.A > 0 Then
            sb.Append($"{Rgb(stroke)} RG {PdfDocument.F(Math.Max(0.3F, strokeWidth))} w ")
            sb.Append(If(dashed, "[3 2] 0 d ", "[] 0 d "))
        End If
        If fill.A > 0 Then sb.Append($"{Rgb(fill)} rg ")
        sb.Append($"{PdfDocument.F(points(0).X)} {PdfDocument.F(Y(points(0).Y))} m ")
        For i = 1 To points.Length - 1
            sb.Append($"{PdfDocument.F(points(i).X)} {PdfDocument.F(Y(points(i).Y))} l ")
        Next
        If closed Then sb.Append("h ")
        If fill.A > 0 AndAlso stroke.A > 0 Then
            sb.AppendLine("B")
        ElseIf fill.A > 0 Then
            sb.AppendLine("f")
        Else
            sb.AppendLine("S")
        End If
    End Sub

    Public Sub Rect(x As Single, top As Single, w As Single, h As Single, stroke As Color, Optional strokeWidth As Single = 1, Optional fill As Color = Nothing)
        Path({New PointF(x, top), New PointF(x + w, top), New PointF(x + w, top + h), New PointF(x, top + h)}, True, stroke, strokeWidth, False, fill)
    End Sub

    ''' <summary>Text with its baseline at (x, baseline).</summary>
    Public Sub Text(x As Single, baseline As Single, size As Single, txt As String, Optional bold As Boolean = False,
                    Optional color As Color = Nothing, Optional angleDeg As Single = 0)
        If String.IsNullOrEmpty(txt) Then Return
        If color.A = 0 Then color = Color.Black
        Dim a = -angleDeg * Math.PI / 180
        Dim c = CSng(Math.Cos(a)), s = CSng(Math.Sin(a))
        Content.AppendLine($"BT /{If(bold, "F2", "F1")} {PdfDocument.F(size)} Tf {Rgb(color)} rg " &
                           $"{PdfDocument.F(c)} {PdfDocument.F(s)} {PdfDocument.F(-s)} {PdfDocument.F(c)} {PdfDocument.F(x)} {PdfDocument.F(Y(baseline))} Tm ({Escape(txt)}) Tj ET")
    End Sub

    Private Shared Function Escape(t As String) As String
        Dim sb As New StringBuilder()
        For Each ch In VectorExport.Plain(t)
            Select Case ch
                Case "("c, ")"c, "\"c : sb.Append("\"c).Append(ch)
                Case Else
                    sb.Append(If(AscW(ch) < 256, ch, "?"c))
            End Select
        Next
        Return sb.ToString()
    End Function
End Class

''' <summary>Exports drawings to SVG, DXF and PDF.</summary>
Public Module VectorExport

    ''' <summary>Replaces characters that the standard PDF and DXF fonts cannot show.</summary>
    Public Function Plain(t As String) As String
        Return t.Replace("₹", "Rs ").Replace("→", "->").Replace("≥", ">=").Replace("≤", "<=").Replace("•", "-").
                 Replace("—", "-").Replace("–", "-").Replace("³", "3").Replace(ChrW(&H2019), "'")
    End Function

    ''' <summary>Records a circuit's drawing as vector shapes.</summary>
    Public Function Record(c As Circuit, Optional simulating As Boolean = False) As VectorSurface
        Dim vs As New VectorSurface()
        Using canvas As New CircuitCanvas() With {.Circuit = c, .Simulating = simulating}
            canvas.PaintTo(vs)
        End Using
        Return vs
    End Function

    Private Function Hex(c As Color) As String
        Return $"#{c.R:X2}{c.G:X2}{c.B:X2}"
    End Function

    Private Function N(v As Single) As String
        Return v.ToString("0.##", CultureInfo.InvariantCulture)
    End Function

    Public Sub SaveSvg(c As Circuit, path As String)
        Dim vs = Record(c)
        Dim b = vs.Bounds()
        b.Inflate(20, 20)
        Dim sb As New StringBuilder()
        sb.AppendLine("<?xml version=""1.0"" encoding=""UTF-8""?>")
        sb.AppendLine($"<svg xmlns=""http://www.w3.org/2000/svg"" width=""{N(b.Width)}"" height=""{N(b.Height)}"" viewBox=""{N(b.Left)} {N(b.Top)} {N(b.Width)} {N(b.Height)}"">")
        sb.AppendLine($"<rect x=""{N(b.Left)}"" y=""{N(b.Top)}"" width=""{N(b.Width)}"" height=""{N(b.Height)}"" fill=""white""/>")
        For Each s In vs.Shapes
            If s.IsText Then
                Dim esc = Security.SecurityElement.Escape(s.Text)
                Dim rot = If(Math.Abs(s.AngleDeg) > 0.1, $" transform=""rotate({N(s.AngleDeg)} {N(s.TextPos.X)} {N(s.TextPos.Y)})""", "")
                sb.AppendLine($"<text x=""{N(s.TextPos.X)}"" y=""{N(s.TextPos.Y)}"" font-family=""Segoe UI, Arial, sans-serif"" font-size=""{N(s.FontSize)}""{If(s.Bold, " font-weight=""bold""", "")} fill=""{Hex(s.Stroke)}""{rot}>{esc}</text>")
            Else
                Dim pts = String.Join(" ", s.Points.Select(Function(p) $"{N(p.X)},{N(p.Y)}"))
                Dim fill = If(s.Fill.A > 0, Hex(s.Fill), "none")
                Dim stroke = If(s.Stroke.A > 0, $" stroke=""{Hex(s.Stroke)}"" stroke-width=""{N(s.StrokeWidth)}""{If(s.Dashed, " stroke-dasharray=""4 3""", "")} stroke-linejoin=""round""", "")
                sb.AppendLine($"<{If(s.Closed, "polygon", "polyline")} points=""{pts}"" fill=""{fill}""{stroke}/>")
            End If
        Next
        sb.AppendLine("</svg>")
        File.WriteAllText(path, sb.ToString(), New UTF8Encoding(False))
    End Sub

    ''' <summary>DXF (AutoCAD R12 ASCII): lines and texts, in millimetres (1 drawing unit = 0.25 mm).</summary>
    Public Sub SaveDxf(c As Circuit, path As String)
        Dim vs = Record(c)
        Const scale = 0.25F
        Dim sb As New StringBuilder()
        Dim pair = Sub(code As Integer, value As String) sb.Append(code).Append(vbCrLf).Append(value).Append(vbCrLf)
        Dim num = Function(v As Single) v.ToString("0.###", CultureInfo.InvariantCulture)
        pair(0, "SECTION") : pair(2, "HEADER")
        pair(9, "$ACADVER") : pair(1, "AC1009")
        pair(9, "$INSUNITS") : pair(70, "4")
        pair(0, "ENDSEC")
        pair(0, "SECTION") : pair(2, "ENTITIES")
        For Each s In vs.Shapes
            Dim layer = If(s.IsText, "TEXT", If(s.Fill.A > 0 AndAlso s.Stroke.A = 0, "FILL", "LINES"))
            If s.IsText Then
                pair(0, "TEXT") : pair(8, layer)
                pair(10, num(s.TextPos.X * scale)) : pair(20, num(-s.TextPos.Y * scale)) : pair(30, "0")
                pair(40, num(s.FontSize * 0.72F * scale)) : pair(1, Plain(s.Text))
                If Math.Abs(s.AngleDeg) > 0.1 Then pair(50, num(-s.AngleDeg))
            Else
                Dim pts = s.Points.ToList()
                If s.Closed Then pts.Add(pts(0))
                For i = 0 To pts.Count - 2
                    pair(0, "LINE") : pair(8, layer)
                    pair(10, num(pts(i).X * scale)) : pair(20, num(-pts(i).Y * scale)) : pair(30, "0")
                    pair(11, num(pts(i + 1).X * scale)) : pair(21, num(-pts(i + 1).Y * scale)) : pair(31, "0")
                Next
            End If
        Next
        pair(0, "ENDSEC") : pair(0, "EOF")
        File.WriteAllText(path, sb.ToString(), Encoding.ASCII)
    End Sub

    ''' <summary>Draws recorded shapes onto a PDF page, scaled to fit the given area.</summary>
    Public Sub DrawShapes(page As PdfPage, vs As VectorSurface, area As RectangleF)
        Dim b = vs.Bounds()
        If b.IsEmpty Then Return
        b.Inflate(10, 10)
        Dim scale = Math.Min(1.0F, Math.Min(area.Width / b.Width, area.Height / b.Height))
        Dim ox = area.Left + (area.Width - b.Width * scale) / 2 - b.Left * scale
        Dim oy = area.Top - b.Top * scale
        Dim map = Function(p As PointF) New PointF(ox + p.X * scale, oy + p.Y * scale)
        For Each s In vs.Shapes
            If s.IsText Then
                Dim p = map(s.TextPos)
                page.Text(p.X, p.Y, s.FontSize * scale * 0.75F, s.Text, s.Bold, s.Stroke, s.AngleDeg)
            Else
                page.Path(s.Points.Select(map).ToArray(), s.Closed, s.Stroke, s.StrokeWidth * scale * 0.75F, s.Dashed, s.Fill)
            End If
        Next
    End Sub

    ''' <summary>Border and title block at the bottom right of an A4/A3 landscape page.</summary>
    Public Sub DrawTitleBlock(page As PdfPage, info As ProjectInfo, sheetName As String, sheet As Integer, sheets As Integer)
        Const m = 20.0F
        Dim gray = Color.FromArgb(90, 90, 90)
        page.Rect(m, m, page.Width - 2 * m, page.Height - 2 * m, Color.Black, 1.2F)
        Dim w = 330.0F, h = 74.0F
        Dim x = page.Width - m - w, y = page.Height - m - h
        page.Rect(x, y, w, h, Color.Black, 1.2F, Color.White)
        ' Rows: title / drawn by + date / company + drawing no. + rev / sheet.
        page.Path({New PointF(x, y + 26), New PointF(x + w, y + 26)}, False, Color.Black, 0.8F, False, Color.Empty)
        page.Path({New PointF(x, y + 50), New PointF(x + w, y + 50)}, False, Color.Black, 0.8F, False, Color.Empty)
        page.Path({New PointF(x + 200, y + 26), New PointF(x + 200, y + h)}, False, Color.Black, 0.8F, False, Color.Empty)
        page.Text(x + 4, y + 9, 6, "TITLE", color:=gray)
        page.Text(x + 4, y + 21, 11, If(info.Title, ""), bold:=True)
        page.Text(x + 4, y + 33, 6, "DRAWN BY", color:=gray)
        page.Text(x + 4, y + 45, 9, info.Author)
        page.Text(x + 204, y + 33, 6, "DATE", color:=gray)
        page.Text(x + 204, y + 45, 9, info.DateText)
        page.Text(x + 4, y + 57, 6, "COMPANY / INSTITUTE", color:=gray)
        page.Text(x + 4, y + 69, 9, info.Company)
        page.Text(x + 204, y + 57, 6, "DRAWING NO. / REV / SHEET", color:=gray)
        page.Text(x + 204, y + 69, 9, $"{info.DrawingNumber}  rev {info.Revision}  {sheet}/{sheets}")
        page.Text(x + 120, y + 9, 6, sheetName, color:=gray)
        page.Text(m + 6, page.Height - m - 6, 6, "Drawn with PneuSim", color:=gray)
    End Sub
End Module
