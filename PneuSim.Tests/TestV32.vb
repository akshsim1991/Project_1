''' Tests for PneuSim 3.2: new components, faults, troubleshooting, replay, measuring.
Module TestV32

    Private Function Lamp(c As Circuit, plusX As Single, contact As CircuitElement, label As String) As ElectricCoil
        Dim plus = c.Add(New PowerTerminal(), plusX, 0)
        Dim lmp = c.Add(New ElectricCoil() With {.Kind = CoilKind.Lamp}, plusX, 200, label)
        Dim zero = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, plusX, 300)
        c.Connect(plus, "1", contact, "1")
        c.Connect(contact, "2", lmp, "A1")
        c.Connect(lmp, "A2", zero, "1")
        Return lmp
    End Function

    ''' <summary>Supply - 3/2 push button valve (1S1) - single-acting cylinder 1A.</summary>
    Private Function SimpleCylinder() As Circuit
        Dim c As New Circuit()
        Dim sup = c.Add(New AirSupply(), 0, 300, "0Z")
        Dim v = c.Add(Library.V32(ValveActuator.PushButton), 0, 150, "1S1")
        Dim cyl = c.Add(New SingleActingCylinder(), 0, 0, "1A")
        c.Connect(sup, "1", v, "1")
        c.Connect(v, "2", cyl, "1")
        Return c
    End Function

    Private Sub Check(name As String, ok As Boolean, Optional detail As String = "")
        TestMain.Check(name, ok, detail)
    End Sub

    Private ReadOnly CutawaySheet As New List(Of Drawing.Bitmap)

    ''' <summary>Renders the cutaway of an element for the review sheet; returns False if drawing fails.</summary>
    Private Function AddCutaway(e As CircuitElement, simulating As Boolean) As Boolean
        Try
            Using view As New CutawayView() With {.Size = New Drawing.Size(600, 300), .Element = e, .Simulating = simulating}
                Dim bmp As New Drawing.Bitmap(600, 300)
                Using g = Drawing.Graphics.FromImage(bmp)
                    view.Render(g)
                End Using
                CutawaySheet.Add(bmp)
            End Using
            Return True
        Catch ex As Exception
            Console.WriteLine("cutaway error " & e.DisplayName & ": " & ex.Message)
            Return False
        End Try
    End Function

    Private Sub SaveCutawaySheet()
        Dim rows = (CutawaySheet.Count + 3) \ 4
        Using big As New Drawing.Bitmap(4 * 600, Math.Max(1, rows) * 300)
            Using g = Drawing.Graphics.FromImage(big)
                g.Clear(Drawing.Color.Gray)
                For k = 0 To CutawaySheet.Count - 1
                    g.DrawImage(CutawaySheet(k), (k Mod 4) * 600, (k \ 4) * 300)
                Next
            End Using
            big.Save(IO.Path.Combine("out", "v32_cutaways.png"), Drawing.Imaging.ImageFormat.Png)
        End Using
    End Sub

    Sub RunAll()
        NewComponents()
        FaultTests()
    End Sub

    Sub NewComponents()
        ' Pressure switch operates a contact and a lamp.
        Dim c As New Circuit()
        Dim sup = c.Add(New AirSupply(), 0, 300)
        Dim v = c.Add(Library.V32(ValveActuator.PushButton), 0, 150, "1S1")
        Dim ps = c.Add(New PressureSwitch() With {.Setting = 4}, 0, 0, "B1")
        c.Connect(sup, "1", v, "1") : c.Connect(v, "2", ps, "1")
        Dim k = c.Add(Library.Contact(ContactOperator.PressureSwitch, False, "", "B1"), 300, 80)
        Dim h = Lamp(c, 300, k, "H1")
        Dim s As New Simulator(c) : s.Reset()
        Check("pressure switch off at rest", Not ps.IsOn AndAlso Not h.Active)
        Press(s, v)
        Check("pressure switch on at 6 bar, lamp lit", ps.IsOn AndAlso h.Active)
        Release(s, v)
        Check("pressure switch off again", Not ps.IsOn AndAlso Not h.Active)
        ps.Setting = 7 : s.Reset() : Press(s, v)
        Check("pressure switch set above supply stays off", Not ps.IsOn)
        Check("checker accepts the pressure switch contact", Not CircuitAnalysis.StaticChecks(c).Any(Function(i) i.Message.Contains("no pressure switch")))
        k.Reference = "B9"
        Check("checker flags a pressure switch contact without a switch", CircuitAnalysis.StaticChecks(c).Any(Function(i) i.Message.Contains("no pressure switch")))

        ' Shut-off valve.
        c = New Circuit()
        sup = c.Add(New AirSupply(), 0, 200)
        Dim sv = c.Add(New ShutOffValve(), 100, 100, "0V1")
        Dim gauge = c.Add(New PressureGauge(), 200, 0)
        c.Connect(sup, "1", sv, "1") : c.Connect(sv, "2", gauge, "1")
        s = New Simulator(c) : s.Reset()
        Check("shut-off valve open passes air", gauge.Ports(0).Pressure > 5.9)
        Press(s, sv) : Release(s, sv)
        Check("shut-off valve closed: the line is cut off (air trapped)", gauge.Ports(0).State = PortState.Floating)
        Press(s, sv) : Release(s, sv)
        Check("shut-off valve opens again", gauge.Ports(0).Pressure > 5.9)

        ' Vacuum generator, suction cup and vacuum switch.
        c = New Circuit()
        sup = c.Add(New AirSupply(), 0, 400)
        v = c.Add(Library.V32(ValveActuator.Selector), 0, 300, "1S1")
        Dim gen = c.Add(New VacuumGenerator(), 0, 200, "1Z1")
        Dim j = c.Add(New Junction(), 20, 120)
        Dim cup = c.Add(New SuctionCup(), 0, 0, "1U1")
        Dim vs = c.Add(New PressureSwitch() With {.Setting = -0.5}, 100, 40, "B2")
        c.Connect(sup, "1", v, "1") : c.Connect(v, "2", gen, "1")
        c.Connect(gen, "V", j, "1") : c.Connect(j, "1", cup, "1") : c.Connect(j, "1", vs, "1")
        s = New Simulator(c) : s.Reset()
        Check("no vacuum while the ejector is off", cup.Ports(0).Pressure > -0.01 AndAlso Not cup.Holding)
        Press(s, v) : Run(s, 0.1)
        Check("ejector makes vacuum, cup holds", cup.Ports(0).Pressure < -0.8 AndAlso cup.Holding, cup.Ports(0).Pressure.ToString("0.00"))
        Check("vacuum switch reports the part held", vs.IsOn)
        Render(c, True, "v32_vacuum_sim")
        Dim cutOk = AddCutaway(gen, True) And AddCutaway(cup, True) And AddCutaway(vs, True)
        Check("ejector uses air", s.AirConsumed > 0)
        Press(s, cup) : Release(s, cup)
        Check("without a workpiece the vacuum is lost", cup.Ports(0).Pressure > -0.1 AndAlso Not vs.IsOn)
        Press(s, cup) : Release(s, cup)
        Check("workpiece back: held again", cup.Holding AndAlso vs.IsOn)
        Press(s, v)
        Check("ejector off: part released", Not cup.Holding AndAlso Not vs.IsOn)
        Check("vacuum switch help mentions vacuum", ComponentHelp.HelpFor(vs).Contains("Vacuum switch"))

        ' Gripper closes and opens.
        c = New Circuit()
        sup = c.Add(New AirSupply(), 0, 300)
        Dim v52 = c.Add(Library.V52(ValveActuator.Selector, ValveReturn.Spring), 0, 150, "1S1")
        Dim gr = c.Add(New Gripper() With {.ExtendedMark = "1B2"}, 0, 0, "1A")
        c.Connect(sup, "1", v52, "1") : c.Connect(v52, "4", gr, "1") : c.Connect(v52, "2", gr, "2")
        s = New Simulator(c) : s.Reset()
        Press(s, v52) : Run(s, 0.4)
        Check("gripper closes", gr.Position = 1 AndAlso s.IsMarkActive("1B2"))
        cutOk = cutOk And AddCutaway(gr, True)
        Press(s, v52) : Run(s, 0.4)
        Check("gripper opens", gr.Position = 0)

        ' Compressor fills a receiver and stops at the cut-out pressure.
        c = New Circuit()
        Dim comp = c.Add(New Compressor() With {.DeliveryNlMin = 300, .CutOutPressure = 8, .CutInPressure = 6.5}, 0, 300)
        Dim rec = c.Add(New AirReceiver() With {.VolumeLitres = 5, .InitialPressure = 0}, 0, 200)
        gauge = c.Add(New PressureGauge(), 200, 100)
        c.Connect(comp, "1", rec, "1") : c.Connect(rec, "2", gauge, "1")
        s = New Simulator(c) : s.Reset()
        Check("compressor charges the receiver (not a direct supply)", comp.ChargesReceiver AndAlso comp.Running)
        Run(s, 4)
        Dim mid = rec.Pressure
        Check("receiver pressure rises", mid > 3 AndAlso mid < 6, mid.ToString("0.00"))
        Run(s, 6)
        Check("compressor stops at cut-out", Not comp.Running AndAlso Math.Abs(rec.Pressure - 8) < 0.2, rec.Pressure.ToString("0.00"))
        Check("receiver feeds the line", Math.Abs(gauge.Ports(0).Pressure - rec.Pressure) < 0.01)
        cutOk = cutOk And AddCutaway(rec, True) And AddCutaway(comp, True)
        rec.Fault = FaultKind.Leak
        Run(s, 3)
        Check("leaking receiver loses pressure until the compressor restarts", comp.Running OrElse rec.Pressure < 8, rec.Pressure.ToString("0.00"))
        rec.Fault = FaultKind.None
        ' A compressor on its own is a plain supply.
        c = New Circuit()
        comp = c.Add(New Compressor(), 0, 300)
        gauge = c.Add(New PressureGauge(), 0, 100)
        c.Connect(comp, "1", gauge, "1")
        s = New Simulator(c) : s.Reset()
        Check("compressor without receiver supplies the cut-out pressure", Math.Abs(gauge.Ports(0).Pressure - 8) < 0.01)
        ' Air drawn from a receiver lowers its pressure.
        c = SimpleCylinder()
        Dim oldSup = Find(c, "0Z")
        c.Remove(oldSup)
        rec = c.Add(New AirReceiver() With {.VolumeLitres = 1, .InitialPressure = 6}, 0, 300)
        c.Connect(rec.GetPort("1"), Find(c, "1S1").GetPort("1"))
        s = New Simulator(c) : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 1.2)
        Check("cylinder stroke takes air from a small receiver", rec.Pressure < 5.9 AndAlso Pos(c) = 1, rec.Pressure.ToString("0.00"))

        ' Idle-return roller: a short pulse when the cylinder arrives, none when it leaves.
        c = New Circuit()
        sup = c.Add(New AirSupply(), 0, 400)
        Dim v5 = c.Add(Library.V52(ValveActuator.Selector, ValveReturn.Spring), 0, 250, "1S1")
        Dim cyl = c.Add(New DoubleActingCylinder() With {.RetractedMark = "1S2a", .ExtendedMark = "1S3"}, 0, 0, "1A")
        Dim idle = c.Add(Library.V32(ValveActuator.IdleReturnRoller), 300, 250, "1S3")
        DirectCast(idle, DirectionalValve).TriggerMark = "1S3"
        Dim sup2 = c.Add(New AirSupply(), 300, 400)
        gauge = c.Add(New PressureGauge(), 300, 100)
        c.Connect(sup, "1", v5, "1") : c.Connect(v5, "4", cyl, "1") : c.Connect(v5, "2", cyl, "2")
        c.Connect(sup2, "1", idle, "1") : c.Connect(idle, "2", gauge, "1")
        s = New Simulator(c) : s.Reset()
        Press(s, v5)
        Dim pulseSeen = False, pulseTime = 0.0
        For i = 1 To 400
            s.Step(0.005)
            If DirectCast(idle, DirectionalValve).State = 1 Then pulseSeen = True : pulseTime += 0.005
        Next
        Check("idle-return roller gives a pulse on arrival", pulseSeen AndAlso Math.Abs(pulseTime - DirectionalValve.IdleRollerPulse) < 0.03, pulseTime.ToString("0.000"))
        Check("idle-return roller released while the cylinder stays out", cyl.Position = 1 AndAlso DirectCast(idle, DirectionalValve).State = 0)
        Press(s, v5)
        pulseSeen = False
        For i = 1 To 400
            s.Step(0.005)
            If DirectCast(idle, DirectionalValve).State = 1 Then pulseSeen = True
        Next
        Check("no pulse when the cylinder moves back", Not pulseSeen AndAlso cyl.Position = 0)

        ' Counter: three pulses switch its contact, reset clears it.
        c = New Circuit()
        Dim plus = c.Add(New PowerTerminal(), 0, 0)
        Dim zero = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 0, 300)
        Dim s1 = c.Add(Library.Contact(ContactOperator.PushButton, False, "S1"), 0, 80)
        Dim s2 = c.Add(Library.Contact(ContactOperator.PushButton, False, "S2"), 100, 80)
        Dim counter = c.Add(New ElectricCounter() With {.Preset = 3}, 0, 180, "C1")
        Dim jp = c.Add(New Junction() With {.IsElectric = True}, 60, 40)
        Dim jz = c.Add(New Junction() With {.IsElectric = True}, 60, 280)
        c.Connect(plus, "1", jp, "1") : c.Connect(jp, "1", s1, "1") : c.Connect(jp, "1", s2, "1")
        c.Connect(s1, "2", counter, "A1") : c.Connect(s2, "2", counter, "R1")
        c.Connect(counter, "A2", jz, "1") : c.Connect(counter, "R2", jz, "1") : c.Connect(jz, "1", zero, "1")
        Dim kc = c.Add(Library.Contact(ContactOperator.Relay, False, "", "C1"), 300, 80)
        h = Lamp(c, 300, kc, "H1")
        s = New Simulator(c) : s.Reset()
        Click(s, s1) : Click(s, s1)
        Check("counter at 2 of 3: contact still open", counter.Count = 2 AndAlso Not h.Active)
        Click(s, s1)
        Check("counter reaches preset: contact closes", counter.Count = 3 AndAlso h.Active)
        cutOk = cutOk And AddCutaway(counter, True)
        Click(s, s2)
        Check("counter reset", counter.Count = 0 AndAlso Not h.Active)
        Check("checker accepts counter contacts", Not CircuitAnalysis.StaticChecks(c).Any(Function(i) i.Message.Contains("C1") AndAlso i.Severity = IssueSeverity.Error),
              String.Join(" | ", CircuitAnalysis.StaticChecks(c).Where(Function(i) i.Severity = IssueSeverity.Error).Select(Function(i) i.Message)))

        ' Emergency stop latches.
        c = New Circuit()
        Dim es = c.Add(Library.Contact(ContactOperator.EmergencyStop, True, "S0"), 0, 80)
        h = Lamp(c, 0, es, "H1")
        s = New Simulator(c) : s.Reset()
        Check("emergency stop closed at rest", h.Active)
        Click(s, es)
        Check("emergency stop latches open after release", Not h.Active)
        cutOk = cutOk And AddCutaway(es, True)
        Click(s, es)
        Check("emergency stop released by a second click", h.Active)

        ' Flow meter in the supply line of a single-acting cylinder.
        c = SimpleCylinder()
        Dim fm = c.Add(New FlowMeter(), 0, 220, "F1")
        Dim t = c.Tubes.First(Function(x) x.A.Owner Is Find(c, "0Z") OrElse x.B.Owner Is Find(c, "0Z"))
        c.RemoveTube(t)
        c.Connect(Find(c, "0Z"), "1", fm, "1") : c.Connect(fm, "2", Find(c, "1S1"), "1")
        s = New Simulator(c) : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 0.3)
        ' 32 mm bore at 0.1 m/s filled at 6 bar: 0.000804 m² × 0.1 × 60000 × 7.013 / 1.013 ≈ 33 NL/min.
        Check("flow meter reads the cylinder's air flow", Math.Abs(fm.Flow - 33.4) < 3, fm.Flow.ToString("0.0"))
        cutOk = cutOk And AddCutaway(fm, True)
        Check("flow meter channel recorded", s.Channels.Keys.Any(Function(n) n.StartsWith("F1 flow")))
        Run(s, 1)
        Check("no flow once the cylinder stops", Math.Abs(fm.Flow) < 0.01)
        Release(s, Find(c, "1S1")) : Run(s, 0.3)
        Check("no flow through the supply line while exhausting", Math.Abs(fm.Flow) < 0.01, fm.Flow.ToString("0.0"))

        ' Force sensor at the end of the stroke (realistic mode).
        c = SimpleCylinder()
        DirectCast(Find(c, "1A"), SingleActingCylinder).SpringPreloadN = 0
        Dim fsn = c.Add(New ForceSensor() With {.Cylinder = "1A"}, 300, 0, "BF1")
        s = New Simulator(c) With {.RealPhysics = True} : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 3)
        cutOk = cutOk And AddCutaway(fsn, True) And AddCutaway(sv, True)
        Check("force sensor shows p × A at the end of the stroke", Math.Abs(fsn.Force - 482) < 25, fsn.Force.ToString("0"))

        ' New parts render, have help and cutaways, and save and load.
        Dim newOnes As CircuitElement() = {New PressureSwitch(), New ShutOffValve(), New VacuumGenerator(), New SuctionCup(), New Gripper(),
                                           New Compressor(), New AirReceiver(), New FlowMeter(), New ForceSensor(), New ElectricCounter(),
                                           Library.Contact(ContactOperator.EmergencyStop, True, "S0"), Library.V32(ValveActuator.IdleReturnRoller)}
        Check("new parts have help texts", newOnes.All(Function(e) ComponentHelp.HelpFor(e).Length > e.DisplayName.Length + 20),
              String.Join(", ", newOnes.Where(Function(e) ComponentHelp.HelpFor(e).Length <= e.DisplayName.Length + 20).Select(Function(e) e.DisplayName)))
        Dim rt As New Circuit()
        Dim px = 0
        For Each e In newOnes
            rt.Add(e, px, 0) : px += 200
        Next
        Render(rt, False, "v32_new_parts")
        For Each e In newOnes
            cutOk = cutOk And AddCutaway(e, False)
        Next
        Check("new parts have cutaways that draw", cutOk AndAlso newOnes.All(Function(e) Cutaways.Supports(e)))
        SaveCutawaySheet()
        Dim back = Circuit.FromXml(rt.ToXml())
        Check("new parts round-trip through the file format", back.Elements.Count = newOnes.Length AndAlso
              back.Elements.Select(Function(e) e.TypeName).SequenceEqual(newOnes.Select(Function(e) e.TypeName)))
        Check("new parts have prices", newOnes.All(Function(e) PartsList.DefaultPrice(e.DisplayName) > 0 OrElse TypeOf e Is ElectricContact),
              String.Join(", ", newOnes.Where(Function(e) PartsList.DefaultPrice(e.DisplayName) = 0).Select(Function(e) e.DisplayName)))
    End Sub

    Sub FaultTests()
        ' A blocked tube stops the cylinder.
        Dim c = SimpleCylinder()
        Dim cylTube = c.Tubes.First(Function(t) t.A.Owner Is Find(c, "1A") OrElse t.B.Owner Is Find(c, "1A"))
        cylTube.Fault = FaultKind.Blocked
        Dim s As New Simulator(c) : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 1.5)
        Check("fault: blocked tube, cylinder does not move", Pos(c) = 0)
        ' A leaking tube: slower, lower pressure, extra air use.
        cylTube.Fault = FaultKind.Leak
        s = New Simulator(c) : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 1.0)
        Check("fault: leaking tube slows the cylinder", Pos(c) > 0.2 AndAlso Pos(c) < 0.5, Pos(c).ToString("0.00"))
        Check("fault: leaking tube loses pressure", Find(c, "1A").Ports(0).Pressure < 4.5, Find(c, "1A").Ports(0).Pressure.ToString("0.0"))
        Run(s, 3)
        Dim used = s.AirConsumed
        Run(s, 1)
        Check("fault: leaking tube keeps using air", s.AirConsumed - used > 0.3, (s.AirConsumed - used).ToString("0.00"))
        cylTube.Fault = FaultKind.None

        ' Valve stuck, cylinder jammed or sticking.
        Dim v = DirectCast(Find(c, "1S1"), DirectionalValve)
        v.Fault = FaultKind.StuckNormal
        s = New Simulator(c) : s.Reset() : Press(s, v) : Run(s, 1.5)
        Check("fault: stuck valve does not switch", v.State = 0 AndAlso Pos(c) = 0)
        v.Fault = FaultKind.StuckOperated
        s = New Simulator(c) : s.Reset() : Run(s, 1.5)
        Check("fault: valve stuck operated extends without the button", Pos(c) = 1)
        v.Fault = FaultKind.None
        Dim cyl = DirectCast(Find(c, "1A"), CylinderBase)
        cyl.Fault = FaultKind.Jammed
        s = New Simulator(c) : s.Reset() : Press(s, v) : Run(s, 1.5)
        Check("fault: jammed cylinder does not move", Pos(c) = 0)
        cyl.Fault = FaultKind.Sticking
        s = New Simulator(c) : s.Reset() : Press(s, v) : Run(s, 1.0)
        Check("fault: sticking cylinder is slow", Pos(c) > 0.2 AndAlso Pos(c) < 0.4, Pos(c).ToString("0.00"))
        cyl.Fault = FaultKind.Jammed
        s = New Simulator(c) With {.RealPhysics = True} : s.Reset() : Press(s, v) : Run(s, 1.5)
        Check("fault: jammed cylinder does not move (realistic)", Pos(c) = 0)
        cyl.Fault = FaultKind.Leak
        s = New Simulator(c) With {.RealPhysics = True} : s.Reset() : Press(s, v) : Run(s, 3)
        Check("fault: leaking piston seal (realistic) loses force", cyl.PistonForce() < 440, cyl.PistonForce().ToString("0"))
        cyl.Fault = FaultKind.None

        ' Solenoid valve with a burnt coil: the solenoid gets current but the valve stays; the manual override still works.
        c = New Circuit()
        Dim sup = c.Add(New AirSupply(), 0, 300)
        Dim sv = c.Add(Library.V32(ValveActuator.Solenoid), 0, 150, "1V1")
        Dim cy = c.Add(New SingleActingCylinder(), 0, 0, "1A")
        c.Connect(sup, "1", sv, "1") : c.Connect(sv, "2", cy, "1")
        Dim plus = c.Add(New PowerTerminal(), 300, 0)
        Dim sw = c.Add(Library.Contact(ContactOperator.Selector, False, "S1"), 300, 80)
        Dim sol = c.Add(New ElectricCoil() With {.Kind = CoilKind.Solenoid}, 300, 180, "1M1")
        Dim zero = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 300, 300)
        c.Connect(plus, "1", sw, "1") : c.Connect(sw, "2", sol, "A1") : c.Connect(sol, "A2", zero, "1")
        sv.Fault = FaultKind.BurntCoil
        s = New Simulator(c) : s.Reset() : Press(s, sw) : Run(s, 1.2)
        Check("fault: burnt valve solenoid does not switch the valve", sol.Active AndAlso sv.State = 0)
        Press(s, sv) : Run(s, 1.2)
        Check("fault: manual override still works", sv.State = 1 AndAlso Pos(c) = 1)
        Release(s, sv)
        sv.Fault = FaultKind.None
        sol.Fault = FaultKind.BurntCoil
        s = New Simulator(c) : s.Reset() : Press(s, sw)
        Check("fault: burnt coil element carries no current", Not sol.Active AndAlso sv.State = 0)
        sol.Fault = FaultKind.None
        DirectCast(sw, ElectricContact).Fault = FaultKind.ContactOpen
        s = New Simulator(c) : s.Reset() : Press(s, sw)
        Check("fault: broken switch contact never closes", Not sol.Active)
        DirectCast(sw, ElectricContact).Fault = FaultKind.ContactWelded
        s = New Simulator(c) : s.Reset()
        Check("fault: welded contact always closed", sol.Active)
        DirectCast(sw, ElectricContact).Fault = FaultKind.None
        Dim wire = c.Tubes.First(Function(t) t.IsElectric)
        wire.Fault = FaultKind.Blocked
        s = New Simulator(c) : s.Reset() : Press(s, sw)
        Check("fault: broken wire", Not sol.Active)
        wire.Fault = FaultKind.None

        ' Supply too low, clogged silencer, clogged throttle.
        c = SimpleCylinder()
        Find(c, "0Z").Fault = FaultKind.LowOutput
        s = New Simulator(c) : s.Reset() : Press(s, Find(c, "1S1"))
        Check("fault: low supply pressure", Find(c, "1A").Ports(0).Pressure < 3, Find(c, "1A").Ports(0).Pressure.ToString("0.0"))
        c = Examples.All(1).Build()
        Dim s0 As New Simulator(c) : s0.Reset() : Press(s0, Find(c, "1S1")) : Run(s0, 1.0)
        Dim normal = Pos(c)
        For Each fcv In c.Elements.OfType(Of FlowControlValve)()
            fcv.Fault = FaultKind.Blocked
        Next
        s0 = New Simulator(c) : s0.Reset() : Press(s0, Find(c, "1S1")) : Run(s0, 1.0)
        Check("fault: clogged throttle slows the cylinder a lot", Pos(c) < normal * 0.4, $"{Pos(c):0.000} vs {normal:0.000}")

        ' Every component offers sensible faults and describes them.
        Dim noDescription As New List(Of String)
        For Each lp In Library.Presets
            Dim el = lp.Factory.Invoke()
            For Each fk In el.PossibleFaults()
                If String.IsNullOrWhiteSpace(el.FaultDescription(fk)) Then noDescription.Add(lp.Name)
            Next
        Next
        Check("fault descriptions exist", noDescription.Count = 0, String.Join(", ", noDescription))
        Check("cylinders, valves, sensors and supplies can be faulty",
              New CircuitElement() {New DoubleActingCylinder(), New Valve52(), New ElectricContact(), New AirSupply(), New ElectricCoil()}.All(Function(e) e.PossibleFaults().Length > 0))

        ' Faults are saved: visible ones by name, hidden ones scrambled.
        c = SimpleCylinder()
        Find(c, "1S1").Fault = FaultKind.StuckNormal
        Find(c, "1A").Fault = FaultKind.Jammed : Find(c, "1A").FaultHidden = True
        c.Tubes(0).Fault = FaultKind.Leak : c.Tubes(0).FaultHidden = True
        Dim xml = c.ToXml()
        Check("visible fault saved by name", xml.Contains("fault=""StuckNormal"""))
        Check("hidden faults are not readable in the file", Not xml.Contains("Jammed") AndAlso Not xml.Contains("""Leak"""))
        Dim back = Circuit.FromXml(xml)
        Check("faults load back", back.Elements.First(Function(e) e.Label = "1S1").Fault = FaultKind.StuckNormal AndAlso
              back.Elements.First(Function(e) e.Label = "1A").Fault = FaultKind.Jammed AndAlso back.Elements.First(Function(e) e.Label = "1A").FaultHidden AndAlso
              back.Tubes.Any(Function(t) t.Fault = FaultKind.Leak AndAlso t.FaultHidden))
        Check("scrambled faults round-trip", [Enum].GetValues(GetType(FaultKind)).Cast(Of FaultKind)().Where(Function(k) k <> FaultKind.None).
              All(Function(k) Faults.Unscramble(Faults.Scramble(k)) = k) AndAlso Faults.Unscramble("garbage!") = FaultKind.None)
        Render(c, False, "faults_visible")
    End Sub
End Module
