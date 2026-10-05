''' <summary>
''' Solves the pneumatic network and advances time.
'''
''' Every port is a node. Tubes and the open passages inside valves are edges. Nodes joined by
''' edges form a group; a group linked to a supply is pressurized, a group linked to an open
''' exhaust is vented, and any other group keeps the pressure trapped inside it.
''' Flow control valves give edges a reduced, possibly direction dependent, capacity that
''' slows down the cylinders fed or vented through them.
''' </summary>
Public Class Simulator

    Private Structure Edge
        Public A As Integer
        Public B As Integer
        ''' <summary>Capacity for air flowing from A to B.</summary>
        Public AB As Double
        ''' <summary>Capacity for air flowing from B to A.</summary>
        Public BA As Double
    End Structure

    ''' <summary>Pilot pressure (bar) needed to switch a valve.</summary>
    Public Const PilotThreshold As Double = 1.5
    Private Const MaxLogicPasses As Integer = 40
    Private Const HistoryInterval As Double = 0.05
    Private Const HistoryLength As Double = 60

    Private ReadOnly _circuit As Circuit
    Private _ports As New List(Of Port)
    Private ReadOnly _edges As New List(Of Edge)
    Private ReadOnly _supplies As New Dictionary(Of Integer, Double)
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

    ''' <summary>Problems found during the last solve (open ports blowing air, oscillation...).</summary>
    Public ReadOnly Property Warnings As New List(Of String)

    ''' <summary>Recorded (time, value) samples for the displacement-step diagram.</summary>
    Public ReadOnly Property History As New Dictionary(Of CircuitElement, List(Of PointF))

    ' ------------------------------------------------------------ used by elements

    Public Sub AddEdge(a As Port, b As Port, Optional capacityAB As Double = 1, Optional capacityBA As Double = 1)
        If a Is Nothing OrElse b Is Nothing Then Return
        _edges.Add(New Edge With {.A = a.NodeIndex, .B = b.NodeIndex, .AB = capacityAB, .BA = capacityBA})
    End Sub

    Public Sub AddSupply(p As Port, pressure As Double)
        Dim existing As Double
        _supplies.TryGetValue(p.NodeIndex, existing)
        _supplies(p.NodeIndex) = Math.Max(existing, pressure)
    End Sub

    ''' <summary>True if a cylinder carrying the named position mark is at that end position.</summary>
    Public Function IsMarkActive(mark As String) As Boolean
        If String.IsNullOrWhiteSpace(mark) Then Return False
        For Each cyl In _circuit.Elements.OfType(Of CylinderBase)()
            If cyl.IsAtMark(mark) Then Return True
        Next
        Return False
    End Function

    ' ------------------------------------------------------------ control

    Public Sub Reset()
        Time = 0
        _nextSample = 0
        History.Clear()
        Warnings.Clear()
        For Each e In _circuit.Elements
            e.ResetSim()
        Next
        RunLogic()
        RecordHistory()
    End Sub

    ''' <summary>Advances the simulation by <paramref name="dt"/> seconds.</summary>
    Public Sub [Step](dt As Double)
        Time += dt
        For Each e In _circuit.Elements
            e.UpdateDynamics(Me, dt)
        Next
        RunLogic()
        RecordHistory()
    End Sub

    ''' <summary>Solves pressures and lets valves switch until the circuit is stable.</summary>
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
        Warnings.Add("The circuit does not settle: valves keep switching (oscillation).")
    End Sub

    Private Sub RecordHistory()
        Dim due = Time + 0.000001 >= _nextSample
        If due Then _nextSample = Time + HistoryInterval
        For Each e In _circuit.Elements
            Dim value As Double
            If TypeOf e Is CylinderBase Then
                value = DirectCast(e, CylinderBase).Position
            ElseIf TypeOf e Is DirectionalValve Then
                value = DirectCast(e, DirectionalValve).State
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
        Warnings.Clear()

        For Each t In _circuit.Tubes
            AddEdge(t.A, t.B)
        Next
        For Each e In _circuit.Elements
            e.AddEdges(Me)
            e.AddTerminals(Me)
        Next

        Dim n = _ports.Count
        ' Union-find over all edges.
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
        For Each ed In _edges
            Dim ra = find(ed.A), rb = find(ed.B)
            If ra <> rb Then parent(ra) = rb
        Next

        Dim groupSupply(n - 1) As Double
        Dim groupExhaust(n - 1) As Boolean
        Dim groupTrapped(n - 1) As Double
        Dim isExhaust(n - 1) As Boolean
        For i = 0 To n - 1
            Dim p = _ports(i)
            Dim root = find(i)
            isExhaust(i) = p.VentsWhenOpen AndAlso p.ConnectionCount = 0
            If isExhaust(i) Then groupExhaust(root) = True
            groupTrapped(root) = Math.Max(groupTrapped(root), p.Pressure)
        Next
        For Each kv In _supplies
            Dim root = find(kv.Key)
            groupSupply(root) = Math.Max(groupSupply(root), kv.Value)
        Next

        Dim leaking As New HashSet(Of CircuitElement)
        For i = 0 To n - 1
            Dim p = _ports(i)
            Dim root = find(i)
            p.Factor = 0
            If groupSupply(root) > 0 Then
                p.State = PortState.Pressurized
                p.Pressure = groupSupply(root)
                If isExhaust(i) AndAlso TypeOf p.Owner IsNot DirectionalValve Then leaking.Add(p.Owner)
            ElseIf groupExhaust(root) Then
                p.State = PortState.Exhausted
                p.Pressure = 0
            Else
                p.State = PortState.Floating
                p.Pressure = groupTrapped(root)
            End If
        Next
        For Each e In leaking
            Warnings.Add($"Compressed air escapes from an open port of {e}.")
        Next

        ComputeFlowFactors(isExhaust)
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
