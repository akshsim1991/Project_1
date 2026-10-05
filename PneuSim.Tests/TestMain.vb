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
        ' Explanation.
        Dim expl = CircuitAnalysis.Explain(ex(4).Build(), Nothing, False)
        Console.WriteLine(expl)
        Check("explain describes control and sequence", expl.Contains("controlled by valve 1V1") AndAlso expl.Contains("MOTION SEQUENCE"))
        Check("explain narrates the run", expl.Contains("starts to extend") AndAlso expl.Contains("fully extended"))
        Dim expl2 = CircuitAnalysis.Explain(ex(7).Build(), Nothing, False)
        Check("explain follows relays and solenoids", expl2.Contains("relay K1 picks up") AndAlso expl2.Contains("solenoid 1M1"), expl2)

        ' Library thumbnails render.
        For Each p In Library.Presets
            Using b = Library.RenderThumbnail(p.Factory.Invoke(), 72, 48) : End Using
        Next
        Check("thumbnails render", True)
        ' Every element type round-trips through the factory.
        Dim allTypes = Library.Presets.Select(Function(p) p.Factory.Invoke()).ToList()
        Check("factory knows all types", allTypes.All(Function(e) ElementFactory.Create(e.TypeName) IsNot Nothing))

        Console.WriteLine(If(failures = 0, "ALL TESTS PASSED", $"{failures} FAILURE(S)"))
        Environment.ExitCode = failures
    End Sub
End Module
