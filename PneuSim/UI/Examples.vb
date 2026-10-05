''' <summary>Ready-made circuits that demonstrate the basic pneumatic control techniques.</summary>
Public Module Examples

    Public ReadOnly All As (Name As String, Build As Func(Of Circuit))() = {
        ("1. Direct control of a single-acting cylinder", AddressOf DirectSingleActing),
        ("2. Indirect control of a double-acting cylinder with speed control", AddressOf IndirectDoubleActing),
        ("3. OR: operation from two places (shuttle valve)", AddressOf OrControl),
        ("4. AND: two-hand safety control (two-pressure valve)", AddressOf TwoHandControl),
        ("5. Automatic reciprocation with limit valves", AddressOf Reciprocation),
        ("6. Delayed return with a time delay valve", AddressOf DelayedReturn)
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
End Module
