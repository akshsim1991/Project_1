Imports System.Globalization
Imports System.Text

Public Enum FitKind
    Linear
    Polynomial2
    Polynomial3
    Polynomial4
    Polynomial5
    Exponential
    Power
    Logarithmic
End Enum

''' <summary>The outcome of fitting a curve to data.</summary>
Public Class FitResult
    Public Property Kind As FitKind
    ''' <summary>Human-readable equation, e.g. "y = 2.5x + 1.2".</summary>
    Public Property Equation As String
    ''' <summary>Coefficient of determination (1 = perfect fit).</summary>
    Public Property RSquared As Double
    Public Property PointsUsed As Integer
    ''' <summary>Evaluates the fitted curve at an X value (NaN where it is undefined, e.g. ln of a negative).</summary>
    Public Property Evaluate As Func(Of Double, Double)
End Class

''' <summary>Least-squares curve fitting.</summary>
Public NotInheritable Class CurveFit
    Private Sub New()
    End Sub

    Public Shared ReadOnly KindNames As IReadOnlyDictionary(Of FitKind, String) = New Dictionary(Of FitKind, String) From {
        {FitKind.Linear, "Linear:  y = a + bx"},
        {FitKind.Polynomial2, "Polynomial, order 2"},
        {FitKind.Polynomial3, "Polynomial, order 3"},
        {FitKind.Polynomial4, "Polynomial, order 4"},
        {FitKind.Polynomial5, "Polynomial, order 5"},
        {FitKind.Exponential, "Exponential:  y = a·e^(bx)"},
        {FitKind.Power, "Power:  y = a·x^b"},
        {FitKind.Logarithmic, "Logarithmic:  y = a + b·ln(x)"}
    }

    ''' <summary>Fits <paramref name="kind"/> to the points. Throws InvalidOperationException with a readable message when impossible.</summary>
    Public Shared Function Fit(points As IEnumerable(Of PointD), kind As FitKind) As FitResult
        Dim data = points.Where(Function(p) p.IsValid).ToList()
        Select Case kind
            Case FitKind.Exponential
                data = data.Where(Function(p) p.Y > 0).ToList()
                Dim c = LinearFitOf(data, Function(p) p.X, Function(p) Math.Log(p.Y), "Exponential fits need Y values above 0.")
                Dim a = Math.Exp(c.Intercept), b = c.Slope
                Return Finish(kind, data, Function(x) a * Math.Exp(b * x),
                              String.Format("y = {0}·e^({1}x)", Num(a), Num(b)))
            Case FitKind.Power
                data = data.Where(Function(p) p.X > 0 AndAlso p.Y > 0).ToList()
                Dim c = LinearFitOf(data, Function(p) Math.Log(p.X), Function(p) Math.Log(p.Y), "Power fits need X and Y values above 0.")
                Dim a = Math.Exp(c.Intercept), b = c.Slope
                Return Finish(kind, data, Function(x) If(x > 0, a * Math.Pow(x, b), Double.NaN),
                              String.Format("y = {0}·x^{1}", Num(a), Num(b)))
            Case FitKind.Logarithmic
                data = data.Where(Function(p) p.X > 0).ToList()
                Dim c = LinearFitOf(data, Function(p) Math.Log(p.X), Function(p) p.Y, "Logarithmic fits need X values above 0.")
                Dim a = c.Intercept, b = c.Slope
                Return Finish(kind, data, Function(x) If(x > 0, a + b * Math.Log(x), Double.NaN),
                              String.Format("y = {0} {1} {2}·ln(x)", Num(a), If(b < 0, "−", "+"), Num(Math.Abs(b))))
            Case Else
                Dim degree = If(kind = FitKind.Linear, 1, CInt(kind) - CInt(FitKind.Polynomial2) + 2)
                Return FitPolynomial(data, degree, kind)
        End Select
    End Function

    Private Shared Function FitPolynomial(data As List(Of PointD), degree As Integer, kind As FitKind) As FitResult
        Dim distinctX = data.Select(Function(p) p.X).Distinct().Count()
        If distinctX <= degree Then
            Throw New InvalidOperationException(String.Format("An order-{0} fit needs at least {1} points with different X values.", degree, degree + 1))
        End If
        ' Centre and scale X so large values such as years (1990-2020) do not make the problem ill-conditioned.
        Dim mean = data.Average(Function(p) p.X)
        Dim spread = Math.Sqrt(data.Average(Function(p) (p.X - mean) * (p.X - mean)))
        If spread = 0 Then spread = 1
        Dim rows = data.Count, cols = degree + 1
        Dim a(rows - 1, cols - 1) As Double
        Dim y(rows - 1) As Double
        For i = 0 To rows - 1
            Dim t = (data(i).X - mean) / spread
            Dim power = 1.0
            For j = 0 To cols - 1
                a(i, j) = power
                power *= t
            Next
            y(i) = data(i).Y
        Next
        Dim scaled = SolveLeastSquares(a, y)
        Dim evaluate As Func(Of Double, Double) =
            Function(x)
                Dim t = (x - mean) / spread, sum = 0.0
                For k = degree To 0 Step -1
                    sum = sum * t + scaled(k)
                Next
                Return sum
            End Function
        Return Finish(kind, data, evaluate, PolynomialEquation(ToRawCoefficients(scaled, mean, spread)))
    End Function

    ''' <summary>Converts coefficients of t = (x - mean) / spread back to coefficients of x.</summary>
    Private Shared Function ToRawCoefficients(scaled As Double(), mean As Double, spread As Double) As Double()
        Dim n = scaled.Length
        Dim raw(n - 1) As Double
        For k = 0 To n - 1
            ' c_k * ((x - m) / s)^k = c_k / s^k * sum_j C(k,j) x^j (-m)^(k-j)
            Dim factor = scaled(k) / Math.Pow(spread, k)
            For j = 0 To k
                raw(j) += factor * Binomial(k, j) * Math.Pow(-mean, k - j)
            Next
        Next
        Return raw
    End Function

    Private Shared Function Binomial(n As Integer, k As Integer) As Double
        Dim result = 1.0
        For i = 1 To k
            result = result * (n - k + i) / i
        Next
        Return result
    End Function

    Private Structure LineCoefficients
        Public Intercept As Double
        Public Slope As Double
    End Structure

    Private Shared Function LinearFitOf(data As List(Of PointD), fx As Func(Of PointD, Double), fy As Func(Of PointD, Double), requirement As String) As LineCoefficients
        If data.Select(fx).Distinct().Count() < 2 Then
            Throw New InvalidOperationException(requirement & " At least 2 such points with different X values are needed.")
        End If
        Dim a(data.Count - 1, 1) As Double
        Dim y(data.Count - 1) As Double
        For i = 0 To data.Count - 1
            a(i, 0) = 1
            a(i, 1) = fx(data(i))
            y(i) = fy(data(i))
        Next
        Dim c = SolveLeastSquares(a, y)
        Return New LineCoefficients With {.Intercept = c(0), .Slope = c(1)}
    End Function

    Private Shared Function Finish(kind As FitKind, data As List(Of PointD), evaluate As Func(Of Double, Double), equation As String) As FitResult
        Dim meanY = data.Average(Function(p) p.Y)
        Dim ssTot = 0.0, ssRes = 0.0
        For Each p In data
            Dim r = p.Y - evaluate(p.X)
            ssRes += r * r
            ssTot += (p.Y - meanY) * (p.Y - meanY)
        Next
        Dim r2 = If(ssTot > 0, 1 - ssRes / ssTot, If(ssRes < 0.000000000001, 1.0, 0.0))
        Return New FitResult With {.Kind = kind, .Equation = equation, .RSquared = r2, .PointsUsed = data.Count, .Evaluate = evaluate}
    End Function

    ''' <summary>Solves min |A c - y| with Householder QR (stable even when the normal equations would not be).</summary>
    Private Shared Function SolveLeastSquares(a As Double(,), y As Double()) As Double()
        Dim m = a.GetLength(0), n = a.GetLength(1)
        Dim r = CType(a.Clone(), Double(,))
        Dim b = CType(y.Clone(), Double())
        For k = 0 To n - 1
            Dim norm = 0.0
            For i = k To m - 1
                norm += r(i, k) * r(i, k)
            Next
            norm = Math.Sqrt(norm)
            If norm = 0 Then Throw New InvalidOperationException("The points do not contain enough information for this fit.")
            Dim alpha = If(r(k, k) > 0, -norm, norm)
            Dim v(m - 1) As Double
            v(k) = r(k, k) - alpha
            For i = k + 1 To m - 1
                v(i) = r(i, k)
            Next
            Dim vNorm2 = 0.0
            For i = k To m - 1
                vNorm2 += v(i) * v(i)
            Next
            If vNorm2 = 0 Then Continue For
            For j = k To n - 1
                Dim dot = 0.0
                For i = k To m - 1
                    dot += v(i) * r(i, j)
                Next
                Dim f = 2 * dot / vNorm2
                For i = k To m - 1
                    r(i, j) -= f * v(i)
                Next
            Next
            Dim dotB = 0.0
            For i = k To m - 1
                dotB += v(i) * b(i)
            Next
            Dim fb = 2 * dotB / vNorm2
            For i = k To m - 1
                b(i) -= fb * v(i)
            Next
        Next
        Dim c(n - 1) As Double
        For k = n - 1 To 0 Step -1
            Dim sum = b(k)
            For j = k + 1 To n - 1
                sum -= r(k, j) * c(j)
            Next
            If Math.Abs(r(k, k)) < 0.000000000001 Then Throw New InvalidOperationException("The points do not contain enough information for this fit.")
            c(k) = sum / r(k, k)
        Next
        Return c
    End Function

    Private Shared Function PolynomialEquation(coefficients As Double()) As String
        Dim text As New StringBuilder("y =")
        Dim first = True
        For k = coefficients.Length - 1 To 0 Step -1
            Dim c = coefficients(k)
            If c = 0 AndAlso Not (first AndAlso k = 0) Then Continue For
            Dim sign = If(c < 0, "−", "+")
            If first Then
                text.Append(If(c < 0, " −", " "))
            Else
                text.Append(" " & sign & " ")
            End If
            text.Append(Num(Math.Abs(c)))
            If k >= 1 Then text.Append("x")
            If k >= 2 Then text.Append(Superscript(k))
            first = False
        Next
        Return text.ToString()
    End Function

    Private Shared Function Superscript(n As Integer) As String
        Const digits = "⁰¹²³⁴⁵⁶⁷⁸⁹"
        Return String.Concat(n.ToString(CultureInfo.InvariantCulture).Select(Function(ch) digits(AscW(ch) - AscW("0"c))))
    End Function

    Private Shared Function Num(value As Double) As String
        Return value.ToString("G6", CultureInfo.CurrentCulture)
    End Function
End Class
