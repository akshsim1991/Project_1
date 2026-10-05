Imports System.ComponentModel
Imports System.Globalization
Imports System.Xml.Linq

''' <summary>Drawing title block and costing information of a project.</summary>
Public Class ProjectInfo
    <Category("Title block"), DisplayName("Title")> Public Property Title As String = "Untitled circuit"
    <Category("Title block"), DisplayName("Drawn by")> Public Property Author As String = ""
    <Category("Title block"), DisplayName("Company / institute")> Public Property Company As String = ""
    <Category("Title block"), DisplayName("Drawing number")> Public Property DrawingNumber As String = ""
    <Category("Title block"), DisplayName("Revision")> Public Property Revision As String = "A"
    <Category("Title block"), DisplayName("Date")> Public Property DateText As String = Date.Today.ToString("dd-MM-yyyy", CultureInfo.InvariantCulture)
    <Category("Title block"), DisplayName("Description")> Public Property Description As String = ""

    <Category("Running cost"), DisplayName("Compressed air cost (₹ per m³)"),
     Description("Cost of producing one cubic metre of free air, typically ₹1.5–3 including electricity.")>
    Public Property AirCostPerM3 As Double = 2.0

    <Category("Running cost"), DisplayName("Cycles per minute")> Public Property CyclesPerMinute As Double = 2
    <Category("Running cost"), DisplayName("Hours per day")> Public Property HoursPerDay As Double = 8
    <Category("Running cost"), DisplayName("Days per year")> Public Property DaysPerYear As Double = 300

    Friend Function ToXElement() As XElement
        Dim inv = CultureInfo.InvariantCulture
        Dim x As New XElement("Info")
        For Each p In GetType(ProjectInfo).GetProperties()
            x.Add(New XAttribute(p.Name, Convert.ToString(p.GetValue(Me), inv)))
        Next
        Return x
    End Function

    Friend Shared Function FromXElement(x As XElement) As ProjectInfo
        Dim info As New ProjectInfo()
        If x Is Nothing Then Return info
        For Each p In GetType(ProjectInfo).GetProperties()
            Dim a = x.Attribute(p.Name)
            If a Is Nothing Then Continue For
            Try
                p.SetValue(info, Convert.ChangeType(a.Value, p.PropertyType, CultureInfo.InvariantCulture))
            Catch ex As FormatException
            End Try
        Next
        Return info
    End Function
End Class

''' <summary>One sheet of a project.</summary>
Public Class ProjectPage
    Public Property Name As String
    Public Property Circuit As Circuit

    Public Overrides Function ToString() As String
        Return Name
    End Function
End Class

''' <summary>A PneuSim document: one or more pages simulated together, plus title block data.</summary>
Public Class Project
    Public ReadOnly Property Pages As New List(Of ProjectPage)
    Public Property Info As New ProjectInfo()
    ''' <summary>Unit prices entered in the parts list, by component description.</summary>
    Public ReadOnly Property PartCosts As New Dictionary(Of String, Double)

    Public Sub New()
    End Sub

    Public Sub New(first As Circuit, Optional title As String = Nothing)
        Pages.Add(New ProjectPage With {.Name = "Page 1", .Circuit = first})
        If title IsNot Nothing Then Info.Title = title
    End Sub

    Public Function AddPage(Optional name As String = Nothing) As ProjectPage
        Dim p As New ProjectPage With {.Name = If(name, $"Page {Pages.Count + 1}"), .Circuit = New Circuit()}
        Pages.Add(p)
        Return p
    End Function

    ''' <summary>All pages combined into one circuit for simulation (the elements are shared, not copied).</summary>
    Public Function SimulationCircuit() As Circuit
        If Pages.Count = 1 Then Return Pages(0).Circuit
        Dim c As New Circuit()
        For Each p In Pages
            c.Elements.AddRange(p.Circuit.Elements)
            c.Tubes.AddRange(p.Circuit.Tubes)
        Next
        Return c
    End Function

    Public Function AllElements() As IEnumerable(Of CircuitElement)
        Return Pages.SelectMany(Function(p) p.Circuit.Elements)
    End Function

    ''' <summary>Location text "page.column" used for cross-references.</summary>
    Public Shared Function LocationOf(e As CircuitElement) As String
        Dim column = CInt(Math.Floor((e.X + 20) / 100)) + 1
        Return $"{e.PageIndex + 1}.{Math.Max(1, column)}"
    End Function

    ''' <summary>
    ''' Fills in cross-references: under each relay coil where its contacts are, next to each
    ''' contact where its coil is, and for page connectors the pages they continue on.
    ''' </summary>
    Public Sub UpdateCrossReferences()
        For i = 0 To Pages.Count - 1
            For Each e In Pages(i).Circuit.Elements
                e.PageIndex = i
                e.CrossReference = ""
            Next
        Next
        Dim all = AllElements().ToList()
        Dim coils = all.OfType(Of ElectricCoil)().Where(Function(c) c.Kind <> CoilKind.Lamp AndAlso Not String.IsNullOrWhiteSpace(c.Label)).ToList()
        Dim contacts = all.OfType(Of ElectricContact)().Where(Function(c) c.Operator = ContactOperator.Relay).ToList()
        For Each coil In coils
            Dim mine = contacts.Where(Function(k) String.Equals(k.Reference?.Trim(), coil.Label.Trim(), StringComparison.OrdinalIgnoreCase)).ToList()
            If coil.Kind = CoilKind.Solenoid Then
                Dim valves = all.OfType(Of DirectionalValve)().Where(Function(v) v.UsesSolenoid(coil.Label)).ToList()
                coil.CrossReference = String.Join(" ", valves.Select(Function(v) If(String.IsNullOrEmpty(v.Label), "valve", v.Label) & " " & LocationOf(v)))
            Else
                Dim no = mine.Where(Function(k) Not k.NormallyClosed).Select(Function(k) LocationOf(k))
                Dim nc = mine.Where(Function(k) k.NormallyClosed).Select(Function(k) LocationOf(k))
                Dim parts As New List(Of String)
                If no.Any() Then parts.Add("NO " & String.Join(",", no))
                If nc.Any() Then parts.Add("NC " & String.Join(",", nc))
                coil.CrossReference = String.Join("  ", parts)
            End If
            For Each k In mine
                k.CrossReference = LocationOf(coil)
            Next
        Next
        Dim connectors = all.OfType(Of PageConnector)().ToList()
        For Each pc In connectors
            Dim others = connectors.Where(Function(o) o IsNot pc AndAlso o.Matches(pc)).Select(Function(o) LocationOf(o)).ToList()
            pc.CrossReference = If(others.Count = 0, "", "→ " & String.Join(", ", others))
        Next
    End Sub

    ' ---------------------------------------------------------------- file format

    Public Function ToXml() As String
        Return ToXDocument().ToString(SaveOptions.DisableFormatting)
    End Function

    Public Shared Function FromXml(xml As String) As Project
        Return FromXDocument(XDocument.Parse(xml))
    End Function

    Public Sub Save(path As String)
        ToXDocument().Save(path)
    End Sub

    Public Shared Function Load(path As String) As Project
        Return FromXDocument(XDocument.Load(path))
    End Function

    Private Function ToXDocument() As XDocument
        Dim inv = CultureInfo.InvariantCulture
        Dim root As New XElement("PneuSimProject", New XAttribute("version", 3), Info.ToXElement())
        Dim costs As New XElement("PartCosts")
        For Each kv In PartCosts
            costs.Add(New XElement("Part", New XAttribute("name", kv.Key), New XAttribute("cost", kv.Value.ToString(inv))))
        Next
        root.Add(costs)
        For Each p In Pages
            root.Add(New XElement("Page", New XAttribute("name", p.Name), p.Circuit.ToXElement()))
        Next
        Return New XDocument(root)
    End Function

    Private Shared Function FromXDocument(doc As XDocument) As Project
        If doc.Root Is Nothing Then Throw New InvalidOperationException("Empty file.")
        If doc.Root.Name.LocalName = "PneuSimCircuit" Then
            ' Single-page files from PneuSim 1.x and 2.x.
            Return New Project(Circuit.FromXElement(doc.Root))
        End If
        If doc.Root.Name.LocalName <> "PneuSimProject" Then Throw New InvalidOperationException("This is not a PneuSim file.")
        Dim pr As New Project With {.Info = ProjectInfo.FromXElement(doc.Root.Element("Info"))}
        Dim costs = doc.Root.Element("PartCosts")
        If costs IsNot Nothing Then
            For Each x In costs.Elements("Part")
                Dim v As Double
                If Double.TryParse(CStr(x.Attribute("cost")), NumberStyles.Float, CultureInfo.InvariantCulture, v) Then pr.PartCosts(CStr(x.Attribute("name"))) = v
            Next
        End If
        For Each xp In doc.Root.Elements("Page")
            Dim xc = xp.Element("PneuSimCircuit")
            pr.Pages.Add(New ProjectPage With {.Name = CStr(xp.Attribute("name")),
                                               .Circuit = If(xc Is Nothing, New Circuit(), Circuit.FromXElement(xc))})
        Next
        If pr.Pages.Count = 0 Then pr.AddPage()
        Return pr
    End Function
End Class

''' <summary>Joins a tube, wire or hydraulic line to the connector with the same name on another page.</summary>
Public Class PageConnector
    Inherits CircuitElement

    Private _medium As PortKind = PortKind.Pneumatic

    Public Sub New()
        AddPort("1", 0, 10, -1, 0)
        Label = "X1"
    End Sub

    Public Overrides ReadOnly Property TypeName As String = "PageConnector"
    Public Overrides ReadOnly Property DisplayName As String = "Page connector"

    <Category("Connector"), DisplayName("Carries"), Description("What the connector joins: tubes, wires or hydraulic lines.")>
    Public Property Medium As PortKind
        Get
            Return _medium
        End Get
        Set(value As PortKind)
            _medium = value
            Ports(0).Kind = value
        End Set
    End Property

    Public Function Matches(other As PageConnector) As Boolean
        Return other.Medium = Medium AndAlso String.Equals(other.Label?.Trim(), Label?.Trim(), StringComparison.OrdinalIgnoreCase) AndAlso
               Not String.IsNullOrWhiteSpace(Label)
    End Function

    Public Overrides ReadOnly Property LocalBounds As RectangleF
        Get
            Return New RectangleF(0, 0, 50, 20)
        End Get
    End Property

    Public Overrides ReadOnly Property LabelAnchor As PointF
        Get
            Return New PointF(14, 17)
        End Get
    End Property

    Public Overrides Sub DrawSymbol(g As DrawSurface, r As RenderContext)
        Dim pts = {New PointF(0, 10), New PointF(10, 0), New PointF(50, 0), New PointF(50, 20), New PointF(10, 20)}
        g.FillPolygon(r.BodyBrush, pts)
        g.DrawPolygon(r.PenFor(Ports(0)), pts)
        If Not String.IsNullOrEmpty(CrossReference) Then g.DrawString(CrossReference, r.SmallFont, r.MarkBrush, 12, 22)
    End Sub

    Public Overrides Sub AddEdges(sim As Simulator)
        For Each other In sim.Circuit.Elements.OfType(Of PageConnector)()
            If other IsNot Me AndAlso other.Matches(Me) Then sim.AddEdge(Ports(0), other.Ports(0))
        Next
    End Sub
End Class
