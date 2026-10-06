Imports System.ComponentModel
Imports System.Globalization
Imports System.Reflection
Imports System.Xml.Linq

''' <summary>A pneumatic tube or an electrical wire between two ports.</summary>
Public Class Tube
    Public Sub New(a As Port, b As Port)
        Me.A = a
        Me.B = b
    End Sub

    Public ReadOnly Property A As Port
    Public ReadOnly Property B As Port

    ''' <summary>
    ''' User-chosen position of the middle segment (X for a vertical middle segment, Y for a
    ''' horizontal one). Nothing means "half way".
    ''' </summary>
    Public Property Mid As Single?

    ''' <summary>A fault in the tube or wire (troubleshooting practice).</summary>
    Public Property Fault As FaultKind = FaultKind.None

    ''' <summary>True if the fault is part of a troubleshooting exercise and must not be shown.</summary>
    Public Property FaultHidden As Boolean

    ''' <summary>The faults a tube or wire can have.</summary>
    Public Function PossibleFaults() As FaultKind()
        Return If(IsElectric, {FaultKind.Blocked}, {FaultKind.Leak, FaultKind.Blocked})
    End Function

    Public Function FaultDescription(kind As FaultKind) As String
        If IsElectric Then Return If(kind = FaultKind.Blocked, "Wire broken (no connection)", Faults.Describe(kind))
        Dim what = If(A.Kind = PortKind.Hydraulic, "Hose", "Tube")
        Select Case kind
            Case FaultKind.Leak : Return $"{what} leaking (cut or loose fitting)"
            Case FaultKind.Blocked : Return $"{what} blocked (kinked or crushed)"
            Case Else : Return Faults.Describe(kind)
        End Select
    End Function

    Public ReadOnly Property IsElectric As Boolean
        Get
            Return A.Kind = PortKind.Electric
        End Get
    End Property

    Public ReadOnly Property IsPressurized As Boolean
        Get
            Return A.IsPressurized
        End Get
    End Property

    Public Function Route() As PointF()
        Return RouteBetween(A.WorldPos(), A.WorldDir(), B.WorldPos(), B.WorldDir(), Mid)
    End Function

    ''' <summary>Which coordinate <see cref="Mid"/> moves: "X", "Y", or Nothing if the route has no middle segment.</summary>
    Public Function MidAxis() As String
        Dim da = EffectiveDir(A.WorldPos(), A.WorldDir(), B.WorldPos())
        Dim db = EffectiveDir(B.WorldPos(), B.WorldDir(), A.WorldPos())
        Dim aHoriz = Math.Abs(da.X) > 0.5F, bHoriz = Math.Abs(db.X) > 0.5F
        If aHoriz AndAlso bHoriz Then Return "X"
        If Not aHoriz AndAlso Not bHoriz Then Return "Y"
        Return Nothing
    End Function

    ''' <summary>Direction a tube leaves a port; junctions pick the axis pointing towards the other end.</summary>
    Private Shared Function EffectiveDir(p As PointF, d As PointF, other As PointF) As PointF
        If d.X <> 0 OrElse d.Y <> 0 Then Return d
        Dim dx = other.X - p.X, dy = other.Y - p.Y
        If Math.Abs(dx) > Math.Abs(dy) Then Return New PointF(Math.Sign(dx), 0)
        Return New PointF(0, If(dy = 0, 1, Math.Sign(dy)))
    End Function

    ''' <summary>Builds an orthogonal (right-angled) path between two ports.</summary>
    Public Shared Function RouteBetween(a As PointF, da As PointF, b As PointF, db As PointF, Optional mid As Single? = Nothing) As PointF()
        Dim leadA = If(da.X = 0 AndAlso da.Y = 0, 0.0F, 10.0F)
        Dim leadB = If(db.X = 0 AndAlso db.Y = 0, 0.0F, 10.0F)
        da = EffectiveDir(a, da, b)
        db = EffectiveDir(b, db, a)
        Dim p1 As New PointF(a.X + da.X * leadA, a.Y + da.Y * leadA)
        Dim p3 As New PointF(b.X + db.X * leadB, b.Y + db.Y * leadB)
        Dim aHoriz = Math.Abs(da.X) > 0.5F
        Dim bHoriz = Math.Abs(db.X) > 0.5F
        Dim pts As New List(Of PointF) From {a, p1}

        If aHoriz AndAlso bHoriz Then
            Dim midX = If(mid, (p1.X + p3.X) / 2)
            pts.Add(New PointF(midX, p1.Y))
            pts.Add(New PointF(midX, p3.Y))
        ElseIf Not aHoriz AndAlso Not bHoriz Then
            Dim midY = If(mid, (p1.Y + p3.Y) / 2)
            pts.Add(New PointF(p1.X, midY))
            pts.Add(New PointF(p3.X, midY))
        ElseIf aHoriz Then
            pts.Add(New PointF(p3.X, p1.Y))
        Else
            pts.Add(New PointF(p1.X, p3.Y))
        End If
        pts.Add(p3)
        pts.Add(b)
        Dim result = Simplify(pts)
        ' Coincident ends: keep a (zero length) segment so drawing code always gets two points.
        If result.Length < 2 Then Return {a, b}
        Return result
    End Function

    Private Shared Function Simplify(pts As List(Of PointF)) As PointF()
        Dim result As New List(Of PointF)
        For Each p In pts
            If result.Count > 0 AndAlso Near(result(result.Count - 1), p) Then Continue For
            If result.Count >= 2 Then
                Dim a = result(result.Count - 2), b = result(result.Count - 1)
                Dim collinear = (Math.Abs(a.X - b.X) < 0.01 AndAlso Math.Abs(b.X - p.X) < 0.01) OrElse
                                (Math.Abs(a.Y - b.Y) < 0.01 AndAlso Math.Abs(b.Y - p.Y) < 0.01)
                If collinear Then result(result.Count - 1) = p : Continue For
            End If
            result.Add(p)
        Next
        Return result.ToArray()
    End Function

    Private Shared Function Near(a As PointF, b As PointF) As Boolean
        Return Math.Abs(a.X - b.X) < 0.01 AndAlso Math.Abs(a.Y - b.Y) < 0.01
    End Function

    Public Function DistanceTo(p As PointF) As Single
        Dim pts = Route()
        Dim best = Single.MaxValue
        For i = 0 To pts.Length - 2
            best = Math.Min(best, SegmentDistance(p, pts(i), pts(i + 1)))
        Next
        Return best
    End Function

    Private Shared Function SegmentDistance(p As PointF, a As PointF, b As PointF) As Single
        Dim dx = b.X - a.X, dy = b.Y - a.Y
        Dim len2 = dx * dx + dy * dy
        Dim t = If(len2 < 0.0001F, 0F, ((p.X - a.X) * dx + (p.Y - a.Y) * dy) / len2)
        t = Math.Max(0F, Math.Min(1.0F, t))
        Dim cx = a.X + t * dx - p.X, cy = a.Y + t * dy - p.Y
        Return CSng(Math.Sqrt(cx * cx + cy * cy))
    End Function
End Class

''' <summary>The circuit document: elements, tubes and wires, and file input/output.</summary>
Public Class Circuit
    Public ReadOnly Property Elements As New List(Of CircuitElement)
    Public ReadOnly Property Tubes As New List(Of Tube)
    Private _nextId As Integer = 1

    Public Sub Clear()
        Elements.Clear()
        Tubes.Clear()
        _nextId = 1
    End Sub

    Public Function Add(Of T As CircuitElement)(e As T, x As Single, y As Single, Optional label As String = Nothing) As T
        e.X = x
        e.Y = y
        If label IsNot Nothing Then e.Label = label
        If e.Id = 0 OrElse Elements.Any(Function(o) o.Id = e.Id) Then e.Id = _nextId
        _nextId = Math.Max(_nextId, e.Id + 1)
        Elements.Add(e)
        Return e
    End Function

    Public Sub Remove(e As CircuitElement)
        For Each t In Tubes.Where(Function(tb) tb.A.Owner Is e OrElse tb.B.Owner Is e).ToList()
            RemoveTube(t)
        Next
        Elements.Remove(e)
    End Sub

    ''' <summary>True if a tube may join the two ports.</summary>
    Public Shared Function CanConnect(a As Port, b As Port) As Boolean
        Return a IsNot Nothing AndAlso b IsNot Nothing AndAlso a IsNot b AndAlso
               a.Owner IsNot b.Owner AndAlso a.Kind = b.Kind
    End Function

    Public Function Connect(a As Port, b As Port) As Tube
        If Not CanConnect(a, b) Then Return Nothing
        If Tubes.Any(Function(t) (t.A Is a AndAlso t.B Is b) OrElse (t.A Is b AndAlso t.B Is a)) Then Return Nothing
        Dim tube As New Tube(a, b)
        Tubes.Add(tube)
        a.ConnectionCount += 1
        b.ConnectionCount += 1
        Return tube
    End Function

    ''' <summary>Convenience overload used by the examples and the circuit generator.</summary>
    Public Function Connect(a As CircuitElement, aPort As String, b As CircuitElement, bPort As String) As Tube
        Return Connect(a.GetPort(aPort), b.GetPort(bPort))
    End Function

    Public Sub RemoveTube(t As Tube)
        If Tubes.Remove(t) Then
            t.A.ConnectionCount -= 1
            t.B.ConnectionCount -= 1
        End If
    End Sub

    ''' <summary>Removes tubes whose ports no longer exist or no longer match (after a configuration change).</summary>
    Public Sub CleanupTubes()
        For Each t In Tubes.Where(Function(tb) Not tb.A.Owner.Ports.Contains(tb.A) OrElse
                                               Not tb.B.Owner.Ports.Contains(tb.B) OrElse
                                               tb.A.Kind <> tb.B.Kind).ToList()
            RemoveTube(t)
        Next
    End Sub

    Public Function AllPorts() As IEnumerable(Of Port)
        Return Elements.SelectMany(Function(e) e.Ports)
    End Function

    Public Function FindPortAt(p As PointF, radius As Single) As Port
        Dim best As Port = Nothing
        Dim bestD = radius
        For Each port In AllPorts()
            Dim w = port.WorldPos()
            Dim d = CSng(Math.Sqrt((w.X - p.X) ^ 2 + (w.Y - p.Y) ^ 2))
            If d <= bestD Then best = port : bestD = d
        Next
        Return best
    End Function

    Public Function FindElementAt(p As PointF) As CircuitElement
        For i = Elements.Count - 1 To 0 Step -1
            If Elements(i).HitTest(p) Then Return Elements(i)
        Next
        Return Nothing
    End Function

    Public Function FindTubeAt(p As PointF, tolerance As Single) As Tube
        Return Tubes.FirstOrDefault(Function(t) t.DistanceTo(p) <= tolerance)
    End Function

    ''' <summary>Shifts the whole circuit right or down if any part lies left of or above the sheet.</summary>
    Public Sub EnsureOnSheet(Optional margin As Single = 20)
        Dim b = Bounds()
        If b.IsEmpty Then Return
        Dim dx = If(b.Left < 0, CSng(Math.Ceiling((margin - b.Left) / 10) * 10), 0F)
        Dim dy = If(b.Top < 0, CSng(Math.Ceiling((margin - b.Top) / 10) * 10), 0F)
        If dx = 0 AndAlso dy = 0 Then Return
        For Each e In Elements
            e.X += dx : e.Y += dy
        Next
        For Each t In Tubes.Where(Function(tb) tb.Mid.HasValue)
            t.Mid = t.Mid.Value + If(t.MidAxis() = "X", dx, dy)
        Next
    End Sub

    Public Function Bounds() As RectangleF
        If Elements.Count = 0 Then Return RectangleF.Empty
        Dim r = Elements(0).WorldBounds()
        For Each e In Elements.Skip(1)
            r = RectangleF.Union(r, e.WorldBounds())
        Next
        Return r
    End Function

    ' ---------------------------------------------------------------- file format

    Public Sub Save(path As String)
        ToXDocument(Elements).Save(path)
    End Sub

    Public Shared Function Load(path As String) As Circuit
        Return FromXDocument(XDocument.Load(path))
    End Function

    ''' <summary>Serializes the circuit (used for undo snapshots).</summary>
    Public Function ToXml() As String
        Return ToXDocument(Elements).ToString(SaveOptions.DisableFormatting)
    End Function

    Public Shared Function FromXml(xml As String) As Circuit
        Return FromXDocument(XDocument.Parse(xml))
    End Function

    ''' <summary>Serializes some elements and the tubes between them (used for copy and paste).</summary>
    Public Function ExtractXml(subset As IEnumerable(Of CircuitElement)) As String
        Return ToXDocument(subset.ToList()).ToString(SaveOptions.DisableFormatting)
    End Function

    ''' <summary>Adds the elements of a serialized fragment, shifted by an offset. Returns the new elements.</summary>
    Public Function Merge(xml As String, dx As Single, dy As Single) As List(Of CircuitElement)
        Dim fragment = FromXml(xml)
        Dim added As New List(Of CircuitElement)
        For Each e In fragment.Elements
            e.Id = 0
            Add(e, e.X + dx, e.Y + dy)
            added.Add(e)
        Next
        For Each t In fragment.Tubes
            t.A.ConnectionCount -= 1
            t.B.ConnectionCount -= 1
            Dim nt = Connect(t.A, t.B)
            If nt IsNot Nothing AndAlso t.Mid.HasValue Then
                nt.Mid = t.Mid.Value + If(nt.MidAxis() = "X", dx, dy)
            End If
            If nt IsNot Nothing Then nt.Fault = t.Fault : nt.FaultHidden = t.FaultHidden
        Next
        Return added
    End Function

    ''' <summary>The whole circuit as an XML element (one page of a project file).</summary>
    Public Function ToXElement() As XElement
        Return ToXDocument(Elements).Root
    End Function

    Public Shared Function FromXElement(root As XElement) As Circuit
        Return FromXDocument(New XDocument(New XElement(root)))
    End Function

    Private Function ToXDocument(subset As List(Of CircuitElement)) As XDocument
        Dim inv = CultureInfo.InvariantCulture
        Dim root As New XElement("PneuSimCircuit", New XAttribute("version", 2))
        For Each e In subset
            Dim xe As New XElement("Element",
                New XAttribute("type", e.TypeName),
                New XAttribute("id", e.Id),
                New XAttribute("x", e.X.ToString(inv)),
                New XAttribute("y", e.Y.ToString(inv)),
                New XAttribute("rotation", e.Rotation))
            WriteFault(xe, e.Fault, e.FaultHidden)
            For Each prop In PersistentProperties(e)
                Dim value = prop.GetValue(e)
                xe.Add(New XElement("Property", New XAttribute("name", prop.Name),
                                    New XAttribute("value", Convert.ToString(value, inv))))
            Next
            root.Add(xe)
        Next
        Dim set_ = New HashSet(Of CircuitElement)(subset)
        For Each t In Tubes.Where(Function(tb) set_.Contains(tb.A.Owner) AndAlso set_.Contains(tb.B.Owner))
            Dim xt As New XElement("Tube",
                New XAttribute("from", t.A.Owner.Id), New XAttribute("fromPort", t.A.Name),
                New XAttribute("to", t.B.Owner.Id), New XAttribute("toPort", t.B.Name))
            If t.Mid.HasValue Then xt.Add(New XAttribute("mid", t.Mid.Value.ToString(inv)))
            WriteFault(xt, t.Fault, t.FaultHidden)
            root.Add(xt)
        Next
        Return New XDocument(root)
    End Function

    Private Shared Function FromXDocument(doc As XDocument) As Circuit
        Dim inv = CultureInfo.InvariantCulture
        If doc.Root Is Nothing OrElse doc.Root.Name.LocalName <> "PneuSimCircuit" Then
            Throw New InvalidOperationException("This is not a PneuSim circuit file.")
        End If
        Dim c As New Circuit()
        Dim byId As New Dictionary(Of Integer, CircuitElement)
        For Each xe In doc.Root.Elements("Element")
            Dim e = ElementFactory.Create(CStr(xe.Attribute("type")))
            If e Is Nothing Then Continue For
            e.Id = CInt(xe.Attribute("id"))
            e.Rotation = CInt(xe.Attribute("rotation"))
            For Each xp In xe.Elements("Property")
                Dim prop = e.GetType().GetProperty(CStr(xp.Attribute("name")))
                If prop Is Nothing OrElse Not prop.CanWrite Then Continue For
                Dim raw = CStr(xp.Attribute("value"))
                Try
                    Dim value As Object
                    If prop.PropertyType.IsEnum Then
                        value = [Enum].Parse(prop.PropertyType, raw)
                    Else
                        value = Convert.ChangeType(raw, prop.PropertyType, inv)
                    End If
                    prop.SetValue(e, value)
                Catch ex As Exception When TypeOf ex Is FormatException OrElse TypeOf ex Is ArgumentException
                    ' Ignore a malformed value and keep the default.
                End Try
            Next
            ReadFault(xe, Sub(k, h)
                              e.Fault = k
                              e.FaultHidden = h
                          End Sub)
            Dim original = e.Id
            c.Add(e, Single.Parse(CStr(xe.Attribute("x")), inv), Single.Parse(CStr(xe.Attribute("y")), inv))
            byId(original) = e
        Next
        For Each xt In doc.Root.Elements("Tube")
            Dim a As CircuitElement = Nothing, b As CircuitElement = Nothing
            If byId.TryGetValue(CInt(xt.Attribute("from")), a) AndAlso byId.TryGetValue(CInt(xt.Attribute("to")), b) Then
                Dim t = c.Connect(a.GetPort(CStr(xt.Attribute("fromPort"))), b.GetPort(CStr(xt.Attribute("toPort"))))
                Dim mid = xt.Attribute("mid")
                If t IsNot Nothing AndAlso mid IsNot Nothing Then t.Mid = Single.Parse(mid.Value, inv)
                If t IsNot Nothing Then ReadFault(xt, Sub(k, h)
                                                          t.Fault = k
                                                          t.FaultHidden = h
                                                      End Sub)
            End If
        Next
        Return c
    End Function

    ''' <summary>Visible faults are stored by name, hidden (exercise) faults in scrambled form.</summary>
    Private Shared Sub WriteFault(x As XElement, kind As FaultKind, hidden As Boolean)
        If kind = FaultKind.None Then Return
        If hidden Then x.Add(New XAttribute("hf", Faults.Scramble(kind))) Else x.Add(New XAttribute("fault", kind.ToString()))
    End Sub

    Private Shared Sub ReadFault(x As XElement, apply As Action(Of FaultKind, Boolean))
        Dim kind As FaultKind
        Dim visible = x.Attribute("fault"), hidden = x.Attribute("hf")
        If visible IsNot Nothing AndAlso [Enum].TryParse(visible.Value, kind) Then
            apply(kind, False)
        ElseIf hidden IsNot Nothing Then
            kind = Faults.Unscramble(hidden.Value)
            If kind <> FaultKind.None Then apply(kind, True)
        End If
    End Sub

    ''' <summary>All faults in the circuit, in components and in tubes.</summary>
    Public Function FaultCount() As Integer
        Return Elements.Where(Function(e) e.Fault <> FaultKind.None).Count() + Tubes.Where(Function(t) t.Fault <> FaultKind.None).Count()
    End Function

    ''' <summary>Public, editable, browsable properties are what gets stored in a file.</summary>
    Private Shared Function PersistentProperties(e As CircuitElement) As IEnumerable(Of PropertyInfo)
        Return e.GetType().GetProperties(BindingFlags.Public Or BindingFlags.Instance).
            Where(Function(p) p.CanRead AndAlso p.CanWrite AndAlso p.GetSetMethod() IsNot Nothing AndAlso
                              p.GetIndexParameters().Length = 0 AndAlso
                              (p.GetCustomAttribute(Of BrowsableAttribute)() Is Nothing OrElse
                               p.GetCustomAttribute(Of BrowsableAttribute)().Browsable))
    End Function
End Class
