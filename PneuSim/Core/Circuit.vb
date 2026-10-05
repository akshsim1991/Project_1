Imports System.ComponentModel
Imports System.Globalization
Imports System.Reflection
Imports System.Xml.Linq

''' <summary>A pneumatic line between two ports.</summary>
Public Class Tube
    Public Sub New(a As Port, b As Port)
        Me.A = a
        Me.B = b
    End Sub

    Public ReadOnly Property A As Port
    Public ReadOnly Property B As Port

    Public ReadOnly Property IsPressurized As Boolean
        Get
            Return A.IsPressurized
        End Get
    End Property

    Public Function Route() As PointF()
        Return RouteBetween(A.WorldPos(), A.WorldDir(), B.WorldPos(), B.WorldDir())
    End Function

    ''' <summary>Builds an orthogonal (right-angled) path between two ports.</summary>
    Public Shared Function RouteBetween(a As PointF, da As PointF, b As PointF, db As PointF) As PointF()
        Const lead = 10.0F
        Dim p1 As New PointF(a.X + da.X * lead, a.Y + da.Y * lead)
        Dim p3 As New PointF(b.X + db.X * lead, b.Y + db.Y * lead)
        Dim aHoriz = Math.Abs(da.X) > 0.5F
        Dim bHoriz = Math.Abs(db.X) > 0.5F
        Dim pts As New List(Of PointF) From {a, p1}

        If aHoriz AndAlso bHoriz Then
            Dim midX = (p1.X + p3.X) / 2
            pts.Add(New PointF(midX, p1.Y))
            pts.Add(New PointF(midX, p3.Y))
        ElseIf Not aHoriz AndAlso Not bHoriz Then
            Dim midY = (p1.Y + p3.Y) / 2
            pts.Add(New PointF(p1.X, midY))
            pts.Add(New PointF(p3.X, midY))
        ElseIf aHoriz Then
            pts.Add(New PointF(p3.X, p1.Y))
        Else
            pts.Add(New PointF(p1.X, p3.Y))
        End If
        pts.Add(p3)
        pts.Add(b)
        Return Simplify(pts)
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

''' <summary>The circuit document: elements, tubes, and file input/output.</summary>
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
        If e.Id = 0 Then e.Id = _nextId
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

    Public Function Connect(a As Port, b As Port) As Tube
        If a Is Nothing OrElse b Is Nothing OrElse a Is b Then Return Nothing
        If Tubes.Any(Function(t) (t.A Is a AndAlso t.B Is b) OrElse (t.A Is b AndAlso t.B Is a)) Then Return Nothing
        Dim tube As New Tube(a, b)
        Tubes.Add(tube)
        a.ConnectionCount += 1
        b.ConnectionCount += 1
        Return tube
    End Function

    ''' <summary>Convenience overload used by the example circuits.</summary>
    Public Function Connect(a As CircuitElement, aPort As String, b As CircuitElement, bPort As String) As Tube
        Return Connect(a.GetPort(aPort), b.GetPort(bPort))
    End Function

    Public Sub RemoveTube(t As Tube)
        If Tubes.Remove(t) Then
            t.A.ConnectionCount -= 1
            t.B.ConnectionCount -= 1
        End If
    End Sub

    ''' <summary>Removes tubes whose ports no longer exist (after an element's configuration changed).</summary>
    Public Sub CleanupTubes()
        For Each t In Tubes.Where(Function(tb) Not tb.A.Owner.Ports.Contains(tb.A) OrElse
                                               Not tb.B.Owner.Ports.Contains(tb.B)).ToList()
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
        Dim inv = CultureInfo.InvariantCulture
        Dim root As New XElement("PneuSimCircuit", New XAttribute("version", 1))
        For Each e In Elements
            Dim xe As New XElement("Element",
                New XAttribute("type", e.TypeName),
                New XAttribute("id", e.Id),
                New XAttribute("x", e.X.ToString(inv)),
                New XAttribute("y", e.Y.ToString(inv)),
                New XAttribute("rotation", e.Rotation))
            For Each prop In PersistentProperties(e)
                Dim value = prop.GetValue(e)
                xe.Add(New XElement("Property", New XAttribute("name", prop.Name),
                                    New XAttribute("value", Convert.ToString(value, inv))))
            Next
            root.Add(xe)
        Next
        For Each t In Tubes
            root.Add(New XElement("Tube",
                New XAttribute("from", t.A.Owner.Id), New XAttribute("fromPort", t.A.Name),
                New XAttribute("to", t.B.Owner.Id), New XAttribute("toPort", t.B.Name)))
        Next
        Call New XDocument(root).Save(path)
    End Sub

    Public Shared Function Load(path As String) As Circuit
        Dim inv = CultureInfo.InvariantCulture
        Dim doc = XDocument.Load(path)
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
                Catch ex As FormatException
                    ' Ignore a malformed value and keep the default.
                End Try
            Next
            c.Add(e, Single.Parse(CStr(xe.Attribute("x")), inv), Single.Parse(CStr(xe.Attribute("y")), inv))
            byId(e.Id) = e
        Next
        For Each xt In doc.Root.Elements("Tube")
            Dim a As CircuitElement = Nothing, b As CircuitElement = Nothing
            If byId.TryGetValue(CInt(xt.Attribute("from")), a) AndAlso byId.TryGetValue(CInt(xt.Attribute("to")), b) Then
                c.Connect(a.GetPort(CStr(xt.Attribute("fromPort"))), b.GetPort(CStr(xt.Attribute("toPort"))))
            End If
        Next
        Return c
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
