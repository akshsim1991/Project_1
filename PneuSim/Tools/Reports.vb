''' <summary>PDF report: every page of the drawing with a title block, the parts list and an explanation.</summary>
Public Module Reports
    Private Const PageW As Single = 842, PageH As Single = 595 ' A4 landscape, points

    Public Sub SavePdfReport(project As Project, path As String, includeParts As Boolean, explanation As String)
        Dim doc As New PdfDocument()
        project.UpdateCrossReferences()
        Dim extraPages = If(includeParts, 1, 0) + If(String.IsNullOrWhiteSpace(explanation), 0, 1)
        Dim total = project.Pages.Count + extraPages
        Dim sheet = 0
        For Each pg In project.Pages
            sheet += 1
            Dim page = doc.AddPage(PageW, PageH)
            VectorExport.DrawShapes(page, VectorExport.Record(pg.Circuit), New RectangleF(30, 30, PageW - 60, PageH - 60 - 84))
            VectorExport.DrawTitleBlock(page, project.Info, pg.Name, sheet, total)
        Next
        If includeParts Then
            sheet += 1
            Dim page = doc.AddPage(PageW, PageH)
            DrawPartsTable(page, PartsList.Build(project))
            VectorExport.DrawTitleBlock(page, project.Info, "Parts list", sheet, total)
        End If
        If Not String.IsNullOrWhiteSpace(explanation) Then
            sheet += 1
            Dim page = doc.AddPage(PageW, PageH)
            DrawText(page, "How the circuit works", explanation)
            VectorExport.DrawTitleBlock(page, project.Info, "Explanation", sheet, total)
        End If
        doc.Save(path)
    End Sub

    Private Sub DrawPartsTable(page As PdfPage, lines As List(Of PartLine))
        Dim x0 = 40.0F, y = 52.0F
        page.Text(x0, y, 14, "Parts list", bold:=True)
        y += 22
        Dim cols = {(x0, "Qty"), (x0 + 40, "Description"), (x0 + 400, "Labels"), (x0 + 600, "Unit price (Rs)"), (x0 + 700, "Total (Rs)")}
        For Each c In cols
            page.Text(c.Item1, y, 9, c.Item2, bold:=True)
        Next
        y += 4
        page.Path({New PointF(x0, y), New PointF(PageW - 40, y)}, False, Color.Black, 0.8F, False, Color.Empty)
        For Each l In lines
            y += 14
            If y > PageH - 130 Then page.Text(x0, y, 9, "... (continued: export the parts list as CSV for the full list)") : Exit For
            page.Text(x0, y, 9, l.Quantity.ToString())
            page.Text(x0 + 40, y, 9, l.Description)
            page.Text(x0 + 400, y, 9, If(l.Labels.Length > 40, l.Labels.Substring(0, 40) & "...", l.Labels))
            page.Text(x0 + 600, y, 9, l.UnitPrice.ToString("#,##0.00"))
            page.Text(x0 + 700, y, 9, l.Total.ToString("#,##0.00"))
        Next
        y += 8
        page.Path({New PointF(x0, y), New PointF(PageW - 40, y)}, False, Color.Black, 0.8F, False, Color.Empty)
        page.Text(x0 + 600, y + 14, 10, "Total", bold:=True)
        page.Text(x0 + 700, y + 14, 10, lines.Sum(Function(l) l.Total).ToString("#,##0.00"), bold:=True)
    End Sub

    Private Sub DrawText(page As PdfPage, title As String, body As String)
        Dim y = 52.0F
        page.Text(40, y, 14, title, bold:=True)
        y += 20
        For Each raw In body.Replace(vbCr, "").Split(ChrW(10))
            For Each line In Wrap(raw, 150)
                y += 11
                If y > PageH - 120 Then page.Text(40, y, 8, "...") : Return
                page.Text(40, y, 8, line)
            Next
        Next
    End Sub

    Private Iterator Function Wrap(text As String, width As Integer) As IEnumerable(Of String)
        If text.Length <= width Then Yield text : Return
        Dim rest = text
        While rest.Length > width
            Dim cut = rest.LastIndexOf(" "c, width)
            If cut <= 0 Then cut = width
            Yield rest.Substring(0, cut)
            rest = "      " & rest.Substring(cut).TrimStart()
        End While
        Yield rest
    End Function
End Module
