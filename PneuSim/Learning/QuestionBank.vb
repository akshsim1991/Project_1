''' <summary>A multiple-choice question; <see cref="SymbolPreset"/> shows a symbol picture.</summary>
Public Class QuizQuestion
    Public Property Topic As String
    Public Property Text As String
    Public Property Options As String()
    Public Property Correct As Integer
    Public Property Explanation As String
    Public Property SymbolPreset As LibraryPreset
End Class

Public Module QuestionBank

    Private Function Q(topic As String, text As String, options As String(), correct As Integer, explanation As String) As QuizQuestion
        Return New QuizQuestion With {.Topic = topic, .Text = text, .Options = options, .Correct = correct, .Explanation = explanation}
    End Function

    Private ReadOnly Written As QuizQuestion() = {
        Q("Basics", "What is the usual working pressure of an industrial compressed air system?", {"0.6 bar", "6 bar", "60 bar", "600 bar"}, 1,
          "Most pneumatic systems work at about 6 bar (gauge)."),
        Q("Basics", "Why is compressed air passed through a service unit before use?", {"To heat it", "To filter it, remove water and set the pressure", "To increase the flow", "To make it quieter"}, 1,
          "The service unit filters dirt and water and regulates the pressure."),
        Q("Basics", "What does 'free air' (normal litres) mean?", {"Air at 6 bar", "Air at atmospheric pressure", "Air without oil", "Air after the silencer"}, 1,
          "Air consumption is quoted as free air, i.e. the volume at atmospheric pressure."),
        Q("Actuators", "A double-acting cylinder has a 40 mm bore. Roughly what force does it extend with at 6 bar?", {"75 N", "750 N", "7 500 N", "75 000 N"}, 1,
          "F = p × A = 600 000 Pa × 0.00126 m² ≈ 750 N."),
        Q("Actuators", "Why does a double-acting cylinder retract with less force than it extends?", {"The spring opposes it", "The rod reduces the piston area on that side", "The exhaust is blocked", "The pressure is lower"}, 1,
          "On the rod side the piston rod takes up part of the area."),
        Q("Actuators", "What returns a single-acting cylinder?", {"Compressed air", "A spring (or the load)", "A solenoid", "A flow control valve"}, 1,
          "A single-acting cylinder uses air for one direction only; a spring returns it."),
        Q("Valves", "In the name '5/2-way valve', what do 5 and 2 mean?", {"5 bar, 2 cylinders", "5 ports, 2 positions", "5 positions, 2 ports", "5 mm, 2 mm"}, 1,
          "The first number is the number of ports, the second the number of switching positions."),
        Q("Valves", "Which port number is the supply (pressure) port of a pneumatic valve?", {"1", "2", "3", "12"}, 0,
          "1 = supply, 2 and 4 = outputs, 3 and 5 = exhausts, 12/14 = pilot ports."),
        Q("Valves", "A 5/2 valve with pilots 14 and 12 and no spring is called…", {"A shuttle valve", "A memory (impulse) valve", "A time delay valve", "A quick exhaust valve"}, 1,
          "It stays in the last position it was switched to, so it remembers the last signal."),
        Q("Valves", "What happens to a cylinder when a closed-centre 5/3 valve goes to its middle position?", {"It extends", "It retracts", "It stops and holds its position", "It moves freely"}, 2,
          "All ports are blocked, so the air in both chambers is trapped."),
        Q("Valves", "What is the function of a shuttle valve?", {"AND", "OR", "NOT", "Memory"}, 1,
          "The output has air if either input has air."),
        Q("Valves", "Which valve is used for a two-hand safety control?", {"Shuttle valve", "Two-pressure valve", "Quick exhaust valve", "Check valve"}, 1,
          "The two-pressure (AND) valve passes air only when both inputs have air."),
        Q("Speed", "How is the speed of a pneumatic cylinder usually controlled?", {"Throttling the supply air (meter-in)", "Throttling the exhaust air (meter-out)", "Lowering the supply pressure", "Using a bigger valve"}, 1,
          "Meter-out keeps both chambers under pressure and gives a smooth, load-independent speed."),
        Q("Speed", "What does a quick exhaust valve do?", {"Slows the cylinder down", "Lets the cylinder exhaust directly, so it returns faster", "Stops the cylinder", "Filters the exhaust"}, 1,
          "The air escapes next to the cylinder instead of through the long tube and the valve."),
        Q("Speed", "In a one-way flow control valve, which way is the flow throttled?", {"Both ways", "Only one way; the check valve gives free flow the other way", "Neither way", "Only when the pressure is high"}, 1,
          "The check valve bypasses the throttle in one direction."),
        Q("Sequences", "What is signal overlap in a sequence circuit?", {"Two cylinders moving at once", "A memory valve getting both pilot signals at the same time", "A leak in a tube", "Too much pressure"}, 1,
          "The opposite signal is still present, so the valve cannot switch and the sequence stops."),
        Q("Sequences", "In the cascade method, how many cascade valves are needed for 3 groups?", {"1", "2", "3", "4"}, 1,
          "Number of cascade valves = number of groups − 1."),
        Q("Sequences", "How is a sequence split into groups in the cascade method?", {"One group per cylinder", "So that no cylinder appears twice in a group", "Every two movements", "By pressure"}, 1,
          "Each group may contain each cylinder only once."),
        Q("Sequences", "Which valve detects that a cylinder has reached its end position in a pneumatic sequence?", {"Roller lever valve", "Shuttle valve", "Flow control valve", "Service unit"}, 0,
          "Roller lever (limit) valves are operated by the cylinder rod or a cam."),
        Q("Vacuum", "How does a vacuum generator (ejector) make vacuum?", {"With an electric pump", "Compressed air rushing through a nozzle drags air out of the suction port", "By cooling the air", "With a check valve"}, 1,
          "The fast jet of air (venturi principle) sucks air out of the suction cup line."),
        Q("Vacuum", "Why is a vacuum switch fitted in a suction cup line?", {"To make more vacuum", "To report that the part is really held before the arm moves", "To save air", "To release the part"}, 1,
          "Without a workpiece the cup is open and the vacuum stays weak, so the switch does not operate."),
        Q("Air supply", "What does the air receiver do?", {"Filters the air", "Stores compressed air and evens out peaks of air use", "Lowers the pressure", "Dries the air"}, 1,
          "The receiver stores air so the compressor runs in calmer cycles and the pressure does not drop at every stroke."),
        Q("Sensors", "What is an idle-return roller valve used for?", {"To slow a cylinder", "To give only a short signal in one direction, avoiding signal overlap", "To count cycles", "To hold a cylinder in position"}, 1,
          "Its hinged lever operates the valve only when the cam comes from one side."),
        Q("Troubleshooting", "A cylinder moves very slowly and air hisses from the valve exhaust even when the cylinder is not moving. What is the most likely fault?", {"A blocked tube", "A worn piston seal", "A burnt solenoid", "Too much supply pressure"}, 1,
          "Air passes the worn seal from one chamber to the other and out through the valve exhaust."),
        Q("Troubleshooting", "A solenoid valve does not switch, but it switches when you press its manual override. What do you check first?", {"The cylinder", "The supply pressure", "The solenoid and its 24 V signal", "The silencer"}, 2,
          "The valve itself works, so the electrical side is the problem: no signal, a broken wire or a burnt coil."),
        Q("Electro", "What is the usual control voltage in electro-pneumatics?", {"5 V DC", "24 V DC", "230 V AC", "415 V AC"}, 1,
          "24 V DC is safe and standard for control circuits."),
        Q("Electro", "What does a 'normally closed' (NC) contact do?", {"Conducts only when operated", "Conducts until it is operated", "Never conducts", "Conducts with a delay"}, 1,
          "It is closed at rest and opens when operated (stop buttons are NC)."),
        Q("Electro", "What is a self-holding (latching) circuit?", {"A relay holding itself on through its own contact", "A valve with a detent", "A cylinder with a brake", "A timer"}, 0,
          "The relay's own NO contact in parallel with the start button keeps the coil energized."),
        Q("Electro", "An on-delay timer relay…", {"Switches its contacts immediately and back after a delay", "Switches its contacts a set time after the coil is energized", "Never switches", "Counts pulses"}, 1,
          "The contacts change over after the set time has passed."),
        Q("Electro", "What switches a solenoid valve?", {"Air pressure", "An electromagnet (solenoid coil)", "A roller", "A spring"}, 1,
          "Current through the coil pulls the armature and moves the spool."),
        Q("Electro", "Which sensor detects a cylinder position without contact?", {"Limit switch", "Proximity (reed or inductive) sensor", "Push button", "Pressure gauge"}, 1,
          "Proximity sensors react to the magnet on the piston or a metal target."),
        Q("Hydraulics", "Why does every hydraulic circuit need a pressure relief valve?", {"To filter the oil", "To limit the pressure when nothing can move", "To cool the oil", "To make the pump quieter"}, 1,
          "Oil cannot be compressed; without a relief valve the pressure would rise until something breaks."),
        Q("Hydraulics", "A pump delivers 12 l/min into a cylinder with 20 cm² piston area. What is the speed?", {"0.01 m/s", "0.1 m/s", "1 m/s", "10 m/s"}, 1,
          "v = Q / A = 0.0002 m³/s ÷ 0.002 m² = 0.1 m/s."),
        Q("Hydraulics", "In a hydraulic system, what decides the working pressure while the cylinder moves?", {"The pump size", "The load", "The tank size", "The oil colour"}, 1,
          "Pressure builds up only as far as needed to move the load (up to the relief setting)."),
        Q("Hydraulics", "What does a tandem-centre 4/3 valve do in its middle position?", {"Blocks all ports", "Connects P to T so the pump runs unloaded, A and B blocked", "Connects A and B to the pump", "Lets the cylinder float"}, 1,
          "The pump flow returns to tank at low pressure, saving energy, while the cylinder is held."),
        Q("Hydraulics", "What is a counterbalance valve used for?", {"To hold a hanging load and stop it running away", "To increase speed", "To filter oil", "To cool oil"}, 0,
          "It keeps back-pressure on the cylinder so a load cannot fall."),
        Q("Hydraulics", "What does an accumulator do?", {"Stores oil under pressure", "Measures flow", "Cools the oil", "Removes air"}, 0,
          "Gas pressure stores energy that can move actuators when the pump cannot."),
        Q("Hydraulics", "How is hydraulic power calculated (kW)?", {"p × Q / 600 with p in bar and Q in l/min", "p + Q", "Q / p", "p × A"}, 0,
          "P = p × Q / 600."),
        Q("Basics", "What does a pressure sequence valve do?", {"Switches when the pressure reaches a set value", "Limits flow", "Stores air", "Filters air"}, 0,
          "It starts the next action only after the pressure (e.g. clamping force) has been reached.")
    }

    Private _signatures As Dictionary(Of LibraryPreset, String)

    ''' <summary>A fingerprint of each symbol's drawing, so the quiz never asks to tell apart two identical symbols.</summary>
    Private Function Signature(p As LibraryPreset) As String
        If _signatures Is Nothing Then
            _signatures = New Dictionary(Of LibraryPreset, String)
            For Each lp In Library.Presets
                Using bmp = Library.RenderThumbnail(lp.Factory.Invoke(), 120, 50), ms As New IO.MemoryStream()
                    bmp.Save(ms, Imaging.ImageFormat.Png)
                    Using md5 = Security.Cryptography.MD5.Create()
                        _signatures(lp) = Convert.ToBase64String(md5.ComputeHash(ms.ToArray()))
                    End Using
                End Using
            Next
        End If
        Return _signatures(p)
    End Function

    ''' <summary>Symbol identification questions built from the component library.</summary>
    Private Function SymbolQuestions(rnd As Random) As List(Of QuizQuestion)
        Dim list As New List(Of QuizQuestion)
        Dim presets = Library.Presets.Where(Function(p) p.Category <> Library.CatDrawing AndAlso Not p.Name.Contains("junction") AndAlso
                                                       Not p.Name.Contains("connector")).ToList()
        For Each preset In presets
            Dim sig = Signature(preset)
            ' Only symbols that look different from the right answer (and from each other) are offered.
            Dim wrong = presets.Where(Function(p) p IsNot preset AndAlso Signature(p) <> sig).
                GroupBy(Function(p) Signature(p)).Select(Function(g) g.First()).
                OrderBy(Function(x) rnd.Next()).Take(3).Select(Function(p) p.Name).ToList()
            Dim options = wrong.Concat({preset.Name}).OrderBy(Function(x) rnd.Next()).ToArray()
            list.Add(New QuizQuestion With {
                .Topic = "Symbols", .Text = "What does this symbol show?", .Options = options,
                .Correct = Array.IndexOf(options, preset.Name), .SymbolPreset = preset,
                .Explanation = $"It is the symbol for: {preset.Name}."})
        Next
        Return list
    End Function

    ''' <summary>A random selection of questions, mixing written and symbol questions.</summary>
    Public Function Pick(count As Integer, Optional seed As Integer? = Nothing) As List(Of QuizQuestion)
        Dim rnd = If(seed.HasValue, New Random(seed.Value), New Random())
        Dim symbols = SymbolQuestions(rnd).OrderBy(Function(x) rnd.Next()).Take(Math.Max(1, count \ 4)).ToList()
        Dim textQuestions = Written.OrderBy(Function(x) rnd.Next()).Take(count - symbols.Count).ToList()
        Return textQuestions.Concat(symbols).OrderBy(Function(x) rnd.Next()).Select(Function(q) Shuffled(q, rnd)).ToList()
    End Function

    ''' <summary>A copy of the question with its answers in random order.</summary>
    Private Function Shuffled(q As QuizQuestion, rnd As Random) As QuizQuestion
        Dim order = Enumerable.Range(0, q.Options.Length).OrderBy(Function(x) rnd.Next()).ToArray()
        Return New QuizQuestion With {.Topic = q.Topic, .Text = q.Text, .Explanation = q.Explanation, .SymbolPreset = q.SymbolPreset,
                                      .Options = order.Select(Function(i) q.Options(i)).ToArray(),
                                      .Correct = Array.IndexOf(order, q.Correct)}
    End Function

    Public ReadOnly Property WrittenCount As Integer
        Get
            Return Written.Length
        End Get
    End Property
End Module
