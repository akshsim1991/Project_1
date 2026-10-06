''' Simulation tests for PneuSim. Run the built exe; renders and saved circuits go to the out folder.
Module TestMain
    Dim failures As Integer
    Sub Check(name As String, ok As Boolean, Optional detail As String = "")
        Console.WriteLine($"{If(ok, "PASS", "FAIL")}  {name}  {detail}")
        If Not ok Then failures += 1
    End Sub
    Function Find(c As Circuit, label As String) As CircuitElement
        Return c.Elements.First(Function(e) e.Label = label)
    End Function
    Sub Run(sim As Simulator, seconds As Double)
        For i = 1 To CInt(seconds / 0.005) : sim.Step(0.005) : Next
    End Sub
    Sub Press(sim As Simulator, e As CircuitElement)
        e.OnSimMouseDown(Drawing.PointF.Empty) : sim.RunLogic()
    End Sub
    Sub Release(sim As Simulator, e As CircuitElement)
        e.OnSimMouseUp() : sim.RunLogic()
    End Sub
    Sub Click(sim As Simulator, e As CircuitElement)
        Press(sim, e) : Run(sim, 0.1) : Release(sim, e)
    End Sub
    Function Pos(c As Circuit, Optional label As String = "1A") As Double
        Return DirectCast(Find(c, label), CylinderBase).Position
    End Function
    Sub Render(c As Circuit, sim As Boolean, name As String)
        Using bmp = MainForm.RenderCircuitImage(c, sim)
            bmp.Save(IO.Path.Combine("out", name & ".png"), Drawing.Imaging.ImageFormat.Png)
        End Using
    End Sub

    ''' <summary>Runs the simulation and returns cylinder end-position arrivals in order, e.g. "A+ B+ B- A-".</summary>
    Function RecordMoves(c As Circuit, sim As Simulator, seq As MotionSequence, seconds As Double, Optional renderAt As Double = -1, Optional renderName As String = "") As String
        Dim cyls = seq.Letters.ToDictionary(Function(l) l, Function(l) DirectCast(Find(c, $"{seq.Number(l)}A"), CylinderBase))
        Dim state = cyls.ToDictionary(Function(kv) kv.Key, Function(kv) kv.Value.Position >= 0.995)
        Dim events As New List(Of String)
        Dim steps = CInt(seconds / 0.005)
        For i = 1 To steps
            sim.Step(0.005)
            Dim now As New List(Of String)
            For Each kv In cyls
                Dim p = kv.Value.Position
                If Not state(kv.Key) AndAlso p >= 0.995 Then state(kv.Key) = True : now.Add(kv.Key & "+")
                If state(kv.Key) AndAlso p <= 0.005 Then state(kv.Key) = False : now.Add(kv.Key & "-")
            Next
            If now.Count = 1 Then events.Add(now(0))
            If now.Count > 1 Then events.Add("(" & String.Join(" ", now.OrderBy(Function(x) x)) & ")")
            If renderAt > 0 AndAlso Math.Abs(sim.Time - renderAt) < 0.0026 Then Render(c, True, renderName)
        Next
        Return String.Join(" ", events)
    End Function

    Function Normalized(seq As MotionSequence) As String
        Return String.Join(" ", seq.Steps.Select(Function(s) If(s.Count = 1, s(0).ToString(), "(" & String.Join(" ", s.Select(Function(m) m.ToString()).OrderBy(Function(x) x)) & ")")))
    End Function

    Sub Main(args As String())
        IO.Directory.CreateDirectory("out")
        Dim ex = Examples.All

        ' ---------------- regression: original examples
        Dim c = ex(0).Build() : Dim s As New Simulator(c) : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 0.5)
        Check("ex1 extending at half time", Math.Abs(Pos(c) - 0.5) < 0.05, Pos(c).ToString("0.00"))
        Run(s, 0.7) : Check("ex1 extended", Pos(c) = 1)
        Release(s, Find(c, "1S1")) : Run(s, 1.2) : Check("ex1 spring return", Pos(c) = 0)
        Check("ex1 no warnings", s.Warnings.Count = 0, String.Join(";", s.Warnings))

        c = ex(1).Build() : s = New Simulator(c) : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 1.0)
        Check("ex2 meter-out slows extension", Math.Abs(Pos(c) - 0.1875) < 0.02, Pos(c).ToString("0.000"))
        Run(s, 5) : Release(s, Find(c, "1S1")) : Run(s, 1.0)
        Check("ex2 retract speed", Math.Abs(Pos(c) - 0.5) < 0.03, Pos(c).ToString("0.000"))

        For Each btn In {"1S1", "1S2"}
            c = ex(2).Build() : s = New Simulator(c) : s.Reset()
            Press(s, Find(c, btn)) : Run(s, 1.2)
            Check($"ex3 OR via {btn}", Pos(c) = 1)
            Release(s, Find(c, btn)) : Run(s, 1.2)
            Check($"ex3 returns after {btn}", Pos(c) = 0)
        Next

        c = ex(3).Build() : s = New Simulator(c) : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 1.2)
        Check("ex4 one hand does nothing", Pos(c) = 0)
        Press(s, Find(c, "1S2")) : Run(s, 1.2)
        Check("ex4 two hands extend", Pos(c) = 1)
        Release(s, Find(c, "1S1")) : Run(s, 1.2)
        Check("ex4 releasing one retracts", Pos(c) = 0)

        c = ex(4).Build() : s = New Simulator(c) : s.Reset()
        Click(s, Find(c, "1S3"))
        Dim reversals = 0, lastDir = 0, prev = Pos(c)
        For i = 1 To 2000
            s.Step(0.005)
            Dim d = Math.Sign(Pos(c) - prev)
            If d <> 0 AndAlso d <> lastDir Then reversals += 1 : lastDir = d
            prev = Pos(c)
        Next
        Check("ex5 oscillates", reversals >= 8, $"{reversals} direction changes in 10 s")

        c = ex(5).Build() : s = New Simulator(c) : s.Reset()
        Click(s, Find(c, "1S1"))
        Run(s, 1.1) : Check("ex6 extended", Pos(c) = 1)
        Run(s, 1.5) : Check("ex6 still waiting (timer)", Pos(c) = 1)
        Run(s, 2.5) : Check("ex6 retracted after delay", Pos(c) = 0)

        For i = 0 To ex.Length - 1
            Dim orig = ex(i).Build()
            Dim back = Circuit.FromXml(orig.ToXml())
            Check($"round trip example {i + 1} ({ex(i).Name.Substring(0, 12)}...)", back.Elements.Count = orig.Elements.Count AndAlso back.Tubes.Count = orig.Tubes.Count)
            Render(orig, False, $"ex{i + 1}_edit")
            orig.Save(IO.Path.Combine("out", $"ex{i + 1}.pneu"))
        Next

        ' Electro-pneumatic examples.
        c = ex(6).Build() : s = New Simulator(c) : s.Reset()
        Press(s, Find(c, "S1")) : Run(s, 1.2) : Check("ex7 S1 energizes 1M1, cylinder extends", Pos(c) = 1)
        Release(s, Find(c, "S1")) : Run(s, 1.2) : Check("ex7 spring return when released", Pos(c) = 0)
        c = ex(7).Build() : s = New Simulator(c) : s.Reset()
        Click(s, Find(c, "S1")) : Run(s, 1.2)
        Check("ex8 latched: extended and lamp on", Pos(c) = 1 AndAlso DirectCast(Find(c, "H1"), ElectricCoil).Active)
        Render(c, True, "ex8_sim")
        Click(s, Find(c, "S0")) : Run(s, 1.2)
        Check("ex8 stop: retracted and lamp off", Pos(c) = 0 AndAlso Not DirectCast(Find(c, "H1"), ElectricCoil).Active)

        ' ---------------- new pneumatic components
        ' Check valve: supply -> check -> single-acting cylinder; removing supply keeps the cylinder extended (trapped air).
        c = New Circuit()
        Dim cyl = c.Add(New SingleActingCylinder(), 0, 0, "1A")
        Dim pb = c.Add(Library.V32(ValveActuator.PushButton), 0, 200, "1S1")
        Dim chk = c.Add(New CheckValve(), 0, 120)
        Dim sup = c.Add(New AirSupply(), 0, 320)
        c.Connect(sup, "1", pb, "1") : c.Connect(pb, "2", chk, "1") : c.Connect(chk, "2", cyl, "1")
        s = New Simulator(c) : s.Reset()
        Press(s, pb) : Run(s, 1.2) : Check("check valve passes 1->2", Pos(c) = 1)
        Release(s, pb) : Run(s, 1.2) : Check("check valve blocks 2->1 (air trapped)", Pos(c) = 1)

        ' Quick exhaust: cylinder vents through the quick exhaust, not back through the throttled line.
        c = New Circuit()
        cyl = c.Add(New SingleActingCylinder(), 0, 0, "1A")
        pb = c.Add(Library.V32(ValveActuator.PushButton), 0, 300, "1S1")
        Dim fc = c.Add(New FlowControlValve() With {.HasCheckValve = False, .OpeningPercent = 10}, 0, 200)
        Dim qe = c.Add(New QuickExhaustValve(), 0, 100)
        sup = c.Add(New AirSupply(), 0, 420)
        c.Connect(sup, "1", pb, "1") : c.Connect(pb, "2", fc, "1") : c.Connect(fc, "2", qe, "1") : c.Connect(qe, "2", cyl, "1")
        s = New Simulator(c) : s.Reset()
        Press(s, pb) : Run(s, 11) : Check("quick exhaust: slow throttled extension completes", Pos(c) = 1)
        Release(s, pb) : Run(s, 1.1) : Check("quick exhaust: fast retraction despite 10% throttle", Pos(c) = 0)

        ' 5/3 closed centre: releasing the button mid-stroke stops the cylinder.
        c = New Circuit()
        c.Add(New DoubleActingCylinder(), 0, 0, "1A")
        Dim cyl2 = DirectCast(c.Elements(0), DoubleActingCylinder)
        Dim v53 = c.Add(New Valve53() With {.Actuator = ValveActuator.Solenoid, .ReturnType = ValveReturn.Solenoid}, 0, 150, "1V1")
        sup = c.Add(New AirSupply(), 100, 250)
        c.Connect(v53, "4", cyl2, "1") : c.Connect(v53, "2", cyl2, "2") : c.Connect(sup, "1", v53, "1")
        s = New Simulator(c) : s.Reset()
        Press(s, v53) : Run(s, 0.4) : Release(s, v53)
        Dim mid = Pos(c)
        Run(s, 1) : Check("5/3 closed centre holds position", Math.Abs(Pos(c) - mid) < 0.001 AndAlso mid > 0.3 AndAlso mid < 0.5, mid.ToString("0.00"))
        Render(c, False, "valve53")

        ' Pressure regulator limits the downstream pressure.
        c = New Circuit()
        sup = c.Add(New AirSupply(), 0, 100)
        Dim reg = c.Add(New PressureRegulator() With {.Setting = 3.5}, 50, 0)
        Dim gauge = c.Add(New PressureGauge(), 200, -40)
        c.Connect(sup, "1", reg, "1") : c.Connect(reg, "2", gauge, "1")
        s = New Simulator(c) : s.Reset()
        Check("regulator output 3.5 bar", Math.Abs(gauge.Ports(0).Pressure - 3.5) < 0.001, gauge.Ports(0).Pressure.ToString())

        ' Air motor turns when supplied.
        c = New Circuit()
        Dim motor = c.Add(New AirMotor(), 0, 0, "1M")
        sup = c.Add(New AirSupply(), 0, 100)
        c.Connect(sup, "1", motor, "1")
        s = New Simulator(c) : s.Reset() : Run(s, 0.5)
        Check("air motor powered", motor.Ports(0).IsPressurized)

        ' ---------------- electrical
        ' Self-holding (latching) circuit: S1 start, S0 stop, K1 holds, K1 contact drives lamp H1.
        c = New Circuit()
        Dim p1 = c.Add(New PowerTerminal(), 0, 0)
        Dim s0 = c.Add(Library.Contact(ContactOperator.PushButton, True, "S0"), 0, 50)
        Dim s1 = c.Add(Library.Contact(ContactOperator.PushButton, False, "S1"), 0, 130)
        Dim k1hold = c.Add(Library.Contact(ContactOperator.Relay, False, "", "K1"), 80, 130)
        Dim k1 = c.Add(New ElectricCoil() With {.Label = "K1"}, 0, 210)
        Dim z1 = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 0, 290)
        c.Connect(p1, "1", s0, "1") : c.Connect(s0, "2", s1, "1") : c.Connect(s0, "2", k1hold, "1")
        c.Connect(s1, "2", k1, "A1") : c.Connect(k1hold, "2", k1, "A1") : c.Connect(k1, "A2", z1, "1")
        Dim p2 = c.Add(New PowerTerminal(), 200, 0)
        Dim k1lamp = c.Add(Library.Contact(ContactOperator.Relay, False, "", "K1"), 200, 50)
        Dim h1 = c.Add(New ElectricCoil() With {.Kind = CoilKind.Lamp, .Label = "H1"}, 200, 130)
        Dim z2 = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 200, 210)
        c.Connect(p2, "1", k1lamp, "1") : c.Connect(k1lamp, "2", h1, "A1") : c.Connect(h1, "A2", z2, "1")
        s = New Simulator(c) : s.Reset()
        Check("latch: lamp off initially", Not h1.Active)
        Click(s, s1) : Run(s, 0.1) : Check("latch: S1 latches K1, lamp on", k1.Active AndAlso h1.Active)
        Click(s, s0) : Run(s, 0.1) : Check("latch: S0 releases", Not k1.Active AndAlso Not h1.Active)
        Check("latch: no warnings", s.Warnings.Count = 0, String.Join(";", s.Warnings))
        Click(s, s1)
        Render(c, True, "latch_sim")

        ' On-delay timer relay.
        c = New Circuit()
        p1 = c.Add(New PowerTerminal(), 0, 0)
        Dim sw = c.Add(Library.Contact(ContactOperator.Selector, False, "S1"), 0, 50)
        Dim kt = c.Add(New ElectricCoil() With {.Kind = CoilKind.OnDelayTimer, .Label = "K2", .DelaySeconds = 1.5}, 0, 130)
        z1 = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 0, 210)
        c.Connect(p1, "1", sw, "1") : c.Connect(sw, "2", kt, "A1") : c.Connect(kt, "A2", z1, "1")
        s = New Simulator(c) : s.Reset()
        Press(s, sw) : Release(s, sw)
        Run(s, 1.4) : Check("on-delay: not yet after 1.4 s", Not kt.Active)
        Run(s, 0.2) : Check("on-delay: active after 1.6 s", kt.Active)
        Press(s, sw) : Release(s, sw) : Run(s, 0.05) : Check("on-delay: drops at once when switched off", Not kt.Active)

        ' Short circuit is reported.
        c = New Circuit()
        p1 = c.Add(New PowerTerminal(), 0, 0)
        z1 = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 0, 100)
        c.Connect(p1, "1", z1, "1")
        s = New Simulator(c) : s.Reset()
        Check("short circuit warning", s.Warnings.Any(Function(w) w.Contains("Short circuit")))

        ' ---------------- editor features
        c = ex(4).Build()
        Dim before = c.Elements.Count
        Dim tubesBefore = c.Tubes.Count
        Dim sel = c.Elements.Take(3).ToList()
        Dim added = c.Merge(c.ExtractXml(sel), 40, 40)
        Check("paste adds copies", c.Elements.Count = before + 3 AndAlso added.All(Function(e) e.Id > 0) AndAlso
              c.Elements.Select(Function(e) e.Id).Distinct().Count() = c.Elements.Count)
        Dim internalTubes = ex(4).Build().Tubes.Count
        Check("paste keeps internal tubes", c.Tubes.Count >= tubesBefore)
        Dim snap = c.ToXml()
        c.Remove(c.Elements(0))
        Dim restored = Circuit.FromXml(snap)
        Check("undo snapshot restores", restored.Elements.Count = before + 3)
        Dim t = restored.Tubes.First(Function(tb) tb.MidAxis() IsNot Nothing)
        t.Mid = 123
        Check("tube middle segment persists", Circuit.FromXml(restored.ToXml()).Tubes.Any(Function(tb) tb.Mid.HasValue AndAlso tb.Mid.Value = 123))

        ' ---------------- generator
        Dim parseErrors = {"A+ A+", "A+ B+", "A-", "(A+ A-)", "Q", "A+ B+ ("}
        For Each bad In parseErrors
            Dim threw = False
            Try
                MotionSequence.Parse(bad)
            Catch fe As FormatException
                threw = True
            End Try
            Check($"rejects invalid sequence '{bad}'", threw)
        Next

        Dim sequences = {"A+ B+ B- A-", "A+ B+ A- B-", "A+ B+ C+ C- B- A-", "A+ B+ B- C+ C- A-", "A+ A- B+ B-",
                         "A+ B+ B- A- C+ C-", "A+ B+ C+ A- B- C-", "A+ B+ C+ D+ D- C- B- A-"}
        For Each method In {GeneratorMethod.ElectroRelayChain, GeneratorMethod.PneumaticCascade}
            For Each seqText In sequences
                Dim seq = MotionSequence.Parse(seqText)
                Dim gen = SequenceGenerator.Generate(seq, method, continuous:=False)
                c = gen.Circuit : s = New Simulator(c) : s.Reset()
                Dim startLabel = If(method = GeneratorMethod.ElectroRelayChain, "S1", "1S0")
                Dim name = $"{If(method = GeneratorMethod.ElectroRelayChain, "electro", "cascade")}_{seqText.Replace(" ", "").Replace("+", "p").Replace("-", "m")}"
                Dim idle = RecordMoves(c, s, seq, 1.5)
                Click(s, Find(c, startLabel))
                Dim got = RecordMoves(c, s, seq, seq.Steps.Count * 1.2 + 2, 1.5 + 1.6, name)
                Dim expect = Normalized(seq)
                Check($"{method} {seqText}", idle = "" AndAlso got = expect AndAlso s.Warnings.Count = 0,
                      $"got '{got}'{If(idle <> "", $" idle '{idle}'", "")}{If(s.Warnings.Count > 0, " warn: " & s.Warnings(0), "")}")
                If seqText = "A+ B+ B- A-" OrElse seqText = "A+ B+ C+ C- B- A-" OrElse seqText = "A+ B+ A- B-" Then
                    Render(c, False, name & "_edit")
                    c.Save(IO.Path.Combine("out", name & ".pneu"))
                End If
                ' A second press runs a second cycle.
                Click(s, Find(c, startLabel))
                Dim again = RecordMoves(c, s, seq, seq.Steps.Count * 1.2 + 2)
                Check($"{method} {seqText} second cycle", again = expect, $"got '{again}'")
            Next
        Next

        ' Parallel movements (electro only) and continuous cycling.
        Dim par = MotionSequence.Parse("A+ (B+ C+) B- C- A-")
        Dim gpar = SequenceGenerator.Generate(par, GeneratorMethod.ElectroRelayChain, continuous:=True)
        c = gpar.Circuit : s = New Simulator(c) : s.Reset()
        Click(s, Find(c, "S1"))
        Check("electro parallel step", RecordMoves(c, s, par, 7) = Normalized(par))
        Press(s, Find(c, "S2")) : Release(s, Find(c, "S2"))
        Dim cont = RecordMoves(c, s, par, 14)
        Check("continuous switch repeats cycles", cont.StartsWith(Normalized(par) & " " & Normalized(par)), cont)
        Render(c, False, "electro_parallel_edit")
        Dim cascadeParallelRejected = False
        Try
            SequenceGenerator.Generate(par, GeneratorMethod.PneumaticCascade, False)
        Catch fe As FormatException
            cascadeParallelRejected = True
        End Try
        Check("cascade rejects parallel steps with a message", cascadeParallelRejected)

        ' ---------------- realistic physics
        c = ex(1).Build() : s = New Simulator(c) With {.RealPhysics = True} : s.Reset()
        Press(s, Find(c, "1S1"))
        Dim tExt = -1.0
        For i = 1 To 2000
            s.Step(0.005)
            If tExt < 0 AndAlso Pos(c) >= 0.999 Then tExt = s.Time
        Next
        Dim cylR = DirectCast(Find(c, "1A"), CylinderBase)
        Check("real: meter-out cylinder extends", tExt > 0.05 AndAlso tExt < 8, $"t={tExt:0.00}s")
        Check("real: cap pressure builds to supply", cylR.CapPressure > 5.5, $"{cylR.CapPressure:0.00} bar")
        Check("real: air consumption recorded", s.AirConsumed > 0.05, $"{s.AirConsumed:0.000} NL")
        c = ex(1).Build()
        DirectCast(c.Elements.First(Function(e) e.Label = "1V3"), FlowControlValve).OpeningPercent = 100
        s = New Simulator(c) With {.RealPhysics = True} : s.Reset()
        Press(s, Find(c, "1S1"))
        Dim tFast = -1.0
        For i = 1 To 2000
            s.Step(0.005)
            If tFast < 0 AndAlso Pos(c) >= 0.999 Then tFast = s.Time
        Next
        Check("real: opening the throttle makes it faster", tFast > 0 AndAlso tFast < tExt, $"t={tFast:0.00}s vs {tExt:0.00}s")
        c = ex(1).Build()
        DirectCast(Find(c, "1A"), CylinderBase).LoadForceN = 600
        s = New Simulator(c) With {.RealPhysics = True} : s.Reset()
        Press(s, Find(c, "1S1")) : Run(s, 3)
        Check("real: overload stalls the cylinder (600 N > 6 bar x 32 mm)", Pos(c) < 0.01, Pos(c).ToString("0.000"))

        ' ---------------- hydraulics
        c = ex(8).Build() : s = New Simulator(c) : s.Reset()
        Dim hcyl = DirectCast(Find(c, "1A"), HydraulicCylinder)
        Dim hg = DirectCast(Find(c, "0G1"), PressureGauge)
        Check("hydraulic: tandem centre unloads the pump", hg.Ports(0).Pressure <= 3.01, $"{hg.Ports(0).Pressure:0.0} bar")
        Press(s, Find(c, "S1")) : Run(s, 0.5)
        Dim pMoving = hg.Ports(0).Pressure
        Check("hydraulic: pressure set by the load while moving (~18 bar)", pMoving > 15 AndAlso pMoving < 21, $"{pMoving:0.0} bar")
        Dim tH = -1.0
        For i = 1 To 1000
            s.Step(0.005)
            If tH < 0 AndAlso hcyl.Position >= 0.999 Then tH = s.Time
        Next
        Check("hydraulic: speed = flow / area (200 mm in ~1.9 s)", tH > 1.7 AndAlso tH < 2.1, $"t={tH:0.00}s")
        Check("hydraulic: relief pressure at end of stroke", Math.Abs(hg.Ports(0).Pressure - 60) < 0.1 AndAlso DirectCast(Find(c, "0V1"), ReliefValve).IsOpen, $"{hg.Ports(0).Pressure:0.0} bar")
        Release(s, Find(c, "S1")) : Run(s, 0.5)
        Check("hydraulic: cylinder holds in centre position", hcyl.Position >= 0.999)
        Press(s, Find(c, "S2")) : Run(s, 2.5)
        Check("hydraulic: retracts faster (smaller annulus area)", hcyl.Position <= 0.001)
        Check("hydraulic: no warnings", s.Warnings.Count = 0, String.Join(";", s.Warnings))
        c.Remove(Find(c, "0V1"))
        s = New Simulator(c) : s.Reset()
        Press(s, Find(c, "S1")) : Run(s, 3)
        Check("hydraulic: missing relief valve is warned about", s.Warnings.Any(Function(w) w.Contains("relief")), String.Join(";", s.Warnings))
        Render(ex(8).Build(), False, "hydraulic_edit")

        ' Pressure sequence valve in realistic mode: drill starts only after clamping pressure reaches 4 bar.
        c = ex(9).Build() : s = New Simulator(c) With {.RealPhysics = True} : s.Reset()
        Click(s, Find(c, "1V1"))
        Dim clampAtStart = -1.0, drillAtStart = -1.0
        For i = 1 To 1200
            s.Step(0.005)
            If clampAtStart < 0 AndAlso Pos(c, "1A") >= 0.999 Then clampAtStart = s.Time
            If drillAtStart < 0 AndAlso Pos(c, "2A") > 0.01 Then drillAtStart = s.Time
        Next
        Check("sequence valve: drill starts after clamping is complete", clampAtStart > 0 AndAlso drillAtStart > clampAtStart, $"clamp {clampAtStart:0.00}s, drill {drillAtStart:0.00}s")
        Check("sequence valve: drill extends", Pos(c, "2A") >= 0.999)

        ' Projects: multi-page save/load with page connectors and cross references.
        Dim pr As New Project(ex(7).Build(), "Latch")
        Dim pg2 = pr.AddPage("Lamp")
        pr.UpdateCrossReferences()
        Dim coilK1 = pr.AllElements().OfType(Of ElectricCoil)().First(Function(k) k.Label = "K1")
        Check("cross-reference lists relay contacts", coilK1.CrossReference.Contains("NO"), coilK1.CrossReference)
        Dim pr2 = Project.FromXml(pr.ToXml())
        Check("project round trip keeps pages", pr2.Pages.Count = 2 AndAlso pr2.Pages(1).Name = "Lamp" AndAlso pr2.Info.Title = "Latch")
        Dim legacy = Project.FromXml(ex(0).Build().ToXml())
        Check("old single-page files still open", legacy.Pages.Count = 1 AndAlso legacy.Pages(0).Circuit.Elements.Count = ex(0).Build().Elements.Count)
        ' A wire split across two pages with page connectors still works.
        Dim split As New Project(New Circuit())
        Dim pageA = split.Pages(0).Circuit, pageB = split.AddPage().Circuit
        Dim plusT = pageA.Add(New PowerTerminal(), 0, 0)
        Dim btnS1 = pageA.Add(Library.Contact(ContactOperator.PushButton, False, "S1"), 0, 60)
        Dim conA = pageA.Add(New PageConnector() With {.Medium = PortKind.Electric, .Label = "W1"}, 40, 140)
        pageA.Connect(plusT, "1", btnS1, "1") : pageA.Connect(btnS1, "2", conA, "1")
        Dim conB = pageB.Add(New PageConnector() With {.Medium = PortKind.Electric, .Label = "W1"}, 0, 0)
        Dim lampB = pageB.Add(New ElectricCoil() With {.Kind = CoilKind.Lamp, .Label = "H1"}, 60, 0)
        Dim zeroB = pageB.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 60, 80)
        pageB.Connect(conB, "1", lampB, "A1") : pageB.Connect(lampB, "A2", zeroB, "1")
        s = New Simulator(split.SimulationCircuit()) : s.Reset()
        Press(s, btnS1) : Run(s, 0.05)
        Check("page connectors join wires across pages", lampB.Active)

        ' Vector export.
        Dim exC = ex(7).Build()
        VectorExport.SaveSvg(exC, IO.Path.Combine("out", "latch.svg"))
        VectorExport.SaveDxf(exC, IO.Path.Combine("out", "latch.dxf"))
        Dim doc As New PdfDocument()
        Dim page = doc.AddPage(842, 595)
        VectorExport.DrawShapes(page, VectorExport.Record(exC), New Drawing.RectangleF(30, 30, 782, 450))
        VectorExport.DrawTitleBlock(page, New ProjectInfo With {.Title = "Self-holding circuit", .Author = "Test"}, "Page 1", 1, 1)
        doc.Save(IO.Path.Combine("out", "latch.pdf"))
        Dim svgText = IO.File.ReadAllText(IO.Path.Combine("out", "latch.svg"))
        Check("SVG export has shapes and text", svgText.Contains("<polyline") AndAlso svgText.Contains(">K1<"))
        Check("DXF export has lines", IO.File.ReadAllText(IO.Path.Combine("out", "latch.dxf")).Contains("LINE"))
        Dim pdfBytes = IO.File.ReadAllBytes(IO.Path.Combine("out", "latch.pdf"))
        Check("PDF export is a PDF", pdfBytes.Length > 1000 AndAlso System.Text.Encoding.ASCII.GetString(pdfBytes, 0, 5) = "%PDF-")

        ' ---------------- checker and explainer
        ' Classic A+ B+ B- A- wired directly with roller valves: has signal overlap.
        Dim ov As New Circuit()
        Dim cA = ov.Add(New DoubleActingCylinder() With {.RetractedMark = "1S1", .ExtendedMark = "1S2"}, 100, 40, "1A")
        Dim cB = ov.Add(New DoubleActingCylinder() With {.RetractedMark = "2S1", .ExtendedMark = "2S2"}, 500, 40, "2A")
        Dim vA = ov.Add(Library.V52(ValveActuator.Pilot, ValveReturn.Pilot), 10, 170, "1V1")
        Dim vB = ov.Add(Library.V52(ValveActuator.Pilot, ValveReturn.Pilot), 410, 170, "2V1")
        ov.Connect(vA, "4", cA, "1") : ov.Connect(vA, "2", cA, "2")
        ov.Connect(vB, "4", cB, "1") : ov.Connect(vB, "2", cB, "2")
        Dim supA = ov.Add(New AirSupply(), 100, 270) : ov.Connect(supA, "1", vA, "1")
        Dim supB = ov.Add(New AirSupply(), 500, 270) : ov.Connect(supB, "1", vB, "1")
        Dim rollers = {("1S1", "14", vA), ("1S2", "14", vB), ("2S2", "12", vB), ("2S1", "12", vA)}
        Dim startV = ov.Add(Library.V32(ValveActuator.PushButton), 0, 520, "1S0")
        Dim sup0 = ov.Add(New AirSupply(), 50, 620) : ov.Connect(sup0, "1", startV, "1")
        For i = 0 To 3
            Dim rv = ov.Add(Library.V32(ValveActuator.RollerLever), 150 + i * 150, 380, rollers(i).Item1)
            rv.TriggerMark = rollers(i).Item1
            ov.Connect(rv, "2", rollers(i).Item3, rollers(i).Item2)
            If i = 0 Then
                ov.Connect(startV, "2", rv, "1")
            Else
                Dim sp = ov.Add(New AirSupply(), 200 + i * 150, 480) : ov.Connect(sp, "1", rv, "1")
            End If
        Next
        Dim issuesOv = CircuitAnalysis.StaticChecks(ov)
        Dim overlapIssue = issuesOv.FirstOrDefault(Function(x) x.Message.Contains("Signal overlap"))
        Check("checker finds signal overlap", overlapIssue IsNot Nothing, If(overlapIssue Is Nothing, String.Join(" | ", issuesOv), overlapIssue.Message))
        Check("checker infers the intended sequence", overlapIssue IsNot Nothing AndAlso overlapIssue.Message.Contains("A+ B+ B- A-"))
        Check("checker offers a one-click fix", overlapIssue IsNot Nothing AndAlso overlapIssue.Fix IsNot Nothing)
        If overlapIssue IsNot Nothing AndAlso overlapIssue.Fix IsNot Nothing Then
            Dim fixedC = overlapIssue.Fix.Invoke()
            s = New Simulator(fixedC) : s.Reset()
            Click(s, Find(fixedC, "1S0"))
            Check("the cascade fix runs the sequence", RecordMoves(fixedC, s, MotionSequence.Parse("A+ B+ B- A-"), 8) = "A+ B+ B- A-")
            Dim eFix = CircuitAnalysis.ElectroFix(overlapIssue).Invoke()
            s = New Simulator(eFix) : s.Reset()
            Click(s, Find(eFix, "S1"))
            Check("the electro-pneumatic fix runs the sequence", RecordMoves(eFix, s, MotionSequence.Parse("A+ B+ B- A-"), 8) = "A+ B+ B- A-")
        End If
        Dim dyn = CircuitAnalysis.DynamicCheck(ov, False)
        Check("test run confirms the overlap", dyn.Any(Function(x) x.Message.Contains("both signals")), String.Join(" | ", dyn))
        ' Good circuits have no errors.
        For i = 0 To ex.Length - 1
            Dim errs = CircuitAnalysis.StaticChecks(ex(i).Build()).Where(Function(x) x.Severity = IssueSeverity.Error).ToList()
            Check($"no errors in example {i + 1}", errs.Count = 0, String.Join(" | ", errs))
        Next
        Dim genCascade = SequenceGenerator.Generate(MotionSequence.Parse("A+ B+ C+ C- B- A-"), GeneratorMethod.PneumaticCascade, False).Circuit
        Check("no errors or overlap in a generated cascade", Not CircuitAnalysis.StaticChecks(genCascade).Any(Function(x) x.Severity = IssueSeverity.Error))
        ' Typical mistakes.
        Dim badSol = ex(6).Build()
        DirectCast(badSol.Elements.OfType(Of DirectionalValve)().First(), DirectionalValve).SolenoidLabel = "1M9"
        Check("checker: solenoid label without coil", CircuitAnalysis.StaticChecks(badSol).Any(Function(x) x.Message.Contains("1M9")))
        Dim badSupply = ex(0).Build()
        badSupply.Remove(badSupply.Elements.OfType(Of AirSupply)().First())
        Check("checker: missing air supply", CircuitAnalysis.StaticChecks(badSupply).Any(Function(x) x.Message.Contains("no compressed air supply")))
        ' A contact labelled like a cylinder mark but left as a relay contact (common beginner mistake).
        Dim relayMix = ex(6).Build()
        Dim cylMix = relayMix.Elements.OfType(Of CylinderBase)().First()
        cylMix.RetractedMark = "1S1"
        Dim wrongContact = relayMix.Elements.OfType(Of ElectricContact)().First()
        wrongContact.Operator = ContactOperator.Relay : wrongContact.Reference = "" : wrongContact.Label = "1S1"
        Dim mixIssues = CircuitAnalysis.StaticChecks(relayMix)
        Check("checker: relay contact named after a cylinder mark", mixIssues.Any(Function(x) x.Message.Contains("set 'Operated by' to Limit switch") AndAlso x.Message.Contains("'Reference' to 1S1")),
              String.Join(" | ", mixIssues))
        wrongContact.Label = "X9"
        Check("checker: relay contact without reference", CircuitAnalysis.StaticChecks(relayMix).Any(Function(x) x.Message.Contains("has no Reference")))
        ' Explanation.
        Dim expl = CircuitAnalysis.Explain(ex(4).Build(), Nothing, False)
        Console.WriteLine(expl)
        Check("explain describes control and sequence", expl.Contains("controlled by valve 1V1") AndAlso expl.Contains("MOTION SEQUENCE"))
        Check("explain narrates the run", expl.Contains("starts to extend") AndAlso expl.Contains("fully extended"))
        Dim expl2 = CircuitAnalysis.Explain(ex(7).Build(), Nothing, False)
        Check("explain follows relays and solenoids", expl2.Contains("relay K1 picks up") AndAlso expl2.Contains("solenoid 1M1"), expl2)


        ' ---------------- Phase 3: lessons, quiz, parts list, PDF, GIF
        Dim relabel = Function(circ As Circuit) As Circuit
                          For Each el In circ.Elements
                              If el.Label = "1S1" Then el.Label = "S1"
                              If el.Label = "1S2" AndAlso el.IsManuallyOperated Then el.Label = "S2"
                          Next
                          Return circ
                      End Function
        Dim solutions = New(Lesson As Integer, Build As Func(Of Circuit))() {
            (0, Function() relabel(ex(0).Build())),
            (3, Function() relabel(ex(2).Build())),
            (4, Function() relabel(ex(3).Build())),
            (7, Function() ex(7).Build()),
            (8, Function() ex(8).Build()),
            (9, Function() relabel(SequenceGenerator.Generate(MotionSequence.Parse("A+ B+ B- A-"), GeneratorMethod.PneumaticCascade, False).Circuit))}
        For Each sol In solutions
            Dim lesson = Lessons.All(sol.Lesson)
            Dim res = lesson.Check(sol.Build())
            Check($"lesson '{lesson.Title}' accepts a correct circuit", res.Passed, res.Feedback)
        Next
        For Each lesson In Lessons.All.Where(Function(l) l.Start IsNot Nothing)
            Dim res = lesson.Check(lesson.Start.Invoke())
            Check($"lesson '{lesson.Title}' rejects the unconnected parts", Not res.Passed, res.Feedback)
        Next
        Dim quiz = QuestionBank.Pick(20, 1)
        Check("quiz picks 20 valid questions", quiz.Count = 20 AndAlso quiz.All(Function(q) q.Correct >= 0 AndAlso q.Correct < q.Options.Length AndAlso q.Options.Distinct().Count() = q.Options.Length))
        Check("question bank has written questions", QuestionBank.WrittenCount >= 25, QuestionBank.WrittenCount.ToString())
        Check("hover help for every component", Library.Presets.All(Function(lp) ComponentHelp.HelpFor(lp.Factory.Invoke()) <> ""))

        Dim proj As New Project(ex(4).Build())
        Dim parts = PartsList.Build(proj)
        Check("parts list counts cylinders", parts.Any(Function(l) l.Description.Contains("cylinder") AndAlso l.Quantity = 1), String.Join(" | ", parts.Select(Function(l) $"{l.Quantity}x {l.Description}")))
        Check("parts list has prices", parts.Sum(Function(l) l.Total) > 0)
        Check("parts list CSV", PartsList.ToCsv(parts).Split(ChrW(10)).Length > parts.Count)
        Dim pdfPath = IO.Path.Combine("out", "report.pdf")
        Reports.SavePdfReport(proj, pdfPath, True, CircuitAnalysis.Explain(proj.Pages(0).Circuit, Nothing, False))
        Dim pdfText = IO.File.ReadAllText(pdfPath)
        Check("PDF report written", pdfText.StartsWith("%PDF") AndAlso pdfText.TrimEnd().EndsWith("%%EOF"), New IO.FileInfo(pdfPath).Length.ToString())

        Dim gc = ex(4).Build() : Dim gs As New Simulator(gc) : gs.Reset()
        Dim gb = gc.Bounds() : gb.Inflate(20, 20)
        Dim rec As New GifRecorder(CInt(gb.Width), CInt(gb.Height), 6)
        Dim gcan As New CircuitCanvas() With {.Circuit = gc, .Simulating = True, .Simulator = gs}
        Press(gs, Find(gc, "1S1"))
        For f = 1 To 12
            Run(gs, 0.1)
            Using bmp As New Drawing.Bitmap(CInt(gb.Width), CInt(gb.Height))
                Using g = Drawing.Graphics.FromImage(bmp)
                    g.Clear(Drawing.Color.White) : g.TranslateTransform(-gb.Left, -gb.Top) : gcan.PaintTo(g)
                End Using
                rec.AddFrame(bmp)
            End Using
        Next
        Dim gifPath = IO.Path.Combine("out", "recording.gif")
        rec.Save(gifPath)
        Using img = Drawing.Image.FromFile(gifPath)
            Dim frames = img.GetFrameCount(Drawing.Imaging.FrameDimension.Time)
            Check("GIF decodes with all frames", frames = 12, frames.ToString())
        End Using

        ' ---------------- Hydraulic additions: meter-out throttle, reducing valve, single-acting ram
        Dim hx = Examples.All(8).Build()
        Dim hxCyl = hx.Elements.OfType(Of HydraulicCylinder)().First()
        Dim hv = hx.Elements.OfType(Of HydraulicValve43)().First()
        Dim rodTube = hx.Tubes.First(Function(tb) (tb.A.Owner Is hxCyl AndAlso tb.A.Name = "2") OrElse (tb.B.Owner Is hxCyl AndAlso tb.B.Name = "2"))
        hx.RemoveTube(rodTube)
        Dim hfc = hx.Add(New FlowControlValve() With {.Hydraulic = True, .OpeningPercent = 25}, 300, 150, "1V2")
        Check("hydraulic flow control connects to oil lines", hx.Connect(hv, "B", hfc, "1") IsNot Nothing AndAlso hx.Connect(hfc, "2", hxCyl, "2") IsNot Nothing)
        Dim hs As New Simulator(hx) : hs.Reset()
        Press(hs, Find(hx, "S1"))
        Dim ht = 0.0
        While hxCyl.Position < 0.999 AndAlso ht < 30 : Run(hs, 0.05) : ht += 0.05 : End While
        Check("hydraulic meter-out throttle slows extension (~4x of 1.9 s)", ht > 6 AndAlso ht < 10, ht.ToString("0.0") & " s")
        Release(hs, Find(hx, "S1")) : Press(hs, Find(hx, "S2"))
        Dim hr = 0.0
        While hxCyl.Position > 0.001 AndAlso hr < 30 : Run(hs, 0.05) : hr += 0.05 : End While
        Check("hydraulic one-way flow control: free return", hr < 2, hr.ToString("0.0") & " s")
        Check("hydraulic flow control circuit has no errors", Not CircuitAnalysis.StaticChecks(hx).Any(Function(x) x.Severity = IssueSeverity.Error),
              String.Join(" | ", CircuitAnalysis.StaticChecks(hx)))

        Dim hrd = Examples.All(8).Build()
        Dim hv2 = hrd.Elements.OfType(Of HydraulicValve43)().First()
        Dim pTube = hrd.Tubes.First(Function(tb) (tb.A.Owner Is hv2 AndAlso tb.A.Name = "P") OrElse (tb.B.Owner Is hv2 AndAlso tb.B.Name = "P"))
        Dim jn = If(pTube.A.Owner Is hv2, pTube.B, pTube.A)
        hrd.RemoveTube(pTube)
        Dim red = hrd.Add(New PressureRegulator() With {.Hydraulic = True, .Setting = 30}, 100, 300, "0V2")
        hrd.Connect(jn, red.GetPort("1")) : hrd.Connect(red, "2", hv2, "P")
        Dim hrs As New Simulator(hrd) : hrs.Reset()
        Press(hrs, Find(hrd, "S1")) : Run(hrs, 5)
        Dim capP = hrd.Elements.OfType(Of HydraulicCylinder)().First().Ports(0).Pressure
        Dim pumpP = hrd.Elements.OfType(Of HydraulicPump)().First().Ports(0).Pressure
        Check("pressure reducing valve limits the cylinder to 30 bar", Math.Abs(capP - 30) < 0.5 AndAlso pumpP > 55, $"cylinder {capP:0.0} bar, pump {pumpP:0.0} bar")

        Dim ram = Examples.All(8).Build()
        Dim oldCyl = ram.Elements.OfType(Of HydraulicCylinder)().First()
        ram.Remove(oldCyl)
        Dim ramCyl = ram.Add(New HydraulicSingleActingCylinder(), 200, 60, "1A")
        Dim hv3 = ram.Elements.OfType(Of HydraulicValve43)().First()
        ram.Connect(hv3, "A", ramCyl, "1")
        Dim rs As New Simulator(ram) : rs.Reset()
        Press(rs, Find(ram, "S1")) : Run(rs, 3)
        Check("single-acting hydraulic cylinder extends with oil", ramCyl.Position > 0.99, ramCyl.Position.ToString("0.00"))
        Release(rs, Find(ram, "S1")) : Run(rs, 1)
        Check("single-acting hydraulic cylinder holds in the centre position", ramCyl.Position > 0.99)
        Press(rs, Find(ram, "S2")) : Run(rs, 3)
        Check("single-acting hydraulic cylinder returns by spring/load", ramCyl.Position < 0.01, ramCyl.Position.ToString("0.00"))
        Dim ramRound = Circuit.FromXml(ram.ToXml())
        Check("new hydraulic parts survive saving", ramRound.Elements.OfType(Of HydraulicSingleActingCylinder)().Count() = 1)
        Dim rx = Circuit.FromXml(hrd.ToXml()).Elements.OfType(Of PressureRegulator)().First()
        Check("reducing valve keeps its oil setting after loading", rx.Hydraulic AndAlso Math.Abs(rx.Setting - 30) < 0.01)

        ' ---------------- Quiz fairness and exam clock
        Dim counts(3) As Integer
        Dim twins = 0
        For seed = 1 To 60
            For Each q In QuestionBank.Pick(20, seed)
                If q.Options.Length = 4 Then counts(q.Correct) += 1
                Dim opts = q.Options.ToList()
                If (opts.Contains("Pressure gauge") AndAlso opts.Contains("Hydraulic pressure gauge")) OrElse
                   (opts.Contains("Double-acting cylinder") AndAlso opts.Contains("Hydraulic cylinder")) Then twins += 1
            Next
        Next
        Dim total = counts.Sum()
        Check("quiz answers are spread over A-D", counts.All(Function(n) n > total * 0.15 AndAlso n < total * 0.35), String.Join("/", counts))
        Check("quiz never asks to tell apart identical symbols", twins = 0, twins.ToString())
        Dim exam As New QuizDialog(True)
        exam.Show() : Windows.Forms.Application.DoEvents()
        exam.Close() : Windows.Forms.Application.DoEvents()
        Dim examTimer = DirectCast(GetType(QuizDialog).GetField("_timer", Reflection.BindingFlags.NonPublic Or Reflection.BindingFlags.Instance).GetValue(exam), Windows.Forms.Timer)
        Check("closing an exam early stops its clock", Not examTimer.Enabled)

        ' ---------------- Cutaway views for every component
        Dim cutawayFailures As New List(Of String)
        Dim sheet As New List(Of Drawing.Bitmap)
        Dim views As New List(Of (El As CircuitElement, Sim As Boolean))
        For Each lp In Library.Presets
            views.Add((lp.Factory.Invoke(), False))
        Next
        ' Live states: a few components in running example circuits.
        For Each exIndex In {4, 7, 8, 9}
            Dim lc = Examples.All(exIndex).Build()
            Dim ls As New Simulator(lc) : ls.Reset()
            Dim firstButton = lc.Elements.FirstOrDefault(Function(e) e.IsManuallyOperated AndAlso TypeOf e IsNot HydraulicPump)
            If firstButton IsNot Nothing Then Press(ls, firstButton)
            Run(ls, 0.6)
            For Each e In lc.Elements.Where(Function(x) Cutaways.Supports(x))
                views.Add((e, True))
            Next
        Next
        For Each v In views
            Try
                Using view As New CutawayView() With {.Size = New Drawing.Size(600, 300), .Element = v.El, .Simulating = v.Sim}
                    Dim bmp As New Drawing.Bitmap(600, 300)
                    Using g = Drawing.Graphics.FromImage(bmp)
                        view.Render(g)
                    End Using
                    sheet.Add(bmp)
                End Using
            Catch cutErr As Exception
                cutawayFailures.Add(v.El.DisplayName & ": " & cutErr.Message)
            End Try
        Next
        Check("every component has a cutaway that draws without errors", cutawayFailures.Count = 0, String.Join(" | ", cutawayFailures))
        ' Contact sheets for review (4 columns).
        For pageNo = 0 To (sheet.Count - 1) \ 16
            Using big As New Drawing.Bitmap(4 * 600, 4 * 300)
                Using g = Drawing.Graphics.FromImage(big)
                    g.Clear(Drawing.Color.Gray)
                    For k = 0 To 15
                        Dim idx = pageNo * 16 + k
                        If idx >= sheet.Count Then Exit For
                        g.DrawImage(sheet(idx), (k Mod 4) * 600, (k \ 4) * 300)
                    Next
                End Using
                big.Save(IO.Path.Combine("out", $"cutaways_{pageNo + 1}.png"), Drawing.Imaging.ImageFormat.Png)
            End Using
        Next
        For Each b In sheet : b.Dispose() : Next

        ' ---------------- Fixes from the full review
        ' Invalid inputs are clamped; a stroke of 0 can no longer crash realistic mode.
        Dim vc = Examples.All(0).Build()
        Dim vcyl = vc.Elements.OfType(Of CylinderBase)().First()
        vcyl.StrokeLength = 0 : vcyl.BoreMm = -10 : vcyl.FrictionN = -5
        Check("cylinder inputs are clamped", vcyl.StrokeLength >= 5 AndAlso vcyl.BoreMm >= 4 AndAlso vcyl.FrictionN >= 0)
        Dim vs As New Simulator(vc) With {.RealPhysics = True} : vs.Reset()
        Press(vs, Find(vc, "1S1"))
        Dim crashed = False
        Try
            Run(vs, 1)
        Catch
            crashed = True
        End Try
        Check("tiny stroke runs in realistic mode without error", Not crashed AndAlso Not Double.IsNaN(vcyl.Position))
        Dim pumpTest As New HydraulicPump() With {.FlowLpm = -8}
        Check("pump flow cannot be negative", pumpTest.FlowLpm > 0)

        ' New parts get their own names; pasted copies are renamed consistently.
        Dim nc As New Circuit()
        Dim placed As New List(Of CircuitElement)
        For Each presetName In {"5/2 valve, single solenoid", "5/2 valve, single solenoid", "Push button, NO", "Push button, NO", "Relay coil", "Relay coil", "Double-acting cylinder", "Double-acting cylinder"}
            Dim el = Library.Presets.First(Function(lp) lp.Name = presetName).Factory.Invoke()
            nc.Add(el, 100 * placed.Count, 100)
            Naming.NameNewElement(el, nc.Elements)
            placed.Add(el)
        Next
        Check("second solenoid valve gets its own solenoid", DirectCast(placed(0), DirectionalValve).SolenoidLabel <> DirectCast(placed(1), DirectionalValve).SolenoidLabel,
              DirectCast(placed(0), DirectionalValve).SolenoidLabel & "/" & DirectCast(placed(1), DirectionalValve).SolenoidLabel)
        Check("push buttons, relays and cylinders are numbered", placed(2).Label <> placed(3).Label AndAlso placed(4).Label <> placed(5).Label AndAlso placed(6).Label <> placed(7).Label,
              String.Join(",", placed.Select(Function(el) el.Label)))
        Dim coilForValve = New ElectricCoil() With {.Kind = CoilKind.Solenoid}
        nc.Add(coilForValve, 0, 300) : Naming.NameNewElement(coilForValve, nc.Elements)
        Check("a new solenoid coil takes the name of a valve still waiting for one", coilForValve.Label = DirectCast(placed(0), DirectionalValve).SolenoidLabel, coilForValve.Label)
        Dim pc = Examples.All(7).Build()
        Dim copied = pc.Merge(pc.ExtractXml(pc.Elements), 40, 40)
        Naming.RenamePasted(copied, pc.Elements)
        Dim copiedValve = copied.OfType(Of DirectionalValve)().First()
        Dim copiedCoils = copied.OfType(Of ElectricCoil)().Where(Function(k) k.Kind = CoilKind.Solenoid).Select(Function(k) k.Label).ToList()
        Check("pasted valve and its pasted coil are renamed together", copiedValve.SolenoidLabel <> "1M1" AndAlso copiedCoils.Contains(copiedValve.SolenoidLabel), copiedValve.SolenoidLabel & " / " & String.Join(",", copiedCoils))
        Dim copiedRelay = copied.OfType(Of ElectricCoil)().First(Function(k) k.Kind = CoilKind.Relay).Label
        Check("pasted relay contacts follow the pasted relay", copied.OfType(Of ElectricContact)().Where(Function(k) k.Operator = ContactOperator.Relay).All(Function(k) k.Reference = copiedRelay), copiedRelay)
        Dim ps As New Simulator(pc) : ps.Reset()
        Press(ps, Find(pc, "S1")) : Run(ps, 1.5)
        Check("the original circuit still works next to its copy", DirectCast(pc.Elements.First(Function(el) el.Label = "1A"), CylinderBase).Position > 0.99)
        Check("the copy did not move along", DirectCast(copied.First(Function(el) TypeOf el Is CylinderBase), CylinderBase).Position < 0.01)
        Dim twoValves = Examples.All(6).Build()
        Dim extraValve = twoValves.Add(V52(ValveActuator.Solenoid, ValveReturn.Spring), 600, 400, "2V1")
        Check("checker: two valves on one solenoid", CircuitAnalysis.StaticChecks(twoValves).Any(Function(i) i.Message.Contains("always switch together")))
        Dim marksTwice = Examples.All(4).Build()
        marksTwice.Add(New DoubleActingCylinder() With {.RetractedMark = "1S1"}, 600, 60, "2A")
        Check("checker: position mark used twice", CircuitAnalysis.StaticChecks(marksTwice).Any(Function(i) i.Message.Contains("is used 2 times")))
        Dim lone As New Circuit()
        lone.Add(New PageConnector(), 0, 0)
        Check("checker: page connector without partner", CircuitAnalysis.StaticChecks(lone).Any(Function(i) i.Message.Contains("no partner")))

        ' Air motor uses air; lamps do not act as relays; 3-position valves keep two actuators.
        Dim am As New Circuit()
        Dim mot = am.Add(New AirMotor(), 100, 0)
        Dim amv = am.Add(Library.V32(ValveActuator.Selector), 80, 100, "S1")
        Dim ams = am.Add(New AirSupply(), 90, 220)
        am.Connect(ams, "1", amv, "1") : am.Connect(amv, "2", mot, "1")
        Dim asim As New Simulator(am) : asim.Reset()
        Press(asim, amv) : Release(asim, amv) : Run(asim, 6)
        Check("air motor air consumption is counted (~150 NL/min)", asim.AirConsumed > 12 AndAlso asim.AirConsumed < 18, asim.AirConsumed.ToString("0.0") & " NL in 6 s")
        Dim lk As New Circuit()
        Dim lp1 = lk.Add(New PowerTerminal(), 0, 0)
        Dim lsw = lk.Add(New ElectricContact() With {.Operator = ContactOperator.Selector}, 0, 60, "S1")
        Dim lamp = lk.Add(New ElectricCoil() With {.Kind = CoilKind.Lamp, .Label = "K1"}, 0, 160)
        Dim lz = lk.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 0, 260)
        Dim relayContact = lk.Add(New ElectricContact() With {.Operator = ContactOperator.Relay, .Reference = "K1"}, 200, 60)
        lk.Connect(lp1, "1", lsw, "1") : lk.Connect(lsw, "2", lamp, "A1") : lk.Connect(lamp, "A2", lz, "1")
        Dim lsim As New Simulator(lk) : lsim.Reset()
        Press(lsim, lsw) : Release(lsim, lsw)
        Check("a lamp named K1 does not switch relay contacts K1", lamp.Active AndAlso Not relayContact.IsClosed)
        Dim v53r As New Valve53() With {.Actuator = ValveActuator.Solenoid}
        v53r.ReturnType = ValveReturn.Spring
        Check("3-position valve keeps an actuator on both sides", v53r.ReturnType = ValveReturn.Solenoid)
        ' Clicking the right half of a double-solenoid valve operates the right solenoid's override.
        Dim dsc = Examples.All(6).Build()
        Dim dsv = DirectCast(dsc.Elements.OfType(Of DirectionalValve)().First(), DirectionalValve)
        dsv.ReturnType = ValveReturn.Solenoid
        Dim dss As New Simulator(dsc) : dss.Reset()
        dsv.OnSimMouseDown(New Drawing.PointF(5, 30)) : dss.RunLogic() : dsv.OnSimMouseUp() : dss.RunLogic()
        Check("left half click: left override", dsv.State = 1)
        dsv.OnSimMouseDown(New Drawing.PointF(dsv.LocalBounds.Right - 5, 30)) : dss.RunLogic() : dsv.OnSimMouseUp() : dss.RunLogic()
        Check("right half click: right solenoid override", dsv.State = 0)

        ' Cross-references use column numbers that are printed on the drawing.
        Dim crossProject As New Project(Examples.All(7).Build())
        crossProject.UpdateCrossReferences()
        Dim k1Coil = crossProject.AllElements().OfType(Of ElectricCoil)().First(Function(k) k.Label = "K1")
        Dim firstContact = crossProject.AllElements().OfType(Of ElectricContact)().First(Function(k) k.Operator = ContactOperator.Relay)
        Dim fcb = firstContact.WorldBounds()
        Check("cross-reference column = printed column of the contact", k1Coil.CrossReference.Contains("1." & Project.ColumnAt(fcb.X + fcb.Width / 2)), k1Coil.CrossReference)
        Dim svgPath = IO.Path.Combine("out", "columns.svg")
        VectorExport.SaveSvg(Examples.All(7).Build(), svgPath)
        Check("exported drawings show the column numbers", IO.File.ReadAllText(svgPath).Contains(">5</text>"))

        ' Elements cannot be lost off the sheet.
        Dim off As New Circuit()
        off.Add(New AirSupply(), -200, -100)
        off.EnsureOnSheet()
        Check("circuits off the sheet are moved back when loaded", off.Bounds().Left >= 0 AndAlso off.Bounds().Top >= 0, off.Bounds().ToString())

        ' Parts list: hoses and tubing.
        Dim hydParts = PartsList.Build(New Project(Examples.All(8).Build()))
        Check("parts list counts hydraulic hoses", hydParts.Any(Function(l) l.Description.StartsWith("Hydraulic hose") AndAlso l.Quantity > 3))
        Dim airParts = PartsList.Build(New Project(Examples.All(0).Build()))
        Check("parts list estimates tubing", airParts.Any(Function(l) l.Description.StartsWith("Plastic tubing") AndAlso l.Quantity = 2))

        ' Two-hand control is not reported as a problem.
        Dim twoHand = CircuitAnalysis.DynamicCheck(Examples.All(3).Build(), False)
        Check("two-hand control: no 'did not move' message", Not twoHand.Any(Function(i) i.Message.Contains("did not move")), String.Join(" | ", twoHand))

        ' PDF: text the standard font cannot show is embedded as a picture instead of '?'.
        Dim intl = Examples.All(0).Build()
        intl.Add(New TextNote() With {.Text = "ಕನ್ನಡ हिंदी ≈ 6 bar"}, 40, 300)
        Dim intlPath = IO.Path.Combine("out", "international.pdf")
        Reports.SavePdfReport(New Project(intl, "Test ಕನ್ನಡ"), intlPath, False, "")
        Dim intlBytes = IO.File.ReadAllBytes(intlPath)
        Dim intlText = System.Text.Encoding.GetEncoding(28591).GetString(intlBytes)
        Check("PDF embeds non-Latin text as images", intlText.Contains("/Subtype /Image") AndAlso Not intlText.Contains("(?????"))

        ' Help texts.
        Dim exhaustCentre = ComponentHelp.HelpFor(New Valve53() With {.Centre = CentrePosition.Exhausted})
        Check("help: exhaust centre is described correctly", exhaustCentre.Contains("Exhaust centre") AndAlso Not exhaustCentre.Contains("holds its position"))
        Check("help: hydraulic valve talks about oil", ComponentHelp.HelpFor(New HydraulicValve42()).Contains("oil"))
        Check("help: roller without mark", Not ComponentHelp.HelpFor(Library.V32(ValveActuator.RollerLever)).Contains("mark ."))

        ' Properties panel: irrelevant settings are hidden, names readable.
        Dim pbValve = Library.V32(ValveActuator.PushButton)
        Dim shown = ComponentModel.TypeDescriptor.GetProperties(pbValve).Cast(Of ComponentModel.PropertyDescriptor)().Select(Function(pd) pd.Name).ToList()
        Check("push-button valve hides solenoid and delay settings", Not shown.Contains("SolenoidLabel") AndAlso Not shown.Contains("DelaySeconds") AndAlso shown.Contains("Actuator"))
        Dim conv = ComponentModel.TypeDescriptor.GetConverter(GetType(Polarity))
        Check("enum values are shown by their description", conv.ConvertToString(Polarity.Plus24V) = "+24 V" AndAlso CType(conv.ConvertFromString("0 V"), Polarity) = Polarity.Zero0V)

        ' No property may share its name with its group (some property grids then hide the whole group).
        Dim clashes As New List(Of String)
        For Each lp In Library.Presets
            Dim el = lp.Factory.Invoke()
            For Each pd In ComponentModel.TypeDescriptor.GetProperties(el).Cast(Of ComponentModel.PropertyDescriptor)()
                If pd.IsBrowsable AndAlso pd.DisplayName = pd.Category Then clashes.Add($"{lp.Name}: {pd.DisplayName}")
            Next
        Next
        Check("no property is named like its group", clashes.Count = 0, String.Join(" | ", clashes.Distinct()))

        ' Library thumbnails render.
        For Each p In Library.Presets
            Using b = Library.RenderThumbnail(p.Factory.Invoke(), 72, 48) : End Using
        Next
        Check("thumbnails render", True)
        ' Every element type round-trips through the factory.
        Dim allTypes = Library.Presets.Select(Function(p) p.Factory.Invoke()).ToList()
        Check("factory knows all types", allTypes.All(Function(e) ElementFactory.Create(e.TypeName) IsNot Nothing))

        TestV32.RunAll()

        Console.WriteLine(If(failures = 0, "ALL TESTS PASSED", $"{failures} FAILURE(S)"))
        Environment.ExitCode = failures
    End Sub
End Module
