''' <summary>Measured quantities recorded for the plotter, plus cycle time.</summary>
Partial Public Class Simulator

    Private Const ChannelInterval As Double = 0.02
    Private Const ChannelLength As Double = 120
    Private _nextChannelSample As Double
    Private _lastCycles As Integer
    Private _lastCycleEnd As Double

    ''' <summary>Recorded (time, value) samples by channel name, e.g. "1A position (mm)".</summary>
    Public ReadOnly Property Channels As New Dictionary(Of String, List(Of PointF))

    ''' <summary>Duration of the last complete cycle in seconds, or 0.</summary>
    Public Property LastCycleTime As Double

    Private Shared Function NameOf_(e As CircuitElement, fallback As String, index As Integer) As String
        Return If(String.IsNullOrWhiteSpace(e.Label), $"{fallback} {index}", e.Label)
    End Function

    ''' <summary>The current value of every measurable quantity.</summary>
    Public Function ChannelValues() As List(Of (Name As String, Value As Double))
        Dim list As New List(Of (String, Double))
        Dim i = 0
        For Each cyl In _circuit.Elements.OfType(Of CylinderBase)()
            i += 1
            Dim n = NameOf_(cyl, "cylinder", i)
            list.Add(($"{n} position (mm)", cyl.Position * cyl.StrokeLength))
            list.Add(($"{n} speed (m/s)", cyl.Velocity))
            list.Add(($"{n} pressure, cap side (bar)", If(RealPhysics, cyl.CapPressure, cyl.Ports(0).Pressure)))
            If cyl.Ports.Count > 1 Then list.Add(($"{n} pressure, rod side (bar)", If(RealPhysics, cyl.RodPressure, cyl.Ports(1).Pressure)))
        Next
        i = 0
        For Each g In _circuit.Elements.OfType(Of PressureGauge)()
            i += 1
            list.Add(($"{NameOf_(g, "gauge", i)} pressure (bar)", g.Ports(0).Pressure))
        Next
        For Each v In _circuit.Elements.OfType(Of DirectionalValve)().Where(Function(x) Not String.IsNullOrWhiteSpace(x.Label))
            list.Add(($"{v.Label} valve position", v.DiagramValue))
        Next
        For Each k In _circuit.Elements.OfType(Of ElectricCoil)().Where(Function(x) Not String.IsNullOrWhiteSpace(x.Label))
            list.Add(($"{k.Label} {If(k.Kind = CoilKind.Lamp, "lamp", "coil")} (on = 1)", If(k.Active, 1, 0)))
        Next
        i = 0
        For Each m In _circuit.Elements.OfType(Of HydraulicMotor)()
            i += 1
            list.Add(($"{NameOf_(m, "motor", i)} speed (rpm)", m.Rpm))
        Next
        For Each a In _circuit.Elements.OfType(Of Accumulator)()
            list.Add(($"{NameOf_(a, "accumulator", 1)} oil volume (l)", a.StoredLitres))
        Next
        list.Add(("Air consumption (normal litres)", AirConsumed))
        Return list
    End Function

    Private Sub ResetChannels()
        Channels.Clear()
        _nextChannelSample = 0
        _lastCycles = 0
        _lastCycleEnd = 0
        LastCycleTime = 0
    End Sub

    Private Sub RecordChannels()
        Dim cycles = CompletedCycles()
        If cycles > _lastCycles Then
            LastCycleTime = (Time - _lastCycleEnd) / (cycles - _lastCycles)
            _lastCycles = cycles
            _lastCycleEnd = Time
        End If
        If Time + 0.000001 < _nextChannelSample Then Return
        _nextChannelSample = Time + ChannelInterval
        For Each cv In ChannelValues()
            Dim list As List(Of PointF) = Nothing
            If Not Channels.TryGetValue(cv.Name, list) Then
                list = New List(Of PointF)
                Channels(cv.Name) = list
            End If
            list.Add(New PointF(CSng(Time), CSng(cv.Value)))
            If list(0).X < Time - ChannelLength Then list.RemoveAt(0)
        Next
    End Sub

    ''' <summary>Air consumption and running-cost figures for the status line and reports.</summary>
    Public Function CostSummary(info As ProjectInfo) As String
        Dim cycles = CompletedCycles()
        If AirConsumed <= 0.0001 Then Return ""
        Dim perCycle = If(cycles > 0, AirConsumed / cycles, AirConsumed)
        Dim perYearM3 = perCycle * info.CyclesPerMinute * 60 * info.HoursPerDay * info.DaysPerYear / 1000
        Dim cost = perYearM3 * info.AirCostPerM3
        Return $"Air: {perCycle:0.00} NL/{If(cycles > 0, "cycle", "so far")}" &
               If(LastCycleTime > 0, $"   cycle time {LastCycleTime:0.00} s", "") &
               $"   ≈ {perYearM3:0} m³/year → ₹{cost:#,##0}/year at {info.CyclesPerMinute:0.#} cycles/min"
    End Function
End Class
