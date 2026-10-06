Imports System.Text

Public Enum IssueSeverity
    [Error]
    Warning
    Info
End Enum

''' <summary>A problem found by the circuit checker, optionally with an automatic fix.</summary>
Public Class CheckIssue
    Public Property Severity As IssueSeverity
    Public Property Message As String
    Public Property Element As CircuitElement
    ''' <summary>Text of the fix button, e.g. "Redesign with the cascade method".</summary>
    Public Property FixLabel As String
    ''' <summary>Builds a corrected circuit.</summary>
    Public Property Fix As Func(Of Circuit)

    Public Overrides Function ToString() As String
        Return $"[{Severity}] {Message}"
    End Function
End Class

''' <summary>What starts a movement: roller / sensor marks, manual buttons, or a permanent signal.</summary>
Public Class MoveTrigger
    Public ReadOnly Marks As New List(Of String)
    Public ReadOnly Manual As New List(Of String)
    Public AlwaysOn As Boolean
    ''' <summary>True if the signal passes through another pilot valve (e.g. a cascade group line), so it can be switched off.</summary>
    Public Gated As Boolean
    Public Description As String = ""
End Class

''' <summary>A cylinder movement found in a circuit, e.g. 1A extends, with what triggers it.</summary>
Public Class CircuitMove
    Public Cylinder As CylinderBase
    Public Extend As Boolean
    Public Valve As DirectionalValve
    Public Trigger As New MoveTrigger()
    Public Letter As Char

    Public ReadOnly Property EndMark As String
        Get
            Return If(Extend, Cylinder.ExtendedMark, Cylinder.RetractedMark)
        End Get
    End Property

    Public Overrides Function ToString() As String
        Return Letter & If(Extend, "+", "-")
    End Function
End Class

''' <summary>Checks circuits for mistakes and explains what they do.</summary>
Public Module CircuitAnalysis

    ' ================================================================= tube network helpers

    Private Function BuildTubeMap(c As Circuit) As Dictionary(Of Port, List(Of Port))
        Dim map As New Dictionary(Of Port, List(Of Port))
        Dim link = Sub(a As Port, b As Port)
                       If Not map.ContainsKey(a) Then map(a) = New List(Of Port)
                       map(a).Add(b)
                   End Sub
        For Each t In c.Tubes
            link(t.A, t.B) : link(t.B, t.A)
        Next
        Dim connectors = c.Elements.OfType(Of PageConnector)().ToList()
        For Each a In connectors
            For Each b In connectors
                If a IsNot b AndAlso a.Matches(b) Then link(a.Ports(0), b.Ports(0))
            Next
        Next
        Return map
    End Function

    Private Function Neighbours(map As Dictionary(Of Port, List(Of Port)), p As Port) As IEnumerable(Of Port)
        Dim l As List(Of Port) = Nothing
        Return If(map.TryGetValue(p, l), l, Enumerable.Empty(Of Port)())
    End Function

    Private Function Name(e As CircuitElement) As String
        Return If(String.IsNullOrWhiteSpace(e.Label), e.DisplayName.ToLowerInvariant(), e.Label)
    End Function

    ''' <summary>Elements that a line passes straight through (in-line components).</summary>
    Private Function PassThrough(p As Port) As IEnumerable(Of Port)
        Dim e = p.Owner
        If TypeOf e Is Junction Then Return Enumerable.Empty(Of Port)()
        If TypeOf e Is FlowControlValve OrElse TypeOf e Is CheckValve OrElse TypeOf e Is PressureRegulator OrElse
           TypeOf e Is CompensatedFlowControl Then
            Return e.Ports.Where(Function(o) o IsNot p)
        End If
        If TypeOf e Is CounterbalanceValve AndAlso p.Name <> "X" Then Return e.Ports.Where(Function(o) o IsNot p AndAlso o.Name <> "X")
        If TypeOf e Is QuickExhaustValve AndAlso p.Name <> "3" Then Return e.Ports.Where(Function(o) o IsNot p AndAlso o.Name <> "3")
        Return Enumerable.Empty(Of Port)()
    End Function

    ''' <summary>Follows a working line from a cylinder port to the directional valve port feeding it.</summary>
    Private Function TraceToValve(map As Dictionary(Of Port, List(Of Port)), start As Port, inline As List(Of CircuitElement)) As Port
        Dim seen As New HashSet(Of Port) From {start}
        Dim queue As New Queue(Of Port)
        queue.Enqueue(start)
        While queue.Count > 0
            Dim p = queue.Dequeue()
            For Each q In Neighbours(map, p)
                If Not seen.Add(q) Then Continue For
                If TypeOf q.Owner Is DirectionalValve AndAlso DirectCast(q.Owner, DirectionalValve).WorkingPortNames().Contains(q.Name) Then Return q
                Dim through = PassThrough(q).ToList()
                If through.Count > 0 AndAlso Not inline.Contains(q.Owner) Then inline.Add(q.Owner)
                For Each o In through
                    If seen.Add(o) Then queue.Enqueue(o)
                Next
                If TypeOf q.Owner Is Junction Then queue.Enqueue(q)
            Next
        End While
        Return Nothing
    End Function

    ''' <summary>Finds the elements that supply a signal into a pneumatic pilot port.</summary>
    Private Sub TraceSignal(map As Dictionary(Of Port, List(Of Port)), target As Port, trigger As MoveTrigger, depth As Integer)
        If target Is Nothing OrElse depth > 4 Then Return
        Dim seen As New HashSet(Of Port) From {target}
        Dim queue As New Queue(Of Port)
        queue.Enqueue(target)
        Dim parts As New List(Of String)
        While queue.Count > 0
            Dim p = queue.Dequeue()
            For Each q In Neighbours(map, p)
                If Not seen.Add(q) Then Continue For
                Dim e = q.Owner
                If TypeOf e Is Junction Then
                    queue.Enqueue(q)
                ElseIf IsAirSource(e) Then
                    trigger.AlwaysOn = True
                    parts.Add("the air supply directly")
                ElseIf (TypeOf e Is ShuttleValve OrElse TypeOf e Is TwoPressureValve) AndAlso q.Name = "2" Then
                    For Each inPort In e.Ports.Where(Function(o) o.Name <> "2")
                        If seen.Add(inPort) Then queue.Enqueue(inPort)
                    Next
                    parts.Add(If(TypeOf e Is ShuttleValve, $"shuttle valve {Name(e)} (OR)", $"two-pressure valve {Name(e)} (AND)"))
                ElseIf TypeOf e Is DirectionalValve AndAlso q.Name = "2" OrElse TypeOf e Is DirectionalValve AndAlso q.Name = "4" Then
                    Dim v = DirectCast(e, DirectionalValve)
                    Select Case v.Actuator
                        Case ValveActuator.RollerLever, ValveActuator.IdleReturnRoller
                            If Not String.IsNullOrWhiteSpace(v.TriggerMark) Then trigger.Marks.Add(v.TriggerMark)
                            parts.Add($"roller valve {Name(v)} (operated at {v.TriggerMark})")
                        Case ValveActuator.PushButton, ValveActuator.Selector
                            trigger.Manual.Add(Name(v))
                            parts.Add($"{If(v.Actuator = ValveActuator.PushButton, "push button", "selector")} valve {Name(v)}")
                        Case ValveActuator.DelayedPilot
                            parts.Add($"time delay valve {Name(v)} ({v.DelaySeconds:0.#} s)")
                            TraceSignal(map, v.GetPort(v.PilotPortName(True)), trigger, depth + 1)
                        Case Else
                            parts.Add($"valve {Name(v)}")
                            trigger.Gated = True
                            TraceSignal(map, v.GetPort(v.PilotPortName(True)), trigger, depth + 1)
                    End Select
                    ' A signal valve fed through another valve (e.g. a start button in series).
                    TraceSignal(map, v.GetPort("1"), trigger, depth + 1)
                Else
                    For Each o In PassThrough(q)
                        If seen.Add(o) Then queue.Enqueue(o)
                    Next
                End If
            Next
        End While
        If parts.Count > 0 AndAlso depth = 0 Then trigger.Description = String.Join(", ", parts)
    End Sub

    ' ================================================================= electrical expressions

    ''' <summary>Boolean description of the contacts that energize a coil, e.g. "S1 AND 1B1 OR K1".</summary>
    Public Function CoilCondition(c As Circuit, coil As ElectricCoil, trigger As MoveTrigger) As String
        Dim map = BuildTubeMap(c)
        Return Expression(map, coil.GetPort("A1"), New HashSet(Of CircuitElement), trigger, 0)
    End Function

    Private Function Expression(map As Dictionary(Of Port, List(Of Port)), node As Port, visited As HashSet(Of CircuitElement),
                                trigger As MoveTrigger, depth As Integer) As String
        If depth > 12 Then Return "?"
        Dim alternatives As New List(Of String)
        Dim seen As New HashSet(Of Port) From {node}
        Dim queue As New Queue(Of Port)
        queue.Enqueue(node)
        While queue.Count > 0
            Dim p = queue.Dequeue()
            For Each q In Neighbours(map, p).Concat(If(TypeOf p.Owner Is Junction, Neighbours(map, p), Enumerable.Empty(Of Port)()))
                If Not seen.Add(q) Then Continue For
                Dim e = q.Owner
                If TypeOf e Is Junction OrElse TypeOf e Is PageConnector Then
                    queue.Enqueue(q)
                ElseIf TypeOf e Is PowerTerminal AndAlso DirectCast(e, PowerTerminal).Polarity = Polarity.Plus24V Then
                    alternatives.Add("")
                ElseIf TypeOf e Is ElectricContact AndAlso Not visited.Contains(e) Then
                    Dim k = DirectCast(e, ElectricContact)
                    visited.Add(k)
                    Dim other = k.Ports.First(Function(o) o IsNot q)
                    Dim upstream = Expression(map, other, visited, trigger, depth + 1)
                    visited.Remove(k)
                    If upstream = "?" Then Continue For
                    Dim nm = If(String.IsNullOrEmpty(k.Label), k.Reference, k.Label)
                    If k.IsSensor Then
                        If Not k.NormallyClosed AndAlso trigger IsNot Nothing Then trigger.Marks.Add(k.Reference)
                    ElseIf k.Operator <> ContactOperator.Relay AndAlso Not k.NormallyClosed AndAlso trigger IsNot Nothing Then
                        trigger.Manual.Add(nm)
                    End If
                    Dim term = If(k.NormallyClosed, "NOT " & nm, nm)
                    alternatives.Add(If(upstream = "", term, upstream & " AND " & term))
                End If
            Next
        End While
        If alternatives.Count = 0 Then Return "?"
        If alternatives.Contains("") Then Return ""
        Return If(alternatives.Count = 1, alternatives(0), "(" & String.Join(") OR (", alternatives) & ")")
    End Function

    ' ================================================================= movements and sequence

    ''' <summary>Finds every cylinder movement, the valve that commands it and what triggers it.</summary>
    Public Function FindMoves(c As Circuit) As List(Of CircuitMove)
        Dim map = BuildTubeMap(c)
        Dim moves As New List(Of CircuitMove)
        Dim cylinders = c.Elements.OfType(Of CylinderBase)().OrderBy(Function(k) k.Label).ToList()
        For i = 0 To cylinders.Count - 1
            Dim cyl = cylinders(i)
            Dim inline As New List(Of CircuitElement)
            Dim vp = TraceToValve(map, cyl.Ports(0), inline)
            If vp Is Nothing Then Continue For
            Dim v = DirectCast(vp.Owner, DirectionalValve)
            ' Which position feeds supply into the cap side?
            Dim supplyName = If(v.WorkingPortNames().Contains("P"), "P", "1")
            For Each extend In {True, False}
                Dim feedsCap = Function(pos As Integer) v.PassagesIn(pos).Any(Function(f) f(0) = supplyName AndAlso f(1) = vp.Name)
                Dim wantCapFed = extend
                Dim position = -1
                For pos = 0 To v.Positions - 1
                    If feedsCap(pos) = wantCapFed AndAlso pos <> 0 Then position = pos : Exit For
                Next
                If position < 0 AndAlso feedsCap(0) = wantCapFed Then position = 0
                Dim mv As New CircuitMove With {.Cylinder = cyl, .Extend = extend, .Valve = v, .Letter = ChrW(AscW("A"c) + i)}
                Dim leftSide = position = 1
                ' A two-position memory valve reaches its normal position through the right-hand signal.
                Dim viaSpring = position = 0 AndAlso Not (v.Positions = 2 AndAlso v.ReturnType <> ValveReturn.Spring)
                If viaSpring Then
                    ' Reached by spring return: triggered by the left signal going away.
                    mv.Trigger.Description = $"spring return of {Name(v)}"
                ElseIf v.Actuator = ValveActuator.Solenoid AndAlso leftSide OrElse v.ReturnType = ValveReturn.Solenoid AndAlso Not leftSide Then
                    Dim sol = If(leftSide, v.SolenoidLabel, v.ReturnSolenoidLabel)
                    Dim coil = c.Elements.OfType(Of ElectricCoil)().FirstOrDefault(Function(k) k.Kind = CoilKind.Solenoid AndAlso
                                   String.Equals(k.Label, sol, StringComparison.OrdinalIgnoreCase))
                    If coil IsNot Nothing Then
                        Dim cond = CoilCondition(c, coil, mv.Trigger)
                        mv.Trigger.Description = $"solenoid {sol}, energized when {If(cond = "", "always", cond)}"
                    Else
                        mv.Trigger.Description = $"solenoid {sol} (no coil with this label!)"
                    End If
                ElseIf leftSide AndAlso (v.Actuator = ValveActuator.PushButton OrElse v.Actuator = ValveActuator.Selector) Then
                    mv.Trigger.Manual.Add(Name(v))
                    mv.Trigger.Description = $"operating valve {Name(v)} by hand"
                ElseIf leftSide AndAlso v.IsRollerOperated Then
                    mv.Trigger.Marks.Add(v.TriggerMark)
                    mv.Trigger.Description = $"its roller at {v.TriggerMark}"
                Else
                    TraceSignal(map, v.GetPort(v.PilotPortName(leftSide)), mv.Trigger, 0)
                    If mv.Trigger.Description = "" Then mv.Trigger.Description = $"pilot {v.PilotPortName(leftSide)} (not connected!)"
                End If
                moves.Add(mv)
            Next
        Next
        Return moves
    End Function

    ''' <summary>Infers the motion sequence from what triggers each movement, e.g. "A+ B+ B- A-".</summary>
    Public Function InferSequence(moves As List(Of CircuitMove)) As List(Of CircuitMove)
        Dim result As New List(Of CircuitMove)
        Dim first = moves.FirstOrDefault(Function(m) m.Trigger.Manual.Count > 0 AndAlso m.Extend)
        If first Is Nothing Then first = moves.FirstOrDefault(Function(m) m.Trigger.Manual.Count > 0)
        If first Is Nothing Then Return result
        Dim current = first
        While current IsNot Nothing AndAlso Not result.Contains(current)
            result.Add(current)
            Dim mark = current.EndMark
            If String.IsNullOrWhiteSpace(mark) Then Exit While
            current = moves.FirstOrDefault(Function(m) Not result.Contains(m) AndAlso m.Trigger.Marks.Any(Function(k) String.Equals(k, mark, StringComparison.OrdinalIgnoreCase)))
        End While
        Return result
    End Function

    ''' <summary>
    ''' Classic signal overlap: when a movement must start, the opposite pilot of the same memory
    ''' valve is still held on by a roller valve that is still operated.
    ''' </summary>
    Public Function FindOverlaps(moves As List(Of CircuitMove), sequence As List(Of CircuitMove)) As List(Of String)
        Dim found As New List(Of String)
        If sequence.Count < 2 Then Return found
        Dim extended As New Dictionary(Of CylinderBase, Boolean)
        For Each m In moves
            extended(m.Cylinder) = False
        Next
        For Each m In sequence
            Dim opposite = moves.FirstOrDefault(Function(o) o.Cylinder Is m.Cylinder AndAlso o.Extend <> m.Extend)
            If opposite IsNot Nothing AndAlso m.Valve.IsMemoryValve AndAlso opposite.Trigger.Marks.Count > 0 AndAlso Not opposite.Trigger.Gated Then
                Dim activeMarks = extended.Select(Function(kv) If(kv.Value, kv.Key.ExtendedMark, kv.Key.RetractedMark)).
                                  Where(Function(k) Not String.IsNullOrWhiteSpace(k)).ToList()
                Dim held = opposite.Trigger.Marks.Where(Function(k) activeMarks.Any(Function(a) String.Equals(a, k, StringComparison.OrdinalIgnoreCase))).ToList()
                If held.Count = opposite.Trigger.Marks.Count Then
                    found.Add($"Signal overlap at valve {Name(m.Valve)}: when {m} should start, the signal for {opposite} " &
                              $"is still on ({String.Join(", ", held)} is still operated), so the valve cannot switch and the sequence stops.")
                End If
            End If
            extended(m.Cylinder) = m.Extend
        Next
        Return found
    End Function

    ' ================================================================= checks

    ''' <summary>Fast checks run while editing.</summary>
    Public Function StaticChecks(c As Circuit) As List(Of CheckIssue)
        Dim issues As New List(Of CheckIssue)
        Dim add = Sub(sev As IssueSeverity, msg As String, el As CircuitElement)
                      issues.Add(New CheckIssue With {.Severity = sev, .Message = msg, .Element = el})
                  End Sub
        If c.Elements.Count = 0 Then
            add(IssueSeverity.Info, "The circuit is empty. Place components from the library or load an example.", Nothing)
            Return issues
        End If
        Dim els = c.Elements

        Dim hasAir = els.Any(Function(e) e.Ports.Any(Function(p) p.Kind = PortKind.Pneumatic) AndAlso TypeOf e IsNot Junction AndAlso TypeOf e IsNot TextNote)
        If hasAir AndAlso Not els.Any(Function(x) IsAirSource(x)) AndAlso Not els.OfType(Of PageConnector)().Any() Then
            add(IssueSeverity.Error, "There is no compressed air supply in the circuit.", Nothing)
        End If
        Dim hasOil = els.Any(Function(e) e.Ports.Any(Function(p) p.Kind = PortKind.Hydraulic) AndAlso TypeOf e IsNot Junction)
        If hasOil AndAlso Not els.OfType(Of HydraulicPump)().Any() Then add(IssueSeverity.Error, "There is no hydraulic pump in the circuit.", Nothing)
        If els.OfType(Of HydraulicPump)().Any() AndAlso Not els.OfType(Of ReliefValve)().Any() Then
            add(IssueSeverity.Error, "Hydraulic circuit without a pressure relief valve: the pump pressure rises without limit when nothing moves.", els.OfType(Of HydraulicPump)().First())
        End If
        Dim usesElectric = els.Any(Function(e) TypeOf e Is ElectricContact OrElse TypeOf e Is ElectricCoil)
        If usesElectric AndAlso Not els.OfType(Of PowerTerminal)().Any(Function(t) t.Polarity = Polarity.Plus24V) Then
            add(IssueSeverity.Error, "The electrical circuit has no +24 V connection.", Nothing)
        End If
        If usesElectric AndAlso Not els.OfType(Of PowerTerminal)().Any(Function(t) t.Polarity = Polarity.Zero0V) Then
            add(IssueSeverity.Error, "The electrical circuit has no 0 V connection.", Nothing)
        End If

        For Each e In els
            If TypeOf e Is Junction OrElse TypeOf e Is TextNote Then Continue For
            Dim open = e.Ports.Where(Function(p) p.ConnectionCount = 0).ToList()
            If open.Count = 0 Then Continue For
            Dim v = TryCast(e, DirectionalValve)
            If v IsNot Nothing Then
                For Each p In open
                    If p.Name = "1" OrElse p.Name = "P" Then
                        add(IssueSeverity.Error, $"Valve {Name(v)}: supply port {p.Name} is not connected, so the valve never delivers anything.", v)
                    ElseIf p.Name = v.PilotPortName(True) OrElse p.Name = v.PilotPortName(False) Then
                        add(IssueSeverity.Error, $"Valve {Name(v)}: pilot port {p.Name} is not connected, so the valve can never switch that way.", v)
                    ElseIf p.Kind = PortKind.Hydraulic Then
                        add(IssueSeverity.Error, $"Valve {Name(v)}: hydraulic port {p.Name} is open; oil would leak out.", v)
                    ElseIf Not p.VentsWhenOpen Then
                        add(IssueSeverity.Warning, $"Valve {Name(v)}: output {p.Name} is not connected; air will blow out when it is switched on.", v)
                    End If
                Next
            ElseIf TypeOf e Is CylinderBase Then
                For Each p In open
                    add(If(p.Kind = PortKind.Hydraulic, IssueSeverity.Error, IssueSeverity.Warning),
                        $"Cylinder {Name(e)}: port {p.Name} is not connected{If(p.Kind = PortKind.Hydraulic, " (oil would leak)", "; it vents to atmosphere")}.", e)
                Next
            ElseIf TypeOf e Is ElectricCoil OrElse TypeOf e Is ElectricContact OrElse TypeOf e Is PowerTerminal Then
                add(IssueSeverity.Error, $"{e.DisplayName} {Name(e)}: terminal {open(0).Name} is not wired.", e)
            ElseIf IsAirSource(e) OrElse TypeOf e Is HydraulicPump Then
                add(IssueSeverity.Warning, $"{e.DisplayName} {Name(e)}: port {open(0).Name} is not connected.", e)
            ElseIf Not TypeOf e Is Silencer AndAlso Not TypeOf e Is QuickExhaustValve Then
                add(IssueSeverity.Warning, $"{e.DisplayName} {Name(e)}: port {open(0).Name} is not connected.", e)
            End If
        Next

        ' Names that must match.
        Dim coils = els.OfType(Of ElectricCoil)().ToList()
        Dim same = Function(a As String, b As String) String.Equals(a?.Trim(), b?.Trim(), StringComparison.OrdinalIgnoreCase)
        For Each v In els.OfType(Of DirectionalValve)()
            For Each sol In {If(v.Actuator = ValveActuator.Solenoid, v.SolenoidLabel, Nothing), If(v.ReturnType = ValveReturn.Solenoid, v.ReturnSolenoidLabel, Nothing)}
                If sol Is Nothing Then Continue For
                If Not coils.Any(Function(k) k.Kind = CoilKind.Solenoid AndAlso same(k.Label, sol)) Then
                    add(IssueSeverity.Error, $"Valve {Name(v)} uses solenoid {sol}, but there is no 'Valve solenoid' coil labelled {sol} in the electrical circuit.", v)
                End If
            Next
            If v.IsRollerOperated AndAlso Not MarkExists(els, v.TriggerMark) Then
                add(IssueSeverity.Error, $"Roller valve {Name(v)}: no cylinder has a position mark named '{v.TriggerMark}', so the roller is never operated.", v)
            End If
        Next
        For Each k In coils.Where(Function(x) x.Kind = CoilKind.Solenoid)
            If Not els.OfType(Of DirectionalValve)().Any(Function(v) v.UsesSolenoid(k.Label)) Then
                add(IssueSeverity.Warning, $"Solenoid coil {k.Label} does not operate any valve (no valve uses solenoid label {k.Label}).", k)
            End If
        Next
        For Each k In els.OfType(Of ElectricContact)()
            Dim labelIsMark = Not String.IsNullOrWhiteSpace(k.Label) AndAlso MarkExists(els, k.Label.Trim())
            If k.Operator = ContactOperator.Relay AndAlso Not coils.Any(Function(x) x.Kind <> CoilKind.Solenoid AndAlso x.Kind <> CoilKind.Lamp AndAlso same(x.Label, k.Reference)) AndAlso
               Not els.OfType(Of ElectricCounter)().Any(Function(x) same(x.Label, k.Reference)) Then
                If labelIsMark Then
                    add(IssueSeverity.Error, $"Contact {Name(k)} is a relay contact, so it only closes when a relay coil switches it. To make the cylinder operate it, " &
                        $"set 'Operated by' to Limit switch (or Proximity sensor) and 'Reference' to {k.Label.Trim()}.", k)
                ElseIf String.IsNullOrWhiteSpace(k.Reference) Then
                    add(IssueSeverity.Error, $"Relay contact {Name(k)} has no Reference, so it never switches. Enter the label of its relay coil (e.g. K1) in 'Reference'.", k)
                Else
                    add(IssueSeverity.Error, $"Relay contact {Name(k)} refers to {k.Reference}, but there is no relay coil labelled {k.Reference}.", k)
                End If
            End If
            If k.Operator = ContactOperator.PressureSwitch AndAlso Not els.OfType(Of PressureSwitch)().Any(Function(x) same(x.Label, k.Reference)) Then
                add(IssueSeverity.Error, $"Pressure switch contact {Name(k)} refers to '{k.Reference}', but there is no pressure switch with that label.", k)
            End If
            If (k.Operator = ContactOperator.ProximitySensor OrElse k.Operator = ContactOperator.LimitSwitch) AndAlso Not MarkExists(els, k.Reference) Then
                If String.IsNullOrWhiteSpace(k.Reference) AndAlso labelIsMark Then
                    add(IssueSeverity.Error, $"Sensor {Name(k)} has no Reference. Enter the cylinder position mark {k.Label.Trim()} in 'Reference' (the label alone does not link it).", k)
                Else
                    add(IssueSeverity.Error, $"Sensor {Name(k)}: no cylinder has a position mark named '{k.Reference}'.", k)
                End If
            End If
        Next
        For Each grp In coils.Where(Function(k) Not String.IsNullOrWhiteSpace(k.Label)).GroupBy(Function(k) k.Label.Trim().ToUpperInvariant())
            If grp.Count() > 1 Then add(IssueSeverity.Error, $"{grp.Count()} coils share the label {grp.First().Label}; each coil needs its own name.", grp.First())
        Next
        ' Two valves on one solenoid switch together, which is almost always a copy-and-paste slip.
        Dim solenoidUse As New Dictionary(Of String, List(Of DirectionalValve))(StringComparer.OrdinalIgnoreCase)
        For Each v In els.OfType(Of DirectionalValve)()
            For Each sol In {If(v.Actuator = ValveActuator.Solenoid, v.SolenoidLabel, Nothing), If(v.ReturnType = ValveReturn.Solenoid, v.ReturnSolenoidLabel, Nothing)}
                If String.IsNullOrWhiteSpace(sol) Then Continue For
                Dim users As List(Of DirectionalValve) = Nothing
                If Not solenoidUse.TryGetValue(sol.Trim(), users) Then users = New List(Of DirectionalValve) : solenoidUse(sol.Trim()) = users
                users.Add(v)
            Next
        Next
        For Each kv In solenoidUse
            If kv.Value.Count > 1 AndAlso kv.Value.Distinct().Count() > 1 Then
                add(IssueSeverity.Error, $"Valves {String.Join(" and ", kv.Value.Distinct().Select(Function(v) Name(v)))} all use solenoid {kv.Key}, so they always switch together. Give each valve its own solenoid label.", kv.Value(1))
            ElseIf kv.Value.Count > 1 Then
                add(IssueSeverity.Error, $"Valve {Name(kv.Value(0))} uses solenoid {kv.Key} on both sides; the right solenoid needs its own label.", kv.Value(0))
            End If
        Next
        ' Position marks must be unique, otherwise a sensor reacts to the wrong cylinder.
        Dim marks = els.OfType(Of CylinderBase)().SelectMany(Function(cy) {(Mark:=cy.RetractedMark, Cyl:=cy), (Mark:=cy.ExtendedMark, Cyl:=cy)}).
            Where(Function(t) Not String.IsNullOrWhiteSpace(t.Mark)).GroupBy(Function(t) t.Mark.Trim().ToUpperInvariant())
        For Each grp In marks.Where(Function(g) g.Count() > 1)
            add(IssueSeverity.Error, $"The position mark {grp.First().Mark} is used {grp.Count()} times ({String.Join(", ", grp.Select(Function(t) Name(t.Cyl)).Distinct())}); each end position needs its own mark.", grp.First().Cyl)
        Next
        ' Duplicate names of cylinders, valves and push buttons.
        Dim named = els.Where(Function(e) (TypeOf e Is CylinderBase OrElse TypeOf e Is DirectionalValve OrElse
                                           (TypeOf e Is ElectricContact AndAlso DirectCast(e, ElectricContact).IsManuallyOperated)) AndAlso
                                          Not String.IsNullOrWhiteSpace(e.Label))
        For Each grp In named.GroupBy(Function(e) e.Label.Trim().ToUpperInvariant()).Where(Function(g) g.Count() > 1)
            add(IssueSeverity.Warning, $"{grp.Count()} components are called {grp.First().Label}; give each its own name so the circuit can be read and explained.", grp.ElementAt(1))
        Next
        ' Page connectors work in pairs.
        For Each grp In els.OfType(Of PageConnector)().Where(Function(pc) Not String.IsNullOrWhiteSpace(pc.Label)).
                GroupBy(Function(pc) pc.Label.Trim().ToUpperInvariant() & "|" & pc.Medium.ToString())
            If grp.Count() = 1 Then add(IssueSeverity.Warning, $"Page connector {grp.First().Label} has no partner with the same name, so the line ends there.", grp.First())
        Next

        ' Can each cylinder ever move? Consider every valve position.
        AddReachabilityIssues(c, issues)

        ' Overlapping symbols.
        Dim list = els.Where(Function(e) TypeOf e IsNot Junction AndAlso TypeOf e IsNot TextNote).ToList()
        For i = 0 To list.Count - 1
            For j = i + 1 To list.Count - 1
                Dim a = list(i).WorldBounds(), b = list(j).WorldBounds()
                a.Inflate(-6, -6) : b.Inflate(-6, -6)
                If a.IntersectsWith(b) Then
                    Dim r = RectangleF.Intersect(a, b)
                    If r.Width * r.Height > 0.3 * Math.Min(a.Width * a.Height, b.Width * b.Height) Then
                        add(IssueSeverity.Info, $"{Name(list(i))} and {Name(list(j))} overlap on the drawing.", list(j))
                    End If
                End If
            Next
        Next

        ' Signal overlap in pneumatic sequence circuits.
        Try
            Dim moves = FindMoves(c)
            Dim seq = InferSequence(moves)
            For Each msg In FindOverlaps(moves, seq)
                issues.Add(OverlapIssue(c, msg, seq))
            Next
        Catch ex As Exception
            ' Analysis is best effort; never let it break editing.
        End Try
        Return issues
    End Function

    ''' <summary>Components that feed compressed air into the circuit.</summary>
    Private Function IsAirSource(e As CircuitElement) As Boolean
        Return TypeOf e Is AirSupply OrElse TypeOf e Is Compressor OrElse TypeOf e Is AirReceiver
    End Function

    Private Function MarkExists(els As IEnumerable(Of CircuitElement), mark As String) As Boolean
        If String.IsNullOrWhiteSpace(mark) Then Return False
        Return els.OfType(Of ISignalSource)().Any(Function(k) k.SignalNames().Contains(mark.Trim(), StringComparer.OrdinalIgnoreCase))
    End Function

    Private Function OverlapIssue(c As Circuit, msg As String, seq As List(Of CircuitMove)) As CheckIssue
        Dim issue As New CheckIssue With {.Severity = IssueSeverity.Error, .Message = msg}
        Dim text = String.Join(" ", seq.Select(Function(m) m.ToString()))
        Try
            Dim parsed = MotionSequence.Parse(text)
            issue.Message &= $" Intended sequence: {text}."
            issue.FixLabel = $"Redesign {text} with the cascade method"
            issue.Fix = Function() SequenceGenerator.Generate(parsed, GeneratorMethod.PneumaticCascade, False).Circuit
        Catch ex As FormatException
            issue.Message &= " Use the cascade method or a relay step chain (Tools > Circuit Generator)."
        End Try
        Return issue
    End Function

    ''' <summary>Second fix for an overlap issue: the electro-pneumatic redesign.</summary>
    Public Function ElectroFix(issue As CheckIssue) As Func(Of Circuit)
        Dim marker = "Intended sequence: "
        Dim i = issue.Message.IndexOf(marker)
        If i < 0 Then Return Nothing
        Dim text = issue.Message.Substring(i + marker.Length).TrimEnd("."c)
        Return Function() SequenceGenerator.Generate(MotionSequence.Parse(text), GeneratorMethod.ElectroRelayChain, False).Circuit
    End Function

    Private Sub AddReachabilityIssues(c As Circuit, issues As List(Of CheckIssue))
        ' Undirected connectivity through tubes and every passage any valve position could open.
        Dim ports = c.AllPorts().ToList()
        Dim index As New Dictionary(Of Port, Integer)
        For i = 0 To ports.Count - 1
            index(ports(i)) = i
        Next
        Dim parent = Enumerable.Range(0, ports.Count).ToArray()
        Dim find As Func(Of Integer, Integer) = Function(i)
                                                    While parent(i) <> i
                                                        parent(i) = parent(parent(i))
                                                        i = parent(i)
                                                    End While
                                                    Return i
                                                End Function
        Dim join = Sub(a As Port, b As Port)
                       If a Is Nothing OrElse b Is Nothing OrElse Not index.ContainsKey(a) OrElse Not index.ContainsKey(b) Then Return
                       Dim ra = find(index(a)), rb = find(index(b))
                       If ra <> rb Then parent(ra) = rb
                   End Sub
        For Each t In c.Tubes
            join(t.A, t.B)
        Next
        For Each e In c.Elements
            Dim v = TryCast(e, DirectionalValve)
            If v IsNot Nothing Then
                For pos = 0 To v.Positions - 1
                    For Each f In v.PassagesIn(pos)
                        join(v.GetPort(f(0)), v.GetPort(f(1)))
                    Next
                Next
            ElseIf TypeOf e Is LogicValve OrElse TypeOf e Is QuickExhaustValve OrElse TypeOf e Is CounterbalanceValve Then
                Dim firstPort = e.Ports(0)
                For Each p In e.Ports.Skip(1)
                    join(firstPort, p)
                Next
            ElseIf TypeOf e Is FlowControlValve OrElse TypeOf e Is CheckValve OrElse TypeOf e Is PressureRegulator OrElse TypeOf e Is CompensatedFlowControl OrElse
                   TypeOf e Is AirReceiver OrElse TypeOf e Is FlowMeter OrElse TypeOf e Is ShutOffValve Then
                join(e.Ports(0), e.Ports(1))
            End If
        Next
        Dim connectors = c.Elements.OfType(Of PageConnector)().ToList()
        For Each a In connectors
            For Each b In connectors
                If a.Matches(b) Then join(a.Ports(0), b.Ports(0))
            Next
        Next
        Dim sources As New HashSet(Of Integer)
        Dim sinks As New HashSet(Of Integer)
        For Each p In ports
            If IsAirSource(p.Owner) OrElse TypeOf p.Owner Is HydraulicPump OrElse TypeOf p.Owner Is PageConnector Then sources.Add(find(index(p)))
            If (p.VentsWhenOpen AndAlso p.ConnectionCount = 0) OrElse TypeOf p.Owner Is Silencer OrElse TypeOf p.Owner Is HydraulicTank OrElse
               TypeOf p.Owner Is PageConnector Then sinks.Add(find(index(p)))
        Next
        For Each cyl In c.Elements.OfType(Of CylinderBase)()
            Dim cap = cyl.Ports(0)
            If cap.ConnectionCount = 0 Then Continue For
            Dim root = find(index(cap))
            If Not sources.Contains(root) Then
                issues.Add(New CheckIssue With {.Severity = IssueSeverity.Error, .Element = cyl,
                    .Message = $"Cylinder {Name(cyl)} can never extend: no valve position connects its cap side to the supply."})
            ElseIf Not sinks.Contains(root) Then
                issues.Add(New CheckIssue With {.Severity = IssueSeverity.Error, .Element = cyl,
                    .Message = $"Cylinder {Name(cyl)}: its cap side can never exhaust, so it cannot retract (missing exhaust)."})
            End If
            If cyl.Ports.Count > 1 AndAlso cyl.Ports(1).ConnectionCount > 0 Then
                Dim rr = find(index(cyl.Ports(1)))
                If Not sinks.Contains(rr) Then
                    issues.Add(New CheckIssue With {.Severity = IssueSeverity.Error, .Element = cyl,
                        .Message = $"Cylinder {Name(cyl)}: its rod side can never exhaust, so it cannot extend (missing exhaust)."})
                End If
            End If
        Next
    End Sub

    ''' <summary>The manual elements a user could operate, most likely start button first.</summary>
    Public Function ManualElements(c As Circuit) As List(Of CircuitElement)
        Dim score = Function(e As CircuitElement) As Integer
                        Dim l = If(e.Label, "").ToUpperInvariant()
                        If l = "S1" OrElse l = "1S0" OrElse l = "1S1" Then Return 0
                        If l.StartsWith("S") OrElse l.Contains("S") Then Return 1
                        Return 2
                    End Function
        ' Main switches, shut-off valves, workpieces, counters and emergency stops are not 'start' controls.
        Return c.Elements.Where(Function(e) e.IsManuallyOperated AndAlso TypeOf e IsNot HydraulicPump AndAlso TypeOf e IsNot Compressor AndAlso
                                     TypeOf e IsNot ShutOffValve AndAlso TypeOf e IsNot SuctionCup AndAlso TypeOf e IsNot ElectricCounter AndAlso
                                     Not (TypeOf e Is ElectricContact AndAlso DirectCast(e, ElectricContact).Operator = ContactOperator.EmergencyStop) AndAlso
                                     Not (TypeOf e Is DirectionalValve AndAlso DirectCast(e, DirectionalValve).Actuator = ValveActuator.Solenoid)).
                          OrderBy(score).ThenBy(Function(e) e.Label).ToList()
    End Function

    ''' <summary>Runs a copy of the circuit and reports run-time problems.</summary>
    Public Function DynamicCheck(c As Circuit, realPhysics As Boolean) As List(Of CheckIssue)
        Dim issues As New List(Of CheckIssue)
        Dim copy = Circuit.FromXml(c.ToXml())
        Dim sim As New Simulator(copy) With {.RealPhysics = realPhysics}
        sim.Reset()
        Dim warnings As New HashSet(Of String)(sim.Warnings)
        Dim starts = ManualElements(copy)
        Dim stuckTime As New Dictionary(Of DirectionalValve, Double)
        Dim moved As New HashSet(Of CylinderBase)
        If starts.Count > 0 Then starts(0).OnSimMouseDown(PointF.Empty) : sim.RunLogic()
        For i = 1 To 3060
            If i = 61 AndAlso starts.Count > 0 Then starts(0).OnSimMouseUp() : sim.RunLogic()
            sim.Step(0.005)
            For Each w In sim.Warnings
                warnings.Add(w)
            Next
            For Each cyl In copy.Elements.OfType(Of CylinderBase)()
                If cyl.Position > 0.02 Then moved.Add(cyl)
            Next
            For Each v In copy.Elements.OfType(Of DirectionalValve)().Where(Function(x) x.IsMemoryValve)
                Dim s = v.ActiveSignals(sim)
                If s(0) AndAlso s(1) Then
                    stuckTime(v) = If(stuckTime.ContainsKey(v), stuckTime(v), 0) + 0.005
                Else
                    stuckTime(v) = 0
                End If
                If stuckTime(v) >= 0.2 AndAlso Not issues.Any(Function(x) x.Message.Contains("both signals")) Then
                    Dim original = c.Elements.FirstOrDefault(Function(e) e.Label = v.Label AndAlso e.TypeName = v.TypeName)
                    issues.Add(New CheckIssue With {.Severity = IssueSeverity.Error, .Element = original,
                        .Message = $"Test run: valve {Name(v)} receives both signals at the same time (signal overlap); the machine stops."})
                End If
            Next
        Next
        For Each w In warnings
            issues.Add(New CheckIssue With {.Severity = IssueSeverity.Warning, .Message = "Test run: " & w})
        Next
        If starts.Count > 0 Then
            Dim still = copy.Elements.OfType(Of CylinderBase)().Where(Function(k) Not moved.Contains(k)).ToList()
            ' Some circuits need several buttons at once (two-hand control): try them all together before reporting.
            Dim together = If(still.Count > 0 AndAlso starts.Count > 1, MovedWithAllOperated(c, realPhysics), New HashSet(Of String))
            For Each cyl In still.Where(Function(k) Not together.Contains(Name(k)))
                issues.Add(New CheckIssue With {.Severity = IssueSeverity.Info,
                    .Message = $"Test run: cylinder {Name(cyl)} did not move within 15 s, neither after operating {Name(starts(0))} " &
                               If(starts.Count > 1, "nor with all hand-operated elements operated together.", "alone.")})
            Next
        End If
        Return issues
    End Function

    ''' <summary>Names of the cylinders that move when every hand-operated element is held at the same time.</summary>
    Private Function MovedWithAllOperated(c As Circuit, realPhysics As Boolean) As HashSet(Of String)
        Dim copy = Circuit.FromXml(c.ToXml())
        Dim sim As New Simulator(copy) With {.RealPhysics = realPhysics}
        sim.Reset()
        For Each e In ManualElements(copy)
            e.OnSimMouseDown(PointF.Empty)
        Next
        sim.RunLogic()
        Dim result As New HashSet(Of String)
        For i = 1 To 3000
            sim.Step(0.005)
            For Each cyl In copy.Elements.OfType(Of CylinderBase)()
                If cyl.Position > 0.02 Then result.Add(Name(cyl))
            Next
        Next
        Return result
    End Function

    ' ================================================================= explanation

    ''' <summary>A plain-English description of the circuit and a step-by-step account of a test run.</summary>
    Public Function Explain(c As Circuit, operate As CircuitElement, realPhysics As Boolean) As String
        Dim sb As New StringBuilder()
        Dim els = c.Elements

        sb.AppendLine("WHAT IS IN THE CIRCUIT")
        Dim parts As New List(Of String)
        Dim describe = Sub(n As Integer, singular As String, plural As String)
                           If n > 0 Then parts.Add($"{n} {If(n = 1, singular, plural)}")
                       End Sub
        describe(els.OfType(Of SingleActingCylinder)().Count(Function(k) TypeOf k IsNot HydraulicSingleActingCylinder), "single-acting cylinder", "single-acting cylinders")
        describe(els.OfType(Of DoubleActingCylinder)().Count(Function(k) TypeOf k IsNot HydraulicCylinder AndAlso TypeOf k IsNot SemiRotaryActuator), "double-acting cylinder", "double-acting cylinders")
        describe(els.OfType(Of HydraulicCylinder)().Count() + els.OfType(Of HydraulicSingleActingCylinder)().Count(), "hydraulic cylinder", "hydraulic cylinders")
        describe(els.OfType(Of DirectionalValve)().Count(), "directional control valve", "directional control valves")
        describe(els.OfType(Of FlowControlValve)().Count(), "flow control valve", "flow control valves")
        describe(els.OfType(Of LogicValve)().Count(), "logic valve", "logic valves")
        describe(els.OfType(Of ElectricCoil)().Count(Function(k) k.Kind = CoilKind.Relay OrElse k.Kind = CoilKind.OnDelayTimer OrElse k.Kind = CoilKind.OffDelayTimer), "relay", "relays")
        describe(els.OfType(Of ElectricContact)().Count(Function(k) k.Operator = ContactOperator.ProximitySensor OrElse k.Operator = ContactOperator.LimitSwitch), "sensor contact", "sensor contacts")
        describe(els.OfType(Of HydraulicPump)().Count(), "hydraulic pump", "hydraulic pumps")
        sb.AppendLine(If(parts.Count = 0, "No actuators.", "The circuit has " & String.Join(", ", parts) & "."))
        sb.AppendLine()

        Dim map = BuildTubeMap(c)
        Dim moves = FindMoves(c)
        If moves.Count > 0 Then
            sb.AppendLine("HOW EACH ACTUATOR IS CONTROLLED")
            For Each cyl In els.OfType(Of CylinderBase)().OrderBy(Function(k) k.Label)
                Dim inline As New List(Of CircuitElement)
                Dim vp = TraceToValve(map, cyl.Ports(0), inline)
                If cyl.Ports.Count > 1 Then TraceToValve(map, cyl.Ports(1), inline)
                If vp Is Nothing Then
                    sb.AppendLine($"• {cyl.DisplayName} {Name(cyl)} is not connected to a directional valve.")
                    Continue For
                End If
                Dim v = DirectCast(vp.Owner, DirectionalValve)
                sb.AppendLine($"• {cyl.DisplayName} {Name(cyl)} is controlled by valve {Name(v)} ({v.DisplayName}).")
                For Each m In moves.Where(Function(x) x.Cylinder Is cyl)
                    sb.AppendLine($"    It {If(m.Extend, "extends", "retracts")} by {If(m.Trigger.Description = "", "?", m.Trigger.Description)}.")
                Next
                For Each fc In inline.OfType(Of FlowControlValve)()
                    sb.AppendLine($"    Its speed is limited by flow control valve {Name(fc)} ({fc.OpeningPercent} % opening{If(fc.HasCheckValve, ", one-way", "")}).")
                Next
                For Each q In inline.OfType(Of QuickExhaustValve)()
                    sb.AppendLine($"    Quick exhaust valve {Name(q)} lets it return faster.")
                Next
            Next
            sb.AppendLine()
            Dim seq = InferSequence(moves)
            If seq.Count >= 2 Then
                sb.AppendLine("MOTION SEQUENCE")
                sb.AppendLine("  " & String.Join(" ", seq.Select(Function(m) $"{m.Cylinder.Label}{If(m.Extend, "+", "-")}")) & $"   (as letters: {String.Join(" ", seq)})")
                For Each o In FindOverlaps(moves, seq)
                    sb.AppendLine("  ! " & o)
                Next
                sb.AppendLine()
            End If
        End If

        sb.AppendLine("STEP BY STEP (test run)")
        sb.Append(Narrate(c, operate, realPhysics))
        Return sb.ToString()
    End Function

    ''' <summary>Simulates a copy of the circuit and writes down what happens.</summary>
    ''' <summary>Name of a valve's current position as on its symbol.</summary>
    Private Function PositionName(v As DirectionalValve) As String
        Select Case v.State
            Case 1 : Return "position a"
            Case 2 : Return "position b"
        End Select
        If v.Positions = 3 Then Return "its centre position"
        ' A memory valve has no spring "normal position": it is switched over to b.
        If v.IsMemoryValve Then Return "position b"
        Return "its normal position (spring return)"
    End Function

    Public Function Narrate(c As Circuit, operate As CircuitElement, realPhysics As Boolean) As String
        Dim sb As New StringBuilder()
        Dim copy = Circuit.FromXml(c.ToXml())
        Dim sim As New Simulator(copy) With {.RealPhysics = realPhysics}
        sim.Reset()
        Dim target = If(operate Is Nothing, Nothing, copy.Elements.FirstOrDefault(Function(e) e.TypeName = operate.TypeName AndAlso e.Label = operate.Label AndAlso e.X = operate.X AndAlso e.Y = operate.Y))
        If target Is Nothing Then target = ManualElements(copy).FirstOrDefault()

        Dim valveState = copy.Elements.OfType(Of DirectionalValve)().ToDictionary(Function(v) v, Function(v) v.State)
        Dim coilState = copy.Elements.OfType(Of ElectricCoil)().ToDictionary(Function(k) k, Function(k) k.Active)
        Dim cylDir = copy.Elements.OfType(Of CylinderBase)().ToDictionary(Function(k) k, Function(k) 0)
        Dim cylPos = copy.Elements.OfType(Of CylinderBase)().ToDictionary(Function(k) k, Function(k) k.Position)
        Dim lines As New List(Of String)
        Dim lastEvent = 0.0
        Dim stepNo = 0

        Dim collect = Function(withMotion As Boolean) As List(Of String)
                          Dim ev As New List(Of String)
                          Dim arrivals As New List(Of String), motions As New List(Of String)
                          For Each k In If(withMotion, cylDir.Keys.ToList(), New List(Of CylinderBase)())
                              Dim d = Math.Sign(k.Position - cylPos(k))
                              If Math.Abs(k.Position - cylPos(k)) < 0.00001 Then d = 0
                              ' End positions are reported when reached, even if the cylinder reverses at once.
                              If cylPos(k) < 0.995 AndAlso k.Position >= 0.995 Then arrivals.Add($"cylinder {Name(k)} is fully extended{If(String.IsNullOrEmpty(k.ExtendedMark), "", $" (operates {k.ExtendedMark})")}")
                              If cylPos(k) > 0.005 AndAlso k.Position <= 0.005 Then arrivals.Add($"cylinder {Name(k)} is fully retracted{If(String.IsNullOrEmpty(k.RetractedMark), "", $" (operates {k.RetractedMark})")}")
                              If d <> cylDir(k) Then
                                  If d > 0 Then motions.Add($"cylinder {Name(k)} starts to extend")
                                  If d < 0 Then motions.Add($"cylinder {Name(k)} starts to retract")
                                  If d = 0 AndAlso k.Position > 0.005 AndAlso k.Position < 0.995 Then motions.Add($"cylinder {Name(k)} stops at {k.Position * 100:0} % of its stroke")
                                  cylDir(k) = d
                              End If
                              cylPos(k) = k.Position
                          Next
                          ev.AddRange(arrivals)
                          For Each k In coilState.Keys.ToList()
                              If k.Active <> coilState(k) Then
                                  coilState(k) = k.Active
                                  Select Case k.Kind
                                      Case CoilKind.Solenoid : ev.Add($"solenoid {k.Label} {If(k.Active, "is energized", "is switched off")}")
                                      Case CoilKind.Lamp : ev.Add($"lamp {k.Label} {If(k.Active, "lights up", "goes out")}")
                                      Case CoilKind.OnDelayTimer, CoilKind.OffDelayTimer : ev.Add($"timer relay {k.Label} {If(k.Active, "switches on", "switches off")}")
                                      Case Else : ev.Add($"relay {k.Label} {If(k.Active, "picks up", "drops out")}")
                                  End Select
                              End If
                          Next
                          ' Valves operated by hand first: they cause the others to switch.
                          For Each v In valveState.Keys.OrderBy(Function(x) If(x.IsManuallyOperated, 0, 1)).ToList()
                              If v.State <> valveState(v) Then
                                  valveState(v) = v.State
                                  ' Roller valves just follow the cylinders; their switching is implied.
                                  If v.IsRollerOperated Then Continue For
                                  ev.Add($"valve {Name(v)} switches to {PositionName(v)}")
                              End If
                          Next
                          ev.AddRange(motions)
                          Return ev
                      End Function
        Dim record = Sub(prefix As String, ev As List(Of String))
                         If ev.Count = 0 AndAlso prefix = "" Then Return
                         stepNo += 1
                         Dim body = String.Join(" → ", ev)
                         lines.Add($"{stepNo,2}. t = {sim.Time,5:0.00} s   {prefix}{If(prefix <> "" AndAlso body <> "", " → ", "")}{body}")
                         lastEvent = sim.Time
                     End Sub

        collect(True)
        If target Is Nothing Then
            lines.Add("There is nothing to operate by hand, so the circuit runs by itself:")
        Else
            Dim isButton = (TypeOf target Is DirectionalValve AndAlso DirectCast(target, DirectionalValve).Actuator = ValveActuator.PushButton) OrElse
                           (TypeOf target Is ElectricContact AndAlso DirectCast(target, ElectricContact).Operator = ContactOperator.PushButton)
            target.OnSimMouseDown(PointF.Empty)
            sim.RunLogic()
            record($"You {If(isButton, "press", "switch on")} {Name(target)}", collect(False))
            If isButton Then
                For i = 1 To 60
                    sim.Step(0.005)
                    record("", collect(True))
                Next
                target.OnSimMouseUp()
                sim.RunLogic()
                record($"You release {Name(target)}", collect(False))
            End If
        End If
        Dim cylinders = cylDir.Keys.ToList()
        Dim anyMoved = False
        For i = 1 To 6000
            sim.Step(0.005)
            record("", collect(True))
            If cylinders.Any(Function(k) k.Position > 0.05) Then anyMoved = True
            If anyMoved AndAlso cylinders.All(Function(k) k.Position <= 0.005) Then
                lines.Add($"       One complete cycle took {sim.Time:0.00} s; all cylinders are back in their start position." &
                          If(cylinders.Any(Function(k) cylDir(k) <> 0), " The circuit keeps repeating the cycle.", ""))
                Exit For
            End If
            If lines.Count >= 60 Then lines.Add("   ... (stopped after 60 events)") : Exit For
            If sim.Time - lastEvent > 4 Then Exit For
        Next
        If lines.Count = 0 Then lines.Add("Nothing happens.")
        For Each w In sim.Warnings.Distinct()
            lines.Add("   Warning: " & w)
        Next
        sb.AppendLine(String.Join(Environment.NewLine, lines))
        Return sb.ToString()
    End Function
End Module
