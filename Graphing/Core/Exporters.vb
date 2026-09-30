Imports System.Globalization
Imports System.IO
Imports System.IO.Compression
Imports System.Security
Imports System.Text

''' <summary>Writes traced data to CSV, tab-separated text (for the clipboard) and Excel .xlsx files. Excel does not need to be installed.</summary>
Public NotInheritable Class Exporters
    Private Sub New()
    End Sub

    ''' <summary>CSV with one row per point: Series, X, Y. Uses "." decimals so any program can read it.</summary>
    Public Shared Function ToCsv(seriesList As IEnumerable(Of DataSeries), calibration As Calibration) As String
        Dim text As New StringBuilder()
        text.AppendLine("Series,X,Y")
        For Each s In seriesList
            For Each p In s.DataPoints(calibration)
                text.Append(CsvField(s.Name)).Append(","c).
                     Append(InvariantNumber(p.X)).Append(","c).
                     Append(InvariantNumber(p.Y)).AppendLine()
            Next
        Next
        Return text.ToString()
    End Function

    ''' <summary>Tab-separated X/Y columns in the local number format, ready to paste into Excel, Origin, etc.</summary>
    Public Shared Function ToTabSeparated(series As DataSeries, calibration As Calibration) As String
        Dim text As New StringBuilder()
        text.Append("X").Append(vbTab).Append("Y").AppendLine()
        For Each p In series.DataPoints(calibration)
            text.Append(LocalNumber(p.X)).Append(vbTab).Append(LocalNumber(p.Y)).AppendLine()
        Next
        Return text.ToString()
    End Function

    ''' <summary>Writes an .xlsx workbook with one sheet per series (columns X and Y).</summary>
    Public Shared Sub SaveXlsx(filePath As String, seriesList As IList(Of DataSeries), calibration As Calibration)
        If seriesList.Count = 0 Then Throw New InvalidOperationException("There is nothing to export.")
        Dim sheetNames = UniqueSheetNames(seriesList.Select(Function(s) s.Name))
        Dim tempPath = filePath & ".tmp"
        Using stream = File.Create(tempPath)
            Using zip As New ZipArchive(stream, ZipArchiveMode.Create)
                WriteEntry(zip, "[Content_Types].xml", ContentTypes(seriesList.Count))
                WriteEntry(zip, "_rels/.rels",
                    "<?xml version=""1.0"" encoding=""UTF-8"" standalone=""yes""?>" &
                    "<Relationships xmlns=""http://schemas.openxmlformats.org/package/2006/relationships"">" &
                    "<Relationship Id=""rId1"" Type=""http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"" Target=""xl/workbook.xml""/>" &
                    "</Relationships>")
                WriteEntry(zip, "xl/workbook.xml", Workbook(sheetNames))
                WriteEntry(zip, "xl/_rels/workbook.xml.rels", WorkbookRelationships(seriesList.Count))
                WriteEntry(zip, "xl/styles.xml", Styles())
                For i = 0 To seriesList.Count - 1
                    WriteEntry(zip, String.Format("xl/worksheets/sheet{0}.xml", i + 1), Worksheet(seriesList(i).DataPoints(calibration)))
                Next
            End Using
        End Using
        File.Move(tempPath, filePath, overwrite:=True)
    End Sub

    Private Shared Sub WriteEntry(zip As ZipArchive, name As String, content As String)
        Dim entry = zip.CreateEntry(name, CompressionLevel.Optimal)
        Using writer As New StreamWriter(entry.Open(), New UTF8Encoding(False))
            writer.Write(content)
        End Using
    End Sub

    Private Shared Function ContentTypes(sheetCount As Integer) As String
        Dim text As New StringBuilder()
        text.Append("<?xml version=""1.0"" encoding=""UTF-8"" standalone=""yes""?>")
        text.Append("<Types xmlns=""http://schemas.openxmlformats.org/package/2006/content-types"">")
        text.Append("<Default Extension=""rels"" ContentType=""application/vnd.openxmlformats-package.relationships+xml""/>")
        text.Append("<Default Extension=""xml"" ContentType=""application/xml""/>")
        text.Append("<Override PartName=""/xl/workbook.xml"" ContentType=""application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml""/>")
        text.Append("<Override PartName=""/xl/styles.xml"" ContentType=""application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml""/>")
        For i = 1 To sheetCount
            text.AppendFormat("<Override PartName=""/xl/worksheets/sheet{0}.xml"" ContentType=""application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml""/>", i)
        Next
        text.Append("</Types>")
        Return text.ToString()
    End Function

    Private Shared Function Workbook(sheetNames As IList(Of String)) As String
        Dim text As New StringBuilder()
        text.Append("<?xml version=""1.0"" encoding=""UTF-8"" standalone=""yes""?>")
        text.Append("<workbook xmlns=""http://schemas.openxmlformats.org/spreadsheetml/2006/main"" xmlns:r=""http://schemas.openxmlformats.org/officeDocument/2006/relationships""><sheets>")
        For i = 0 To sheetNames.Count - 1
            text.AppendFormat("<sheet name=""{0}"" sheetId=""{1}"" r:id=""rId{1}""/>", Xml(sheetNames(i)), i + 1)
        Next
        text.Append("</sheets></workbook>")
        Return text.ToString()
    End Function

    Private Shared Function WorkbookRelationships(sheetCount As Integer) As String
        Dim text As New StringBuilder()
        text.Append("<?xml version=""1.0"" encoding=""UTF-8"" standalone=""yes""?>")
        text.Append("<Relationships xmlns=""http://schemas.openxmlformats.org/package/2006/relationships"">")
        For i = 1 To sheetCount
            text.AppendFormat("<Relationship Id=""rId{0}"" Type=""http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet"" Target=""worksheets/sheet{0}.xml""/>", i)
        Next
        text.AppendFormat("<Relationship Id=""rId{0}"" Type=""http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles"" Target=""styles.xml""/>", sheetCount + 1)
        text.Append("</Relationships>")
        Return text.ToString()
    End Function

    Private Shared Function Styles() As String
        ' Style 0 = normal, style 1 = bold (used for the header row).
        Return "<?xml version=""1.0"" encoding=""UTF-8"" standalone=""yes""?>" &
               "<styleSheet xmlns=""http://schemas.openxmlformats.org/spreadsheetml/2006/main"">" &
               "<fonts count=""2""><font><sz val=""11""/><name val=""Calibri""/></font><font><b/><sz val=""11""/><name val=""Calibri""/></font></fonts>" &
               "<fills count=""2""><fill><patternFill patternType=""none""/></fill><fill><patternFill patternType=""gray125""/></fill></fills>" &
               "<borders count=""1""><border><left/><right/><top/><bottom/><diagonal/></border></borders>" &
               "<cellStyleXfs count=""1""><xf numFmtId=""0"" fontId=""0"" fillId=""0"" borderId=""0""/></cellStyleXfs>" &
               "<cellXfs count=""2""><xf numFmtId=""0"" fontId=""0"" fillId=""0"" borderId=""0"" xfId=""0""/>" &
               "<xf numFmtId=""0"" fontId=""1"" fillId=""0"" borderId=""0"" xfId=""0"" applyFont=""1""/></cellXfs>" &
               "<cellStyles count=""1""><cellStyle name=""Normal"" xfId=""0"" builtinId=""0""/></cellStyles>" &
               "</styleSheet>"
    End Function

    Private Shared Function Worksheet(points As IList(Of PointD)) As String
        Dim text As New StringBuilder()
        text.Append("<?xml version=""1.0"" encoding=""UTF-8"" standalone=""yes""?>")
        text.Append("<worksheet xmlns=""http://schemas.openxmlformats.org/spreadsheetml/2006/main"">")
        text.Append("<sheetViews><sheetView workbookViewId=""0""><pane ySplit=""1"" topLeftCell=""A2"" activePane=""bottomLeft"" state=""frozen""/></sheetView></sheetViews>")
        text.Append("<cols><col min=""1"" max=""2"" width=""16"" customWidth=""1""/></cols><sheetData>")
        text.Append("<row r=""1""><c r=""A1"" t=""inlineStr"" s=""1""><is><t>X</t></is></c><c r=""B1"" t=""inlineStr"" s=""1""><is><t>Y</t></is></c></row>")
        For i = 0 To points.Count - 1
            Dim row = i + 2
            text.AppendFormat("<row r=""{0}"">", row)
            AppendNumberCell(text, "A" & row, points(i).X)
            AppendNumberCell(text, "B" & row, points(i).Y)
            text.Append("</row>")
        Next
        text.Append("</sheetData></worksheet>")
        Return text.ToString()
    End Function

    Private Shared Sub AppendNumberCell(text As StringBuilder, reference As String, value As Double)
        ' Cells that have no valid value (e.g. before calibration) are left empty.
        If Double.IsNaN(value) OrElse Double.IsInfinity(value) Then Return
        text.AppendFormat("<c r=""{0}""><v>{1}</v></c>", reference, InvariantNumber(value))
    End Sub

    ''' <summary>Excel sheet names: at most 31 characters, none of []:*?/\ and unique (case-insensitive).</summary>
    Private Shared Function UniqueSheetNames(names As IEnumerable(Of String)) As List(Of String)
        Dim used As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        Dim result As New List(Of String)
        For Each rawName In names
            Dim clean = New String((If(rawName, "")).Select(Function(ch) If("[]:*?/\".IndexOf(ch) >= 0, "_"c, ch)).ToArray()).Trim().Trim("'"c)
            If clean.Length = 0 Then clean = "Series"
            If clean.Length > 31 Then clean = clean.Substring(0, 31)
            Dim candidate = clean
            Dim n = 2
            While Not used.Add(candidate)
                Dim suffix = " (" & n & ")"
                candidate = If(clean.Length + suffix.Length > 31, clean.Substring(0, 31 - suffix.Length), clean) & suffix
                n += 1
            End While
            result.Add(candidate)
        Next
        Return result
    End Function

    Private Shared Function Xml(value As String) As String
        Return SecurityElement.Escape(value)
    End Function

    Private Shared Function CsvField(value As String) As String
        If value Is Nothing Then Return ""
        If value.IndexOfAny({","c, """"c, ControlChars.Cr, ControlChars.Lf}) >= 0 Then Return """" & value.Replace("""", """""") & """"
        Return value
    End Function

    Private Shared Function InvariantNumber(value As Double) As String
        If Double.IsNaN(value) OrElse Double.IsInfinity(value) Then Return ""
        Return value.ToString("R", CultureInfo.InvariantCulture)
    End Function

    Private Shared Function LocalNumber(value As Double) As String
        If Double.IsNaN(value) OrElse Double.IsInfinity(value) Then Return ""
        Return value.ToString("R", CultureInfo.CurrentCulture)
    End Function
End Class
