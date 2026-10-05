Imports System.Text

''' <summary>Outcome of checking a lesson.</summary>
Public Class LessonResult
    Public Property Passed As Boolean
    Public Property Feedback As String
End Class

''' <summary>A practical task with a starting circuit and an automatic check.</summary>
Public Class Lesson
    Public Property Title As String
    Public Property Goal As String
    Public Property Hint As String
    Public Property Start As Func(Of Circuit)
    Public Property Check As Func(Of Circuit, LessonResult)

    Public Overrides Function ToString() As String
        Return Title
    End Function
End Class

''' <summary>The built-in lessons. Checks run the student's circuit on a copy and observe it.</summary>
Public Module Lessons

    ' ---------------------------------------------------------------- helpers for checks

    Private Class Probe
        Public Circuit As Circuit
        Public Sim As Simulator
        Public Log As New List(Of String)

        Public Sub New(original As Circuit, Optional realPhysics As Boolean = False)
            Circuit = Circuit.FromXml(original.ToXml())
            Sim = New Simulator(Circuit) With {.RealPhysics = realPhysics}
            Sim.Reset()
        End Sub

        Public Function Find(label As String) As CircuitElement
            Return Circuit.Elements.FirstOrDefault(Function(e) String.Equals(e.Label?.Trim(), label, StringComparison.OrdinalIgnoreCase) AndAlso
                                                                   (e.IsManuallyOperated OrElse TypeOf e Is CylinderBase OrElse TypeOf e Is ElectricCoil))
        End Function

        Public Function Cylinder() As CylinderBase
            Return If(TryCast(Find("1A"), CylinderBase), Circuit.Elements.OfType(Of CylinderBase)().FirstOrDefault())
        End Function

        Public Sub Run(seconds As Double)
            For i = 1 To CInt(seconds / 0.005)
                Sim.Step(0.005)
            Next
        End Sub

        Public Sub Press(e As CircuitElement)
            e.OnSimMouseDown(PointF.Empty) : Sim.RunLogic()
        End Sub

        Public Sub Release(e As CircuitElement)
            e.OnSimMouseUp() : Sim.RunLogic()
        End Sub

        Public Sub Tap(e As CircuitElement)
            Press(e) : Run(0.2) : Release(e)
        End Sub
    End Class

    Private Function Pass(msg As String) As LessonResult
        Return New LessonResult With {.Passed = True, .Feedback = "Well done! " & msg}
    End Function

    Private Function Fail(msg As String) As LessonResult
        Return New LessonResult With {.Passed = False, .Feedback = msg}
    End Function

    Private Function Need(p As Probe, ParamArray labels As String()) As String
        Dim missing = labels.Where(Function(l) p.Find(l) Is Nothing).ToList()
        If missing.Count = 0 Then Return Nothing
        Return $"I cannot find {String.Join(", ", missing)}. Give your components these labels in the Properties panel " &
               "(push buttons / switches, and the cylinder 1A)."
    End Function

    ''' <summary>Components laid out side by side, not connected yet.</summary>
    Private Function Parts(ParamArray items As (El As CircuitElement, Label As String)()) As Circuit
        Dim c As New Circuit()
        Dim x = 40.0F
        For Each it In items
            c.Add(it.El, x, 120, it.Label)
            x += it.El.LocalBounds.Width + 50
        Next
        Return c
    End Function

    ' ---------------------------------------------------------------- the lessons

    Public ReadOnly All As Lesson() = {
        New Lesson With {
            .Title = "1. Direct control",
            .Goal = "Make the single-acting cylinder 1A extend while push button valve S1 is held down, and retract when it is released.",
            .Hint = "Supply → port 1 of the 3/2 valve; port 2 → the cylinder. Port 3 is the exhaust.",
            .Start = Function() Parts((New SingleActingCylinder(), "1A"), (Library.V32(ValveActuator.PushButton), "S1"), (New AirSupply(), "")),
            .Check = Function(c)
                         Dim p As New Probe(c)
                         Dim missing = Need(p, "S1", "1A")
                         If missing IsNot Nothing Then Return Fail(missing)
                         p.Run(0.5)
                         If p.Cylinder().Position > 0.05 Then Return Fail("The cylinder moves before S1 is pressed. Use a normally closed (NC) valve.")
                         p.Press(p.Find("S1")) : p.Run(2)
                         If p.Cylinder().Position < 0.99 Then Return Fail("When S1 is held, 1A does not extend fully. Check the supply and the tube from port 2 to the cylinder.")
                         p.Release(p.Find("S1")) : p.Run(2)
                         If p.Cylinder().Position > 0.01 Then Return Fail("1A does not retract after S1 is released. The cylinder air must exhaust through port 3.")
                         Return Pass("This is direct control: the operator's valve feeds the cylinder itself.")
                     End Function},
        New Lesson With {
            .Title = "2. Indirect control",
            .Goal = "Control the double-acting cylinder 1A with the 5/2 valve 1V1 (pneumatic pilot, spring return). " &
                    "Push button valve S1 sends the pilot signal: 1A extends while S1 is held and retracts when it is released.",
            .Hint = "S1 port 2 → pilot 14 of 1V1. 1V1 port 4 → cylinder port 1, port 2 → cylinder port 2. Both valves need a supply.",
            .Start = Function() Parts((New DoubleActingCylinder(), "1A"), (Library.V52(ValveActuator.Pilot, ValveReturn.Spring), "1V1"),
                                      (Library.V32(ValveActuator.PushButton), "S1"), (New AirSupply(), ""), (New AirSupply(), "")),
            .Check = Function(c)
                         Dim p As New Probe(c)
                         Dim missing = Need(p, "S1", "1A")
                         If missing IsNot Nothing Then Return Fail(missing)
                         p.Press(p.Find("S1")) : p.Run(2)
                         If p.Cylinder().Position < 0.99 Then Return Fail("1A does not extend when S1 is held.")
                         Dim direct = CircuitAnalysis.FindMoves(p.Circuit).Any(Function(m) m.Extend AndAlso m.Valve.Actuator = ValveActuator.PushButton)
                         If direct Then Return Fail("1A is fed directly by the push button valve. In indirect control the push button only pilots the 5/2 valve.")
                         p.Release(p.Find("S1")) : p.Run(2)
                         If p.Cylinder().Position > 0.01 Then Return Fail("1A does not retract after S1 is released.")
                         Return Pass("Indirect control lets a small push button valve switch a big power valve.")
                     End Function},
        New Lesson With {
            .Title = "3. Speed control (meter-out)",
            .Goal = "Build an indirect control (as in lesson 2) where 1A extends slowly (at least 2 s for a stroke) but retracts quickly (under 1.2 s). Stroke time of 1A is 1 s.",
            .Hint = "Put a one-way flow control valve in the line to cylinder port 2 so that the air leaving the cylinder is throttled (port 2 of the flow control towards the cylinder).",
            .Start = Function() Parts((New DoubleActingCylinder(), "1A"), (Library.V52(ValveActuator.Pilot, ValveReturn.Spring), "1V1"),
                                      (New FlowControlValve() With {.OpeningPercent = 30}, "1V2"), (Library.V32(ValveActuator.PushButton), "S1"),
                                      (New AirSupply(), ""), (New AirSupply(), "")),
            .Check = Function(c)
                         Dim p As New Probe(c)
                         Dim missing = Need(p, "S1", "1A")
                         If missing IsNot Nothing Then Return Fail(missing)
                         p.Press(p.Find("S1"))
                         Dim t = 0.0
                         While p.Cylinder().Position < 0.999 AndAlso t < 15 : p.Run(0.05) : t += 0.05 : End While
                         If t >= 15 Then Return Fail("1A does not reach the end of its stroke when S1 is held.")
                         If t < 2 Then Return Fail($"1A extends in {t:0.0} s; it should take at least 2 s. Throttle the air leaving the rod side.")
                         p.Release(p.Find("S1"))
                         Dim tr = 0.0
                         While p.Cylinder().Position > 0.001 AndAlso tr < 15 : p.Run(0.05) : tr += 0.05 : End While
                         If tr > 1.2 Then Return Fail($"1A retracts in {tr:0.0} s; it should be under 1.2 s. Use a one-way (check) flow control so the return is free.")
                         Return Pass($"Extends in {t:0.0} s, retracts in {tr:0.0} s. Meter-out control gives smooth, load-independent speed.")
                     End Function},
        New Lesson With {
            .Title = "4. OR: two places",
            .Goal = "Cylinder 1A (single-acting) must extend when push button valve S1 OR push button valve S2 is pressed.",
            .Hint = "Use a shuttle valve. Without it, the air from one button would escape through the other button's exhaust.",
            .Start = Function() Parts((New SingleActingCylinder(), "1A"), (New ShuttleValve(), "1V1"), (Library.V32(ValveActuator.PushButton), "S1"),
                                      (Library.V32(ValveActuator.PushButton), "S2"), (New AirSupply(), ""), (New AirSupply(), "")),
            .Check = Function(c)
                         For Each b In {"S1", "S2"}
                             Dim p As New Probe(c)
                             Dim missing = Need(p, "S1", "S2", "1A")
                             If missing IsNot Nothing Then Return Fail(missing)
                             p.Press(p.Find(b)) : p.Run(2)
                             If p.Cylinder().Position < 0.99 Then Return Fail($"1A does not extend when only {b} is pressed.")
                             p.Release(p.Find(b)) : p.Run(2)
                             If p.Cylinder().Position > 0.01 Then Return Fail($"1A does not retract after {b} is released.")
                         Next
                         Return Pass("The shuttle valve passes whichever input has air and blocks the other.")
                     End Function},
        New Lesson With {
            .Title = "5. AND: two-hand safety",
            .Goal = "A press may only close when the operator uses both hands: double-acting cylinder 1A extends only while S1 AND S2 are both pressed.",
            .Hint = "A two-pressure valve (AND) passes air only when both inputs have air. Its output pilots a 5/2 valve.",
            .Start = Function() Parts((New DoubleActingCylinder(), "1A"), (Library.V52(ValveActuator.Pilot, ValveReturn.Spring), "1V1"),
                                      (New TwoPressureValve(), "1V2"), (Library.V32(ValveActuator.PushButton), "S1"), (Library.V32(ValveActuator.PushButton), "S2"),
                                      (New AirSupply(), ""), (New AirSupply(), ""), (New AirSupply(), "")),
            .Check = Function(c)
                         For Each b In {"S1", "S2"}
                             Dim p As New Probe(c)
                             Dim missing = Need(p, "S1", "S2", "1A")
                             If missing IsNot Nothing Then Return Fail(missing)
                             p.Press(p.Find(b)) : p.Run(2)
                             If p.Cylinder().Position > 0.05 Then Return Fail($"Unsafe: 1A moves when only {b} is pressed.")
                         Next
                         Dim q As New Probe(c)
                         q.Press(q.Find("S1")) : q.Press(q.Find("S2")) : q.Run(2)
                         If q.Cylinder().Position < 0.99 Then Return Fail("1A does not extend when both S1 and S2 are pressed.")
                         q.Release(q.Find("S1")) : q.Run(2)
                         If q.Cylinder().Position > 0.01 Then Return Fail("1A should retract as soon as one hand lets go.")
                         Return Pass("Two-hand control keeps both hands away from the tool.")
                     End Function},
        New Lesson With {
            .Title = "6. Memory valve",
            .Goal = "A short press on S1 makes double-acting cylinder 1A extend and STAY extended. A short press on S2 makes it retract.",
            .Hint = "Use a 5/2 double pilot (memory) valve: S1 → pilot 14, S2 → pilot 12.",
            .Start = Function() Parts((New DoubleActingCylinder(), "1A"), (Library.V52(ValveActuator.Pilot, ValveReturn.Pilot), "1V1"),
                                      (Library.V32(ValveActuator.PushButton), "S1"), (Library.V32(ValveActuator.PushButton), "S2"),
                                      (New AirSupply(), ""), (New AirSupply(), ""), (New AirSupply(), "")),
            .Check = Function(c)
                         Dim p As New Probe(c)
                         Dim missing = Need(p, "S1", "S2", "1A")
                         If missing IsNot Nothing Then Return Fail(missing)
                         p.Tap(p.Find("S1")) : p.Run(2)
                         If p.Cylinder().Position < 0.99 Then Return Fail("After a short press on S1, 1A should extend and stay extended.")
                         p.Tap(p.Find("S2")) : p.Run(2)
                         If p.Cylinder().Position > 0.01 Then Return Fail("After a short press on S2, 1A should retract.")
                         Return Pass("A double pilot valve remembers the last signal: it is a pneumatic memory.")
                     End Function},
        New Lesson With {
            .Title = "7. Automatic return",
            .Goal = "A short press on start button S1 makes cylinder 1A extend; when it reaches the end it returns by itself.",
            .Hint = "Give 1A the extended mark 1S2 and use a roller lever valve with roller mark 1S2 to pilot 12 of a memory valve.",
            .Start = Function() Parts((New DoubleActingCylinder() With {.ExtendedMark = "1S2"}, "1A"), (Library.V52(ValveActuator.Pilot, ValveReturn.Pilot), "1V1"),
                                      (Library.V32(ValveActuator.PushButton), "S1"), (New Valve32() With {.Actuator = ValveActuator.RollerLever, .TriggerMark = "1S2"}, "1S2"),
                                      (New AirSupply(), ""), (New AirSupply(), ""), (New AirSupply(), "")),
            .Check = Function(c)
                         Dim p As New Probe(c)
                         Dim missing = Need(p, "S1", "1A")
                         If missing IsNot Nothing Then Return Fail(missing)
                         p.Tap(p.Find("S1"))
                         Dim reached = False
                         For i = 1 To 100
                             p.Run(0.05)
                             If p.Cylinder().Position >= 0.999 Then reached = True
                             If reached AndAlso p.Cylinder().Position <= 0.001 Then Return Pass("The limit valve detects the end position and starts the return automatically.")
                         Next
                         Return Fail(If(reached, "1A extends but does not come back by itself.", "1A does not extend after a short press on S1."))
                     End Function},
        New Lesson With {
            .Title = "8. Electrical self-holding",
            .Goal = "Electro-pneumatic: pressing S1 briefly switches relay K1 on and it stays on (self-holding); S0 (normally closed) switches it off. " &
                    "K1 operates solenoid 1M1 of a 5/2 single-solenoid valve that extends cylinder 1A.",
            .Hint = "Rung 1: +24 V → S0 (NC) → (S1 in parallel with a K1 NO contact) → coil K1 → 0 V. Rung 2: +24 V → K1 NO contact → solenoid coil 1M1 → 0 V.",
            .Start = Nothing,
            .Check = Function(c)
                         Dim p As New Probe(c)
                         Dim missing = Need(p, "S1", "S0", "1A")
                         If missing IsNot Nothing Then Return Fail(missing)
                         p.Tap(p.Find("S1")) : p.Run(2)
                         If Not p.Sim.IsCoilActive("K1") Then Return Fail("Relay K1 does not stay on after S1 is released. Add the self-holding contact K1 in parallel with S1.")
                         If p.Cylinder().Position < 0.99 Then Return Fail("K1 is on, but 1A does not extend. Check solenoid 1M1 and the valve's solenoid label.")
                         p.Tap(p.Find("S0")) : p.Run(2)
                         If p.Sim.IsCoilActive("K1") Then Return Fail("S0 does not switch K1 off. S0 must be a normally closed contact in series.")
                         If p.Cylinder().Position > 0.01 Then Return Fail("1A does not retract after stopping.")
                         Return Pass("Self-holding (latching) is the basis of every start/stop circuit.")
                     End Function},
        New Lesson With {
            .Title = "9. Hydraulics: relief valve",
            .Goal = "Build a hydraulic circuit: pump, relief valve set to 60 bar, 4/3 valve with solenoids 1M1 / 1M2 (push buttons S1 / S2), and hydraulic cylinder 1A. " &
                    "1A must extend with S1 and retract with S2, and the pressure must never exceed 60 bar.",
            .Hint = "Pump → junction → relief valve P (relief T → tank) and valve P. Valve T → tank. Use hydraulic junctions for branches.",
            .Start = Nothing,
            .Check = Function(c)
                         Dim p As New Probe(c)
                         Dim missing = Need(p, "S1", "S2", "1A")
                         If missing IsNot Nothing Then Return Fail(missing)
                         If Not p.Circuit.Elements.OfType(Of ReliefValve)().Any() Then Return Fail("There is no pressure relief valve.")
                         p.Press(p.Find("S1")) : p.Run(4)
                         If p.Cylinder().Position < 0.99 Then Return Fail("1A does not extend while S1 is held.")
                         Dim pmax = p.Circuit.AllPorts().Where(Function(x) x.Kind = PortKind.Hydraulic).Max(Function(x) x.Pressure)
                         If pmax > 60.5 Then Return Fail($"The pressure rises to {pmax:0} bar. Set the relief valve to 60 bar and connect it right after the pump.")
                         p.Release(p.Find("S1")) : p.Press(p.Find("S2")) : p.Run(4)
                         If p.Cylinder().Position > 0.01 Then Return Fail("1A does not retract while S2 is held.")
                         Return Pass("The relief valve protects the pump and everything else: at the end of the stroke the oil flows back to tank at 60 bar.")
                     End Function},
        New Lesson With {
            .Title = "10. Sequence A+ B+ B- A-",
            .Goal = "Two double-acting cylinders 1A and 2A. After a short press on start button S1 (electrical) or 1S0 (pneumatic), they move A+ B+ B- A- once.",
            .Hint = "Tip: Tools > Circuit Generator builds this for you; study how it avoids signal overlap.",
            .Start = Nothing,
            .Check = Function(c)
                         Dim p As New Probe(c)
                         Dim start = If(p.Find("S1"), p.Find("1S0"))
                         If start Is Nothing Then Return Fail("I cannot find a start button labelled S1 or 1S0.")
                         Dim a = TryCast(p.Find("1A"), CylinderBase), b = TryCast(p.Find("2A"), CylinderBase)
                         If a Is Nothing OrElse b Is Nothing Then Return Fail("Label the cylinders 1A and 2A.")
                         p.Tap(start)
                         Dim events As New List(Of String)
                         Dim ea = False, eb = False
                         For i = 1 To 2000
                             p.Sim.Step(0.005)
                             If Not ea AndAlso a.Position >= 0.995 Then ea = True : events.Add("A+")
                             If ea AndAlso a.Position <= 0.005 Then ea = False : events.Add("A-")
                             If Not eb AndAlso b.Position >= 0.995 Then eb = True : events.Add("B+")
                             If eb AndAlso b.Position <= 0.005 Then eb = False : events.Add("B-")
                         Next
                         Dim got = String.Join(" ", events)
                         If got.StartsWith("A+ B+ B- A-") Then Return Pass("The sequence is correct.")
                         Return Fail($"The cylinders moved: {If(got = "", "(nothing)", got)}. Expected A+ B+ B- A-.")
                     End Function}
    }
End Module
