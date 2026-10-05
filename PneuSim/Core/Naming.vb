Imports System.Text.RegularExpressions

''' <summary>
''' Gives new components unique names (1A, 2A, 1V1, 1V2, S1, S2, K1, 1M1 ...) and renames
''' pasted copies so they do not silently share coils, buttons or position marks with the originals.
''' </summary>
Public Module Naming

    ''' <summary>Labels and solenoid names already in use.</summary>
    Public Function UsedNames(elements As IEnumerable(Of CircuitElement)) As HashSet(Of String)
        Dim used As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        For Each e In elements
            If Not String.IsNullOrWhiteSpace(e.Label) Then used.Add(e.Label.Trim())
            Dim v = TryCast(e, DirectionalValve)
            If v IsNot Nothing Then
                If v.Actuator = ValveActuator.Solenoid AndAlso Not String.IsNullOrWhiteSpace(v.SolenoidLabel) Then used.Add(v.SolenoidLabel.Trim())
                If v.ReturnType = ValveReturn.Solenoid AndAlso Not String.IsNullOrWhiteSpace(v.ReturnSolenoidLabel) Then used.Add(v.ReturnSolenoidLabel.Trim())
            End If
        Next
        Return used
    End Function

    ''' <summary>Position marks of all cylinders.</summary>
    Public Function UsedMarks(elements As IEnumerable(Of CircuitElement)) As HashSet(Of String)
        Dim used As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        For Each c In elements.OfType(Of CylinderBase)()
            If Not String.IsNullOrWhiteSpace(c.RetractedMark) Then used.Add(c.RetractedMark.Trim())
            If Not String.IsNullOrWhiteSpace(c.ExtendedMark) Then used.Add(c.ExtendedMark.Trim())
        Next
        Return used
    End Function

    ''' <summary>The next free name in a family: "1A" gives 2A, 3A ...; "1V1" gives 1V2 ...; "K1" gives K2 ...</summary>
    Public Function NextFree(start As String, used As ICollection(Of String)) As String
        If Not used.Contains(start) Then Return start
        Dim m = Regex.Match(start, "^(.*?)(\d+)(\D*)$")
        Dim prefix = If(m.Success, m.Groups(1).Value, start)
        Dim number = If(m.Success, Integer.Parse(m.Groups(2).Value), 1)
        Dim suffix = If(m.Success, m.Groups(3).Value, "")
        Do
            number += 1
            Dim candidate = prefix & number & suffix
            If Not used.Contains(candidate) Then Return candidate
        Loop
    End Function

    ''' <summary>Names a component that has just been placed from the library.</summary>
    Public Sub NameNewElement(e As CircuitElement, others As IEnumerable(Of CircuitElement))
        Dim existing = others.Where(Function(o) o IsNot e).ToList()
        Dim used = UsedNames(existing)
        Select Case True
            Case TypeOf e Is CylinderBase
                e.Label = NextFree("1A", used)
            Case TypeOf e Is DirectionalValve
                Dim v = DirectCast(e, DirectionalValve)
                Dim signal = v.Actuator = ValveActuator.PushButton OrElse v.Actuator = ValveActuator.Selector OrElse v.Actuator = ValveActuator.RollerLever
                If String.IsNullOrWhiteSpace(v.Label) Then v.Label = NextFree(If(signal, "1S1", "1V1"), used)
                ' Solenoids: first take coils already drawn that no valve uses yet, then new names.
                Dim freeCoils = existing.OfType(Of ElectricCoil)().
                    Where(Function(k) k.Kind = CoilKind.Solenoid AndAlso Not String.IsNullOrWhiteSpace(k.Label) AndAlso
                                      Not existing.OfType(Of DirectionalValve)().Any(Function(o) o.UsesSolenoid(k.Label))).
                    Select(Function(k) k.Label.Trim()).ToList()
                If v.Actuator = ValveActuator.Solenoid Then
                    v.SolenoidLabel = TakeSolenoid(freeCoils, used)
                End If
                If v.ReturnType = ValveReturn.Solenoid Then
                    v.ReturnSolenoidLabel = TakeSolenoid(freeCoils, used)
                End If
            Case TypeOf e Is ElectricCoil
                Dim k = DirectCast(e, ElectricCoil)
                Select Case k.Kind
                    Case CoilKind.Solenoid
                        ' A valve solenoid that has no coil yet gets this coil.
                        Dim coilNames = existing.OfType(Of ElectricCoil)().Where(Function(x) x.Kind = CoilKind.Solenoid).
                            Select(Function(x) x.Label?.Trim()).ToList()
                        Dim waiting = existing.OfType(Of DirectionalValve)().
                            SelectMany(Function(v) {If(v.Actuator = ValveActuator.Solenoid, v.SolenoidLabel, Nothing),
                                                    If(v.ReturnType = ValveReturn.Solenoid, v.ReturnSolenoidLabel, Nothing)}).
                            Where(Function(n) Not String.IsNullOrWhiteSpace(n) AndAlso
                                              Not coilNames.Contains(n.Trim(), StringComparer.OrdinalIgnoreCase)).
                            FirstOrDefault()
                        k.Label = If(waiting?.Trim(), NextFree("1M1", used))
                    Case CoilKind.Lamp
                        k.Label = NextFree("H1", used)
                    Case Else
                        k.Label = NextFree(If(String.IsNullOrWhiteSpace(k.Label), "K1", k.Label), used)
                End Select
            Case TypeOf e Is ElectricContact
                Dim c = DirectCast(e, ElectricContact)
                If c.Operator = ContactOperator.PushButton OrElse c.Operator = ContactOperator.Selector Then
                    c.Label = NextFree(If(String.IsNullOrWhiteSpace(c.Label), "S1", c.Label), used)
                ElseIf c.Operator = ContactOperator.Relay Then
                    ' Point a new relay contact at the most recently drawn relay.
                    Dim relay = existing.OfType(Of ElectricCoil)().LastOrDefault(Function(x) x.Kind <> CoilKind.Solenoid AndAlso x.Kind <> CoilKind.Lamp)
                    If relay IsNot Nothing AndAlso Not String.IsNullOrWhiteSpace(relay.Label) Then c.Reference = relay.Label
                End If
            Case TypeOf e Is PageConnector
                ' Connectors work in pairs: reuse a name that has no partner yet, otherwise a new one.
                Dim counts = existing.OfType(Of PageConnector)().Where(Function(x) Not String.IsNullOrWhiteSpace(x.Label)).
                    GroupBy(Function(x) x.Label.Trim(), StringComparer.OrdinalIgnoreCase).ToDictionary(Function(g) g.Key, Function(g) g.Count(), StringComparer.OrdinalIgnoreCase)
                Dim single_ = counts.FirstOrDefault(Function(kv) kv.Value = 1).Key
                e.Label = If(single_, NextFree("X1", used))
        End Select
    End Sub

    Private Function TakeSolenoid(freeCoils As List(Of String), used As HashSet(Of String)) As String
        If freeCoils.Count > 0 Then
            Dim name = freeCoils(0)
            freeCoils.RemoveAt(0)
            Return name
        End If
        Dim n = NextFree("1M1", used)
        used.Add(n)
        Return n
    End Function

    ''' <summary>
    ''' Renames pasted copies whose names clash with the rest of the project. Names that belong
    ''' together (a relay and its contacts, a valve and its solenoid coil, a cylinder's marks and
    ''' the sensors on them) are renamed consistently when they are pasted together.
    ''' </summary>
    Public Sub RenamePasted(pasted As IList(Of CircuitElement), others As IEnumerable(Of CircuitElement))
        Dim existing = others.Where(Function(o) Not pasted.Contains(o)).ToList()
        Dim takenNames = UsedNames(existing)
        Dim takenMarks = UsedMarks(existing)

        ' Names carried by the pasted parts themselves.
        Dim own = UsedNames(pasted.Where(Function(e) TypeOf e IsNot PageConnector))
        Dim nameMap As New Dictionary(Of String, String)(StringComparer.OrdinalIgnoreCase)
        Dim allNames As New HashSet(Of String)(takenNames.Concat(own), StringComparer.OrdinalIgnoreCase)
        For Each n In own.OrderBy(Function(x) x)
            If takenNames.Contains(n) Then
                Dim fresh = NextFree(n, allNames)
                allNames.Add(fresh)
                nameMap(n) = fresh
            End If
        Next
        Dim ownMarks = UsedMarks(pasted)
        Dim markMap As New Dictionary(Of String, String)(StringComparer.OrdinalIgnoreCase)
        Dim allMarks As New HashSet(Of String)(takenMarks.Concat(ownMarks), StringComparer.OrdinalIgnoreCase)
        For Each m In ownMarks.OrderBy(Function(x) x)
            If takenMarks.Contains(m) Then
                Dim fresh = NextFree(m, allMarks)
                allMarks.Add(fresh)
                markMap(m) = fresh
            End If
        Next

        Dim mapName = Function(n As String) If(n IsNot Nothing AndAlso nameMap.ContainsKey(n.Trim()), nameMap(n.Trim()), n)
        Dim mapMark = Function(n As String) If(n IsNot Nothing AndAlso markMap.ContainsKey(n.Trim()), markMap(n.Trim()), n)
        For Each e In pasted
            If TypeOf e Is PageConnector Then Continue For
            e.Label = mapName(e.Label)
            Select Case True
                Case TypeOf e Is DirectionalValve
                    Dim v = DirectCast(e, DirectionalValve)
                    v.SolenoidLabel = mapName(v.SolenoidLabel)
                    v.ReturnSolenoidLabel = mapName(v.ReturnSolenoidLabel)
                    v.TriggerMark = mapMark(v.TriggerMark)
                Case TypeOf e Is CylinderBase
                    Dim c = DirectCast(e, CylinderBase)
                    c.RetractedMark = mapMark(c.RetractedMark)
                    c.ExtendedMark = mapMark(c.ExtendedMark)
                Case TypeOf e Is ElectricContact
                    Dim k = DirectCast(e, ElectricContact)
                    k.Reference = If(k.Operator = ContactOperator.Relay, mapName(k.Reference), mapMark(k.Reference))
            End Select
        Next
    End Sub
End Module
