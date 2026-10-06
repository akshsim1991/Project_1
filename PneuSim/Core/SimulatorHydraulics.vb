''' <summary>
''' Hydraulic model. Oil is incompressible, so:
''' - actuator speed = available flow / area (or displacement);
''' - system pressure = what the moving load needs, rising to the relief setting when nothing
'''   can move (the pump flow then goes over the relief valve);
''' - a path straight back to tank (tandem centre) unloads the pump at low pressure.
''' </summary>
Partial Public Class Simulator

    ''' <summary>Total delivery of the running pumps, l/min.</summary>
    Public Property HydraulicPumpFlow As Double

    ''' <summary>Flow available to each moving hydraulic actuator, l/min.</summary>
    Public Property HydraulicFlowShare As Double

    Private ReadOnly _accumulatorFed As New Dictionary(Of Accumulator, Boolean)

    ''' <summary>Called before the actuators move: shares the flow among the moving consumers.</summary>
    Private Sub PrepareHydraulics(dt As Double)
        Dim pumps = _circuit.Elements.OfType(Of HydraulicPump)().Where(Function(p) p.Running).ToList()
        HydraulicPumpFlow = pumps.Sum(Function(p) p.EffectiveFlowLpm)
        Dim movers = _circuit.Elements.OfType(Of IHydraulicConsumer)().Count(Function(c) c.WantsFlow())
        Dim accumulatorFlow = 0.0
        For Each acc In _circuit.Elements.OfType(Of Accumulator)()
            Dim fed As Boolean
            _accumulatorFed.TryGetValue(acc, fed)
            If Not fed AndAlso acc.StoredLitres > 0.001 Then accumulatorFlow += 20
        Next
        Dim total = HydraulicPumpFlow + accumulatorFlow
        HydraulicFlowShare = total / Math.Max(1, movers)
        For Each acc In _circuit.Elements.OfType(Of Accumulator)()
            Dim fed As Boolean
            _accumulatorFed.TryGetValue(acc, fed)
            Dim drawn = If(fed OrElse movers = 0, 0, Math.Min(20, HydraulicFlowShare * movers))
            acc.Exchange(acc.Ports(0).Pressure, fed, drawn, dt)
        Next
    End Sub

    Private Sub AfterSolve()
        ApplyVacuum()
        Dim n = _ports.Count
        If Not _ports.Any(Function(p) p.Kind = PortKind.Hydraulic) Then Return

        ' Groups of pressurized hydraulic ports joined by open passages.
        Dim parent(n - 1) As Integer
        For i = 0 To n - 1
            parent(i) = i
        Next
        Dim find As Func(Of Integer, Integer) =
            Function(i)
                While parent(i) <> i
                    parent(i) = parent(parent(i))
                    i = parent(i)
                End While
                Return i
            End Function
        Dim live = Function(i As Integer) _ports(i).Kind = PortKind.Hydraulic AndAlso _ports(i).State = PortState.Pressurized
        For Each ed In _edges
            ' A pressure reducing valve separates two pressure levels.
            If Not Double.IsPositiveInfinity(ed.LimitAB) Then Continue For
            If (ed.AB > 0 OrElse ed.BA > 0) AndAlso live(ed.A) AndAlso live(ed.B) Then
                Dim ra = find(ed.A), rb = find(ed.B)
                If ra <> rb Then parent(ra) = rb
            End If
        Next

        Dim pumpRoots As New Dictionary(Of Integer, Double)          ' root -> pump max pressure
        For Each pump In _circuit.Elements.OfType(Of HydraulicPump)().Where(Function(p) p.Running)
            Dim root = find(pump.Ports(0).NodeIndex)
            pumpRoots(root) = Math.Max(If(pumpRoots.ContainsKey(root), pumpRoots(root), 0), pump.MaxPressureBar)
        Next
        Dim reliefs As New Dictionary(Of Integer, Double)
        For Each rv In _circuit.Elements.OfType(Of ReliefValve)()
            If Not live(rv.Ports(0).NodeIndex) Then Continue For
            Dim root = find(rv.Ports(0).NodeIndex)
            reliefs(root) = Math.Min(If(reliefs.ContainsKey(root), reliefs(root), Double.MaxValue), rv.EffectiveSetting)
        Next
        Dim unloaded As New HashSet(Of Integer)
        For i = 0 To n - 1
            If live(i) AndAlso _exhausts.Contains(i) Then unloaded.Add(find(i))
        Next
        Dim loads As New Dictionary(Of Integer, Double)
        For Each c In _circuit.Elements.OfType(Of IHydraulicConsumer)()
            If Not c.WantsFlow() Then Continue For
            For Each p In DirectCast(c, CircuitElement).Ports
                If Not live(p.NodeIndex) Then Continue For
                Dim root = find(p.NodeIndex)
                loads(root) = Math.Max(If(loads.ContainsKey(root), loads(root), 0), c.LoadPressure)
            Next
        Next

        Dim groupPressure As New Dictionary(Of Integer, Double)
        For i = 0 To n - 1
            If Not live(i) Then Continue For
            Dim root = find(i)
            If groupPressure.ContainsKey(root) Then Continue For
            Dim available = _ports(i).SupplyPressure
            Dim pumpMax As Double
            Dim limit = If(reliefs.ContainsKey(root), reliefs(root), If(pumpRoots.TryGetValue(root, pumpMax), pumpMax, available))
            limit = Math.Min(limit, Math.Max(available, 0.1))
            Dim p As Double
            If unloaded.Contains(root) Then
                p = Math.Min(limit, 3)                  ' oil returns to tank: pump unloaded
            ElseIf loads.ContainsKey(root) Then
                p = Math.Min(limit, loads(root) + 2)    ' pressure set by the moving load
            Else
                p = limit                               ' nothing moves: relief valve opens
            End If
            groupPressure(root) = p
            If pumpRoots.ContainsKey(root) AndAlso Not reliefs.ContainsKey(root) AndAlso Not unloaded.Contains(root) AndAlso Not loads.ContainsKey(root) Then
                Warnings.Add($"No pressure relief valve: with nothing moving the pump pressure rises to {p:0} bar. Add a relief valve after the pump.")
            End If
        Next
        ' The reduced side never exceeds its setting or the pressure in front of the valve.
        For Each ed In _edges
            If Double.IsPositiveInfinity(ed.LimitAB) OrElse Not live(ed.A) OrElse Not live(ed.B) Then Continue For
            Dim up = find(ed.A), down = find(ed.B)
            If up <> down AndAlso groupPressure.ContainsKey(up) AndAlso groupPressure.ContainsKey(down) Then
                groupPressure(down) = Math.Min(groupPressure(down), Math.Min(ed.LimitAB, groupPressure(up)))
            End If
        Next
        For i = 0 To n - 1
            If live(i) Then
                _ports(i).Pressure = groupPressure(find(i))
                _ports(i).SupplyPressure = _ports(i).Pressure
            End If
        Next

        ' Accumulators: fed by a pump, or supplying the system themselves.
        _accumulatorFed.Clear()
        For Each acc In _circuit.Elements.OfType(Of Accumulator)()
            Dim root = find(acc.Ports(0).NodeIndex)
            Dim fed = live(acc.Ports(0).NodeIndex) AndAlso pumpRoots.ContainsKey(root)
            _accumulatorFed(acc) = fed
            acc.Discharging = Not fed
        Next

        ' Overrunning loads press on trapped oil: show that pressure on the cylinder line.
        For Each cyl In _circuit.Elements.OfType(Of HydraulicCylinder)()
            Dim rodPort = cyl.Ports(1)
            If cyl.OverrunPressure <= 0 OrElse rodPort.State <> PortState.Floating Then Continue For
            For Each t In _circuit.Tubes
                If t.A Is rodPort AndAlso t.B.State = PortState.Floating Then t.B.Pressure = cyl.OverrunPressure
                If t.B Is rodPort AndAlso t.A.State = PortState.Floating Then t.A.Pressure = cyl.OverrunPressure
            Next
            rodPort.Pressure = cyl.OverrunPressure
        Next
    End Sub
End Class
