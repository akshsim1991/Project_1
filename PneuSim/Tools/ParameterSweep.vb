Imports System.Globalization
Imports System.Text

''' <summary>What to vary in a parameter sweep and how to run each test.</summary>
Public Class SweepSettings
    ''' <summary>Index of the component in <see cref="Project.AllElements"/>.</summary>
    Public Property ElementIndex As Integer
    Public Property Knob As LiveTuning.Knob
    Public Property FromValue As Double
    Public Property ToValue As Double
    Public Property Steps As Integer = 6
    ''' <summary>Simulated seconds per test run.</summary>
    Public Property Seconds As Double = 10
    ''' <summary>Indices (in <see cref="Project.AllElements"/>) of buttons and switches operated for the whole run.</summary>
    Public Property Operate As New List(Of Integer)
    Public Property RealPhysics As Boolean
End Class

''' <summary>The results: one row per tested value.</summary>
Public Class SweepResult
    Public Property Columns As New List(Of String)
    Public Property Rows As New List(Of Double())

    Public Function ToCsv() As String
        Dim inv = CultureInfo.InvariantCulture
        Dim q = Function(s As String) """" & s.Replace("""", """""") & """"
        Dim sb As New StringBuilder()
        sb.AppendLine(String.Join(",", Columns.Select(q)))
        For Each r In Rows
            sb.AppendLine(String.Join(",", r.Select(Function(v) If(Double.IsNaN(v), "", v.ToString("0.####", inv)))))
        Next
        Return sb.ToString()
    End Function
End Class

''' <summary>
''' Runs the circuit several times with one setting changed step by step (e.g. supply pressure 3
''' to 8 bar) and measures cycle time, air consumption, cylinder timing and speed.
''' Works on a copy of the project, so the drawing is not touched.
''' </summary>
Public Module ParameterSweep

    Public Const TimeStep As Double = 0.005

    Public Function Run(project As Project, settings As SweepSettings, Optional progress As Action(Of Integer) = Nothing) As SweepResult
        Dim result As New SweepResult()
        Dim n = Math.Max(2, Math.Min(50, settings.Steps))
        Dim template = project.ToXml()
        Dim cylNames As List(Of String) = Nothing
        For k = 0 To n - 1
            Dim value = settings.FromValue + (settings.ToValue - settings.FromValue) * k / (n - 1)
            Dim copy = Project.FromXml(template)
            Dim all = copy.AllElements().ToList()
            Dim circuit = copy.SimulationCircuit()
            LiveTuning.SetValue(all(settings.ElementIndex), settings.Knob, value)
            Dim actual = LiveTuning.GetValue(all(settings.ElementIndex), settings.Knob)
            Dim cyls = circuit.Elements.OfType(Of CylinderBase)().ToList()
            If cylNames Is Nothing Then
                cylNames = cyls.Select(Function(c, i) If(String.IsNullOrWhiteSpace(c.Label), $"cylinder {i + 1}", c.Label)).ToList()
                result.Columns.Add(settings.Knob.Caption)
                result.Columns.Add("Cycle time (s)")
                result.Columns.Add("Air per cycle (NL)")
                result.Columns.Add("Air used in the run (NL)")
                For Each nm In cylNames
                    result.Columns.Add($"{nm} first fully out at (s)")
                    result.Columns.Add($"{nm} top speed (m/s)")
                Next
            End If
            Dim sim As New Simulator(circuit) With {.RealPhysics = settings.RealPhysics}
            sim.Reset()
            For Each i In settings.Operate
                If i >= 0 AndAlso i < all.Count Then
                    Dim el = all(i)
                    Dim b = el.LocalBounds
                    el.OnSimMouseDown(New PointF(b.Left + 2, b.Top + b.Height / 2))
                End If
            Next
            sim.RunLogic()
            Dim firstOut = cyls.Select(Function(c) Double.NaN).ToArray()
            Dim topSpeed = cyls.Select(Function(c) 0.0).ToArray()
            Dim steps = CInt(settings.Seconds / TimeStep)
            For s = 1 To steps
                sim.Step(TimeStep)
                For ci = 0 To cyls.Count - 1
                    If Double.IsNaN(firstOut(ci)) AndAlso cyls(ci).Position >= 0.995 Then firstOut(ci) = sim.Time
                    topSpeed(ci) = Math.Max(topSpeed(ci), Math.Abs(cyls(ci).Velocity))
                Next
            Next
            Dim cycles = sim.CompletedCycles()
            Dim row As New List(Of Double) From {
                actual,
                If(sim.LastCycleTime > 0, sim.LastCycleTime, Double.NaN),
                If(cycles > 0, sim.AirConsumed / cycles, Double.NaN),
                sim.AirConsumed}
            For ci = 0 To cyls.Count - 1
                row.Add(firstOut(ci))
                row.Add(topSpeed(ci))
            Next
            result.Rows.Add(row.ToArray())
            progress?.Invoke(CInt((k + 1) * 100 / n))
        Next
        Return result
    End Function
End Module
