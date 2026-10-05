Imports System.Globalization
Imports System.Text

''' <summary>One line of the parts list.</summary>
Public Class PartLine
    Public Property Description As String
    Public Property Labels As String
    Public Property Quantity As Integer
    Public Property UnitPrice As Double

    Public ReadOnly Property Total As Double
        Get
            Return Quantity * UnitPrice
        End Get
    End Property
End Class

''' <summary>Builds the bill of materials of a project with typical prices.</summary>
Public Module PartsList

    ''' <summary>Typical list prices in rupees (educational estimates); users can override them.</summary>
    Public Function DefaultPrice(description As String) As Double
        Dim d = description.ToLowerInvariant()
        If d.Contains("hydraulic power unit") Then Return 45000
        If d.Contains("hydraulic single-acting") Then Return 9000
        If d.Contains("hydraulic check") Then Return 1800
        If d.Contains("hydraulic one-way") OrElse d.Contains("hydraulic throttle") Then Return 2500
        If d.Contains("pressure reducing") Then Return 4500
        If d.Contains("hydraulic hose") Then Return 900
        If d.Contains("hydraulic cylinder") Then Return 12000
        If d.Contains("hydraulic motor") Then Return 15000
        If d.Contains("4/3-way hydraulic") Then Return 9000
        If d.Contains("4/2-way hydraulic") Then Return 7000
        If d.Contains("relief") Then Return 3500
        If d.Contains("accumulator") Then Return 12000
        If d.Contains("pressure-compensated") Then Return 4000
        If d.Contains("counterbalance") Then Return 5000
        If d.Contains("service unit") Then Return 2500
        If d.Contains("pressure regulator") Then Return 1200
        If d.Contains("gauge") Then Return 400
        If d.Contains("silencer") Then Return 80
        If d.Contains("t-fitting") Then Return 40
        If d.Contains("single-acting cylinder") Then Return 2200
        If d.Contains("double-acting cylinder") Then Return 3000
        If d.Contains("semi-rotary") Then Return 6500
        If d.Contains("air motor") Then Return 9000
        Dim extra = If(d.Contains("solenoid"), 900, 0)
        If d.Contains("5/3-way") Then Return 4200 + extra
        If d.Contains("5/2-way") Then Return 2200 + extra
        If d.Contains("4/2-way") Then Return 2000 + extra
        If d.Contains("3/2-way") Then Return If(d.Contains("roller"), 1300, If(d.Contains("time delay"), 2500, 1100)) + extra
        If d.Contains("2/2-way") Then Return 900 + extra
        If d.Contains("shuttle") Then Return 600
        If d.Contains("two-pressure") Then Return 700
        If d.Contains("quick exhaust") Then Return 500
        If d.Contains("check valve") Then Return 300
        If d.Contains("flow control") Then Return 450
        If d.Contains("timer relay") Then Return 1500
        If d.Contains("relay") Then Return 450
        If d.Contains("proximity sensor") Then Return 1200
        If d.Contains("limit switch") Then Return 900
        If d.Contains("push button") OrElse d.Contains("selector switch") Then Return 350
        If d.Contains("indicator lamp") Then Return 250
        If d.Contains("tube fittings") Then Return 25
        If d.Contains("plastic tubing") Then Return 40
        Return 0
    End Function

    ''' <summary>Groups the physical components of all pages (supplies, wiring helpers and relay contacts excluded).</summary>
    Public Function Build(project As Project) As List(Of PartLine)
        Dim lines As New Dictionary(Of String, PartLine)
        Dim add = Sub(desc As String, label As String)
                      Dim line As PartLine = Nothing
                      If Not lines.TryGetValue(desc, line) Then
                          Dim price As Double
                          If Not project.PartCosts.TryGetValue(desc, price) Then price = DefaultPrice(desc)
                          line = New PartLine With {.Description = desc, .Labels = "", .UnitPrice = price}
                          lines(desc) = line
                      End If
                      line.Quantity += 1
                      If Not String.IsNullOrWhiteSpace(label) Then line.Labels = If(line.Labels = "", label, line.Labels & ", " & label)
                  End Sub
        For Each e In project.AllElements()
            Select Case True
                Case TypeOf e Is TextNote, TypeOf e Is PageConnector, TypeOf e Is AirSupply, TypeOf e Is PowerTerminal, TypeOf e Is HydraulicTank
                    Continue For
                Case TypeOf e Is Junction
                    If e.Ports(0).ConnectionCount >= 3 AndAlso e.Ports(0).Kind <> PortKind.Electric Then add("T-fitting", "")
                    Continue For
                Case TypeOf e Is ElectricContact AndAlso DirectCast(e, ElectricContact).Operator = ContactOperator.Relay
                    Continue For ' part of the relay
                Case TypeOf e Is ElectricCoil AndAlso DirectCast(e, ElectricCoil).Kind = CoilKind.Solenoid
                    Continue For ' part of the solenoid valve
            End Select
            add(e.DisplayName, e.Label)
        Next
        Dim airLines = 0, oilLines = 0
        For Each p In project.Pages
            airLines += p.Circuit.Tubes.Where(Function(t) t.A.Kind = PortKind.Pneumatic).Count()
            oilLines += p.Circuit.Tubes.Where(Function(t) t.A.Kind = PortKind.Hydraulic).Count()
        Next
        Dim addLine = Sub(desc As String, qty As Integer)
                          If qty <= 0 Then Return
                          Dim price As Double
                          If Not project.PartCosts.TryGetValue(desc, price) Then price = DefaultPrice(desc)
                          lines(desc) = New PartLine With {.Description = desc, .Labels = "", .Quantity = qty, .UnitPrice = price}
                      End Sub
        ' Two push-in fittings and about one metre of tubing per pneumatic connection.
        addLine("Tube fittings (push-in)", airLines * 2)
        addLine("Plastic tubing, metres (estimate: 1 m per connection)", airLines)
        ' One hose with crimped fittings per hydraulic line.
        addLine("Hydraulic hose with fittings", oilLines)
        Return lines.Values.OrderBy(Function(l) l.Description).ToList()
    End Function

    Public Function ToCsv(lines As List(Of PartLine)) As String
        Dim sb As New StringBuilder()
        Dim inv = CultureInfo.InvariantCulture
        Dim q = Function(s As String) """" & s.Replace("""", """""") & """"
        sb.AppendLine("Qty,Description,Labels,Unit price (INR),Total (INR)")
        For Each l In lines
            sb.AppendLine($"{l.Quantity},{q(l.Description)},{q(l.Labels)},{l.UnitPrice.ToString("0.00", inv)},{l.Total.ToString("0.00", inv)}")
        Next
        sb.AppendLine($",{q("Total")},,,{lines.Sum(Function(l) l.Total).ToString("0.00", inv)}")
        Return sb.ToString()
    End Function
End Module
