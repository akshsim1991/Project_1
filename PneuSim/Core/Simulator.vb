''' <summary>
''' Solves the pneumatic and electrical networks and advances time.
'''
''' Every port is a node. Tubes, wires, open valve passages and closed contacts are edges. An
''' edge may allow flow in one direction only (check valves) and may limit the pressure passed
''' on (pressure regulators).
''' - Pressurized: reachable from a supply (pneumatic) or from +24 V (electric).
''' - Exhausted: not pressurized, and air can flow from it to an open exhaust (or 0 V).
''' - Floating: neither; pneumatic ports keep the pressure trapped in them.
''' Edge capacities below 1 (flow control valves) slow down the cylinders fed or vented through them.
''' </summary>
Partial Public Class Simulator

    Private Structure Edge
        Public A As Integer
        Public B As Integer
        ''' <summary>Capacity for flow from A to B (0 = blocked).</summary>
        Public AB As Double
        ''' <summary>Capacity for flow from B to A (0 = blocked).</summary>
        Public BA As Double
        ''' <summary>Highest pressure passed on from A to B.</summary>
        Public LimitAB As Double
    End Structure

    ''' <summary>Default pilot pressure (bar) needed to switch a valve.</summary>
    Public Const PilotThreshold As Double = 1.5
    Public Const ControlVoltage As Double = 24
    Private Const MaxLogicPasses As Integer = 60
    Private Const HistoryInterval As Double = 0.05
    Private Const HistoryLength As Double = 60

    Private ReadOnly _circuit As Circuit
    Private _ports As New List(Of Port)
    Private ReadOnly _edges As New List(Of Edge)
    Private ReadOnly _supplies As New Dictionary(Of Integer, Double)
    Private ReadOnly _exhausts As New HashSet(Of Integer)
    Private _nextSample As Double

    Public Sub New(circuit As Circuit)
        _circuit = circuit
    End Sub

    Public ReadOnly Property Circuit As Circuit
        Get
            Return _circuit
        End Get
    End Property

    Public Property Time As Double

    ''' <summary>
    ''' Realistic mode: pressures build up, flows are calculated, cylinders move according to
    ''' bore, load and friction. Ideal mode: pressures switch instantly, speeds follow stroke times.
    ''' </summary>
    Public Property RealPhysics As Boolean

    ''' <summary>Free air drawn from the supplies since the start, normal litres.</summary>
    Public Property AirConsumed As Double

    Public Sub AddAirConsumption(normalLitres As Double)
        If normalLitres > 0 Then AirConsumed += normalLitres
    End Sub

    ''' <summary>Completed machine cycles: the smallest stroke count over all cylinders.</summary>
    Public Function CompletedCycles() As Integer
        Dim cyls = _circuit.Elements.OfType(Of CylinderBase)().ToList()
        Return If(cyls.Count = 0, 0, cyls.Min(Function(c) c.CompletedCycles))
    End Function

    ''' <summary>Problems found during the last solve (open ports blowing air, short circuits, oscillation).</summary>
    Public ReadOnly Property Warnings As New List(Of String)

    ''' <summary>Recorded (time, value) samples for the displacement-step diagram.</summary>
    Public ReadOnly Property History As New Dictionary(Of CircuitElement, List(Of PointF))

    ' ------------------------------------------------------------ used by elements

    Public Sub AddEdge(a As Port, b As Port, Optional capacityAB As Double = 1, Optional capacityBA As Double = 1,
                       Optional pressureLimitAB As Double = Double.PositiveInfinity)
        If a Is Nothing OrElse b Is Nothing Then Return
        _edges.Add(New Edge With {.A = a.NodeIndex, .B = b.NodeIndex, .AB = capacityAB, .BA = capacityBA,
                                  .LimitAB = pressureLimitAB})
    End Sub

    ''' <summary>Registers a pressure source (pneumatic supply, or +24 V for electric ports).</summary>
    Public Sub AddSupply(p As Port, pressure As Double)
        Dim existing As Double
        _supplies.TryGetValue(p.NodeIndex, existing)
        _supplies(p.NodeIndex) = Math.Max(existing, pressure)
    End Sub

    ''' <summary>Registers a port that is always open to atmosphere (silencer) or 0 V.</summary>
    Public Sub AddExhaust(p As Port)
        _exhausts.Add(p.NodeIndex)
    End Sub

    ''' <summary>True if a cylinder carrying the named position mark is at that end position.</summary>
    Public Function IsMarkActive(mark As String) As Boolean
        If String.IsNullOrWhiteSpace(mark) Then Return False
        For Each cyl In _circuit.Elements.OfType(Of CylinderBase)()
            If cyl.IsAtMark(mark) Then Return True
        Next
        Return False
    End Function

    ''' <summary>True if a relay, timer relay or solenoid coil with this label is active.</summary>
    Public Function IsCoilActive(label As String) As Boolean
        If String.IsNullOrWhiteSpace(label) Then Return False
        For Each coil In _circuit.Elements.OfType(Of ElectricCoil)()
            If coil.Active AndAlso String.Equals(coil.Label, label.Trim(), StringComparison.OrdinalIgnoreCase) Then Return True
        Next
        Return False
    End Function

    ' ------------------------------------------------------------ control

    Public Sub Reset()
        Time = 0
        AirConsumed = 0
        ResetChannels()
        _nextSample = 0
        History.Clear()
        Warnings.Clear()
        For Each e In _circuit.Elements
            e.ResetSim()
        Next
        RunLogic()
        If RealPhysics Then
            ' Chambers start at the pressures of the circuit at rest (e.g. rod side pressurized).
            For Each cyl In _circuit.Elements.OfType(Of CylinderBase)()
                cyl.InitPhysics()
            Next
            RunLogic()
        End If
        RecordHistory()
        RecordChannels()
    End Sub

    ''' <summary>Advances the simulation by <paramref name="dt"/> seconds.</summary>
    Public Sub [Step](dt As Double)
        Time += dt
        PrepareHydraulics(dt)
        For Each e In _circuit.Elements
            e.UpdateDynamics(Me, dt)
        Next
        RunLogic()
        RecordHistory()
        RecordChannels()
    End Sub

    ''' <summary>Solves the networks and lets valves, relays and contacts switch until the circuit is stable.</summary>
    Public Sub RunLogic()
        For pass = 1 To MaxLogicPasses
            Solve()
            Dim changed = False
            For Each e In _circuit.Elements
                If e.UpdateLogic(Me) Then changed = True
            Next
            If Not changed Then Return
        Next
        Solve()
        Warnings.Add("The circuit does not settle: valves or relays keep switching (oscillation).")
    End Sub

    Private Sub RecordHistory()
        Dim due = Time + 0.000001 >= _nextSample
        If due Then _nextSample = Time + HistoryInterval
        For Each e In _circuit.Elements
            Dim value As Double
            If TypeOf e Is CylinderBase Then
                value = DirectCast(e, CylinderBase).Position
            ElseIf TypeOf e Is DirectionalValve Then
                value = DirectCast(e, DirectionalValve).DiagramValue
            Else
                Continue For
            End If
            Dim list As List(Of PointF) = Nothing
            If Not History.TryGetValue(e, list) Then
                list = New List(Of PointF)
                History(e) = list
            End If
            ' Between regular samples, still record valve switchings so short pulses show up.
            Dim switched = list.Count > 0 AndAlso TypeOf e Is DirectionalValve AndAlso list(list.Count - 1).Y <> CSng(value)
            If Not due AndAlso Not switched Then Continue For
            list.Add(New PointF(CSng(Time), CSng(value)))
            If list.Count > 0 AndAlso list(0).X < Time - HistoryLength Then list.RemoveAt(0)
        Next
    End Sub

    ' ------------------------------------------------------------ network solution

    Public Sub Solve()
        _ports = _circuit.AllPorts().ToList()
        For i = 0 To _ports.Count - 1
            _ports(i).NodeIndex = i
        Next
        _edges.Clear()
        _supplies.Clear()
        _exhausts.Clear()
        Warnings.Clear()

        For Each t In _circuit.Tubes
            AddEdge(t.A, t.B)
        Next
        For Each e In _circuit.Elements
            e.AddEdges(Me)
            e.AddTerminals(Me)
        Next

        Dim n = _ports.Count
        Dim isExhaust(n - 1) As Boolean
        For i = 0 To n - 1
            Dim p = _ports(i)
            isExhaust(i) = _exhausts.Contains(i) OrElse (p.VentsWhenOpen AndAlso p.ConnectionCount = 0)
        Next

        ' Directed adjacency: outgoing(u) holds (v, edge index) for every allowed flow u -> v.
        Dim outgoing(n - 1) As List(Of Integer())
        Dim incoming(n - 1) As List(Of Integer)
        For i = 0 To n - 1
            outgoing(i) = New List(Of Integer())
            incoming(i) = New List(Of Integer)
        Next
        For k = 0 To _edges.Count - 1
            Dim ed = _edges(k)
            If ed.AB > 0 Then outgoing(ed.A).Add({ed.B, k, 0}) : incoming(ed.B).Add(ed.A)
            If ed.BA > 0 Then outgoing(ed.B).Add({ed.A, k, 1}) : incoming(ed.A).Add(ed.B)
        Next

        ' 1. Pressure reachable from the supplies (best pressure over all paths).
        Dim fed(n - 1) As Double
        Dim queue As New Queue(Of Integer)
        For Each kv In _supplies
            If kv.Value > fed(kv.Key) Then fed(kv.Key) = kv.Value
            queue.Enqueue(kv.Key)
        Next
        While queue.Count > 0
            Dim u = queue.Dequeue()
            For Each o In outgoing(u)
                Dim limit = If(o(2) = 0, _edges(o(1)).LimitAB, Double.PositiveInfinity)
                Dim cand = Math.Min(fed(u), limit)
                If cand > fed(o(0)) + 0.000001 Then
                    fed(o(0)) = cand
                    queue.Enqueue(o(0))
                End If
            Next
        End While

        ' 2. Ports that can vent: from each exhaust, walk backwards against the flow direction.
        Dim vents(n - 1) As Boolean
        For i = 0 To n - 1
            If isExhaust(i) AndAlso fed(i) <= 0 Then vents(i) = True : queue.Enqueue(i)
        Next
        While queue.Count > 0
            Dim v = queue.Dequeue()
            For Each u In incoming(v)
                If Not vents(u) AndAlso fed(u) <= 0 Then
                    vents(u) = True
                    queue.Enqueue(u)
                End If
            Next
        End While

        ' 3. Remaining ports hold trapped pressure, shared within each connected pocket.
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
        Dim isFloating = Function(i As Integer) fed(i) <= 0 AndAlso Not vents(i)
        For Each ed In _edges
            If (ed.AB > 0 OrElse ed.BA > 0) AndAlso isFloating(ed.A) AndAlso isFloating(ed.B) Then
                Dim ra = find(ed.A), rb = find(ed.B)
                If ra <> rb Then parent(ra) = rb
            End If
        Next
        Dim trapped(n - 1) As Double
        For i = 0 To n - 1
            If isFloating(i) AndAlso _ports(i).Kind = PortKind.Pneumatic Then
                Dim root = find(i)
                trapped(root) = Math.Max(trapped(root), _ports(i).Pressure)
            End If
        Next

        Dim leaking As New HashSet(Of CircuitElement)
        Dim shortCircuit = False
        For i = 0 To n - 1
            Dim p = _ports(i)
            p.Factor = 0
            p.SupplyPressure = Math.Max(0, fed(i))
            If fed(i) > 0 Then
                p.State = PortState.Pressurized
                p.Pressure = fed(i)
                If isExhaust(i) Then
                    If p.Kind = PortKind.Electric Then
                        shortCircuit = True
                    ElseIf p.Kind = PortKind.Pneumatic Then
                        leaking.Add(p.Owner)
                    End If
                End If
            ElseIf vents(i) Then
                p.State = PortState.Exhausted
                p.Pressure = 0
            Else
                p.State = PortState.Floating
                p.Pressure = If(p.Kind = PortKind.Pneumatic, trapped(find(i)), 0)
            End If
        Next
        If shortCircuit Then Warnings.Add("Short circuit: +24 V is connected directly to 0 V.")
        For Each e In leaking
            Warnings.Add($"Compressed air escapes from an open port of {e}.")
        Next

        ComputeFlowFactors(isExhaust)
        If RealPhysics Then ApplyChamberPressures()
        AfterSolve()
    End Sub

    ''' <summary>
    ''' Realistic mode: a tube line ending at a cylinder chamber carries the chamber pressure
    ''' (the pressure drop is across the valve), so gauges and pressure sequence valves on that
    ''' line see the pressure build up.
    ''' </summary>
    Private Sub ApplyChamberPressures()
        Dim n = _ports.Count
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
        For Each t In _circuit.Tubes
            If t.A.Kind <> PortKind.Pneumatic Then Continue For
            Dim ra = find(t.A.NodeIndex), rb = find(t.B.NodeIndex)
            If ra <> rb Then parent(ra) = rb
        Next
        Dim netPressure As New Dictionary(Of Integer, Double)
        Dim supplied As New HashSet(Of Integer)
        For Each kv In _supplies
            supplied.Add(find(kv.Key))
        Next
        For Each cyl In _circuit.Elements.OfType(Of CylinderBase)()
            For Each p In cyl.Ports
                If p.Kind <> PortKind.Pneumatic Then Continue For
                Dim cp = cyl.ChamberPressureAt(p)
                If cp < 0 Then Continue For
                Dim root = find(p.NodeIndex)
                If supplied.Contains(root) Then Continue For
                Dim existing As Double
                netPressure(root) = If(netPressure.TryGetValue(root, existing), Math.Max(existing, cp), cp)
            Next
        Next
        If netPressure.Count = 0 Then Return
        For i = 0 To n - 1
            Dim v As Double
            If netPressure.TryGetValue(find(i), v) AndAlso _ports(i).State <> PortState.Exhausted Then _ports(i).Pressure = v
        Next
    End Sub

    ''' <summary>
    ''' Finds, for each port, the best path capacity from a supply (pressurized ports) or to an
    ''' exhaust (vented ports): the maximum over all paths of the smallest edge capacity.
    ''' </summary>
    Private Sub ComputeFlowFactors(isExhaust As Boolean())
        Dim n = _ports.Count
        Dim f(n - 1) As Double
        For i = 0 To n - 1
            Dim p = _ports(i)
            If p.State = PortState.Pressurized AndAlso _supplies.ContainsKey(i) Then f(i) = 1
            If p.State = PortState.Exhausted AndAlso isExhaust(i) Then f(i) = 1
        Next

        Dim changed = True
        Dim guard = 0
        While changed AndAlso guard <= n + 1
            changed = False
            guard += 1
            For Each ed In _edges
                Dim sa = _ports(ed.A).State, sb = _ports(ed.B).State
                If sa <> sb OrElse sa = PortState.Floating Then Continue For
                ' Pressurized: air flows away from the supply (A to B uses capacity AB).
                ' Exhausted: air flows towards the exhaust (reaching A through B means flow A to B).
                Dim capToB, capToA As Double
                If sa = PortState.Pressurized Then
                    capToB = ed.AB : capToA = ed.BA
                Else
                    capToB = ed.BA : capToA = ed.AB
                End If
                Dim nb = Math.Min(f(ed.A), capToB)
                If nb > f(ed.B) + 0.000001 Then f(ed.B) = nb : changed = True
                Dim na = Math.Min(f(ed.B), capToA)
                If na > f(ed.A) + 0.000001 Then f(ed.A) = na : changed = True
            Next
        End While

        For i = 0 To n - 1
            _ports(i).Factor = f(i)
        Next
    End Sub
End Class
