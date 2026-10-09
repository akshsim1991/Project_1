Imports System.Drawing.Drawing2D

''' <summary>
''' Sectional drawings of all components except directional valves (those are in
''' <see cref="CutawayView"/>). Each drawing shows the moving parts in their live state and the
''' fluid colour of each chamber; it returns a one-line description of what is happening.
''' </summary>
Public Module Cutaways

    ' ------------------------------------------------------------------ helpers

    ''' <summary>Colour of the fluid at a port: air, oil or current when pressurized, pale otherwise.</summary>
    Public Function Fluid(p As Port, pal As CutawayPalette) As Color
        If p Is Nothing OrElse Not p.IsPressurized Then Return pal.Vent
        Select Case p.Kind
            Case PortKind.Hydraulic : Return pal.Oil
            Case PortKind.Electric : Return Color.FromArgb(220, 40, 40)
            Case Else : Return pal.Air
        End Select
    End Function

    ''' <summary>Horizontal spring between x1 and x2.</summary>
    Public Sub Zigzag(g As Graphics, pen As Pen, x1 As Single, x2 As Single, cy As Single, amp As Single, Optional turns As Integer = 8)
        If Math.Abs(x2 - x1) < 2 Then Return
        Dim pts As New List(Of PointF)
        For i = 0 To turns
            pts.Add(New PointF(x1 + (x2 - x1) * i / turns, cy + If(i = 0 OrElse i = turns, 0, If(i Mod 2 = 0, -amp, amp))))
        Next
        g.DrawLines(pen, pts.ToArray())
    End Sub

    ''' <summary>Vertical spring between y1 and y2.</summary>
    Private Sub ZigzagV(g As Graphics, pen As Pen, cx As Single, y1 As Single, y2 As Single, amp As Single, Optional turns As Integer = 8)
        If Math.Abs(y2 - y1) < 2 Then Return
        Dim pts As New List(Of PointF)
        For i = 0 To turns
            pts.Add(New PointF(cx + If(i = 0 OrElse i = turns, 0, If(i Mod 2 = 0, -amp, amp)), y1 + (y2 - y1) * i / turns))
        Next
        g.DrawLines(pen, pts.ToArray())
    End Sub

    ''' <summary>Scales a design of w × h units into the drawing area, centred.</summary>
    Private Sub Fit(g As Graphics, area As RectangleF, w As Single, h As Single)
        Dim s = Math.Min(1.6F, Math.Min(area.Width / w, area.Height / h))
        g.TranslateTransform(area.X + (area.Width - w * s) / 2, area.Y + (area.Height - h * s) / 2)
        g.ScaleTransform(s, s)
    End Sub

    Private Sub Fill(g As Graphics, c As Color, x As Single, y As Single, w As Single, h As Single)
        Using b As New SolidBrush(c)
            g.FillRectangle(b, x, y, w, h)
        End Using
    End Sub

    Private Sub Metal(g As Graphics, pal As CutawayPalette, x As Single, y As Single, w As Single, h As Single, Optional dark As Boolean = False)
        Fill(g, If(dark, pal.MetalDark, pal.Metal), x, y, w, h)
        Using p As New Pen(pal.Edge)
            g.DrawRectangle(p, x, y, w, h)
        End Using
    End Sub

    Private Sub Disc(g As Graphics, c As Color, edge As Color, cx As Single, cy As Single, r As Single)
        Using b As New SolidBrush(c), p As New Pen(edge)
            g.FillEllipse(b, cx - r, cy - r, 2 * r, 2 * r)
            g.DrawEllipse(p, cx - r, cy - r, 2 * r, 2 * r)
        End Using
    End Sub

    Private Sub Txt(g As Graphics, pal As CutawayPalette, s As String, x As Single, y As Single, Optional bold As Boolean = False, Optional faint As Boolean = False)
        Using f As New Font("Segoe UI", 8.5F, If(bold, FontStyle.Bold, FontStyle.Regular)), b As New SolidBrush(If(faint, pal.SubText, pal.Text))
            g.DrawString(s, f, b, x, y)
        End Using
    End Sub

    Private Sub FlowArrow(g As Graphics, x1 As Single, y1 As Single, x2 As Single, y2 As Single)
        Using p As New Pen(Color.White, 2) With {.EndCap = LineCap.ArrowAnchor}
            g.DrawLine(p, x1, y1, x2, y2)
        End Using
    End Sub

    Private Function Lighter(c As Color, back As Color) As Color
        Return Color.FromArgb((CInt(c.R) + back.R) \ 2, (CInt(c.G) + back.G) \ 2, (CInt(c.B) + back.B) \ 2)
    End Function

    ''' <summary>True if air or oil flows from port a towards port b.</summary>
    Private Function Flows(a As Port, b As Port) As Boolean
        Return a.State = PortState.Pressurized AndAlso (b.State <> PortState.Pressurized OrElse a.Pressure > b.Pressure + 0.05)
    End Function

    ' ------------------------------------------------------------------ dispatcher

    ''' <summary>True if the component has a cutaway drawing (all but wiring and drawing aids).</summary>
    Public Function Supports(e As CircuitElement) As Boolean
        Return Not (TypeOf e Is PowerTerminal OrElse TypeOf e Is Junction OrElse TypeOf e Is TextNote OrElse TypeOf e Is PageConnector)
    End Function

    ''' <summary>Draws the cutaway of a component (not a directional valve) and returns a description of its state.</summary>
    Public Function Draw(g As Graphics, e As CircuitElement, area As RectangleF, pal As CutawayPalette, phase As Double, simulating As Boolean) As String
        Dim state = g.Save()
        Try
            Select Case True
                Case TypeOf e Is SemiRotaryActuator : Return SemiRotary(g, DirectCast(e, SemiRotaryActuator), area, pal)
                Case TypeOf e Is Gripper : Return GripperView(g, DirectCast(e, Gripper), area, pal)
                Case TypeOf e Is PressureSwitch : Return PressureSwitchView(g, DirectCast(e, PressureSwitch), area, pal)
                Case TypeOf e Is ShutOffValve : Return ShutOffView(g, DirectCast(e, ShutOffValve), area, pal)
                Case TypeOf e Is VacuumGenerator : Return EjectorView(g, DirectCast(e, VacuumGenerator), area, pal)
                Case TypeOf e Is SuctionCup : Return CupView(g, DirectCast(e, SuctionCup), area, pal)
                Case TypeOf e Is Compressor : Return CompressorView(g, DirectCast(e, Compressor), area, pal, phase)
                Case TypeOf e Is AirReceiver : Return ReceiverView(g, DirectCast(e, AirReceiver), area, pal)
                Case TypeOf e Is FlowMeter : Return FlowMeterView(g, DirectCast(e, FlowMeter), area, pal)
                Case TypeOf e Is ForceSensor : Return ForceSensorView(g, DirectCast(e, ForceSensor), area, pal)
                Case TypeOf e Is ElectricCounter : Return CounterView(g, DirectCast(e, ElectricCounter), area, pal, simulating)
                Case TypeOf e Is CylinderBase : Return Cylinder(g, DirectCast(e, CylinderBase), area, pal)
                Case TypeOf e Is AirMotor : Return VaneMotor(g, e, DirectCast(e, AirMotor).Rpm, area, pal, phase)
                Case TypeOf e Is HydraulicMotor : Return VaneMotor(g, e, DirectCast(e, HydraulicMotor).Rpm, area, pal, phase)
                Case TypeOf e Is HydraulicPump : Return GearPump(g, DirectCast(e, HydraulicPump), area, pal, phase, simulating)
                Case TypeOf e Is CheckValve : Return Check(g, DirectCast(e, CheckValve), area, pal)
                Case TypeOf e Is ShuttleValve : Return Shuttle(g, DirectCast(e, ShuttleValve), area, pal)
                Case TypeOf e Is TwoPressureValve : Return TwoPressure(g, DirectCast(e, TwoPressureValve), area, pal)
                Case TypeOf e Is QuickExhaustValve : Return QuickExhaust(g, DirectCast(e, QuickExhaustValve), area, pal)
                Case TypeOf e Is FlowControlValve : Return Throttle(g, DirectCast(e, FlowControlValve), area, pal)
                Case TypeOf e Is CompensatedFlowControl : Return Compensated(g, DirectCast(e, CompensatedFlowControl), area, pal)
                Case TypeOf e Is PressureRegulator : Return Regulator(g, DirectCast(e, PressureRegulator), area, pal)
                Case TypeOf e Is ReliefValve : Return Relief(g, DirectCast(e, ReliefValve), area, pal)
                Case TypeOf e Is CounterbalanceValve : Return Counterbalance(g, DirectCast(e, CounterbalanceValve), area, pal)
                Case TypeOf e Is Accumulator : Return AccumulatorView(g, DirectCast(e, Accumulator), area, pal)
                Case TypeOf e Is PressureGauge : Return Gauge(g, DirectCast(e, PressureGauge), area, pal)
                Case TypeOf e Is Silencer : Return SilencerView(g, DirectCast(e, Silencer), area, pal)
                Case TypeOf e Is AirSupply : Return Supply(g, DirectCast(e, AirSupply), area, pal, simulating)
                Case TypeOf e Is HydraulicTank : Return Tank(g, DirectCast(e, HydraulicTank), area, pal)
                Case TypeOf e Is ElectricCoil : Return Coil(g, DirectCast(e, ElectricCoil), area, pal, simulating)
                Case TypeOf e Is ElectricContact : Return Contact(g, DirectCast(e, ElectricContact), area, pal, simulating)
                Case Else
                    Txt(g, pal, "This is a wiring or drawing symbol; there is nothing inside to show.", area.X + 4, area.Y + 10, faint:=True)
                    Return ""
            End Select
        Finally
            g.Restore(state)
        End Try
    End Function

    ' ------------------------------------------------------------------ actuators

    Private Function Cylinder(g As Graphics, c As CylinderBase, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 540, 170)
        Dim doubleActing = c.Ports.Count >= 2
        Dim hydraulic = c.Ports(0).Kind = PortKind.Hydraulic
        Dim pos = CSng(c.Position)
        ' Barrel 30..262 with end caps; bore 42..250; piston 22 wide travels 186.
        Metal(g, pal, 30, 50, 232, 70)
        Fill(g, pal.Cavity, 42, 60, 208, 50)
        Dim px = 42 + pos * (208 - 22)
        Fill(g, Fluid(c.Ports(0), pal), 42, 60, px - 42, 50)
        If doubleActing Then
            Fill(g, Fluid(c.Ports(1), pal), px + 22, 60, 250 - px - 22, 50)
        Else
            Using sp As New Pen(pal.Spring, 1.5F)
                Zigzag(g, sp, px + 22, 248, 85, 18, 10)
            End Using
        End If
        ' Piston with seals; the rod always reaches out through the gland in the end cap.
        Metal(g, pal, px, 60, 22, 50, dark:=True)
        Fill(g, Color.Black, px + 8, 60, 6, 3) : Fill(g, Color.Black, px + 8, 107, 6, 3)
        Dim tip = px + 22 + 220
        Metal(g, pal, px + 22, 78, tip - px - 22, 14)
        Metal(g, pal, 250, 74, 12, 22, dark:=True)
        Metal(g, pal, tip, 72, 10, 26, dark:=True)
        ' Ports.
        Fill(g, Fluid(c.Ports(0), pal), 44, 26, 12, 34)
        Txt(g, pal, If(hydraulic, "A", "1"), 44, 8, bold:=True)
        If doubleActing Then
            Fill(g, Fluid(c.Ports(1), pal), 234, 26, 12, 34)
            Txt(g, pal, If(hydraulic, "B", "2"), 234, 8, bold:=True)
        Else
            Fill(g, pal.Vent, 236, 46, 8, 14)
            Txt(g, pal, "vent", 222, 28, faint:=True)
        End If
        If Math.Abs(c.LoadForceN) > 0.5 Then
            Using p As New Pen(pal.Text, 2) With {.EndCap = LineCap.ArrowAnchor}
                If c.LoadForceN > 0 Then g.DrawLine(p, tip + 50, 85, tip + 14, 85) Else g.DrawLine(p, tip + 14, 85, tip + 50, 85)
            End Using
            Txt(g, pal, $"load {Math.Abs(c.LoadForceN):0} N", tip + 4, 100, faint:=True)
        End If
        Dim pressures = $"pressure side {If(hydraulic, "A", "1")}: {c.CapPressure:0.0} bar" &
                        If(doubleActing, $",  side {If(hydraulic, "B", "2")}: {c.RodPressure:0.0} bar", "")
        Return $"Piston at {pos * c.StrokeLength:0} of {c.StrokeLength} mm;  {pressures}"
    End Function

    Private Function SemiRotary(g As Graphics, c As SemiRotaryActuator, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 360, 190)
        Dim cx = 180.0F, cy = 110.0F
        Disc(g, pal.Metal, pal.Edge, cx, cy, 75)
        Dim angle = 180 + CSng(c.Position) * 180
        Using b1 As New SolidBrush(Fluid(c.Ports(0), pal)), b2 As New SolidBrush(Fluid(c.Ports(1), pal))
            g.FillPie(b1, cx - 62, cy - 62, 124, 124, 180, angle - 180)
            g.FillPie(b2, cx - 62, cy - 62, 124, 124, angle, 360 - angle)
        End Using
        Metal(g, pal, cx - 62, cy, 124, 10, dark:=True)
        Dim a = angle * Math.PI / 180
        Using p As New Pen(pal.MetalDark, 9)
            g.DrawLine(p, cx, cy, CSng(cx + 60 * Math.Cos(a)), CSng(cy + 60 * Math.Sin(a)))
        End Using
        Disc(g, pal.MetalDark, pal.Edge, cx, cy, 12)
        Fill(g, Fluid(c.Ports(0), pal), cx - 80, cy - 4, 20, 12) : Txt(g, pal, "1", cx - 100, cy - 6, bold:=True)
        Fill(g, Fluid(c.Ports(1), pal), cx + 60, cy - 4, 20, 12) : Txt(g, pal, "2", cx + 86, cy - 6, bold:=True)
        Return $"Vane turned to {c.Position * 180:0}° of 180°; air on 1 turns it clockwise, air on 2 turns it back."
    End Function

    Private Function VaneMotor(g As Graphics, e As CircuitElement, rpm As Double, area As RectangleF, pal As CutawayPalette, phase As Double) As String
        Fit(g, area, 360, 200)
        Dim cx = 180.0F, cy = 100.0F
        Disc(g, pal.Metal, pal.Edge, cx, cy, 80)
        Dim inlet = e.Ports(0)
        Dim outlet = If(e.Ports.Count > 1, e.Ports(1), Nothing)
        Using b1 As New SolidBrush(Fluid(If(rpm >= 0, inlet, outlet), pal)), b2 As New SolidBrush(If(outlet Is Nothing, pal.Vent, Fluid(If(rpm >= 0, outlet, inlet), pal)))
            g.FillPie(b1, cx - 66, cy - 66, 132, 132, 90, 180)
            g.FillPie(b2, cx - 66, cy - 66, 132, 132, 270, 180)
        End Using
        ' Eccentric rotor with sliding vanes.
        Dim rx = cx + 14
        For k = 0 To 5
            Dim a = (phase + k * 60) * Math.PI / 180
            Dim dx = Math.Cos(a), dy = Math.Sin(a)
            ' Distance from the rotor centre along the vane to the housing bore (radius 66).
            Dim bx = rx - cx
            Dim t = -bx * dx + Math.Sqrt((bx * dx) ^ 2 - (bx * bx - 66 * 66))
            Using p As New Pen(pal.MetalDark, 4)
                g.DrawLine(p, CSng(rx + 30 * dx), CSng(cy + 30 * dy), CSng(rx + t * dx), CSng(cy + t * dy))
            End Using
        Next
        Disc(g, pal.MetalDark, pal.Edge, rx, cy, 42)
        Disc(g, pal.Metal, pal.Edge, rx, cy, 8)
        Fill(g, Fluid(inlet, pal), cx - 120, cy - 7, 56, 14) : Txt(g, pal, If(outlet Is Nothing, "in", inlet.Name), cx - 128, cy - 26, bold:=True)
        Fill(g, If(outlet Is Nothing, pal.Vent, Fluid(outlet, pal)), cx + 64, cy - 7, 56, 14)
        Txt(g, pal, If(outlet Is Nothing, "exhaust", outlet.Name), cx + 100, cy - 26, bold:=True)
        Return If(Math.Abs(rpm) < 0.5, "Stopped: no flow through the motor.", $"Turning at {Math.Abs(rpm):0} rpm: the flow pushes the vanes round.")
    End Function

    Private Function GearPump(g As Graphics, p As HydraulicPump, area As RectangleF, pal As CutawayPalette, phase As Double, simulating As Boolean) As String
        Fit(g, area, 400, 190)
        Metal(g, pal, 110, 40, 180, 110)
        Disc(g, pal.Cavity, pal.Edge, 170, 95, 36)
        Disc(g, pal.Cavity, pal.Edge, 230, 95, 36)
        Dim suction = Lighter(pal.Oil, pal.Back)
        Fill(g, suction, 30, 85, 104, 20)
        Fill(g, Fluid(p.Ports(0), pal), 266, 85, 104, 20)
        Gear(g, pal, 170, 95, CSng(phase))
        Gear(g, pal, 230, 95, CSng(-phase + 18))
        Txt(g, pal, "from tank (suction)", 22, 110, faint:=True)
        Txt(g, pal, "P (pressure)", 290, 110, bold:=True)
        Metal(g, pal, 180, 150, 40, 30, dark:=True)
        Txt(g, pal, "M", 192, 156, bold:=True)
        Dim running = Not simulating OrElse p.Running
        Return If(running, $"The motor turns the gears; oil is carried round the outside and pushed out at P ({p.FlowLpm:0.#} l/min, {p.Ports(0).Pressure:0} bar).",
                  "Pump stopped (click it during simulation to start it).")
    End Function

    Private Sub Gear(g As Graphics, pal As CutawayPalette, cx As Single, cy As Single, phaseDeg As Single)
        Dim pts As New List(Of PointF)
        For k = 0 To 39
            Dim a = (phaseDeg + k * 9) * Math.PI / 180
            Dim r = If((k \ 2) Mod 2 = 0, 34, 27)
            pts.Add(New PointF(CSng(cx + r * Math.Cos(a)), CSng(cy + r * Math.Sin(a))))
        Next
        Using b As New SolidBrush(pal.MetalDark), p As New Pen(pal.Edge)
            g.FillPolygon(b, pts.ToArray())
            g.DrawPolygon(p, pts.ToArray())
        End Using
        Disc(g, pal.Metal, pal.Edge, cx, cy, 6)
    End Sub

    ' ------------------------------------------------------------------ non-return and logic valves

    Private Function Check(g As Graphics, v As CheckValve, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 400, 150)
        Metal(g, pal, 20, 40, 360, 70)
        Dim open = Flows(v.Ports(0), v.Ports(1))
        Fill(g, Fluid(v.Ports(0), pal), 20, 60, 170, 30)
        Fill(g, Fluid(v.Ports(1), pal), 190, 60, 190, 30)
        ' Conical seat and the ball, held by a light spring.
        Using b As New SolidBrush(pal.Metal), pe As New Pen(pal.Edge)
            Dim top = {New PointF(176, 60), New PointF(196, 60), New PointF(186, 70)}
            Dim bottom = {New PointF(176, 90), New PointF(196, 90), New PointF(186, 80)}
            g.FillPolygon(b, top) : g.DrawPolygon(pe, top)
            g.FillPolygon(b, bottom) : g.DrawPolygon(pe, bottom)
        End Using
        Dim ballX = If(open, 220.0F, 199.0F)
        Using sp As New Pen(pal.Spring, 1.5F)
            Zigzag(g, sp, ballX + 13, 300, 75, 7)
        End Using
        Metal(g, pal, 300, 62, 8, 26, dark:=True)
        Disc(g, pal.MetalDark, pal.Edge, ballX, 75, 13)
        If open Then FlowArrow(g, 40, 75, 160, 75)
        Txt(g, pal, "1", 22, 18, bold:=True) : Txt(g, pal, "2", 364, 18, bold:=True)
        Return If(open, "Flow from 1 lifts the ball off its seat: the valve is open.", "The ball sits on its seat: flow from 2 to 1 is blocked.")
    End Function

    Private Function Shuttle(g As Graphics, v As ShuttleValve, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 400, 160)
        Metal(g, pal, 20, 50, 360, 70)
        Fill(g, Fluid(v.Ports(0), pal), 20, 70, 110, 30)
        Fill(g, Fluid(v.Ports(1), pal), 270, 70, 110, 30)
        Fill(g, Fluid(v.Ports(2), pal), 130, 70, 140, 30)
        Fill(g, Fluid(v.Ports(2), pal), 192, 10, 16, 60)
        For Each sx In {130.0F, 270.0F}
            Metal(g, pal, sx - 4, 70, 8, 8) : Metal(g, pal, sx - 4, 92, 8, 8)
        Next
        ' The ball is pushed against the seat of the input without pressure.
        Dim ballX = If(v.ConnectedInput = 0, 255.0F, 145.0F)
        Disc(g, pal.MetalDark, pal.Edge, ballX, 85, 14)
        Txt(g, pal, "1 (left input)", 22, 124, faint:=True) : Txt(g, pal, "1 (right input)", 296, 124, faint:=True)
        Txt(g, pal, "2 (output)", 214, 12, bold:=True)
        Return $"OR: the ball closes the side without air; the output is joined to the {If(v.ConnectedInput = 0, "left", "right")} input."
    End Function

    Private Function TwoPressure(g As Graphics, v As TwoPressureValve, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 400, 160)
        Metal(g, pal, 20, 50, 360, 70)
        Fill(g, Fluid(v.Ports(0), pal), 20, 70, 110, 30)
        Fill(g, Fluid(v.Ports(1), pal), 270, 70, 110, 30)
        Fill(g, Fluid(v.Ports(2), pal), 130, 70, 140, 30)
        Fill(g, Fluid(v.Ports(2), pal), 192, 10, 16, 60)
        For Each sx In {130.0F, 270.0F}
            Metal(g, pal, sx - 4, 70, 8, 8) : Metal(g, pal, sx - 4, 92, 8, 8)
        Next
        ' The spool closes the seat of the input with the higher pressure; the lower one feeds the output.
        Dim sh = If(v.ConnectedInput = 0, 8.0F, -8.0F)
        Metal(g, pal, 150 + sh, 82, 100, 6, dark:=True)
        Metal(g, pal, 136 + sh, 72, 10, 26, dark:=True)
        Metal(g, pal, 254 + sh, 72, 10, 26, dark:=True)
        Txt(g, pal, "1 (left input)", 22, 124, faint:=True) : Txt(g, pal, "1 (right input)", 296, 124, faint:=True)
        Txt(g, pal, "2 (output)", 214, 12, bold:=True)
        Dim both = v.Ports(0).IsPressurized AndAlso v.Ports(1).IsPressurized
        Return If(both, "AND: both inputs have air, so air reaches the output.", "AND: the spool closes the input with air; the output gets air only when both inputs have it.")
    End Function

    Private Function QuickExhaust(g As Graphics, v As QuickExhaustValve, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 320, 220)
        Metal(g, pal, 80, 50, 160, 120)
        Fill(g, Fluid(v.Ports(0), pal), 0, 98, 120, 24)
        Fill(g, Fluid(v.Ports(1), pal), 120, 80, 80, 60)
        Fill(g, Fluid(v.Ports(1), pal), 148, 0, 24, 80)
        Fill(g, If(v.Feeding, pal.Vent, Fluid(v.Ports(1), pal)), 148, 140, 24, 80)
        ' The disc closes the exhaust while feeding, and the inlet while exhausting.
        If v.Feeding Then Metal(g, pal, 132, 136, 56, 8, dark:=True) Else Metal(g, pal, 116, 92, 8, 36, dark:=True)
        Txt(g, pal, "1 (from valve)", 0, 72, bold:=True) : Txt(g, pal, "2 (to cylinder)", 178, 4, bold:=True)
        Txt(g, pal, "3 (exhaust)", 178, 196, bold:=True)
        Return If(v.Feeding, "Feeding: air flows 1 → 2; the disc seals the exhaust 3.",
                  "Exhausting: the disc closes 1, so the cylinder air escapes straight through 3.")
    End Function

    ' ------------------------------------------------------------------ flow and pressure control

    Private Function Throttle(g As Graphics, v As FlowControlValve, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 420, 190)
        Metal(g, pal, 20, 60, 380, 60)
        Fill(g, Fluid(v.Ports(0), pal), 20, 78, 190, 24)
        Fill(g, Fluid(v.Ports(1), pal), 210, 78, 190, 24)
        ' Needle screwed down into the passage.
        Dim tipY = 78 + 23 * (1 - v.OpeningPercent / 100.0F)
        Metal(g, pal, 204, 14, 12, 50)
        Metal(g, pal, 190, 6, 40, 10, dark:=True)
        Using b As New SolidBrush(pal.MetalDark)
            g.FillPolygon(b, {New PointF(198, 60), New PointF(222, 60), New PointF(210, CSng(tipY))})
        End Using
        Dim oneWay = v.HasCheckValve
        If oneWay Then
            Fill(g, Fluid(v.Ports(0), pal), 60, 102, 12, 40) : Fill(g, Fluid(v.Ports(1), pal), 350, 102, 12, 40)
            Metal(g, pal, 20, 120, 380, 40)
            Fill(g, Fluid(v.Ports(0), pal), 60, 130, 140, 16) : Fill(g, Fluid(v.Ports(1), pal), 200, 130, 162, 16)
            Dim free = Flows(v.Ports(0), v.Ports(1))
            Disc(g, pal.MetalDark, pal.Edge, If(free, 214.0F, 203.0F), 138, 8)
            Metal(g, pal, 190, 128, 5, 5) : Metal(g, pal, 190, 143, 5, 5)
            Txt(g, pal, "check valve (free flow 1 → 2)", 120, 164, faint:=True)
        End If
        Txt(g, pal, "1", 22, 40, bold:=True) : Txt(g, pal, "2", 388, 40, bold:=True)
        Txt(g, pal, $"{v.OpeningPercent}% open", 236, 22, faint:=True)
        Return If(oneWay, $"Flow 2 → 1 must pass the needle ({v.OpeningPercent}% open); flow 1 → 2 lifts the check ball and passes freely.",
                  $"Both directions pass the needle, {v.OpeningPercent}% open.")
    End Function

    Private Function Compensated(g As Graphics, v As CompensatedFlowControl, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 420, 180)
        Metal(g, pal, 20, 50, 380, 90)
        Fill(g, Fluid(v.Ports(0), pal), 20, 82, 120, 24)
        Fill(g, Fluid(v.Ports(0), pal), 140, 64, 100, 60)
        Fill(g, Fluid(v.Ports(1), pal), 280, 82, 120, 24)
        ' Compensator spool (keeps the pressure drop across the orifice constant) and the orifice.
        Metal(g, pal, 150, 70, 40, 48, dark:=True)
        Using sp As New Pen(pal.Spring, 1.5F)
            Zigzag(g, sp, 190, 236, 94, 10)
        End Using
        Metal(g, pal, 250, 50, 14, 38) : Metal(g, pal, 250, 100, 14, 40)
        Fill(g, Fluid(v.Ports(1), pal), 264, 82, 16, 24)
        Txt(g, pal, "compensator", 140, 30, faint:=True) : Txt(g, pal, "orifice", 244, 30, faint:=True)
        Txt(g, pal, "1", 22, 30, bold:=True) : Txt(g, pal, "2", 388, 30, bold:=True)
        Return $"The compensator spool keeps a constant pressure drop across the orifice, so {v.FlowLpm:0.#} l/min flows whatever the load."
    End Function

    Private Function Regulator(g As Graphics, v As PressureRegulator, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 400, 220)
        Metal(g, pal, 40, 30, 320, 100)
        Fill(g, Fluid(v.Ports(0), pal), 40, 60, 140, 22)
        Fill(g, Fluid(v.Ports(1), pal), 220, 60, 140, 22)
        Fill(g, Fluid(v.Ports(1), pal), 180, 82, 40, 34)
        ' Poppet: open while the outlet is below the setting.
        Dim live = v.Ports(0).IsPressurized
        Dim open = live AndAlso v.Ports(1).Pressure < v.Setting - 0.05
        Fill(g, If(open, Fluid(v.Ports(0), pal), pal.MetalDark), 190, 56, 20, 30)
        Metal(g, pal, 186, If(open, 84.0F, 76.0F), 28, 8, dark:=True)
        ' Diaphragm on the outlet pressure, spring and adjusting screw underneath.
        Metal(g, pal, 150, 116, 100, 6, dark:=True)
        Using sp As New Pen(pal.Spring, 1.5F)
            ZigzagV(g, sp, 200, 122, 190, 14, 10)
        End Using
        Metal(g, pal, 186, 190, 28, 10, dark:=True)
        Metal(g, pal, 196, 200, 8, 18)
        Txt(g, pal, "1 (in)", 42, 40, bold:=True) : Txt(g, pal, "2 (out)", 318, 40, bold:=True)
        Txt(g, pal, $"set to {v.Setting:0.#} bar", 222, 160, faint:=True)
        If Not live Then Return $"No supply at 1. When there is, the spring opens the poppet until the outlet reaches {v.Setting:0.#} bar."
        Return $"Outlet {v.Ports(1).Pressure:0.0} bar (setting {v.Setting:0.#} bar): " &
               If(open, "the spring holds the poppet open.", "the outlet pressure on the diaphragm closes the poppet.")
    End Function

    Private Function Relief(g As Graphics, v As ReliefValve, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 320, 230)
        Metal(g, pal, 90, 20, 140, 170)
        Dim pIn = v.Ports(0)
        Dim open = pIn.IsPressurized AndAlso pIn.Pressure >= v.Setting - 0.5
        Fill(g, Fluid(pIn, pal), 150, 140, 20, 90)
        Fill(g, If(open, Fluid(pIn, pal), Lighter(pal.Oil, pal.Back)), 170, 106, 150, 20)
        Fill(g, If(open, Fluid(pIn, pal), pal.Cavity), 136, 100, 48, 40)
        ' Poppet on its seat, pressed by the spring.
        Dim lift = If(open, 12.0F, 0F)
        Using b As New SolidBrush(pal.MetalDark)
            g.FillPolygon(b, {New PointF(144, 126 - lift), New PointF(176, 126 - lift), New PointF(160, 142 - lift)})
        End Using
        Using sp As New Pen(pal.Spring, 1.5F)
            ZigzagV(g, sp, 160, 40, 126 - lift, 14, 10)
        End Using
        Metal(g, pal, 140, 30, 40, 10, dark:=True)
        Txt(g, pal, "P (from pump)", 176, 206, bold:=True) : Txt(g, pal, "T (to tank)", 236, 84, bold:=True)
        Txt(g, pal, $"opens at {v.Setting:0} bar", 0, 30, faint:=True)
        Return If(open, $"Open: the pressure ({pIn.Pressure:0} bar) lifts the poppet and the oil returns to tank.",
                  $"Closed: {pIn.Pressure:0} bar is below the setting of {v.Setting:0} bar.")
    End Function

    Private Function Counterbalance(g As Graphics, v As CounterbalanceValve, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 420, 220)
        Metal(g, pal, 40, 50, 340, 120)
        Fill(g, Fluid(v.Ports(0), pal), 40, 120, 130, 20)
        Fill(g, Fluid(v.Ports(1), pal), 250, 120, 130, 20)
        Fill(g, If(v.IsOpen, Fluid(v.Ports(1), pal), pal.Cavity), 170, 116, 80, 28)
        Dim lift = If(v.IsOpen, 10.0F, 0F)
        Metal(g, pal, 200, 104 - lift, 20, 18, dark:=True)
        Using sp As New Pen(pal.Spring, 1.5F)
            ZigzagV(g, sp, 210, 60, 104 - lift, 10, 8)
        End Using
        ' Pilot X opens it from above; the check valve below lets oil in freely (1 → 2).
        If v.ExternalPilot Then
            Fill(g, Fluid(v.GetPort("X"), pal), 120, 0, 16, 90)
            Metal(g, pal, 112, 86, 32, 8, dark:=True)
        End If
        Fill(g, Fluid(v.Ports(0), pal), 90, 150, 240, 12)
        Disc(g, pal.MetalDark, pal.Edge, If(Flows(v.Ports(0), v.Ports(1)), 226.0F, 214.0F), 156, 6)
        Txt(g, pal, "1 (valve)", 42, 100, bold:=True) : Txt(g, pal, "2 (cylinder)", 320, 100, bold:=True)
        If v.ExternalPilot Then Txt(g, pal, "X (pilot)", 140, 4, bold:=True)
        Dim medium = If(v.Hydraulic, "oil", "air")
        Return If(v.IsOpen, $"{v.Opening * 100:0} % open: the load pressure{If(v.ExternalPilot, " or the pilot X", "")} has lifted the poppet; the {medium} escapes throttled, so the load moves down under control.",
                  $"Closed: the poppet holds the {medium} in the cylinder, so the load cannot fall.")
    End Function

    Private Function AccumulatorView(g As Graphics, v As Accumulator, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 260, 250)
        Metal(g, pal, 80, 10, 100, 200)
        Fill(g, pal.Cavity, 90, 20, 80, 180)
        Dim level = CSng(Math.Min(1, v.StoredLitres / Math.Max(0.01, v.CapacityL)))
        Dim oilTop = 200 - 170 * level
        Fill(g, Color.FromArgb(200, 205, 215), 90, 20, 80, oilTop - 20)
        Fill(g, If(level > 0, pal.Oil, pal.Vent), 90, oilTop, 80, 200 - oilTop)
        Metal(g, pal, 90, oilTop - 8, 80, 8, dark:=True)
        Fill(g, Fluid(v.Ports(0), pal), 122, 210, 16, 40)
        Txt(g, pal, $"gas (N₂) {If(v.StoredLitres > 0.001, v.GasPressure, v.PrechargeBar):0} bar", 186, 30, faint:=True)
        Txt(g, pal, $"oil {v.StoredLitres:0.00} l", 186, CSng(Math.Min(190, oilTop + 6)), faint:=True)
        Return If(v.StoredLitres > 0.001, $"{v.StoredLitres:0.00} l of oil stored against the gas cushion at {v.GasPressure:0} bar.",
                  $"Empty: the gas is pre-charged to {v.PrechargeBar:0} bar; oil enters when the system pressure is higher.")
    End Function

    Private Function Gauge(g As Graphics, v As PressureGauge, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 260, 230)
        Dim cx = 130.0F, cy = 100.0F
        Disc(g, pal.Cavity, pal.Edge, cx, cy, 90)
        Dim maxP = If(v.Hydraulic, 160, 10)
        Using pe As New Pen(pal.Edge), f As New Font("Segoe UI", 7.5F), tb As New SolidBrush(pal.SubText)
            For k = 0 To 10
                Dim a = (-225 + 27 * k) * Math.PI / 180
                g.DrawLine(pe, CSng(cx + 80 * Math.Cos(a)), CSng(cy + 80 * Math.Sin(a)), CSng(cx + 88 * Math.Cos(a)), CSng(cy + 88 * Math.Sin(a)))
                If k Mod 2 = 0 Then g.DrawString((maxP * k / 10).ToString(), f, tb, CSng(cx + 66 * Math.Cos(a) - 8), CSng(cy + 66 * Math.Sin(a) - 6))
            Next
        End Using
        ' Bourdon tube: pressure tries to straighten it, which moves the needle.
        Using tube As New Pen(Fluid(v.Ports(0), pal), 7)
            g.DrawArc(tube, cx - 45, cy - 45, 90, 90, 150, 240)
        End Using
        Dim p = Math.Min(maxP, Math.Max(0, v.Ports(0).Pressure))
        Dim na = (-225 + 270 * p / maxP) * Math.PI / 180
        Using needle As New Pen(Color.Firebrick, 3)
            g.DrawLine(needle, cx, cy, CSng(cx + 72 * Math.Cos(na)), CSng(cy + 72 * Math.Sin(na)))
        End Using
        Disc(g, pal.MetalDark, pal.Edge, cx, cy, 6)
        Fill(g, Fluid(v.Ports(0), pal), cx - 6, cy + 90, 12, 40)
        Return $"{v.Ports(0).Pressure:0.0} bar: the pressure inside the curved Bourdon tube straightens it a little and turns the needle."
    End Function

    Private Function SilencerView(g As Graphics, v As Silencer, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 240, 220)
        Fill(g, Fluid(v.Ports(0), pal), 110, 0, 20, 60)
        Metal(g, pal, 70, 60, 100, 130)
        Using b As New SolidBrush(pal.MetalDark)
            For yy = 66 To 184 Step 8
                For xx = 76 To 164 Step 8
                    g.FillEllipse(b, xx + ((yy \ 8) Mod 2) * 4 - 2, yy - 2, 4, 4)
                Next
            Next
        End Using
        Txt(g, pal, "porous (sintered) element", 40, 196, faint:=True)
        Return "Exhaust air spreads through the porous element, so it leaves slowly and quietly."
    End Function

    Private Function Supply(g As Graphics, v As AirSupply, area As RectangleF, pal As CutawayPalette, simulating As Boolean) As String
        Fit(g, area, 420, 180)
        Metal(g, pal, 20, 70, 70, 60, dark:=True)
        Txt(g, pal, "M", 46, 90, bold:=True)
        Metal(g, pal, 90, 80, 60, 40)
        Txt(g, pal, "compressor", 82, 130, faint:=True)
        Dim air = If(simulating, pal.Air, pal.Vent)
        Using b As New SolidBrush(pal.Metal), pe As New Pen(pal.Edge)
            g.FillEllipse(b, 170, 50, 200, 100) : g.DrawEllipse(pe, 170, 50, 200, 100)
        End Using
        Using b As New SolidBrush(air)
            g.FillEllipse(b, 178, 58, 184, 84)
        End Using
        Fill(g, air, 150, 94, 28, 12)
        Fill(g, air, 362, 94, 58, 12)
        Txt(g, pal, $"receiver {v.Pressure:0.#} bar", 222, 92, bold:=True)
        Return $"The compressor fills the receiver; the line is kept at {v.Pressure:0.#} bar."
    End Function

    Private Function Tank(g As Graphics, v As HydraulicTank, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 360, 210)
        Metal(g, pal, 40, 40, 280, 150)
        Fill(g, pal.Cavity, 50, 50, 260, 130)
        Fill(g, Lighter(pal.Oil, pal.Back), 50, 100, 260, 80)
        Fill(g, Fluid(v.Ports(0), pal), 100, 0, 16, 150)
        Metal(g, pal, 230, 20, 40, 20, dark:=True)
        Txt(g, pal, "breather", 228, 0, faint:=True)
        Txt(g, pal, "return line (T)", 122, 0, bold:=True)
        Return "Oil returns here, settles and cools; the pump draws it back out."
    End Function

    ' ------------------------------------------------------------------ electrical

    Private Function Coil(g As Graphics, k As ElectricCoil, area As RectangleF, pal As CutawayPalette, simulating As Boolean) As String
        Fit(g, area, 420, 190)
        Dim on_ = simulating AndAlso k.Active
        Dim current = Color.FromArgb(220, 40, 40)
        If k.Kind = CoilKind.Lamp Then
            Using b As New SolidBrush(If(on_, Color.FromArgb(255, 220, 60), pal.Cavity)), pe As New Pen(pal.Edge, 2)
                g.FillEllipse(b, 160, 20, 100, 100) : g.DrawEllipse(pe, 160, 20, 100, 100)
            End Using
            Using pe As New Pen(If(on_, current, pal.Edge), 2)
                g.DrawBezier(pe, 190, 110, 195, 60, 225, 60, 230, 110)
            End Using
            Metal(g, pal, 180, 118, 60, 40, dark:=True)
            Return If(on_, "Current heats the filament until it glows.", "No current: the lamp is off.")
        End If
        ' Winding on an iron core.
        Using b As New SolidBrush(If(on_, Color.FromArgb(200, 120, 60), Color.FromArgb(170, 120, 80))), pe As New Pen(pal.Edge)
            g.FillRectangle(b, 60, 60, 120, 70) : g.DrawRectangle(pe, 60, 60, 120, 70)
            For xx = 66 To 174 Step 6
                g.DrawLine(pe, xx, 60, xx + 3, 130)
            Next
        End Using
        If on_ Then Txt(g, pal, "current flows: magnetic field", 54, 140, faint:=True)
        If k.Kind = CoilKind.Solenoid Then
            ' Plunger pulled into the coil; it pushes the valve spool.
            Dim inset = If(on_, 40.0F, 0F)
            Metal(g, pal, 150 - inset, 82, 150, 26, dark:=True)
            Metal(g, pal, 300 - inset, 70, 12, 50)
            Txt(g, pal, "plunger → valve spool", 230, 130, faint:=True)
            Return If(on_, $"Energized: the plunger is pulled in and switches the valve that uses {k.Label}.",
                      "Not energized: the valve spring pushes the plunger out.")
        End If
        ' Relay: the armature is pulled down and moves the contacts.
        Metal(g, pal, 40, 60, 20, 90, dark:=True)
        Metal(g, pal, 40, 140, 160, 12, dark:=True)
        Dim tilt = If(on_, 10.0F, -12.0F)
        Using pe As New Pen(pal.MetalDark, 8)
            g.DrawLine(pe, 70, 48, 280, 48 + tilt * 2)
        End Using
        Dim contactY = 48 + tilt * 2
        Using pe As New Pen(If(on_, current, pal.Edge), 3)
            g.DrawLine(pe, 280, contactY, 330, contactY)
        End Using
        Metal(g, pal, 326, 64, 30, 6) : Txt(g, pal, "NO", 360, 58, faint:=True)
        Metal(g, pal, 326, 14, 30, 6) : Txt(g, pal, "NC", 360, 8, faint:=True)
        Dim what = If(k.Kind = CoilKind.Relay, "relay", "timer relay")
        Return If(on_, $"Energized: the core pulls the armature, so every contact of {what} {k.Label} changes over.",
                  $"Not energized: the spring holds the armature up; the NC contacts of {k.Label} are closed.")
    End Function

    Private Function Contact(g As Graphics, k As ElectricContact, area As RectangleF, pal As CutawayPalette, simulating As Boolean) As String
        Fit(g, area, 360, 200)
        Dim closed = If(simulating, k.IsClosed, k.NormallyClosed)
        Dim operated = closed Xor k.NormallyClosed
        Dim live = Color.FromArgb(220, 40, 40)
        Dim termColor = If(simulating AndAlso closed AndAlso (k.Ports(0).IsPressurized OrElse k.Ports(1).IsPressurized), live, pal.Edge)
        ' Housing, fixed contacts and the moving bridge.
        Using pe As New Pen(pal.Edge)
            g.DrawRectangle(pe, 60, 60, 240, 110)
        End Using
        Metal(g, pal, 70, 130, 60, 10) : Metal(g, pal, 230, 130, 60, 10)
        Using pe As New Pen(termColor, 3)
            g.DrawLine(pe, 70, 135, 20, 135) : g.DrawLine(pe, 290, 135, 340, 135)
        End Using
        Dim bridgeY = If(closed, 120.0F, If(k.NormallyClosed, 120.0F, 100.0F))
        If k.NormallyClosed AndAlso Not closed Then bridgeY = 150
        Metal(g, pal, 110, bridgeY, 140, 10, dark:=True)
        Metal(g, pal, 176, 40, 8, bridgeY - 40)
        ' What operates it.
        Select Case k.Operator
            Case ContactOperator.PushButton, ContactOperator.Selector
                Metal(g, pal, 150, If(operated, 28.0F, 16.0F), 60, 14, dark:=True)
                Txt(g, pal, If(k.Operator = ContactOperator.Selector, "selector (stays put)", "push button"), 214, 16, faint:=True)
            Case ContactOperator.LimitSwitch
                Disc(g, pal.MetalDark, pal.Edge, 180, If(operated, 34.0F, 24.0F), 12)
                Txt(g, pal, $"roller, cylinder mark {k.Reference}", 200, 14, faint:=True)
            Case ContactOperator.ProximitySensor
                Fill(g, If(operated, Color.FromArgb(200, 60, 60), pal.MetalDark), 150, 20, 60, 16)
                Txt(g, pal, $"magnet / target at mark {k.Reference}", 214, 18, faint:=True)
            Case ContactOperator.Relay
                Txt(g, pal, $"moved by relay {k.Reference}", 194, 18, faint:=True)
            Case ContactOperator.PressureSwitch
                Txt(g, pal, $"moved by pressure switch {k.Reference}", 194, 18, faint:=True)
            Case ContactOperator.EmergencyStop
                Using b As New SolidBrush(Color.FromArgb(215, 30, 30))
                    g.FillEllipse(b, 140, If(operated, 2.0F, -10.0F), 80, 40)
                End Using
                Txt(g, pal, If(operated, "pressed and latched: turn to release", "emergency stop"), 224, 16, faint:=True)
        End Select
        Txt(g, pal, If(k.NormallyClosed, "NC contact", "NO contact"), 64, 176, faint:=True)
        Return If(closed, "The bridge touches both fixed contacts: current can flow.", "The bridge is lifted off the fixed contacts: the circuit is open.")
    End Function

    ' ------------------------------------------------------------------ parts added in 3.2

    Private ReadOnly VacuumTint As Color = Color.FromArgb(120, 200, 190)

    Private Function GripperView(g As Graphics, c As Gripper, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 420, 220)
        Metal(g, pal, 20, 60, 220, 100)
        Dim pos = CSng(c.Position)
        ' Piston moves right to close; a wedge turns the motion into the jaw stroke.
        Dim px = 50 + pos * 110
        Fill(g, Fluid(c.Ports(0), pal), 30, 75, px - 30, 70)
        Fill(g, Fluid(c.Ports(1), pal), px + 16, 75, 220 - px, 70)
        Metal(g, pal, px, 75, 16, 70, dark:=True)
        Metal(g, pal, px + 16, 104, 120 - pos * 110, 12)
        Dim gap = 50 * (1 - pos)
        Metal(g, pal, 250, 110 - gap - 40, 60, 30, dark:=True)
        Metal(g, pal, 250, 110 + gap + 10, 60, 30, dark:=True)
        Metal(g, pal, 240, 60, 10, 100)
        Fill(g, Fluid(c.Ports(0), pal), 40, 160, 14, 40) : Txt(g, pal, "1 close", 30, 200, bold:=True)
        Fill(g, Fluid(c.Ports(1), pal), 200, 160, 14, 40) : Txt(g, pal, "2 open", 190, 200, bold:=True)
        Return If(pos >= 0.995, "Air at port 1 pushed the piston over: the jaws are closed and grip the part.",
                  If(pos <= 0.005, "Air at port 2 holds the piston back: the jaws are open.", "The piston moves and the jaws follow it."))
    End Function

    Private Function PressureSwitchView(g As Graphics, v As PressureSwitch, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 300, 240)
        Metal(g, pal, 60, 20, 180, 170)
        Fill(g, pal.Cavity, 80, 40, 140, 70)
        ' Pressure under the diaphragm pushes a plunger against the adjustable spring.
        Dim lift = If(v.IsOn, 14.0F, 0F)
        Dim fluidCol = If(v.Ports(0).Pressure < -0.05, VacuumTint, Fluid(v.Ports(0), pal))
        Fill(g, fluidCol, 80, 130 - lift, 140, 40 + lift)
        Fill(g, fluidCol, 140, 170, 20, 60)
        Using dp As New Pen(Color.FromArgb(40, 40, 40), 3)
            g.DrawLine(dp, 80, 130 - lift, 220, 130 - lift)
        End Using
        Metal(g, pal, 145, 90 - lift, 10, 40, dark:=True)
        Using sp As New Pen(pal.Spring, 1.5F)
            ZigzagV(g, sp, 150, 40, 90 - lift, 10)
        End Using
        ' Micro switch contacts.
        Using cp As New Pen(If(v.IsOn, Color.FromArgb(220, 40, 40), pal.Edge), 3)
            g.DrawLine(cp, 90, 60, 135, If(v.IsOn, 60, 50))
        End Using
        Disc(g, pal.MetalDark, pal.Edge, 138, 60, 3)
        Txt(g, pal, $"switches at {v.Setting:0.0#} bar", 70, 0, faint:=True)
        Txt(g, pal, "1", 166, 210, bold:=True)
        If v.Setting < 0 Then
            Return If(v.IsOn, $"{v.Ports(0).Pressure:0.00} bar: the vacuum has pulled the diaphragm and the contact has switched.",
                      $"{v.Ports(0).Pressure:0.00} bar is not enough vacuum: the contact is at rest.")
        End If
        Return If(v.IsOn, $"{v.Ports(0).Pressure:0.0} bar: the diaphragm has pushed the plunger up against the spring and the contact has switched.",
                  $"{v.Ports(0).Pressure:0.0} bar is not enough to overcome the spring: the contact is at rest.")
    End Function

    Private Function ShutOffView(g As Graphics, v As ShutOffValve, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 400, 200)
        Metal(g, pal, 20, 60, 360, 80)
        Dim open = v.IsOpen
        Fill(g, Fluid(v.Ports(0), pal), 20, 85, 140, 30)
        Fill(g, Fluid(v.Ports(1), pal), 240, 85, 140, 30)
        Disc(g, pal.Metal, pal.Edge, 200, 100, 45)
        ' The bore through the ball lines up with the pipe when open.
        If open Then
            Fill(g, Fluid(v.Ports(0), pal), 155, 85, 90, 30)
            FlowArrow(g, 60, 100, 340, 100)
        Else
            Fill(g, pal.Cavity, 185, 55, 30, 90)
        End If
        Metal(g, pal, 195, 10, 10, 45, dark:=True)
        Metal(g, pal, If(open, 200.0F, 195.0F), If(open, 6.0F, 0F), If(open, 120.0F, 10.0F), If(open, 10.0F, 10.0F), dark:=True)
        Txt(g, pal, "1", 22, 40, bold:=True) : Txt(g, pal, "2", 364, 40, bold:=True)
        Return If(open, "The hole through the ball lines up with the pipe: air flows through.", "The ball is turned a quarter turn: its solid side blocks the pipe.")
    End Function

    Private Function EjectorView(g As Graphics, v As VacuumGenerator, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 440, 220)
        Metal(g, pal, 20, 70, 400, 80)
        Dim on_ = v.Working
        Dim air = If(on_, pal.Air, pal.Vent)
        ' Nozzle: the passage narrows, so the air comes out very fast.
        Using b As New SolidBrush(air)
            g.FillPolygon(b, {New PointF(20, 90), New PointF(150, 104), New PointF(150, 116), New PointF(20, 130)})
            g.FillPolygon(b, {New PointF(200, 104), New PointF(420, 90), New PointF(420, 130), New PointF(200, 116)})
        End Using
        Using b As New SolidBrush(If(on_, VacuumTint, pal.Cavity))
            g.FillRectangle(b, 150, 80, 50, 60)
            g.FillRectangle(b, 160, 10, 30, 70)
        End Using
        If on_ Then
            FlowArrow(g, 175, 20, 175, 75)
            FlowArrow(g, 220, 110, 400, 110)
        End If
        Txt(g, pal, "1 (compressed air)", 20, 160, bold:=True)
        Txt(g, pal, "V (vacuum)", 200, 10, bold:=True)
        Txt(g, pal, "to silencer", 340, 160, faint:=True)
        Return If(on_, $"The fast jet of air drags the air out of the suction chamber: {v.Ports(1).Pressure:0.00} bar at V.",
                  "No compressed air at port 1: no jet, no vacuum.")
    End Function

    Private Function CupView(g As Graphics, v As SuctionCup, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 300, 230)
        Dim vac = v.Ports(0).Pressure < -0.05
        Fill(g, If(vac, VacuumTint, pal.Vent), 140, 0, 20, 80)
        Using b As New SolidBrush(Color.FromArgb(60, 60, 60)), cav As New SolidBrush(If(vac AndAlso v.Sealed, VacuumTint, pal.Cavity))
            Dim cup = {New PointF(120, 80), New PointF(180, 80), New PointF(250, 150), New PointF(50, 150)}
            g.FillPolygon(b, cup)
            g.FillPolygon(cav, {New PointF(135, 92), New PointF(165, 92), New PointF(225, 148), New PointF(75, 148)})
        End Using
        If v.WorkpiecePresent Then
            Dim y = If(v.Holding, 150.0F, 175.0F)
            Using b As New SolidBrush(If(v.Holding, Color.FromArgb(120, 160, 90), Color.FromArgb(170, 170, 170)))
                g.FillRectangle(b, 30, y, 240, 30)
            End Using
            If v.Fault = FaultKind.Leak Then Txt(g, pal, "torn lip: air leaks in", 60, 210, faint:=True)
        End If
        Return If(v.Holding, $"The outside air pressure presses the part against the cup: {v.HoldingForce():0} N holding force.",
                  If(v.WorkpiecePresent, "Not enough vacuum in the cup: the part is not held.", "No workpiece: the cup is open to the air."))
    End Function

    Private Function CompressorView(g As Graphics, v As Compressor, area As RectangleF, pal As CutawayPalette, phase As Double) As String
        Fit(g, area, 320, 300)
        Metal(g, pal, 90, 30, 140, 150)
        Dim a = If(v.Running, phase * 2 * Math.PI, 0)
        Dim crankX = 160 + 30 * Math.Sin(a), crankY = 230 + 30 * Math.Cos(a)
        Dim pistonY = CSng(crankY - 120)
        Fill(g, If(v.Running, pal.Air, pal.Vent), 105, 45, 110, pistonY - 45)
        Metal(g, pal, 105, pistonY, 110, 26, dark:=True)
        Using rod As New Pen(pal.MetalDark, 8)
            g.DrawLine(rod, 160, pistonY + 13, CSng(crankX), CSng(crankY))
        End Using
        Disc(g, pal.Metal, pal.Edge, 160, 230, 40)
        Disc(g, pal.MetalDark, pal.Edge, CSng(crankX), CSng(crankY), 8)
        Fill(g, Fluid(v.Ports(0), pal), 215, 50, 90, 16)
        Txt(g, pal, "1 → receiver", 220, 30, bold:=True)
        Return If(v.Running, $"The crank drives the piston up and down; air is drawn in, compressed and pushed out ({v.EffectiveDelivery:0} NL/min).",
                  "The pressure switch has stopped the motor: the receiver is full (or the main switch is off).")
    End Function

    Private Function ReceiverView(g As Graphics, v As AirReceiver, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 420, 200)
        Using b As New SolidBrush(pal.Metal), pe As New Pen(pal.Edge, 2)
            g.FillEllipse(b, 40, 30, 340, 140)
            g.DrawEllipse(pe, 40, 30, 340, 140)
        End Using
        ' The deeper the blue, the more air is stored.
        Dim k = CInt(Math.Min(1, v.Pressure / 10) * 200)
        Using b As New SolidBrush(Color.FromArgb(40 + k \ 4, pal.Air))
            g.FillEllipse(b, 50, 40, 320, 120)
        End Using
        Fill(g, Fluid(v.Ports(0), pal), 0, 92, 50, 16)
        Fill(g, Fluid(v.Ports(1), pal), 370, 92, 50, 16)
        Txt(g, pal, $"{v.Pressure:0.0} bar   {(v.Pressure + CylinderBase.Atm) / CylinderBase.Atm * v.VolumeLitres:0} NL stored", 120, 90, bold:=True)
        Metal(g, pal, 205, 170, 10, 20, dark:=True) : Txt(g, pal, "drain", 220, 175, faint:=True)
        Return "The receiver stores compressed air: it covers peaks of air use and lets the compressor run in longer, calmer cycles."
    End Function

    Private Function FlowMeterView(g As Graphics, v As FlowMeter, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 260, 300)
        ' Rotameter: a float rises in a tapered tube until the flow just carries it.
        Using b As New SolidBrush(pal.Cavity), pe As New Pen(pal.Edge, 2)
            g.FillPolygon(b, {New PointF(110, 30), New PointF(150, 30), New PointF(140, 250), New PointF(120, 250)})
            g.DrawPolygon(pe, {New PointF(110, 30), New PointF(150, 30), New PointF(140, 250), New PointF(120, 250)})
        End Using
        Dim fullScale = If(v.Hydraulic, 40.0, 200.0)
        Dim h = CSng(Math.Min(1, Math.Abs(v.Flow) / fullScale))
        Dim y = 235 - h * 190
        Using b As New SolidBrush(Color.FromArgb(200, 60, 40))
            g.FillPolygon(b, {New PointF(118, y), New PointF(142, y), New PointF(130, y + 16)})
        End Using
        Using pe As New Pen(pal.SubText)
            For i = 0 To 4
                Dim ty = 235 - i * 47.5F
                g.DrawLine(pe, 152, ty, 162, ty)
                Txt(g, pal, $"{fullScale * i / 4:0}", 166, ty - 8, faint:=True)
            Next
        End Using
        Fill(g, Fluid(v.Ports(0), pal), 40, 250, 180, 14)
        Txt(g, pal, $"{Math.Abs(v.Flow):0.0} {v.Unit}", 60, 270, bold:=True)
        Return "The flow lifts the float in the tapered tube; the higher it floats, the more flow."
    End Function

    Private Function ForceSensorView(g As Graphics, v As ForceSensor, area As RectangleF, pal As CutawayPalette) As String
        Fit(g, area, 360, 200)
        ' Bending beam with strain gauges: the force bends it a little.
        Dim bend = CSng(Math.Max(-1, Math.Min(1, v.Force / 2000)) * 14)
        Using b As New SolidBrush(pal.Metal), pe As New Pen(pal.Edge)
            Dim beam = {New PointF(40, 80), New PointF(320, 80 + bend), New PointF(320, 110 + bend), New PointF(40, 110)}
            g.FillPolygon(b, beam) : g.DrawPolygon(pe, beam)
        End Using
        Metal(g, pal, 20, 60, 20, 70, dark:=True)
        Fill(g, Color.FromArgb(200, 160, 40), 120, 74 + bend * 0.3F, 50, 6)
        Fill(g, Color.FromArgb(200, 160, 40), 120, 110 + bend * 0.3F, 50, 6)
        Using pe As New Pen(Color.Firebrick, 3) With {.EndCap = LineCap.ArrowAnchor}
            g.DrawLine(pe, 300, 30 + bend, 300, 76 + bend)
        End Using
        Txt(g, pal, $"{v.Force:0} N on {v.Cylinder}", 120, 150, bold:=True)
        Return "The force bends the beam very slightly; the strain gauges glued on it change their resistance, which is measured."
    End Function

    Private Function CounterView(g As Graphics, v As ElectricCounter, area As RectangleF, pal As CutawayPalette, simulating As Boolean) As String
        Fit(g, area, 300, 180)
        Metal(g, pal, 20, 20, 260, 140)
        Fill(g, Color.FromArgb(20, 30, 20), 50, 45, 200, 60)
        Using f As New Font("Consolas", 26, FontStyle.Bold), b As New SolidBrush(Color.FromArgb(120, 255, 120))
            g.DrawString($"{If(simulating, v.Count, 0),4}", f, b, 60, 50)
        End Using
        Txt(g, pal, $"preset {v.Preset}", 60, 115, bold:=True)
        Fill(g, If(v.Active AndAlso simulating, Color.FromArgb(220, 40, 40), pal.Vent), 210, 115, 30, 18)
        Return If(simulating AndAlso v.Active, "The preset has been reached: the output contacts have switched.", "Each pulse on A1/A2 adds one; at the preset the contacts switch.")
    End Function
End Module