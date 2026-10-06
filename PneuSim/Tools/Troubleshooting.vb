Imports System.Text

''' <summary>
''' A troubleshooting exercise: one hidden fault in the project that the student has to find by
''' operating and measuring the circuit. Counts the checks, hints and wrong guesses and scores the
''' result. The fault itself lives in the project (so it survives undo, saving and loading).
''' </summary>
Public Class TroubleshootExercise

    Public Sub New()
        Started = Date.Now
    End Sub

    Public Property Started As Date
    Public Property Finished As Date?
    Public Property HintsUsed As Integer
    Public Property WrongGuesses As Integer
    Public Property Solved As Boolean
    Public Property GaveUp As Boolean
    ''' <summary>True if the fault type was also named correctly.</summary>
    Public Property KindRight As Boolean

    ''' <summary>The components and tubes measured, in order (each counted once).</summary>
    Public ReadOnly Property Checks As New List(Of String)

    ''' <summary>Everything that happened, for the diagnosis log.</summary>
    Public ReadOnly Property Log As New List(Of String)

    Public ReadOnly Property IsOver As Boolean
        Get
            Return Solved OrElse GaveUp
        End Get
    End Property

    Private Sub Note(text As String)
        Log.Add($"{(Date.Now - Started).TotalSeconds,5:0}s  {text}")
    End Sub

    ' ------------------------------------------------------------------ the fault

    ''' <summary>The hidden fault in the project: a component or a tube, or Nothing.</summary>
    Public Shared Function FindHidden(project As Project) As Object
        For Each pg In project.Pages
            Dim el = pg.Circuit.Elements.FirstOrDefault(Function(e) e.Fault <> FaultKind.None AndAlso e.FaultHidden)
            If el IsNot Nothing Then Return el
            Dim t = pg.Circuit.Tubes.FirstOrDefault(Function(x) x.Fault <> FaultKind.None AndAlso x.FaultHidden)
            If t IsNot Nothing Then Return t
        Next
        Return Nothing
    End Function

    Public Shared Function FaultOf(target As Object) As FaultKind
        If TypeOf target Is CircuitElement Then Return DirectCast(target, CircuitElement).Fault
        If TypeOf target Is Tube Then Return DirectCast(target, Tube).Fault
        Return FaultKind.None
    End Function

    Public Shared Function PossibleFaultsOf(target As Object) As FaultKind()
        If TypeOf target Is CircuitElement Then Return DirectCast(target, CircuitElement).PossibleFaults()
        If TypeOf target Is Tube Then Return DirectCast(target, Tube).PossibleFaults()
        Return Array.Empty(Of FaultKind)()
    End Function

    Public Shared Function DescribeFault(target As Object, kind As FaultKind) As String
        If TypeOf target Is CircuitElement Then Return DirectCast(target, CircuitElement).FaultDescription(kind)
        If TypeOf target Is Tube Then Return DirectCast(target, Tube).FaultDescription(kind)
        Return Faults.Describe(kind)
    End Function

    Public Shared Sub SetFault(target As Object, kind As FaultKind, hidden As Boolean)
        If TypeOf target Is CircuitElement Then
            Dim e = DirectCast(target, CircuitElement)
            e.Fault = kind : e.FaultHidden = hidden AndAlso kind <> FaultKind.None
        ElseIf TypeOf target Is Tube Then
            Dim t = DirectCast(target, Tube)
            t.Fault = kind : t.FaultHidden = hidden AndAlso kind <> FaultKind.None
        End If
    End Sub

    ''' <summary>A readable name for a component or tube.</summary>
    Public Shared Function NameOf_(target As Object) As String
        If TypeOf target Is CircuitElement Then
            Dim e = DirectCast(target, CircuitElement)
            Return If(String.IsNullOrWhiteSpace(e.Label), e.DisplayName, $"{e.Label} ({e.DisplayName})")
        End If
        If TypeOf target Is Tube Then
            Dim t = DirectCast(target, Tube)
            Dim what = If(t.IsElectric, "wire", If(t.A.Kind = PortKind.Hydraulic, "hose", "tube"))
            Return $"{what} {EndName(t.A)} – {EndName(t.B)}"
        End If
        Return "?"
    End Function

    Private Shared Function EndName(p As Port) As String
        Dim owner = If(String.IsNullOrWhiteSpace(p.Owner.Label), p.Owner.DisplayName, p.Owner.Label)
        Return If(TypeOf p.Owner Is Junction, "junction", $"{owner}:{p.Name}")
    End Function

    Public Shared Sub RemoveAllFaults(project As Project)
        For Each pg In project.Pages
            For Each e In pg.Circuit.Elements
                e.Fault = FaultKind.None : e.FaultHidden = False
            Next
            For Each t In pg.Circuit.Tubes
                t.Fault = FaultKind.None : t.FaultHidden = False
            Next
        Next
    End Sub

    ''' <summary>Parts that can be given a fault for an exercise: connected components and tubes.</summary>
    Public Shared Function Candidates(project As Project) As List(Of Object)
        Dim list As New List(Of Object)
        For Each pg In project.Pages
            For Each e In pg.Circuit.Elements
                If e.PossibleFaults().Length = 0 Then Continue For
                If e.Ports.Count > 0 AndAlso e.Ports.All(Function(p) p.ConnectionCount = 0) Then Continue For
                list.Add(e)
            Next
            ' Tubes into instruments only change a reading; leave those out.
            For Each t In pg.Circuit.Tubes
                If TypeOf t.A.Owner Is PressureGauge OrElse TypeOf t.B.Owner Is PressureGauge Then Continue For
                list.Add(t)
            Next
        Next
        Return list
    End Function

    ''' <summary>
    ''' Removes all faults and hides one random fault in the project. Returns the new exercise,
    ''' or Nothing if the circuit has nothing that can fail.
    ''' </summary>
    Public Shared Function StartRandom(project As Project, rnd As Random) As TroubleshootExercise
        Dim pool = Candidates(project)
        If pool.Count = 0 Then Return Nothing
        RemoveAllFaults(project)
        Dim target = pool(rnd.Next(pool.Count))
        Dim kinds = PossibleFaultsOf(target)
        SetFault(target, kinds(rnd.Next(kinds.Length)), hidden:=True)
        Dim ex As New TroubleshootExercise()
        ex.Note("Exercise started: one component or tube has a hidden fault.")
        Return ex
    End Function

    ' ------------------------------------------------------------------ during the exercise

    ''' <summary>Records a measurement of a component or tube. Returns True if it is a new check.</summary>
    Public Function RecordCheck(target As Object) As Boolean
        If IsOver Then Return False
        Dim name = NameOf_(target)
        If Checks.Contains(name) Then Return False
        Checks.Add(name)
        Note($"Measured {name}")
        Return True
    End Function

    ''' <summary>The next hint: first the area, then the neighbours, then the cause.</summary>
    Public Function NextHint(project As Project) As String
        Dim target = FindHidden(project)
        If target Is Nothing Then Return "There is no hidden fault in this circuit."
        HintsUsed += 1
        Dim text As String
        Select Case HintsUsed
            Case 1 : text = AreaHint(project, target)
            Case 2 : text = NeighbourHint(project, target)
            Case Else
                text = $"The fault is in {NameOf_(target)}: {DescribeFault(target, FaultOf(target)).ToLowerInvariant()}."
                HintsUsed = Math.Min(HintsUsed, 3)
        End Select
        Note($"Hint {HintsUsed}: {text}")
        Return text
    End Function

    Private Shared Function AreaHint(project As Project, target As Object) As String
        Dim page = 0, x As Single, medium As PortKind
        If TypeOf target Is CircuitElement Then
            Dim e = DirectCast(target, CircuitElement)
            page = project.Pages.FindIndex(Function(p) p.Circuit.Elements.Contains(e))
            Dim b = e.WorldBounds()
            x = b.X + b.Width / 2
            medium = If(e.Ports.Count = 0, PortKind.Pneumatic, e.Ports(0).Kind)
        Else
            Dim t = DirectCast(target, Tube)
            page = project.Pages.FindIndex(Function(p) p.Circuit.Tubes.Contains(t))
            x = (t.A.WorldPos().X + t.B.WorldPos().X) / 2
            medium = t.A.Kind
        End If
        Dim part = If(medium = PortKind.Electric, "the electrical control circuit", If(medium = PortKind.Hydraulic, "the hydraulic circuit", "the pneumatic circuit"))
        Dim where = $"column {Project.ColumnAt(x)}" & If(project.Pages.Count > 1, $" of page {page + 1}", "")
        Return $"Look in {part}, around {where}. Compare what should happen with what does happen there."
    End Function

    Private Shared Function NeighbourHint(project As Project, target As Object) As String
        If TypeOf target Is Tube Then
            Dim t = DirectCast(target, Tube)
            Return $"It is a {If(t.IsElectric, "wire", "line")} connected to {EndName(t.A)}. Measure at both ends of each line there."
        End If
        Dim e = DirectCast(target, CircuitElement)
        Dim near As New List(Of String)
        For Each p In e.Ports
            For Each o In TubeNeighbours(project, p)
                Dim n = If(String.IsNullOrWhiteSpace(o.Label), o.DisplayName, o.Label)
                If Not near.Contains(n) Then near.Add(n)
            Next
        Next
        If near.Count = 0 Then Return $"It is a {e.DisplayName.ToLowerInvariant()}."
        Return $"The faulty part is directly connected to {String.Join(", ", near)}. Check the signals going in and out of it."
    End Function

    ''' <summary>Components joined to a port by tubes (through junctions and page connectors).</summary>
    Private Shared Function TubeNeighbours(project As Project, start As Port) As List(Of CircuitElement)
        Dim result As New List(Of CircuitElement)
        Dim tubes = project.Pages.SelectMany(Function(pg) pg.Circuit.Tubes).ToList()
        Dim seen As New HashSet(Of Port) From {start}
        Dim queue As New Queue(Of Port)
        queue.Enqueue(start)
        While queue.Count > 0
            Dim p = queue.Dequeue()
            For Each t In tubes
                Dim other = If(t.A Is p, t.B, If(t.B Is p, t.A, Nothing))
                If other Is Nothing OrElse Not seen.Add(other) Then Continue For
                If TypeOf other.Owner Is Junction OrElse TypeOf other.Owner Is PageConnector Then
                    queue.Enqueue(other)
                ElseIf Not result.Contains(other.Owner) Then
                    result.Add(other.Owner)
                End If
            Next
        End While
        Return result
    End Function

    ''' <summary>The student names a component or tube (and a fault type). Returns whether it was right and a message.</summary>
    Public Function Guess(project As Project, target As Object, kind As FaultKind) As (Correct As Boolean, Message As String)
        Dim actual = FindHidden(project)
        If actual Is Nothing Then Return (False, "There is no hidden fault in this circuit.")
        If target IsNot actual Then
            WrongGuesses += 1
            Note($"Wrong guess: {NameOf_(target)}")
            Return (False, $"No, {NameOf_(target)} is working correctly. Keep measuring.")
        End If
        Solved = True
        KindRight = kind = FaultOf(actual)
        Finished = Date.Now
        Note($"Found it: {NameOf_(target)}")
        Dim what = DescribeFault(actual, FaultOf(actual))
        SetFault(actual, FaultOf(actual), hidden:=False)
        Return (True, If(KindRight, $"Correct! {NameOf_(target)}: {what}.",
                         $"Right part! {NameOf_(target)} is faulty, but the fault is: {what}."))
    End Function

    ''' <summary>Shows the fault.</summary>
    Public Function GiveUp(project As Project) As String
        Dim actual = FindHidden(project)
        GaveUp = True
        Finished = Date.Now
        If actual Is Nothing Then Return "There is no hidden fault in this circuit."
        Note($"Gave up. The fault was in {NameOf_(actual)}")
        Dim text = $"The fault was in {NameOf_(actual)}: {DescribeFault(actual, FaultOf(actual))}."
        SetFault(actual, FaultOf(actual), hidden:=False)
        Return text
    End Function

    ''' <summary>Points out of 100: a few checks are free; hints, extra checks and wrong guesses cost points.</summary>
    Public Function Score() As Integer
        If Not Solved Then Return 0
        Dim points = 100 - 15 * HintsUsed - 20 * WrongGuesses - 4 * Math.Max(0, Checks.Count - 4) - If(KindRight, 0, 10)
        Return Math.Max(0, Math.Min(100, points))
    End Function

    ''' <summary>Text for the status line of the troubleshooting panel.</summary>
    Public Function StatusText() As String
        Dim t = If(Finished, Date.Now) - Started
        Dim head As String
        If Solved Then
            head = $"Solved in {t.TotalMinutes:0}:{t.Seconds:00} min — score {Score()} / 100"
        ElseIf GaveUp Then
            head = "Exercise ended (fault shown)"
        Else
            head = $"Find the fault!  Time {t.TotalMinutes:0}:{t.Seconds:00}"
        End If
        Return $"{head}    Checks: {Checks.Count}   Hints: {HintsUsed}   Wrong guesses: {WrongGuesses}"
    End Function

    ''' <summary>The diagnosis report (also saved by the panel).</summary>
    Public Function Report(projectTitle As String) As String
        Dim sb As New StringBuilder()
        sb.AppendLine($"PneuSim troubleshooting report — {projectTitle}")
        sb.AppendLine($"Date: {Started:yyyy-MM-dd HH:mm}")
        sb.AppendLine(StatusText())
        sb.AppendLine()
        sb.AppendLine("Diagnosis log:")
        For Each l In Log
            sb.AppendLine("  " & l)
        Next
        Return sb.ToString()
    End Function
End Class

''' <summary>Settings that can be changed with sliders while the simulation runs.</summary>
Public Module LiveTuning

    Public Class Knob
        Public Property PropertyName As String
        Public Property Caption As String
        Public Property Min As Double
        Public Property Max As Double
    End Class

    Private Function K(name As String, caption As String, min As Double, max As Double) As Knob
        Return New Knob With {.PropertyName = name, .Caption = caption, .Min = min, .Max = max}
    End Function

    ''' <summary>The live settings of a component (empty if it has none).</summary>
    Public Function KnobsFor(e As CircuitElement) As List(Of Knob)
        Dim list As New List(Of Knob)
        Select Case True
            Case TypeOf e Is AirSupply : list.Add(K("Pressure", "Supply pressure (bar)", 0.5, 10))
            Case TypeOf e Is PressureRegulator
                list.Add(If(DirectCast(e, PressureRegulator).Hydraulic, K("Setting", "Reduced pressure (bar)", 1, 250), K("Setting", "Output pressure (bar)", 0.2, 10)))
            Case TypeOf e Is FlowControlValve : list.Add(K("OpeningPercent", "Throttle opening (%)", 1, 100))
            Case TypeOf e Is CompensatedFlowControl : list.Add(K("FlowLpm", "Flow (l/min)", 0.5, 50))
            Case TypeOf e Is SemiRotaryActuator : list.Add(K("StrokeTime", "Swivel time (s)", 0.1, 10))
            Case TypeOf e Is CylinderBase
                Dim hydraulic = e.Ports(0).Kind = PortKind.Hydraulic
                If Not hydraulic Then list.Add(K("StrokeTime", "Stroke time, ideal mode (s)", 0.1, 10))
                list.Add(K("LoadForceN", "Load force (N)", -2000, If(hydraulic, 50000, 3000)))
                If Not hydraulic Then list.Add(K("LoadMassKg", "Moving mass (kg)", 0, 200))
                list.Add(K("FrictionN", "Friction (N)", 0, 500))
            Case TypeOf e Is DirectionalValve
                Dim v = DirectCast(e, DirectionalValve)
                If v.ShowProperty("DelaySeconds") Then list.Add(K("DelaySeconds", "Delay (s)", 0, 20))
                If v.ShowProperty("SwitchingPressure") Then list.Add(K("SwitchingPressure", "Switching pressure (bar)", 0.5, 10))
            Case TypeOf e Is ElectricCoil
                If e.ShowProperty("DelaySeconds") Then list.Add(K("DelaySeconds", "Delay (s)", 0, 20))
            Case TypeOf e Is AirMotor : list.Add(K("NominalSpeed", "Speed at full flow (rpm)", 0, 3000))
            Case TypeOf e Is HydraulicPump : list.Add(K("FlowLpm", "Pump flow (l/min)", 0.5, 100))
            Case TypeOf e Is ReliefValve : list.Add(K("Setting", "Opening pressure (bar)", 5, 250))
            Case TypeOf e Is HydraulicMotor : list.Add(K("LoadPressureBar", "Load pressure (bar)", 0, 200))
            Case TypeOf e Is Compressor : list.Add(K("DeliveryNlMin", "Delivery (NL/min)", 10, 2000))
            Case TypeOf e Is PressureSwitch : list.Add(K("Setting", "Switching pressure (bar)", -0.9, 10))
            Case TypeOf e Is VacuumGenerator : list.Add(K("VacuumLevel", "Vacuum (bar)", -0.95, -0.1))
        End Select
        ' Only keep settings the component really has.
        list.RemoveAll(Function(kn) e.GetType().GetProperty(kn.PropertyName) Is Nothing)
        Return list
    End Function

    Public Function GetValue(e As CircuitElement, knob As Knob) As Double
        Return Convert.ToDouble(e.GetType().GetProperty(knob.PropertyName).GetValue(e))
    End Function

    Public Sub SetValue(e As CircuitElement, knob As Knob, value As Double)
        Dim prop = e.GetType().GetProperty(knob.PropertyName)
        prop.SetValue(e, Convert.ChangeType(If(prop.PropertyType Is GetType(Integer), Math.Round(value), value), prop.PropertyType))
    End Sub

    ''' <summary>The values shown in the inspector: the component's own values and every port.</summary>
    Public Function Inspect(target As Object) As List(Of (Name As String, Value As String))
        Dim list As New List(Of (Name As String, Value As String))
        Dim portText = Function(p As Port)
                           If p.Kind = PortKind.Electric Then
                               Return If(p.State = PortState.Pressurized, "+24 V", If(p.State = PortState.Exhausted, "0 V", "not connected to a supply"))
                           End If
                           Dim state = If(p.State = PortState.Pressurized, "supplied", If(p.State = PortState.Exhausted, "vented", "closed in"))
                           Return $"{p.Pressure:0.00} bar ({state})"
                       End Function
        If TypeOf target Is CircuitElement Then
            Dim e = DirectCast(target, CircuitElement)
            list.AddRange(e.InspectValues())
            For Each p In e.Ports
                list.Add(($"Port {p.Name}", portText(p)))
            Next
        ElseIf TypeOf target Is Tube Then
            Dim t = DirectCast(target, Tube)
            list.Add(($"End at {t.A.Owner.Label}:{t.A.Name}", portText(t.A)))
            list.Add(($"End at {t.B.Owner.Label}:{t.B.Name}", portText(t.B)))
        End If
        Return list
    End Function
End Module
