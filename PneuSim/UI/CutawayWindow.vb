Imports System.Drawing.Drawing2D

''' <summary>Animated sectional view of a directional valve: the spool slides and connects the ports.</summary>
Public Class CutawayWindow
    Inherits Form

    Private ReadOnly _view As New CutawayView() With {.Dock = DockStyle.Fill}
    Private ReadOnly _timer As New Timer() With {.Interval = 40}

    ''' <summary>Returns the valve to show (selected or last hovered).</summary>
    Public Property ValveSource As Func(Of DirectionalValve)

    Public Sub New()
        Text = "Valve cutaway"
        Font = New Font("Segoe UI", 9)
        Size = New Size(560, 300)
        FormBorderStyle = FormBorderStyle.SizableToolWindow
        StartPosition = FormStartPosition.Manual
        ShowInTaskbar = False
        Controls.Add(_view)
        AddHandler _timer.Tick, Sub()
                                    _view.Valve = If(ValveSource Is Nothing, Nothing, ValveSource.Invoke())
                                    _view.Animate(0.04)
                                End Sub
        _timer.Start()
    End Sub

    Protected Overrides Sub OnFormClosed(e As FormClosedEventArgs)
        _timer.Stop()
        MyBase.OnFormClosed(e)
    End Sub
End Class

Public Class CutawayView
    Inherits Control

    Private _valve As DirectionalValve
    Private _shift As Single
    Private Const ViewScale As Single = 3.2F

    Public Sub New()
        SetStyle(ControlStyles.AllPaintingInWmPaint Or ControlStyles.UserPaint Or ControlStyles.OptimizedDoubleBuffer Or ControlStyles.ResizeRedraw, True)
        BackColor = Color.White
    End Sub

    Public Property Valve As DirectionalValve
        Get
            Return _valve
        End Get
        Set(value As DirectionalValve)
            If value IsNot _valve Then _shift = TargetShift(value)
            _valve = value
        End Set
    End Property

    Private Function TargetShift(v As DirectionalValve) As Single
        If v Is Nothing Then Return 0
        Dim pitch = v.BoxSize * ViewScale * 0.45F
        Return If(v.State = 1, pitch, If(v.State = 2, -pitch, 0))
    End Function

    Public Sub Animate(dt As Double)
        Dim target = TargetShift(_valve)
        Dim stepPx = CSng(260 * dt)
        If Math.Abs(target - _shift) <= stepPx Then _shift = target Else _shift += Math.Sign(target - _shift) * stepPx
        Invalidate()
    End Sub

    Protected Overrides Sub OnPaint(e As PaintEventArgs)
        Dim g = e.Graphics
        g.SmoothingMode = SmoothingMode.AntiAlias
        g.Clear(Color.White)
        Using f As New Font("Segoe UI", 8.5F), fb As New Font("Segoe UI", 9, FontStyle.Bold)
            If _valve Is Nothing Then
                g.DrawString("Select a directional valve (or hover over one) to see inside it." & vbCrLf &
                             "Start the simulation to watch the spool move.", f, Brushes.Gray, 12, 12)
                Return
            End If
            Dim v = _valve
            Dim layout = v.WorkingPortLayout()
            Dim boreLen = v.BoxSize * ViewScale + 120
            Dim left = (Width - boreLen) / 2, top = 60.0F
            Dim boreTop = top + 34, boreBottom = boreTop + 36
            Dim bodyBottom = boreBottom + 34
            Dim xOf = Function(offset As Single) left + 60 + offset * ViewScale
            Dim pressCol = If(v.Ports.Any(Function(p) p.Kind = PortKind.Hydraulic), RenderContext.HydraulicColor, RenderContext.PressureColor)
            Dim portColor = Function(name As String) As Color
                                Dim p = v.GetPort(name)
                                If p IsNot Nothing AndAlso p.IsPressurized Then Return pressCol
                                Return Color.FromArgb(225, 232, 242)
                            End Function

            g.DrawString($"{If(String.IsNullOrEmpty(v.Label), "", v.Label & ": ")}{v.DisplayName}", fb, Brushes.Black, 8, 6)
            Dim posName = If(v.State = 0, "normal position", If(v.State = 1, "position a", "position b"))
            Dim flows = v.PassagesIn(v.State)
            g.DrawString($"Now in {posName}: " & If(flows.Length = 0, "all ports closed", String.Join(",  ", flows.Select(Function(fl) $"{fl(0)} → {fl(1)}"))), f, Brushes.DimGray, 8, 26)

            ' Body.
            Using body As New SolidBrush(Color.FromArgb(205, 210, 218))
                g.FillRectangle(body, left, top, boreLen, bodyBottom - top)
            End Using
            g.DrawRectangle(Pens.DimGray, left, top, boreLen, bodyBottom - top)
            ' Port channels.
            For Each p In layout
                Dim x = xOf(p.Offset)
                Using b As New SolidBrush(portColor(p.Name))
                    If p.Top Then g.FillRectangle(b, x - 7, top - 12, 14, boreTop - top + 12) Else g.FillRectangle(b, x - 7, boreBottom, 14, bodyBottom - boreBottom + 12)
                End Using
                g.DrawString(p.Name, fb, Brushes.Black, x - 4, If(p.Top, top - 28, bodyBottom + 12))
                If p.Vents Then g.DrawString("exhaust", f, Brushes.Gray, x - 18, bodyBottom + 26)
            Next
            ' Bore with the spool: dark lands, light grooves where ports are connected.
            g.FillRectangle(Brushes.White, left + 10, boreTop, boreLen - 20, boreBottom - boreTop)
            Dim target = TargetShift(v)
            Dim offsetNow = _shift - target
            Using land As New SolidBrush(Color.FromArgb(95, 100, 110))
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
                ' Flow arrow along the groove.
                Using pen As New Pen(Color.White, 2) With {.EndCap = LineCap.ArrowAnchor}
                    Dim ya = (boreTop + boreBottom) / 2
                    g.DrawLine(pen, xOf(a.Offset) + offsetNow, ya, xOf(b.Offset) + offsetNow, ya)
                End Using
            Next
            ' Spool rod ends and actuators.
            Using rod As New SolidBrush(Color.FromArgb(70, 74, 82))
                g.FillRectangle(rod, left - 18 + _shift, (boreTop + boreBottom) / 2 - 4, 32, 8)
                g.FillRectangle(rod, left + boreLen - 14 + _shift, (boreTop + boreBottom) / 2 - 4, 32, 8)
            End Using
            Dim leftText As String
            Select Case v.Actuator
                Case ValveActuator.Solenoid : leftText = $"solenoid {v.SolenoidLabel}"
                Case ValveActuator.Pilot, ValveActuator.DelayedPilot : leftText = $"pilot {v.PilotPortName(True)}"
                Case ValveActuator.RollerLever : leftText = $"roller {v.TriggerMark}"
                Case ValveActuator.Selector : leftText = "selector"
                Case Else : leftText = "push button"
            End Select
            g.DrawString(leftText, f, Brushes.Black, Math.Max(2, left - 90 + _shift), boreBottom + 2)
            Dim rightText = If(v.ReturnType = ValveReturn.Spring, "spring", If(v.ReturnType = ValveReturn.Solenoid, $"solenoid {v.ReturnSolenoidLabel}", $"pilot {v.PilotPortName(False)}"))
            g.DrawString(rightText, f, Brushes.Black, left + boreLen + 16 + _shift, boreBottom + 2)
            If v.ReturnType = ValveReturn.Spring Then
                Dim sx = left + boreLen + 18 + _shift
                Dim ex = left + boreLen + 70
                Dim cy = (boreTop + boreBottom) / 2
                Dim pts As New List(Of PointF)
                For i = 0 To 8
                    pts.Add(New PointF(sx + (ex - sx) * i / 8, cy + If(i Mod 2 = 0, -8, 8)))
                Next
                g.DrawLines(Pens.DimGray, pts.ToArray())
            End If
            g.DrawString("Blue/orange = pressurized   Light = vented or no pressure", f, Brushes.Gray, 8, Height - 20)
        End Using
    End Sub
End Class
