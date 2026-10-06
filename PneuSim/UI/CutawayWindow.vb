Imports System.Drawing.Drawing2D

''' <summary>Animated sectional (cutaway) view of the selected or hovered component.</summary>
Public Class CutawayWindow
    Inherits Form

    Private ReadOnly _view As New CutawayView() With {.Dock = DockStyle.Fill}
    Private ReadOnly _timer As New Timer() With {.Interval = 40}

    ''' <summary>Returns the component to show (selected or last hovered).</summary>
    Public Property ElementSource As Func(Of CircuitElement)

    ''' <summary>True while the simulation runs (contacts and valves then show their live state).</summary>
    Public Property SimulatingSource As Func(Of Boolean)

    Public Sub New()
        Text = "Cutaway view"
        Font = New Font("Segoe UI", 9)
        Size = New Size(600, 340)
        FormBorderStyle = FormBorderStyle.SizableToolWindow
        StartPosition = FormStartPosition.Manual
        ShowInTaskbar = False
        KeyPreview = True
        Controls.Add(_view)
        AddHandler KeyDown, Sub(s, e)
                                If e.KeyCode = Keys.Escape Then Close()
                            End Sub
        AddHandler _timer.Tick, Sub()
                                    _view.Element = If(ElementSource Is Nothing, Nothing, ElementSource.Invoke())
                                    _view.Simulating = SimulatingSource IsNot Nothing AndAlso SimulatingSource.Invoke()
                                    _view.Animate(0.04)
                                End Sub
        _timer.Start()
    End Sub

    Protected Overrides Sub OnFormClosed(e As FormClosedEventArgs)
        _timer.Stop()
        _timer.Dispose()
        MyBase.OnFormClosed(e)
    End Sub
End Class

''' <summary>Colours of the cutaway drawings (light or dark mode).</summary>
Public Class CutawayPalette
    Public Property Back As Color
    Public Property Text As Color
    Public Property SubText As Color
    Public Property Metal As Color
    Public Property MetalDark As Color
    Public Property Edge As Color
    Public Property Cavity As Color
    Public Property Vent As Color
    Public Property Air As Color
    Public Property Oil As Color
    Public Property Spring As Color

    Public Shared Function Current() As CutawayPalette
        If AppSettings.DarkMode Then
            Return New CutawayPalette With {
                .Back = Color.FromArgb(28, 30, 36), .Text = Color.FromArgb(225, 228, 235), .SubText = Color.FromArgb(160, 166, 178),
                .Metal = Color.FromArgb(112, 118, 130), .MetalDark = Color.FromArgb(70, 75, 86), .Edge = Color.FromArgb(175, 180, 190),
                .Cavity = Color.FromArgb(46, 49, 58), .Vent = Color.FromArgb(68, 75, 92), .Air = Color.FromArgb(70, 150, 255),
                .Oil = Color.FromArgb(240, 140, 40), .Spring = Color.FromArgb(205, 205, 205)}
        End If
        Return New CutawayPalette With {
            .Back = Color.White, .Text = Color.Black, .SubText = Color.FromArgb(105, 105, 105),
            .Metal = Color.FromArgb(205, 210, 218), .MetalDark = Color.FromArgb(95, 100, 110), .Edge = Color.FromArgb(100, 100, 100),
            .Cavity = Color.White, .Vent = Color.FromArgb(225, 232, 242), .Air = RenderContext.PressureColor,
            .Oil = RenderContext.HydraulicColor, .Spring = Color.FromArgb(90, 90, 90)}
    End Function
End Class

Public Class CutawayView
    Inherits Control

    Private _element As CircuitElement
    Private _shift As Single
    Private _phase As Double
    Private Const ViewScale As Single = 3.2F

    Public Sub New()
        SetStyle(ControlStyles.AllPaintingInWmPaint Or ControlStyles.UserPaint Or ControlStyles.OptimizedDoubleBuffer Or ControlStyles.ResizeRedraw, True)
    End Sub

    ''' <summary>True while the simulation runs.</summary>
    Public Property Simulating As Boolean

    Public Property Element As CircuitElement
        Get
            Return _element
        End Get
        Set(value As CircuitElement)
            If value IsNot _element Then _shift = TargetShift(TryCast(value, DirectionalValve))
            _element = value
        End Set
    End Property

    ''' <summary>Kept for compatibility: the directional valve being shown, if any.</summary>
    Public Property Valve As DirectionalValve
        Get
            Return TryCast(_element, DirectionalValve)
        End Get
        Set(value As DirectionalValve)
            Element = value
        End Set
    End Property

    Private Function TargetShift(v As DirectionalValve) As Single
        If v Is Nothing Then Return 0
        Dim pitch = v.BoxSize * ViewScale * 0.45F
        Return If(v.State = 1, pitch, If(v.State = 2, -pitch, 0))
    End Function

    Public Sub Animate(dt As Double)
        Dim target = TargetShift(TryCast(_element, DirectionalValve))
        Dim stepPx = CSng(260 * dt)
        If Math.Abs(target - _shift) <= stepPx Then _shift = target Else _shift += Math.Sign(target - _shift) * stepPx
        ' Rotating parts turn at a speed that can be followed by eye.
        Dim turn As Double = 0
        If TypeOf _element Is AirMotor Then turn = DirectCast(_element, AirMotor).Rpm
        If TypeOf _element Is HydraulicMotor Then turn = DirectCast(_element, HydraulicMotor).Rpm
        If TypeOf _element Is HydraulicPump AndAlso Simulating AndAlso DirectCast(_element, HydraulicPump).Running Then turn = 60
        _phase = (_phase + Math.Sign(turn) * Math.Min(Math.Abs(turn), 120) * 3 * dt) Mod 360
        Invalidate()
    End Sub

    Protected Overrides Sub OnPaint(e As PaintEventArgs)
        Render(e.Graphics)
    End Sub

    ''' <summary>Draws the cutaway onto any graphics the size of this control (also used for tests and documentation).</summary>
    Public Sub Render(g As Graphics)
        g.SmoothingMode = SmoothingMode.AntiAlias
        Dim pal = CutawayPalette.Current()
        g.Clear(pal.Back)
        Using f As New Font("Segoe UI", 8.5F), fb As New Font("Segoe UI", 9, FontStyle.Bold), tb As New SolidBrush(pal.Text), sb As New SolidBrush(pal.SubText)
            If _element Is Nothing Then
                g.DrawString("Select a component (or hover over one) to see inside it." & vbCrLf &
                             "Start the simulation to watch the parts move.", f, sb, 12, 12)
                Return
            End If
            g.DrawString($"{If(String.IsNullOrEmpty(_element.Label), "", _element.Label & ": ")}{_element.DisplayName}", fb, tb, 8, 6)
            If TypeOf _element Is DirectionalValve Then
                DrawValve(g, DirectCast(_element, DirectionalValve), pal, f, fb)
            Else
                Dim area = New RectangleF(8, 44, Width - 16, Height - 44 - 24)
                Dim status = Cutaways.Draw(g, _element, area, pal, _phase, Simulating)
                g.DrawString(status, f, sb, 8, 24)
            End If
            g.DrawString("Coloured = under pressure (blue air, orange oil, red current)   Pale = vented or no pressure", f, sb, 8, Height - 20)
        End Using
    End Sub

    Private Sub DrawValve(g As Graphics, v As DirectionalValve, pal As CutawayPalette, f As Font, fb As Font)
        Dim layout = v.WorkingPortLayout()
        Dim boreLen = v.BoxSize * ViewScale + 120
        Dim left = (Width - boreLen) / 2, top = 70.0F
        Dim boreTop = top + 34, boreBottom = boreTop + 36
        Dim bodyBottom = boreBottom + 34
        Dim xOf = Function(offset As Single) left + 60 + offset * ViewScale
        Dim portColor = Function(name As String) Cutaways.Fluid(v.GetPort(name), pal)
        Using tb As New SolidBrush(pal.Text), sb As New SolidBrush(pal.SubText), edge As New Pen(pal.Edge)
            Dim posName = If(v.State = 0, "normal position", If(v.State = 1, "position a", "position b"))
            Dim flows = v.PassagesIn(v.State)
            g.DrawString($"Now in {posName}: " & If(flows.Length = 0, "all ports closed", String.Join(",  ", flows.Select(Function(fl) $"{fl(0)} → {fl(1)}"))), f, sb, 8, 24)

            ' Body.
            Using body As New SolidBrush(pal.Metal)
                g.FillRectangle(body, left, top, boreLen, bodyBottom - top)
            End Using
            g.DrawRectangle(edge, left, top, boreLen, bodyBottom - top)
            ' Port channels.
            For Each p In layout
                Dim x = xOf(p.Offset)
                Using b As New SolidBrush(portColor(p.Name))
                    If p.Top Then g.FillRectangle(b, x - 7, top - 12, 14, boreTop - top + 12) Else g.FillRectangle(b, x - 7, boreBottom, 14, bodyBottom - boreBottom + 12)
                End Using
                g.DrawString(p.Name, fb, tb, x - 4, If(p.Top, top - 28, bodyBottom + 12))
                If p.Vents Then g.DrawString("exhaust", f, sb, x - 18, bodyBottom + 26)
            Next
            ' Bore with the spool: dark lands, coloured grooves where ports are connected.
            Using cav As New SolidBrush(pal.Cavity)
                g.FillRectangle(cav, left + 10, boreTop, boreLen - 20, boreBottom - boreTop)
            End Using
            Dim target = TargetShift(v)
            Dim offsetNow = _shift - target
            Using land As New SolidBrush(pal.MetalDark)
                g.FillRectangle(land, left + 14 + _shift, boreTop + 2, boreLen - 28, boreBottom - boreTop - 4)
            End Using
            For Each fl In flows
                Dim a = layout.FirstOrDefault(Function(p) p.Name = fl(0))
                Dim b = layout.FirstOrDefault(Function(p) p.Name = fl(1))
                If a.Name Is Nothing OrElse b.Name Is Nothing Then Continue For
                Dim x1 = Math.Min(xOf(a.Offset), xOf(b.Offset)) - 9 + offsetNow
                Dim x2 = Math.Max(xOf(a.Offset), xOf(b.Offset)) + 9 + offsetNow
                Using groove As New SolidBrush(portColor(fl(0)))
                    g.FillRectangle(groove, x1, boreTop + 6, x2 - x1, boreBottom - boreTop - 12)
                End Using
                Using pen As New Pen(Color.White, 2) With {.EndCap = LineCap.ArrowAnchor}
                    Dim ya = (boreTop + boreBottom) / 2
                    g.DrawLine(pen, xOf(a.Offset) + offsetNow, ya, xOf(b.Offset) + offsetNow, ya)
                End Using
            Next
            ' Spool rod ends.
            Dim cy = (boreTop + boreBottom) / 2
            Using rod As New SolidBrush(pal.MetalDark)
                g.FillRectangle(rod, left - 18 + _shift, cy - 4, 32, 8)
                g.FillRectangle(rod, left + boreLen - 14 + _shift, cy - 4, 32, 8)
            End Using
            ' Springs: return spring, or centring springs on both sides of a 3-position valve.
            Dim springLeft = v.Positions = 3
            Dim springRight = v.Positions = 3 OrElse v.ReturnType = ValveReturn.Spring
            Using sp As New Pen(pal.Spring, 1.5F)
                If springRight Then Cutaways.Zigzag(g, sp, left + boreLen + 18 + _shift, left + boreLen + 64, cy, 8)
                If springLeft Then Cutaways.Zigzag(g, sp, left - 64, left - 18 + _shift, cy, 8)
            End Using
            ' Actuator names above the spool ends, outside the body (they never overlap it).
            Dim leftText As String
            Select Case v.Actuator
                Case ValveActuator.Solenoid : leftText = $"solenoid {v.SolenoidLabel}"
                Case ValveActuator.Pilot, ValveActuator.DelayedPilot : leftText = $"pilot {v.PilotPortName(True)}"
                Case ValveActuator.RollerLever : leftText = $"roller {v.TriggerMark}"
                Case ValveActuator.IdleReturnRoller : leftText = $"idle-return roller {v.TriggerMark}"
                Case ValveActuator.Selector : leftText = "selector"
                Case Else : leftText = "push button"
            End Select
            Dim rightText = If(v.ReturnType = ValveReturn.Spring, "spring",
                               If(v.ReturnType = ValveReturn.Solenoid, $"solenoid {v.ReturnSolenoidLabel}", $"pilot {v.PilotPortName(False)}"))
            If v.Positions = 3 Then rightText &= " + centring springs"
            Dim lsz = g.MeasureString(leftText, f)
            g.DrawString(leftText, f, tb, Math.Max(2, left - 4 - lsz.Width), boreTop - 6 - lsz.Height)
            g.DrawString(rightText, f, tb, Math.Min(Width - g.MeasureString(rightText, f).Width - 2, left + boreLen + 4), boreTop - 6 - lsz.Height)
        End Using
    End Sub
End Class
