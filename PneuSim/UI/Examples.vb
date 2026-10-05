''' <summary>Ready-made circuits that demonstrate the basic pneumatic control techniques.</summary>
Public Module Examples

    Public ReadOnly All As (Name As String, Build As Func(Of Circuit))() = {
        ("1. Direct control of a single-acting cylinder", AddressOf DirectSingleActing),
        ("2. Indirect control of a double-acting cylinder with speed control", AddressOf IndirectDoubleActing),
        ("3. OR: operation from two places (shuttle valve)", AddressOf OrControl),
        ("4. AND: two-hand safety control (two-pressure valve)", AddressOf TwoHandControl),
        ("5. Automatic reciprocation with limit valves", AddressOf Reciprocation),
        ("6. Delayed return with a time delay valve", AddressOf DelayedReturn),
        ("7. Electro-pneumatic: direct control with a solenoid valve", AddressOf ElectroDirect),
        ("8. Electro-pneumatic: self-holding circuit with start / stop and lamp", AddressOf ElectroLatch)
    }

    Private Function DirectSingleActing() As Circuit
        Dim c As New Circuit()
        Dim cyl = c.Add(New SingleActingCylinder(), 200, 60, "1A")
        Dim v = c.Add(V32(ValveActuator.PushButton), 140, 200, "1S1")
        Dim s = c.Add(New AirSupply(), 190, 320, "0Z")
        c.Connect(v, "2", cyl, "1")
        c.Connect(s, "1", v, "1")
        Return c
    End Function

    Private Function IndirectDoubleActing() As Circuit
        Dim c As New Circuit()
        Dim cyl = c.Add(New DoubleActingCylinder() With {.StrokeTime = 0.8}, 220, 40, "1A")
        Dim fc1 = c.Add(New FlowControlValve() With {.OpeningPercent = 40, .Rotation = 3}, 200, 210, "1V2")
        Dim fc2 = c.Add(New FlowControlValve() With {.OpeningPercent = 15, .Rotation = 3}, 300, 210, "1V3")
        Dim v = c.Add(V52(ValveActuator.Pilot, ValveReturn.Spring), 130, 280, "1V1")
        Dim s1 = c.Add(New AirSupply(), 220, 380, "0Z")
        Dim pb = c.Add(V32(ValveActuator.PushButton), 10, 380, "1S1")
        Dim s2 = c.Add(New AirSupply(), 60, 480)
        c.Connect(fc1, "2", cyl, "1")
        c.Connect(fc2, "2", cyl, "2")
        c.Connect(v, "4", fc1, "1")
        c.Connect(v, "2", fc2, "1")
        c.Connect(s1, "1", v, "1")
        c.Connect(pb, "2", v, "14")
        c.Connect(s2, "1", pb, "1")
        Return c
    End Function

    Private Function OrControl() As Circuit
        Dim c As New Circuit()
        Dim cyl = c.Add(New SingleActingCylinder(), 230, 60, "1A")
        Dim orValve = c.Add(New ShuttleValve(), 200, 200, "1V1")
        Dim b1 = c.Add(V32(ValveActuator.PushButton), 60, 300, "1S1")
        Dim b2 = c.Add(V32(ValveActuator.PushButton), 250, 300, "1S2")
        Dim s1 = c.Add(New AirSupply(), 110, 400)
        Dim s2 = c.Add(New AirSupply(), 300, 400)
        c.Connect(orValve, "2", cyl, "1")
        c.Connect(b1, "2", orValve, "1a")
        c.Connect(b2, "2", orValve, "1b")
        c.Connect(s1, "1", b1, "1")
        c.Connect(s2, "1", b2, "1")
        Return c
    End Function

    Private Function TwoHandControl() As Circuit
        Dim c As New Circuit()
        Dim cyl = c.Add(New DoubleActingCylinder(), 360, 40, "1A")
        Dim v = c.Add(V52(ValveActuator.Pilot, ValveReturn.Spring), 270, 170, "1V1")
        Dim s = c.Add(New AirSupply(), 360, 270, "0Z")
        Dim andValve = c.Add(New TwoPressureValve(), 150, 260, "1V2")
        Dim b1 = c.Add(V32(ValveActuator.PushButton), 50, 360, "1S1")
        Dim b2 = c.Add(V32(ValveActuator.PushButton), 170, 360, "1S2")
        Dim s1 = c.Add(New AirSupply(), 100, 460)
        Dim s2 = c.Add(New AirSupply(), 220, 460)
        c.Connect(v, "4", cyl, "1")
        c.Connect(v, "2", cyl, "2")
        c.Connect(s, "1", v, "1")
        c.Connect(andValve, "2", v, "14")
        c.Connect(b1, "2", andValve, "1a")
        c.Connect(b2, "2", andValve, "1b")
        c.Connect(s1, "1", b1, "1")
        c.Connect(s2, "1", b2, "1")
        Return c
    End Function

    Private Function Reciprocation() As Circuit
        Dim c As New Circuit()
        Dim cyl = c.Add(New DoubleActingCylinder() With {.RetractedMark = "1S1", .ExtendedMark = "1S2"}, 360, 40, "1A")
        Dim v = c.Add(V52(ValveActuator.Pilot, ValveReturn.Pilot), 270, 170, "1V1")
        Dim s = c.Add(New AirSupply(), 360, 270, "0Z")
        Dim lim2 = c.Add(V32(ValveActuator.RollerLever), 520, 260, "1S2")
        lim2.TriggerMark = "1S2"
        Dim s2 = c.Add(New AirSupply(), 570, 360)
        Dim lim1 = c.Add(V32(ValveActuator.RollerLever), 80, 260, "1S1")
        lim1.TriggerMark = "1S1"
        Dim start = c.Add(V32(ValveActuator.Selector), 80, 400, "1S3")
        Dim s3 = c.Add(New AirSupply(), 130, 500)
        c.Connect(v, "4", cyl, "1")
        c.Connect(v, "2", cyl, "2")
        c.Connect(s, "1", v, "1")
        c.Connect(lim2, "2", v, "12")
        c.Connect(s2, "1", lim2, "1")
        c.Connect(lim1, "2", v, "14")
        c.Connect(start, "2", lim1, "1")
        c.Connect(s3, "1", start, "1")
        Return c
    End Function

    Private Function DelayedReturn() As Circuit
        Dim c As New Circuit()
        Dim cyl = c.Add(New DoubleActingCylinder() With {.ExtendedMark = "1S2"}, 360, 40, "1A")
        Dim v = c.Add(V52(ValveActuator.Pilot, ValveReturn.Pilot), 270, 170, "1V1")
        Dim s = c.Add(New AirSupply(), 360, 270, "0Z")
        Dim pb = c.Add(V32(ValveActuator.PushButton), 80, 260, "1S1")
        Dim s1 = c.Add(New AirSupply(), 130, 360)
        Dim timer = c.Add(V32(ValveActuator.DelayedPilot), 520, 260, "1V2")
        timer.DelaySeconds = 2
        Dim s2 = c.Add(New AirSupply(), 570, 360)
        Dim lim2 = c.Add(V32(ValveActuator.RollerLever), 380, 380, "1S2")
        lim2.TriggerMark = "1S2"
        Dim s3 = c.Add(New AirSupply(), 430, 480)
        c.Connect(v, "4", cyl, "1")
        c.Connect(v, "2", cyl, "2")
        c.Connect(s, "1", v, "1")
        c.Connect(pb, "2", v, "14")
        c.Connect(s1, "1", pb, "1")
        c.Connect(timer, "2", v, "12")
        c.Connect(s2, "1", timer, "1")
        c.Connect(lim2, "2", timer, "12")
        c.Connect(s3, "1", lim2, "1")
        Return c
    End Function

    ''' <summary>Cylinder with a 5/2 single-solenoid valve; shared by the electro-pneumatic examples.</summary>
    Private Function SolenoidPowerPart(c As Circuit) As Valve52
        Dim cyl = c.Add(New DoubleActingCylinder(), 200, 60, "1A")
        Dim v = c.Add(V52(ValveActuator.Solenoid, ValveReturn.Spring), 110, 190, "1V1")
        v.SolenoidLabel = "1M1"
        Dim s = c.Add(New AirSupply(), 200, 290, "0Z")
        c.Connect(v, "4", cyl, "1")
        c.Connect(v, "2", cyl, "2")
        c.Connect(s, "1", v, "1")
        Return v
    End Function

    Private Function ElectroDirect() As Circuit
        Dim c As New Circuit()
        SolenoidPowerPart(c)
        Dim plus = c.Add(New PowerTerminal(), 450, 60)
        Dim s1 = c.Add(Contact(ContactOperator.PushButton, False, "S1"), 450, 120)
        Dim coil = c.Add(New ElectricCoil() With {.Kind = CoilKind.Solenoid, .Label = "1M1"}, 450, 220)
        Dim zero = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 450, 300)
        c.Connect(plus, "1", s1, "1")
        c.Connect(s1, "2", coil, "A1")
        c.Connect(coil, "A2", zero, "1")
        Return c
    End Function

    Private Function ElectroLatch() As Circuit
        Dim c As New Circuit()
        SolenoidPowerPart(c)
        ' Rung 1: S0 (stop, NC) in series with S1 (start) in parallel with the holding contact K1.
        Dim p1 = c.Add(New PowerTerminal(), 450, 40)
        Dim s0 = c.Add(Contact(ContactOperator.PushButton, True, "S0"), 450, 100)
        Dim s1 = c.Add(Contact(ContactOperator.PushButton, False, "S1"), 450, 190)
        Dim hold = c.Add(Contact(ContactOperator.Relay, False, "", "K1"), 530, 190)
        Dim k1 = c.Add(New ElectricCoil() With {.Label = "K1"}, 450, 290)
        Dim z1 = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 450, 370)
        c.Connect(p1, "1", s0, "1")
        c.Connect(s0, "2", s1, "1")
        c.Connect(s0, "2", hold, "1")
        c.Connect(s1, "2", k1, "A1")
        Dim join = c.Connect(hold, "2", k1, "A1")
        join.Mid = 280
        c.Connect(k1, "A2", z1, "1")
        ' Rung 2: K1 operates the valve solenoid. Rung 3: K1 lights the lamp.
        Dim p2 = c.Add(New PowerTerminal(), 650, 40)
        Dim k1a = c.Add(Contact(ContactOperator.Relay, False, "", "K1"), 650, 100)
        Dim sol = c.Add(New ElectricCoil() With {.Kind = CoilKind.Solenoid, .Label = "1M1"}, 650, 290)
        Dim z2 = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 650, 370)
        c.Connect(p2, "1", k1a, "1")
        c.Connect(k1a, "2", sol, "A1")
        c.Connect(sol, "A2", z2, "1")
        Dim p3 = c.Add(New PowerTerminal(), 770, 40)
        Dim k1b = c.Add(Contact(ContactOperator.Relay, False, "", "K1"), 770, 100)
        Dim lamp = c.Add(New ElectricCoil() With {.Kind = CoilKind.Lamp, .Label = "H1"}, 770, 290)
        Dim z3 = c.Add(New PowerTerminal() With {.Polarity = Polarity.Zero0V}, 770, 370)
        c.Connect(p3, "1", k1b, "1")
        c.Connect(k1b, "2", lamp, "A1")
        c.Connect(lamp, "A2", z3, "1")
        c.Add(New TextNote() With {.Text = "Press S1 to start, S0 to stop." & vbCrLf & "K1 holds itself through its own contact."}, 450, 420)
        Return c
    End Function
End Module
