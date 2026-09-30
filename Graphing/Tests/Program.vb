Imports System.IO
Imports System.IO.Compression

''' <summary>Checks for the Graphing calculation engine. Exit code 0 means every check passed.</summary>
Module Program
    Private _failures As Integer
    Private _checks As Integer

    Function Main() As Integer
        TestCalibrationAxisAligned()
        TestCalibrationRotatedAndSkewed()
        TestCalibrationLogAxes()
        TestCalibrationMessages()
        TestDataTools()
        TestLogInterpolation()
        TestCurveFits()
        TestAutoTrace()
        TestExporters()
        TestProjectFiles()
        TestUndo()
        TestTicks()
        TestNumberParsing()

        Console.WriteLine()
        Console.WriteLine(If(_failures = 0, String.Format("ALL {0} CHECKS PASSED", _checks), String.Format("{0} OF {1} CHECKS FAILED", _failures, _checks)))
        Return If(_failures = 0, 0, 1)
    End Function

#Region "Helpers"

    Private Sub Check(name As String, ok As Boolean, Optional detail As String = "")
        _checks += 1
        If Not ok Then
            _failures += 1
            Console.WriteLine("FAIL  {0}  {1}", name, detail)
        Else
            Console.WriteLine("PASS  {0}", name)
        End If
    End Sub

    Private Sub Near(name As String, actual As Double, expected As Double, Optional tolerance As Double = 0.000001)
        Dim ok = Math.Abs(actual - expected) <= tolerance * Math.Max(1, Math.Abs(expected))
        Check(name, ok, String.Format("expected {0}, got {1}", expected, actual))
    End Sub

    Private Sub Throws(Of T As Exception)(name As String, action As Action)
        Try
            action()
            Check(name, False, "no exception")
        Catch ex As T
            Check(name, True)
        Catch ex As Exception
            Check(name, False, ex.GetType().Name & ": " & ex.Message)
        End Try
    End Sub

    ''' <summary>A calibration where pixel = origin + x·ux + y·uy (x, y possibly log10'd).</summary>
    Private Function MakeCalibration(origin As PointD, ux As PointD, uy As PointD, x1 As Double, x2 As Double, xRefY As Double,
                                     y1 As Double, y2 As Double, yRefX As Double, logX As Boolean, logY As Boolean) As Calibration
        Dim toPixel = Function(x As Double, y As Double)
                          Dim ax = If(logX, Math.Log10(x), x), ay = If(logY, Math.Log10(y), y)
                          Return New PointD(origin.X + ax * ux.X + ay * uy.X, origin.Y + ax * ux.Y + ay * uy.Y)
                      End Function
        Return New Calibration With {
            .LogX = logX, .LogY = logY,
            .X1 = New AxisReference With {.Pixel = toPixel(x1, xRefY), .Value = x1},
            .X2 = New AxisReference With {.Pixel = toPixel(x2, xRefY), .Value = x2},
            .Y1 = New AxisReference With {.Pixel = toPixel(yRefX, y1), .Value = y1},
            .Y2 = New AxisReference With {.Pixel = toPixel(yRefX, y2), .Value = y2}
        }
    End Function

    ''' <summary>Simple upright calibration: x_px = 50 + 8x, y_px = 450 - 4y (so data (0,0) is at pixel (50,450)).</summary>
    Private Function SimpleCalibration() As Calibration
        Return MakeCalibration(New PointD(50, 450), New PointD(8, 0), New PointD(0, -4), 0, 50, 0, 0, 100, 0, False, False)
    End Function

    Private Function SeriesFromData(calibration As Calibration, ParamArray data As PointD()) As DataSeries
        Return New DataSeries With {.Points = data.Select(Function(d) calibration.DataToPixel(d)).ToList()}
    End Function

#End Region

    Sub TestCalibrationAxisAligned()
        ' X axis from 1990 (pixel 100) to 2020 (pixel 700); Y axis from 200 (pixel 500) to 800 (pixel 100).
        Dim cal As New Calibration With {
            .X1 = New AxisReference With {.Pixel = New PointD(100, 500), .Value = 1990},
            .X2 = New AxisReference With {.Pixel = New PointD(700, 500), .Value = 2020},
            .Y1 = New AxisReference With {.Pixel = New PointD(100, 500), .Value = 200},
            .Y2 = New AxisReference With {.Pixel = New PointD(100, 100), .Value = 800}
        }
        Check("upright calibration is complete", cal.IsComplete, cal.Validate())
        Dim d = cal.PixelToData(New PointD(400, 300))
        Near("axis not starting at 0: X", d.X, 2005)
        Near("axis not starting at 0: Y", d.Y, 500)
        Dim back = cal.DataToPixel(New PointD(2011, 650))
        Near("data to pixel X", back.X, 520)
        Near("data to pixel Y", back.Y, 200)
    End Sub

    Sub TestCalibrationRotatedAndSkewed()
        ' A scan rotated by a few degrees and slightly skewed; reference points not on the axes themselves.
        Dim origin = New PointD(120, 650), ux = New PointD(8, -0.6), uy = New PointD(0.45, -9)
        Dim cal = MakeCalibration(origin, ux, uy, 1, 9, 2, 1, 7, 3, False, False)
        Check("rotated calibration is complete", cal.IsComplete, cal.Validate())
        Dim rnd As New Random(1)
        Dim worst = 0.0
        For i = 1 To 200
            Dim truth = New PointD(rnd.NextDouble() * 20 - 5, rnd.NextDouble() * 20 - 5)
            Dim pixel = New PointD(origin.X + truth.X * ux.X + truth.Y * uy.X, origin.Y + truth.X * ux.Y + truth.Y * uy.Y)
            Dim got = cal.PixelToData(pixel)
            worst = Math.Max(worst, Math.Max(Math.Abs(got.X - truth.X), Math.Abs(got.Y - truth.Y)))
            Dim again = cal.DataToPixel(got)
            worst = Math.Max(worst, pixel.DistanceTo(again) / 100)
        Next
        Check("rotated + skewed scan: 200 random points exact", worst < 0.000000001, "worst error " & worst)
    End Sub

    Sub TestCalibrationLogAxes()
        Dim origin = New PointD(80, 600), ux = New PointD(150, 2), uy = New PointD(-1, -120)
        Dim cal = MakeCalibration(origin, ux, uy, 1, 1000, 0.1, 0.01, 100, 1, True, True)
        Check("log-log calibration is complete", cal.IsComplete, cal.Validate())
        Dim pixel = New PointD(origin.X + Math.Log10(50) * ux.X + Math.Log10(0.3) * uy.X, origin.Y + Math.Log10(50) * ux.Y + Math.Log10(0.3) * uy.Y)
        Dim got = cal.PixelToData(pixel)
        Near("log X value", got.X, 50, 0.000000001)
        Near("log Y value", got.Y, 0.3, 0.000000001)
        ' Only the Y axis logarithmic (semi-log plot).
        Dim semi = MakeCalibration(origin, ux, uy, 0, 30, 1, 1, 1000, 0, False, True)
        Dim p2 = New PointD(origin.X + 12 * ux.X + 2 * uy.X, origin.Y + 12 * ux.Y + 2 * uy.Y)
        Dim got2 = semi.PixelToData(p2)
        Near("semi-log X", got2.X, 12, 0.000000001)
        Near("semi-log Y", got2.Y, 100, 0.000000001)
    End Sub

    Sub TestCalibrationMessages()
        Dim cal As New Calibration
        Check("empty calibration asks for X1", cal.Validate() = "Pick the X1 point on the image.", cal.Validate())
        Check("empty calibration gives NaN", Double.IsNaN(cal.PixelToData(New PointD(1, 1)).X))
        Dim good = SimpleCalibration()
        Dim sameValues = good.Clone() : sameValues.X2.Value = sameValues.X1.Value
        Check("equal X values rejected", sameValues.Validate() = "X1 and X2 must have different values.", sameValues.Validate())
        Dim logZero = good.Clone() : logZero.LogX = True
        Check("log axis with 0 rejected", logZero.Validate() IsNot Nothing AndAlso logZero.Validate().Contains("above 0"), logZero.Validate())
        Dim parallel = good.Clone()
        parallel.Y1.Pixel = New PointD(100, 450) : parallel.Y2.Pixel = New PointD(300, 452)
        Check("parallel axes rejected", parallel.Validate() IsNot Nothing AndAlso parallel.Validate().Contains("same direction"), parallel.Validate())
        Dim samePlace = good.Clone() : samePlace.Y2.Pixel = samePlace.Y1.Pixel
        Check("reference points at same place rejected", samePlace.Validate() IsNot Nothing AndAlso samePlace.Validate().Contains("same place"), samePlace.Validate())
        Dim noValue = good.Clone() : noValue.Y2.Value = Nothing
        Check("missing value reported", noValue.Validate() = "Type the value of Y2.", noValue.Validate())
    End Sub

    Sub TestDataTools()
        Dim cal = SimpleCalibration()
        ' y = x² traced out of order, with a duplicate.
        Dim s = SeriesFromData(cal, New PointD(3, 9), New PointD(1, 1), New PointD(0, 0), New PointD(2, 4), New PointD(2, 4), New PointD(4, 16))
        Check("remove duplicates removes 1", DataTools.RemoveDuplicates(s) = 1)
        DataTools.SortByX(s, cal)
        Dim xs = s.DataPoints(cal).Select(Function(p) Math.Round(p.X, 6)).ToList()
        Check("sort by X", xs.SequenceEqual({0.0, 1, 2, 3, 4}), String.Join(",", xs))
        Near("interpolate at 2.5", DataTools.InterpolateAt(s, cal, 2.5).Value, 6.5)
        Check("interpolate outside range is Nothing", Not DataTools.InterpolateAt(s, cal, 5).HasValue)
        Dim stats = DataTools.Statistics(s, cal)
        Check("stats count", stats.Count = 5)
        Near("stats max Y", stats.MaxY, 16)
        ' Trapezoid area of y = x² sampled at 0..4: (0+1)/2+(1+4)/2+(4+9)/2+(9+16)/2 = 22
        Near("area (trapezoid)", stats.Area, 22)

        Dim byCount = s.Clone()
        DataTools.ResampleByCount(byCount, cal, 9)
        Dim r = byCount.DataPoints(cal)
        Check("resample to 9 points", r.Count = 9)
        Near("resampled x[1]", r(1).X, 0.5)
        Near("resampled y at 0.5", r(1).Y, 0.5)
        Near("resampled last", r(8).Y, 16)

        Dim byStep = s.Clone()
        DataTools.ResampleByStep(byStep, cal, 1.5)
        Dim st = byStep.DataPoints(cal).Select(Function(p) Math.Round(p.X, 9)).ToList()
        Check("resample by step 1.5", st.SequenceEqual({0.0, 1.5, 3}), String.Join(",", st))
        Throws(Of ArgumentOutOfRangeException)("step of 0 rejected", Sub() DataTools.ResampleByStep(s.Clone(), cal, 0))
        Throws(Of InvalidOperationException)("too many points rejected", Sub() DataTools.ResampleByStep(s.Clone(), cal, 0.00000001))
    End Sub

    Sub TestLogInterpolation()
        ' On log-log axes y = x² is a straight line, so interpolation at √10 must give exactly 10.
        Dim cal = MakeCalibration(New PointD(80, 600), New PointD(150, 0), New PointD(0, -60), 1, 1000, 1, 1, 1000000, 1, True, True)
        Dim s = SeriesFromData(cal, New PointD(1, 1), New PointD(10, 100), New PointD(100, 10000))
        Near("log-log interpolation", DataTools.InterpolateAt(s, cal, Math.Sqrt(10)).Value, 10, 0.000000001)
        DataTools.ResampleByCount(s, cal, 5)
        Dim xs = s.DataPoints(cal).Select(Function(p) p.X).ToList()
        Near("log resample is even in log space", xs(1), Math.Sqrt(10), 0.000000001)
        Throws(Of InvalidOperationException)("step resample refused on log axis", Sub() DataTools.ResampleByStep(s, cal, 1))
    End Sub

    Sub TestCurveFits()
        Dim line = Enumerable.Range(0, 10).Select(Function(i) New PointD(i, 3 * i - 2)).ToList()
        Dim f = CurveFit.Fit(line, FitKind.Linear)
        Near("linear R²", f.RSquared, 1)
        Near("linear evaluate", f.Evaluate(20), 58)
        Check("linear equation text", f.Equation = "y = 3x − 2", f.Equation)

        ' Cubic over years: badly conditioned without centring.
        Dim cubic = Enumerable.Range(1990, 31).Select(Function(yr) New PointD(yr, 0.002 * (yr - 2000) ^ 3 - 0.5 * (yr - 2000) + 7)).ToList()
        Dim f3 = CurveFit.Fit(cubic, FitKind.Polynomial3)
        Near("cubic over years R²", f3.RSquared, 1, 0.0000001)
        Near("cubic over years evaluate", f3.Evaluate(2013.5), 0.002 * 13.5 ^ 3 - 0.5 * 13.5 + 7, 0.0000001)
        Check("cubic equation has x³", f3.Equation.Contains("x³"), f3.Equation)

        Dim expo = Enumerable.Range(0, 12).Select(Function(i) New PointD(i, 2 * Math.Exp(0.3 * i))).ToList()
        Dim fe = CurveFit.Fit(expo, FitKind.Exponential)
        Near("exponential evaluate", fe.Evaluate(5), 2 * Math.Exp(1.5), 0.0000001)
        Dim pow = Enumerable.Range(1, 12).Select(Function(i) New PointD(i, 1.5 * Math.Pow(i, 0.7))).ToList()
        Near("power evaluate", CurveFit.Fit(pow, FitKind.Power).Evaluate(7.5), 1.5 * Math.Pow(7.5, 0.7), 0.0000001)
        Dim lg = Enumerable.Range(1, 12).Select(Function(i) New PointD(i, 4 - 2 * Math.Log(i))).ToList()
        Dim fl = CurveFit.Fit(lg, FitKind.Logarithmic)
        Near("logarithmic evaluate", fl.Evaluate(3.3), 4 - 2 * Math.Log(3.3), 0.0000001)
        Check("logarithmic equation", fl.Equation.Contains("ln(x)") AndAlso fl.Equation.Contains("−"), fl.Equation)

        Dim noisy = Enumerable.Range(0, 50).Select(Function(i) New PointD(i, i + If(i Mod 2 = 0, 3, -3))).ToList()
        Dim fn = CurveFit.Fit(noisy, FitKind.Linear)
        Check("noisy data R² below 1", fn.RSquared < 0.99 AndAlso fn.RSquared > 0.9, fn.RSquared.ToString())

        Throws(Of InvalidOperationException)("order 4 needs 5 points", Sub() CurveFit.Fit(line.Take(4), FitKind.Polynomial4))
        Throws(Of InvalidOperationException)("exponential needs positive Y", Sub() CurveFit.Fit({New PointD(1, -1), New PointD(2, -2)}, FitKind.Exponential))
    End Sub

    Sub TestAutoTrace()
        ' White 400×300 image with a red sine curve 3 px thick, a gap (x 180–195), and a blue line crossing it.
        Const w = 400, h = 300
        Dim pixels(w * h - 1) As Integer
        Dim white = &HFFFFFFFF, red = &HFFDD2222, blue = &HFF2244CC
        For i = 0 To pixels.Length - 1
            pixels(i) = white
        Next
        Dim truth = Function(x As Double) 150 + 80 * Math.Sin(x / 60)
        For x = 0 To w - 1
            Dim yc = truth(x)
            For y = 0 To h - 1
                If x >= 180 AndAlso x <= 195 Then Continue For
                If Math.Abs(y - yc) <= 1.5 Then pixels(y * w + x) = red
            Next
            ' A blue straight line from top-left to bottom-right crosses the red curve.
            Dim yb = CInt(x * 0.7)
            If yb < h Then
                pixels(yb * w + x) = blue
                If yb + 1 < h Then pixels((yb + 1) * w + x) = blue
            End If
        Next
        Dim image As New PixelBuffer(w, h, pixels)

        Dim picked = AutoTracer.PickCurveColor(image, 100, CInt(truth(100)) + 4)
        Check("color picked near the curve is red", picked.HasValue AndAlso AutoTracer.ColorDistance(picked.Value, red) < 1)

        Dim pts = AutoTracer.Trace(image, 100, CInt(truth(100)), red, New AutoTraceOptions With {.StepPixels = 4})
        Dim minX = pts.Min(Function(p) p.X), maxX = pts.Max(Function(p) p.X)
        Check("auto trace covers the whole width (bridging the gap)", minX <= 4 AndAlso maxX >= 395, String.Format("{0}..{1}", minX, maxX))
        Dim worst = pts.Max(Function(p) Math.Abs(p.Y - truth(p.X)))
        Check("auto trace stays on the red curve (±1.5 px)", worst <= 1.5, "worst " & worst)
        Check("points are ordered left to right", pts.Zip(pts.Skip(1), Function(a, b) a.X < b.X).All(Function(ok) ok))
        Check("clicking on empty space finds nothing", AutoTracer.Trace(image, 100, 20, red, New AutoTraceOptions()).Count = 0)

        Dim limited = AutoTracer.Trace(image, 100, CInt(truth(100)), red, New AutoTraceOptions With {.StepPixels = 4}, (50, 0, 300, h - 1))
        Check("auto trace respects the area limits", limited.Min(Function(p) p.X) >= 50 AndAlso limited.Max(Function(p) p.X) <= 300)
    End Sub

    Sub TestExporters()
        Dim cal = SimpleCalibration()
        Dim a = SeriesFromData(cal, New PointD(1, 2.5), New PointD(2, 5))
        a.Name = "Sample, A"
        Dim b = SeriesFromData(cal, New PointD(3, 7))
        b.Name = "Sample [B]"
        Dim csv = Exporters.ToCsv({a, b}, cal).Replace(vbCrLf, vbLf).TrimEnd()
        Check("CSV content", csv = "Series,X,Y" & vbLf & """Sample, A"",1,2.5" & vbLf & """Sample, A"",2,5" & vbLf & "Sample [B],3,7", csv)
        Dim tsv = Exporters.ToTabSeparated(a, cal)
        Check("tab-separated has header", tsv.StartsWith("X" & vbTab & "Y"))

        Dim path = IO.Path.Combine(IO.Path.GetTempPath(), "graphing-test.xlsx")
        Exporters.SaveXlsx(path, {a, b, New DataSeries With {.Name = "Sample [B]"}}, cal)
        Using zip = ZipFile.OpenRead(path)
            Dim names = zip.Entries.Select(Function(e) e.FullName).ToList()
            Check("xlsx has 3 sheets", names.Contains("xl/worksheets/sheet3.xml") AndAlso names.Contains("xl/workbook.xml"), String.Join(" ", names))
            Using reader As New StreamReader(zip.GetEntry("xl/workbook.xml").Open())
                Dim wb = reader.ReadToEnd()
                Check("xlsx sheet names cleaned and unique", wb.Contains("name=""Sample, A""") AndAlso wb.Contains("name=""Sample _B_""") AndAlso wb.Contains("name=""Sample _B_ (2)"""), wb)
            End Using
            Using reader As New StreamReader(zip.GetEntry("xl/worksheets/sheet1.xml").Open())
                Dim sheet = reader.ReadToEnd()
                Check("xlsx numbers written", sheet.Contains("<v>2.5</v>") AndAlso sheet.Contains("<v>5</v>"), sheet)
            End Using
        End Using
        Console.WriteLine("      (xlsx written to {0})", path)
    End Sub

    Sub TestProjectFiles()
        Dim project As New GraphProject With {.ImageFileName = "scan.png", .ImageData = {1, 2, 3, 250}, .Calibration = SimpleCalibration()}
        project.Calibration.LogY = False
        Dim s = project.CreateSeries()
        s.Points.Add(New PointD(10.25, 20.5))
        project.Series.Add(s)
        project.Series.Add(project.CreateSeries())
        Check("new series names are unique", project.Series(1).Name = "Series 2" AndAlso project.Series(0).ColorArgb <> project.Series(1).ColorArgb)

        Dim path = IO.Path.Combine(IO.Path.GetTempPath(), "graphing-test" & GraphProject.FileExtension)
        project.Save(path)
        Dim loaded = GraphProject.Load(path)
        Check("project image round-trips", loaded.ImageData.SequenceEqual(project.ImageData) AndAlso loaded.ImageFileName = "scan.png")
        Check("project calibration round-trips", loaded.Calibration.IsComplete AndAlso loaded.Calibration.X2.Value.GetValueOrDefault() = 50)
        Check("project points round-trip", loaded.Series(0).Points(0).X = 10.25 AndAlso loaded.Series.Count = 2)

        Dim partial1 As New GraphProject
        partial1.Calibration.X1.Pixel = New PointD(3, 4)
        partial1.Save(path)
        Dim loadedPartial = GraphProject.Load(path)
        Check("unset calibration values stay unset", loadedPartial.Calibration.X1.Pixel.HasValue AndAlso Not loadedPartial.Calibration.X1.Value.HasValue AndAlso Not loadedPartial.Calibration.Y2.Pixel.HasValue)

        File.WriteAllText(path, "not json")
        Throws(Of InvalidDataException)("damaged project rejected", Sub() GraphProject.Load(path))
        File.WriteAllText(path, "{""Version"": 99}")
        Throws(Of InvalidDataException)("newer version rejected", Sub() GraphProject.Load(path))
    End Sub

    Sub TestUndo()
        Dim project As New GraphProject
        Dim history As New UndoHistory
        project.Series.Add(project.CreateSeries())
        history.Record(project.CaptureState())
        project.Series(0).Points.Add(New PointD(1, 1))
        history.Record(project.CaptureState())
        project.Series(0).Points.Add(New PointD(2, 2))
        project.RestoreState(history.Undo(project.CaptureState()))
        Check("undo removes last point", project.Series(0).Points.Count = 1)
        project.RestoreState(history.Undo(project.CaptureState()))
        Check("undo twice", project.Series(0).Points.Count = 0 AndAlso Not history.CanUndo)
        project.RestoreState(history.Redo(project.CaptureState()))
        project.RestoreState(history.Redo(project.CaptureState()))
        Check("redo twice", project.Series(0).Points.Count = 2 AndAlso Not history.CanRedo)
        history.Record(project.CaptureState())
        Check("new change clears redo", Not history.CanRedo AndAlso history.CanUndo)
    End Sub

    Sub TestTicks()
        Dim t = AxisTicks.Generate(0, 100, 10, False)
        Check("linear ticks 0..100", t.SequenceEqual({0.0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100}), String.Join(",", t))
        Dim t2 = AxisTicks.Generate(-0.3, 0.3, 6, False)
        Check("ticks include exact zero", t2.Contains(0.0), String.Join(",", t2))
        Dim lg = AxisTicks.Generate(1, 1000, 10, True)
        Check("log ticks", lg.Contains(1) AndAlso lg.Contains(2) AndAlso lg.Contains(500) AndAlso lg.Contains(1000), String.Join(",", lg))
    End Sub

    Sub TestNumberParsing()
        Dim v As Double
        Check("parse 1e-3", DataTools.TryParseNumber("1e-3", v) AndAlso v = 0.001)
        Check("parse -2.5", DataTools.TryParseNumber(" -2.5 ", v) AndAlso v = -2.5)
        Check("reject text", Not DataTools.TryParseNumber("abc", v))
        Check("reject lone minus", Not DataTools.TryParseNumber("-", v))
        Check("reject empty", Not DataTools.TryParseNumber("", v))
    End Sub
End Module
