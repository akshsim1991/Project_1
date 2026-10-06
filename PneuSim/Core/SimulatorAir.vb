''' <summary>Compressors and receivers, vacuum, and the flow meters and force sensors.</summary>
Partial Public Class Simulator

    ''' <summary>Receivers charged by each compressor (found from the tubes once per run).</summary>
    Private _compressorReceivers As Dictionary(Of Compressor, List(Of AirReceiver))

    ''' <summary>Finds which receivers each compressor charges: those joined to it by tubes, junctions and in-line parts.</summary>
    Private Sub LinkCompressors()
        _compressorReceivers = New Dictionary(Of Compressor, List(Of AirReceiver))
        Dim byPort As New Dictionary(Of Port, List(Of Port))
        For Each t In _circuit.Tubes
            If Not byPort.ContainsKey(t.A) Then byPort(t.A) = New List(Of Port)
            If Not byPort.ContainsKey(t.B) Then byPort(t.B) = New List(Of Port)
            byPort(t.A).Add(t.B) : byPort(t.B).Add(t.A)
        Next
        For Each comp In _circuit.Elements.OfType(Of Compressor)()
            Dim found As New List(Of AirReceiver)
            Dim seen As New HashSet(Of Port) From {comp.Ports(0)}
            Dim queue As New Queue(Of Port)
            queue.Enqueue(comp.Ports(0))
            While queue.Count > 0
                Dim p = queue.Dequeue()
                Dim next_ As New List(Of Port)
                Dim linked As List(Of Port) = Nothing
                If byPort.TryGetValue(p, linked) Then next_.AddRange(linked)
                Dim owner = p.Owner
                ' Air passes straight through these parts.
                If TypeOf owner Is AirReceiver OrElse TypeOf owner Is FlowMeter OrElse TypeOf owner Is ShutOffValve OrElse TypeOf owner Is CheckValve Then
                    next_.AddRange(owner.Ports)
                End If
                If TypeOf owner Is AirReceiver AndAlso Not found.Contains(DirectCast(owner, AirReceiver)) Then found.Add(DirectCast(owner, AirReceiver))
                For Each q In next_
                    If seen.Add(q) Then queue.Enqueue(q)
                Next
            End While
            _compressorReceivers(comp) = found
            comp.ChargesReceiver = found.Count > 0
        Next
    End Sub

    ''' <summary>
    ''' Takes the air used in this step out of the receivers, lets leaking receivers lose air and
    ''' lets the compressors charge them.
    ''' </summary>
    Private Sub UpdateAirGeneration(dt As Double, consumed As Double)
        Dim receivers = _circuit.Elements.OfType(Of AirReceiver)().ToList()
        If receivers.Count = 0 Then Return
        If _compressorReceivers Is Nothing Then LinkCompressors()
        ' The air used comes from every source that feeds the circuit.
        Dim feeding = receivers.Where(Function(r) r.Pressure > 0.05).ToList()
        Dim otherSources = _circuit.Elements.OfType(Of AirSupply)().Count() +
                           _circuit.Elements.OfType(Of Compressor)().Count(Function(c) Not c.ChargesReceiver)
        If consumed > 0 AndAlso feeding.Count > 0 Then
            Dim share = consumed / (feeding.Count + otherSources)
            For Each r In feeding
                r.Exchange(-share)
            Next
        End If
        For Each r In receivers.Where(Function(x) x.Fault = FaultKind.Leak AndAlso x.Pressure > 0)
            r.Exchange(-60 * (r.Pressure + CylinderBase.Atm) / (6 + CylinderBase.Atm) * dt / 60)
        Next
        For Each kv In _compressorReceivers
            If kv.Value.Count = 0 Then Continue For
            kv.Key.UpdatePressureSwitch(kv.Value.Min(Function(r) r.Pressure), kv.Value.Max(Function(r) r.Pressure))
            If Not kv.Key.Running Then Continue For
            Dim each_ = kv.Key.EffectiveDelivery / 60 * dt / kv.Value.Count
            For Each r In kv.Value
                r.Exchange(each_)
            Next
        Next
    End Sub

    ''' <summary>
    ''' A working vacuum generator sucks the air out of the lines on its V port. If that part of the
    ''' circuit is open to atmosphere (an open port, an exhaust, a suction cup without a workpiece)
    ''' only a weak vacuum is reached.
    ''' </summary>
    Private Sub ApplyVacuum()
        Dim generators = _circuit.Elements.OfType(Of VacuumGenerator)().Where(Function(v) v.Working).ToList()
        If generators.Count = 0 Then Return
        Dim adjacency = BuildAdjacency(Nothing)
        For Each gen In generators
            Dim start = gen.Ports(1).NodeIndex
            Dim seen As New HashSet(Of Integer) From {start}
            Dim queue As New Queue(Of Integer)
            queue.Enqueue(start)
            Dim open = False
            While queue.Count > 0
                Dim i = queue.Dequeue()
                Dim p = _ports(i)
                If p.State = PortState.Exhausted OrElse _exhausts.Contains(i) OrElse (p.VentsWhenOpen AndAlso p.ConnectionCount = 0) Then open = True
                Dim cup = TryCast(p.Owner, SuctionCup)
                If cup IsNot Nothing AndAlso Not cup.Sealed Then open = True
                For Each j In adjacency(i)
                    If _ports(j).State = PortState.Pressurized Then Continue For
                    If seen.Add(j) Then queue.Enqueue(j)
                Next
            End While
            Dim level = If(open, -0.05, gen.VacuumLevel)
            For Each i In seen
                If _ports(i).Pressure > level Then _ports(i).Pressure = level
            Next
        Next
    End Sub

    ''' <summary>Neighbouring ports through open passages and tubes (either direction), optionally leaving out one flow meter.</summary>
    Private Function BuildAdjacency(skipMeter As FlowMeter) As List(Of Integer)()
        Dim n = _ports.Count
        Dim adjacency(n - 1) As List(Of Integer)
        For i = 0 To n - 1
            adjacency(i) = New List(Of Integer)
        Next
        For Each ed In _edges
            If ed.AB <= 0 AndAlso ed.BA <= 0 Then Continue For
            If skipMeter IsNot Nothing AndAlso _ports(ed.A).Owner Is skipMeter AndAlso _ports(ed.B).Owner Is skipMeter Then Continue For
            adjacency(ed.A).Add(ed.B)
            adjacency(ed.B).Add(ed.A)
        Next
        Return adjacency
    End Function

    ''' <summary>Updates the readings of the flow meters and force sensors.</summary>
    Private Sub MeasureFlows()
        For Each fs In _circuit.Elements.OfType(Of ForceSensor)()
            Dim cyl = fs.FindCylinder(_circuit.Elements)
            fs.Force = If(cyl Is Nothing, 0, cyl.PistonForce())
        Next
        For Each meter In _circuit.Elements.OfType(Of FlowMeter)()
            Dim adjacency = BuildAdjacency(meter)
            Dim downstream = SideFlow(adjacency, meter.Ports(1))
            meter.Flow = If(Math.Abs(downstream) > 0.000001, downstream, -SideFlow(adjacency, meter.Ports(0)))
        Next
    End Sub

    ''' <summary>Total flow into the consumers reachable from a port (cylinder chambers, motors, ejectors, leaks).</summary>
    Private Function SideFlow(adjacency As List(Of Integer)(), start As Port) As Double
        Dim seen As New HashSet(Of Integer) From {start.NodeIndex}
        Dim queue As New Queue(Of Integer)
        queue.Enqueue(start.NodeIndex)
        While queue.Count > 0
            Dim i = queue.Dequeue()
            For Each j In adjacency(i)
                If seen.Add(j) Then queue.Enqueue(j)
            Next
        End While
        Dim total = 0.0
        For Each i In seen
            Dim p = _ports(i)
            Select Case True
                Case TypeOf p.Owner Is CylinderBase
                    total += DirectCast(p.Owner, CylinderBase).PortFlow(p)
                Case TypeOf p.Owner Is AirMotor
                    total += DirectCast(p.Owner, AirMotor).ConsumptionNow()
                Case TypeOf p.Owner Is VacuumGenerator AndAlso p Is p.Owner.Ports(0)
                    total += DirectCast(p.Owner, VacuumGenerator).ConsumptionNow()
            End Select
        Next
        For Each t In _circuit.Tubes
            If t.Fault <> FaultKind.Leak OrElse t.A.Kind <> PortKind.Pneumatic Then Continue For
            If Not seen.Contains(t.A.NodeIndex) AndAlso Not seen.Contains(t.B.NodeIndex) Then Continue For
            Dim pr = Math.Max(t.A.Pressure, t.B.Pressure)
            If pr > 0.2 Then total += LeakNlPerMin * (pr + CylinderBase.Atm) / (6 + CylinderBase.Atm)
        Next
        Return total
    End Function
End Class
