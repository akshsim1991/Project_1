Imports System.Text
Imports System.Text.RegularExpressions

''' <summary>One cylinder movement in a sequence, e.g. "A+".</summary>
Public Class SequenceMove
    Public Sub New(letter As Char, extend As Boolean)
        Me.Letter = Char.ToUpperInvariant(letter)
        Me.Extend = extend
    End Sub

    Public ReadOnly Property Letter As Char
    Public ReadOnly Property Extend As Boolean

    Public Overrides Function ToString() As String
        Return Letter & If(Extend, "+", "-")
    End Function
End Class

''' <summary>A parsed motion sequence: steps of one or more simultaneous movements.</summary>
Public Class MotionSequence
    Public ReadOnly Property Steps As New List(Of List(Of SequenceMove))

    ''' <summary>Cylinder letters in alphabetical order; cylinder number = index + 1.</summary>
    Public ReadOnly Property Letters As New List(Of Char)

    Public Function Number(letter As Char) As Integer
        Return Letters.IndexOf(letter) + 1
    End Function

    Public Function AllMoves() As IEnumerable(Of SequenceMove)
        Return Steps.SelectMany(Function(s) s)
    End Function

    Public ReadOnly Property HasParallelSteps As Boolean
        Get
            Return Steps.Any(Function(s) s.Count > 1)
        End Get
    End Property

    Public Overrides Function ToString() As String
        Return String.Join(" ", Steps.Select(Function(s) If(s.Count = 1, s(0).ToString(), "(" & String.Join(" ", s) & ")")))
    End Function

    ''' <summary>
    ''' Parses text such as "A+ B+ B- A-" or "A+ (B+ C+) B- C- A-". Throws
    ''' <see cref="FormatException"/> with an explanation when the sequence is not valid.
    ''' </summary>
    Public Shared Function Parse(text As String) As MotionSequence
        Dim seq As New MotionSequence()
        Dim cleaned = If(text, "").Replace(",", " ").Replace(";", " ").Trim()
        If cleaned.Length = 0 Then Throw New FormatException("Type a sequence, for example: A+ B+ B- A-")

        Dim pos = 0
        Dim tokenRx As New Regex("\G\s*(?:\(([^()]*)\)|([A-Za-z])\s*([+-]))")
        Dim moveRx As New Regex("([A-Za-z])\s*([+-])")
        While pos < cleaned.Length
            Dim m = tokenRx.Match(cleaned, pos)
            If Not m.Success OrElse m.Length = 0 Then
                If cleaned.Substring(pos).Trim().Length = 0 Then Exit While
                Throw New FormatException($"Cannot read ""{cleaned.Substring(pos).Trim()}"". Write each movement as a letter followed by + or -, e.g. A+ B+ B- A-. Put simultaneous movements in brackets: (B+ C+).")
            End If
            Dim stepMoves As New List(Of SequenceMove)
            If m.Groups(1).Success Then
                For Each mm As Match In moveRx.Matches(m.Groups(1).Value)
                    stepMoves.Add(New SequenceMove(mm.Groups(1).Value(0), mm.Groups(2).Value = "+"))
                Next
                If stepMoves.Count = 0 Then Throw New FormatException("Empty brackets in the sequence.")
            Else
                stepMoves.Add(New SequenceMove(m.Groups(2).Value(0), m.Groups(3).Value = "+"))
            End If
            seq.Steps.Add(stepMoves)
            pos = m.Index + m.Length
        End While

        seq.Letters.AddRange(seq.AllMoves().Select(Function(mv) mv.Letter).Distinct().OrderBy(Function(c) c))
        seq.Validate()
        Return seq
    End Function

    Private Sub Validate()
        If Steps.Count < 2 Then Throw New FormatException("A sequence needs at least two steps.")
        If Letters.Count > 8 Then Throw New FormatException("At most 8 cylinders are supported.")
        For Each s In Steps
            Dim dup = s.GroupBy(Function(mv) mv.Letter).FirstOrDefault(Function(gr) gr.Count() > 1)
            If dup IsNot Nothing Then Throw New FormatException($"Cylinder {dup.Key} appears twice in the same step.")
        Next
        For Each letter In Letters
            Dim extended = False
            For Each mv In AllMoves().Where(Function(m) m.Letter = letter)
                If mv.Extend = extended Then
                    Throw New FormatException($"Cylinder {letter} must alternate: it starts retracted, so its first movement is {letter}+, then {letter}-, and so on.")
                End If
                extended = mv.Extend
            Next
            If extended Then Throw New FormatException($"Cylinder {letter} must end retracted ({letter}-) so the cycle can repeat.")
        Next
    End Sub
End Class

Public Enum GeneratorMethod
    ''' <summary>Double-solenoid valves, proximity sensors and a relay step chain.</summary>
    ElectroRelayChain
    ''' <summary>Double-pilot valves, roller valves and cascade (group) valves.</summary>
    PneumaticCascade
End Enum

''' <summary>Result of a generation: the circuit and a plain-language explanation of its design.</summary>
Public Class GeneratedCircuit
    Public Property Circuit As Circuit
    Public Property Explanation As String
End Class

''' <summary>Designs complete control circuits from a motion sequence.</summary>
Public Module SequenceGenerator

    Private Const CylPitch As Single = 320
    Private Const LeftMargin As Single = 120
    Private Const CylY As Single = 70

    Public Function Generate(seq As MotionSequence, method As GeneratorMethod, continuous As Boolean) As GeneratedCircuit
        If method = GeneratorMethod.PneumaticCascade Then Return GenerateCascade(seq, continuous)
        Return GenerateElectro(seq, continuous)
    End Function

    ' ===================================================================== shared power section

    Private Class PowerUnit
        Public Cylinder As DoubleActingCylinder
        Public Valve As Valve52
    End Class

    Private Function MarkName(seq As MotionSequence, mv As SequenceMove, prefix As String) As String
        Return $"{seq.Number(mv.Letter)}{prefix}{If(mv.Extend, 2, 1)}"
    End Function

    ''' <summary>Cylinders on top, each with its 5/2 power valve and air supply underneath.</summary>
    Private Function BuildPowerSection(c As Circuit, seq As MotionSequence, electric As Boolean) As Dictionary(Of Char, PowerUnit)
        Dim units As New Dictionary(Of Char, PowerUnit)
        Dim markPrefix = If(electric, "B", "S")
        For i = 0 To seq.Letters.Count - 1
            Dim n = i + 1
            Dim cx = LeftMargin + i * CylPitch
            Dim cyl = c.Add(New DoubleActingCylinder() With {
                .RetractedMark = $"{n}{markPrefix}1", .ExtendedMark = $"{n}{markPrefix}2"}, cx, CylY, $"{n}A")
            Dim valve As Valve52
            If electric Then
                valve = Library.V52(ValveActuator.Solenoid, ValveReturn.Solenoid)
                valve.SolenoidLabel = $"{n}M1"
                valve.ReturnSolenoidLabel = $"{n}M2"
            Else
                valve = Library.V52(ValveActuator.Pilot, ValveReturn.Pilot)
            End If
            c.Add(valve, cx - 90, CylY + 130, $"{n}V1")
            Dim sup = c.Add(New AirSupply(), cx, CylY + 230)
            c.Connect(valve, "4", cyl, "1")
            c.Connect(valve, "2", cyl, "2")
            c.Connect(sup, "1", valve, "1")
            units(seq.Letters(i)) = New PowerUnit With {.Cylinder = cyl, .Valve = valve}
        Next
        Return units
    End Function

    Private Sub AddTitle(c As Circuit, text As String)
        c.Add(New TextNote() With {.Text = text, .FontSize = 11, .Bold = True}, LeftMargin - 100, 10)
    End Sub

    ' ===================================================================== electro-pneumatic

    ''' <summary>A contact or coil in a ladder rung.</summary>
    Private Class LadderItem
        Public Op As ContactOperator
        Public NormallyClosed As Boolean
        Public Label As String = ""
        Public Reference As String = ""
        Public Function Build() As ElectricContact
            Return Library.Contact(Op, NormallyClosed, Label, Reference)
        End Function
    End Class

    Private Class Rung
        ''' <summary>Sections in series; each section is a list of parallel branches of series contacts.</summary>
        Public Sections As New List(Of List(Of List(Of LadderItem)))
        Public CoilKind As CoilKind
        Public CoilLabel As String
        Public ReadOnly Property Rows As Integer
            Get
                Return Sections.Sum(Function(s) s.Max(Function(b) b.Count)) + 1
            End Get
        End Property
        Public ReadOnly Property MaxBranches As Integer
            Get
                Return Sections.Max(Function(s) s.Count)
            End Get
        End Property
    End Class

    Private Function RelayNO(k As Integer) As LadderItem
        Return New LadderItem With {.Op = ContactOperator.Relay, .Reference = $"K{k}"}
    End Function

    Private Function RelayNC(k As Integer) As LadderItem
        Return New LadderItem With {.Op = ContactOperator.Relay, .NormallyClosed = True, .Reference = $"K{k}"}
    End Function

    Private Function Sensors(seq As MotionSequence, stepIndex As Integer) As List(Of LadderItem)
        Return seq.Steps(stepIndex).Select(Function(mv) New LadderItem With {
            .Op = ContactOperator.ProximitySensor, .Reference = MarkName(seq, mv, "B")}).ToList()
    End Function

    Private Function GenerateElectro(seq As MotionSequence, continuous As Boolean) As GeneratedCircuit
        Dim c As New Circuit()
        Dim n = seq.Steps.Count
        AddTitle(c, $"Sequence {seq}   —   electro-pneumatic control with a relay step chain")
        BuildPowerSection(c, seq, electric:=True)

        ' Step relays K1..Kn: Ki switches on when K(i-1) is on and step i-1 is complete,
        ' holds itself, and is switched off by K(i+1). Only one step relay is on at a time,
        ' so opposing solenoids can never be energized together.
        Dim rungs As New List(Of Rung)
        For i = 1 To n
            Dim r As New Rung With {.CoilKind = CoilKind.Relay, .CoilLabel = $"K{i}"}
            Dim parallel As New List(Of List(Of LadderItem))
            If i = 1 Then
                ' Start condition: start button (or continuous switch), last step complete,
                ' and no intermediate step relay on.
                Dim condition = Sensors(seq, n - 1)
                For k = 3 To n - 1
                    condition.Add(RelayNC(k))
                Next
                Dim startBranch As New List(Of LadderItem) From {New LadderItem With {.Op = ContactOperator.PushButton, .Label = "S1"}}
                startBranch.AddRange(condition)
                parallel.Add(startBranch)
                If continuous Then
                    Dim contBranch As New List(Of LadderItem) From {New LadderItem With {.Op = ContactOperator.Selector, .Label = "S2"}}
                    contBranch.AddRange(condition.Select(Function(it) New LadderItem With {
                        .Op = it.Op, .NormallyClosed = it.NormallyClosed, .Reference = it.Reference}))
                    parallel.Add(contBranch)
                End If
            Else
                Dim setBranch As New List(Of LadderItem) From {RelayNO(i - 1)}
                setBranch.AddRange(Sensors(seq, i - 2))
                parallel.Add(setBranch)
            End If
            parallel.Add(New List(Of LadderItem) From {RelayNO(i)})
            r.Sections.Add(parallel)
            r.Sections.Add(New List(Of List(Of LadderItem)) From {New List(Of LadderItem) From {RelayNC(If(i = n, 1, i + 1))}})
            rungs.Add(r)
        Next

        ' Solenoid rungs: each solenoid is energized by the step relays of the steps that need it.
        Dim solenoidSteps As New SortedDictionary(Of String, List(Of Integer))
        For i = 0 To n - 1
            For Each mv In seq.Steps(i)
                Dim sol = $"{seq.Number(mv.Letter)}M{If(mv.Extend, 1, 2)}"
                If Not solenoidSteps.ContainsKey(sol) Then solenoidSteps(sol) = New List(Of Integer)
                solenoidSteps(sol).Add(i + 1)
            Next
        Next
        For Each kv In solenoidSteps
            Dim r As New Rung With {.CoilKind = CoilKind.Solenoid, .CoilLabel = kv.Key}
            r.Sections.Add(kv.Value.Select(Function(k) New List(Of LadderItem) From {RelayNO(k)}).ToList())
            rungs.Add(r)
        Next

        LayoutLadder(c, rungs, CylY + 330)

        Dim sb As New StringBuilder()
        sb.AppendLine($"Sequence: {seq}   ({n} steps, {seq.Letters.Count} cylinder(s))")
        sb.AppendLine()
        sb.AppendLine("Pneumatic part")
        For Each letter In seq.Letters
            Dim k = seq.Number(letter)
            sb.AppendLine($"  Cylinder {letter} = {k}A, valve {k}V1 (5/2 double solenoid): {k}M1 extends, {k}M2 retracts.")
            sb.AppendLine($"  Sensors: {k}B1 = {letter} retracted, {k}B2 = {letter} extended.")
        Next
        sb.AppendLine()
        sb.AppendLine("Electrical part: relay step chain (one relay per step)")
        For i = 1 To n
            Dim cond As String
            If i = 1 Then
                cond = $"start S1{If(continuous, " (or continuous switch S2)", "")} AND {String.Join(" AND ", Sensors(seq, n - 1).Select(Function(s) s.Reference))} (initial position)"
            Else
                cond = $"K{i - 1} AND {String.Join(" AND ", Sensors(seq, i - 2).Select(Function(s) s.Reference))} (step {i - 1} done)"
            End If
            sb.AppendLine($"  K{i} on when {cond}; holds itself; off when K{If(i = n, 1, i + 1)} comes on.  Step {i}: {String.Join(" + ", seq.Steps(i - 1))}")
        Next
        For Each kv In solenoidSteps
            sb.AppendLine($"  Solenoid {kv.Key} = {String.Join(" OR ", kv.Value.Select(Function(k) $"K{k}"))}")
        Next
        sb.AppendLine()
        sb.AppendLine("Only one step relay is on at a time, so a valve never gets opposing signals (no signal overlap).")
        sb.AppendLine("Press Start (F9), then press S1" & If(continuous, " or switch on S2 for continuous running.", "."))
        Return New GeneratedCircuit With {.Circuit = c, .Explanation = sb.ToString()}
    End Function

    ''' <summary>Places the rungs left to right between their own +24 V and 0 V connections.</summary>
    Private Sub LayoutLadder(c As Circuit, rungs As List(Of Rung), top As Single)
        Const RowPitch As Single = 80
        Const BranchPitch As Single = 80
        Dim rows = rungs.Max(Function(r) r.Rows)
        Dim firstRowY = top + 60
        Dim coilY = firstRowY + (rows - 1) * RowPitch
        Dim x = LeftMargin - 100

        c.Add(New TextNote() With {.Text = "Electrical control circuit (24 V DC)", .FontSize = 9, .Bold = True}, x, top - 10)

        For Each r In rungs
            Dim plus = c.Add(New PowerTerminal(), x, firstRowY - 50)
            Dim inNode = plus.GetPort("1")
            Dim row = 0
            For s = 0 To r.Sections.Count - 1
                Dim section = r.Sections(s)
                Dim height = section.Max(Function(b) b.Count)
                Dim firsts As New List(Of ElectricContact)
                Dim lasts As New List(Of ElectricContact)
                For bIndex = 0 To section.Count - 1
                    Dim prev As ElectricContact = Nothing
                    For k = 0 To section(bIndex).Count - 1
                        Dim el = c.Add(section(bIndex)(k).Build(), x + bIndex * BranchPitch, firstRowY + (row + k) * RowPitch)
                        If prev Is Nothing Then firsts.Add(el) Else c.Connect(prev, "2", el, "1")
                        prev = el
                    Next
                    lasts.Add(prev)
                Next

                ' Where this section's branches join.
                Dim outNode As Port
                Dim isLast = s = r.Sections.Count - 1
                Dim nextSingle = Not isLast AndAlso r.Sections(s + 1).Count = 1
                Dim coil As ElectricCoil = Nothing
                If isLast Then
                    coil = c.Add(New ElectricCoil() With {.Kind = r.CoilKind, .Label = r.CoilLabel}, x, coilY)
                    outNode = coil.GetPort("A1")
                ElseIf section.Count = 1 AndAlso nextSingle Then
                    outNode = Nothing ' joined directly to the next contact below
                Else
                    Dim j = c.Add(New Junction() With {.IsElectric = True}, x + 20, firstRowY + (row + height) * RowPitch - 10)
                    outNode = j.GetPort("1")
                End If

                For Each f In firsts
                    c.Connect(inNode, f.GetPort("1"))
                Next
                If outNode IsNot Nothing Then
                    Dim joinY = outNode.WorldPos().Y - If(TypeOf outNode.Owner Is Junction, 0, 10)
                    For Each l In lasts
                        Dim t = c.Connect(l.GetPort("2"), outNode)
                        If t IsNot Nothing AndAlso t.MidAxis() = "Y" Then t.Mid = joinY
                    Next
                    inNode = outNode
                Else
                    inNode = lasts(0).GetPort("2")
                End If
                row += height

                If coil IsNot Nothing Then
                    Dim zero = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, x, coilY + 80)
                    c.Connect(coil, "A2", zero, "1")
                End If
            Next
            x += r.MaxBranches * BranchPitch + 30
        Next
    End Sub

    ' ===================================================================== pneumatic cascade

    Private Function GenerateCascade(seq As MotionSequence, continuous As Boolean) As GeneratedCircuit
        If seq.HasParallelSteps Then
            Throw New FormatException("The cascade method needs one movement per step. Remove the brackets, or use the electro-pneumatic method.")
        End If
        Dim moves = seq.AllMoves().ToList()
        For Each letter In seq.Letters
            If moves.Where(Function(m) m.Letter = letter).Count() <> 2 Then
                Throw New FormatException($"The cascade method supports each cylinder extending and retracting once per cycle; {letter} moves more often. Use the electro-pneumatic method.")
            End If
        Next

        ' Split into groups in which no cylinder appears twice.
        Dim groups As New List(Of List(Of Integer)) ' indexes into moves
        Dim current As New List(Of Integer)
        For i = 0 To moves.Count - 1
            Dim letter = moves(i).Letter
            If current.Any(Function(k) moves(k).Letter = letter) Then
                groups.Add(current)
                current = New List(Of Integer)
            End If
            current.Add(i)
        Next
        groups.Add(current)
        ' If the last and first groups share no cylinder they become one group (fewer cascade valves).
        Dim merged = False
        If groups.Count > 2 Then
            Dim firstG = groups(0), lastG = groups(groups.Count - 1)
            If Not firstG.Any(Function(a) lastG.Any(Function(b) moves(a).Letter = moves(b).Letter)) Then
                lastG.AddRange(firstG)
                groups.RemoveAt(0)
                merged = True
            End If
        End If
        Dim g = groups.Count ' group lines 1..g; line g is active at rest
        Dim groupOf(moves.Count - 1) As Integer
        For gi = 0 To g - 1
            For Each k In groups(gi)
                groupOf(k) = gi + 1
            Next
        Next

        Dim c As New Circuit()
        AddTitle(c, $"Sequence {seq}   —   pneumatic cascade control, {g} groups")
        Dim units = BuildPowerSection(c, seq, electric:=False)

        ' Layout rows: power section on top, then one signal lane per movement, the roller valves,
        ' the group lines, and the cascade valves at the bottom.
        Dim laneY0 = CylY + 290
        Dim rollerY = laneY0 + moves.Count * 10 + 40
        Dim lineY0 = rollerY + 120
        Dim lineGap = 30.0F
        Dim cascadeY = lineY0 + g * lineGap + 50

        ' Group line consumers, joined along a horizontal manifold per line.
        Dim lineTaps As New Dictionary(Of Integer, List(Of Port))
        For gi = 1 To g
            lineTaps(gi) = New List(Of Port)
        Next

        ' Cascade valves V(k), k = 1..g-1 (labelled 0V1, 0V2...). V(k).2 feeds line g-k+1, V(g-1).4 feeds line 1,
        ' V(k).1 is supplied from V(k-1).4 (V1 from the main supply).
        Dim cascade As New List(Of Valve52)
        Dim cx = LeftMargin - 90
        For k = 1 To g - 1
            cascade.Add(c.Add(Library.V52(ValveActuator.Pilot, ValveReturn.Pilot), cx + (k - 1) * 260, cascadeY, $"0V{k}"))
        Next
        Dim mainSupply = c.Add(New AirSupply(), cascade(0).X + 90, cascadeY + 100, "0Z")
        c.Connect(mainSupply, "1", cascade(0), "1")
        For k = 2 To g - 1
            c.Connect(cascade(k - 2), "4", cascade(k - 1), "1")
        Next
        For k = 1 To g - 1
            lineTaps(g - k + 1).Add(cascade(k - 1).GetPort("2"))
        Next
        lineTaps(1).Add(cascade(g - 2).GetPort("4"))

        ' Pilot ports that select each group line.
        Dim selectPorts As New Dictionary(Of Integer, List(Of Port))
        selectPorts(1) = cascade.Select(Function(v) v.GetPort("14")).ToList()
        For j = 2 To g
            selectPorts(j) = New List(Of Port) From {cascade(g - j).GetPort("12")}
        Next

        Dim pilotFor = Function(mv As SequenceMove) units(mv.Letter).Valve.GetPort(If(mv.Extend, "14", "12"))

        ' Start valve, turned upside down so its input faces the signal lanes above.
        Dim startValve = c.Add(Library.V32(If(continuous, ValveActuator.Selector, ValveActuator.PushButton)), LeftMargin - 110 + 120, rollerY + 60, "1S0")
        startValve.Rotation = 2
        Dim rollerX = LeftMargin + 40

        Dim sb As New StringBuilder()
        sb.AppendLine($"Sequence: {seq}")
        sb.AppendLine($"Groups:   {String.Join("  /  ", groups.Select(Function(gr, gi) $"{RomanNumeral(gi + 1)}: " & String.Join(" ", gr.Select(Function(k) moves(k)))))}")
        If merged Then sb.AppendLine("          (the last and first groups were combined, saving one cascade valve)")
        sb.AppendLine($"Cascade valves: {g - 1} (0V1..0V{g - 1});  group line {RomanNumeral(g)} is active at rest.")
        sb.AppendLine()
        sb.AppendLine("Signals")

        ' One roller valve per movement, sensing that movement's end position. Its signal runs along
        ' its own horizontal lane so lines never run along another valve's pilot.
        ' Vertical tubes that cross the roller row: group lines up to the pilots they feed directly,
        ' and signal lanes down to the cascade valve pilots. Rollers are placed clear of them.
        Dim crossingX As New List(Of Single)
        For gi = 1 To g
            crossingX.Add(TapX(pilotFor(moves(groups(gi - 1)(0)))))
        Next
        For Each v In cascade
            crossingX.Add(TapX(v.GetPort("14")))
            crossingX.Add(TapX(v.GetPort("12")))
        Next
        Dim nextRollerX = rollerX

        For j = 0 To moves.Count - 1
            Dim mv = moves(j)
            Dim nxt = (j + 1) Mod moves.Count
            Dim mark = MarkName(seq, mv, "S")
            Dim placeX = nextRollerX
            While crossingX.Any(Function(cxp) cxp > placeX - 12 AndAlso cxp < placeX + 132)
                placeX += 10
            End While
            nextRollerX = placeX + 170
            Dim roller = c.Add(Library.V32(ValveActuator.RollerLever), placeX, rollerY, mark)
            roller.TriggerMark = mark
            lineTaps(groupOf(j)).Add(roller.GetPort("1"))
            Dim laneY = laneY0 + j * 10
            Dim output = roller.GetPort("2")
            Dim viaStart = nxt = 0
            Dim targets As List(Of Port)
            Dim targetText As String
            If groupOf(nxt) <> groupOf(j) Then
                targets = selectPorts(groupOf(nxt))
                targetText = $"switches to group {RomanNumeral(groupOf(nxt))}"
            Else
                targets = New List(Of Port) From {pilotFor(moves(nxt))}
                targetText = $"starts {moves(nxt)}"
            End If
            If viaStart Then
                ' End of the cycle: the signal passes through the start valve.
                ConnectViaLane(c, output, {startValve.GetPort("1")}, laneY)
                ConnectViaLane(c, startValve.GetPort("2"), targets, rollerY + 80)
            Else
                ConnectViaLane(c, output, targets, laneY)
            End If
            sb.AppendLine($"  {mark} ({mv} done), fed from line {RomanNumeral(groupOf(j))}{If(viaStart, " via start valve 1S0", "")}: {targetText}")
        Next
        ' The first movement of every group is started by its group line (in a combined group the
        ' cycle start in the middle is handled by the start valve above).
        For gi = 1 To g
            Dim firstMove = groups(gi - 1)(0)
            lineTaps(gi).Add(pilotFor(moves(firstMove)))
            sb.AppendLine($"  Line {RomanNumeral(gi)} directly starts {moves(firstMove)}")
        Next

        ' Build the group line manifolds. Pilots are tapped just outside the valve so the tube
        ' enters the pilot from the side.
        For gi = 1 To g
            Dim y = lineY0 + (gi - 1) * lineGap
            Dim taps = lineTaps(gi).Select(Function(p) New With {.Port = p, .X = TapX(p)}).OrderBy(Function(t) t.X).ToList()
            Dim prev As Junction = Nothing
            For Each tp In taps
                Dim j = c.Add(New Junction(), tp.X, y)
                c.Connect(j.GetPort("1"), tp.Port)
                If prev IsNot Nothing Then c.Connect(prev.GetPort("1"), j.GetPort("1"))
                prev = j
            Next
            If taps.Count > 0 Then
                c.Add(New TextNote() With {.Text = RomanNumeral(gi), .FontSize = 8, .Bold = True}, taps(0).X - 30, y - 8)
            End If
        Next

        sb.AppendLine()
        sb.AppendLine("Each roller valve only receives air from its own group line, and only one line is pressurized at a time,")
        sb.AppendLine("so no valve ever receives opposing pilot signals (signal overlap is eliminated).")
        sb.AppendLine(If(continuous, "Switch the selector valve 1S0 on to run continuously.", "Press Start (F9), then press the start valve 1S0."))
        Return New GeneratedCircuit With {.Circuit = c, .Explanation = sb.ToString()}
    End Function

    ''' <summary>X position from which a tube approaches a port: beside side-facing pilot ports, else straight.</summary>
    Private Function TapX(p As Port) As Single
        Dim d = p.WorldDir()
        Return p.WorldPos().X + If(Math.Abs(d.X) > 0.5F, d.X * 20, 0)
    End Function

    ''' <summary>
    ''' Connects a signal source to one or more targets through bend points on a horizontal lane,
    ''' so the tube runs: source -> lane -> along the lane -> next to each target -> into the target.
    ''' </summary>
    Private Sub ConnectViaLane(c As Circuit, source As Port, targets As IEnumerable(Of Port), laneY As Single)
        Dim prev = source
        For Each t In targets.OrderBy(Function(p) Math.Abs(TapX(p) - source.WorldPos().X))
            Dim bend = c.Add(New Junction() With {.IsElectric = source.Kind = PortKind.Electric}, TapX(t), laneY)
            c.Connect(prev, bend.GetPort("1"))
            c.Connect(bend.GetPort("1"), t)
            prev = bend.GetPort("1")
        Next
    End Sub

    Private Function RomanNumeral(n As Integer) As String
        Dim numerals = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV", "XV", "XVI"}
        Return If(n >= 1 AndAlso n <= numerals.Length, numerals(n - 1), n.ToString())
    End Function
End Module
