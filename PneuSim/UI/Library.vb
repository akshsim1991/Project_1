Imports System.Drawing.Drawing2D

''' <summary>An entry in the component library.</summary>
Public Class LibraryPreset
    Public Sub New(category As String, name As String, factory As Func(Of CircuitElement))
        Me.Category = category
        Me.Name = name
        Me.Factory = factory
    End Sub

    Public ReadOnly Property Category As String
    Public ReadOnly Property Name As String
    Public ReadOnly Property Factory As Func(Of CircuitElement)
End Class

Public Module Library
    Public Const CatSupply = "Supply and measuring"
    Public Const CatActuators = "Actuators"
    Public Const CatDirectional = "Directional control valves"
    Public Const CatFlow = "Logic and flow control valves"

    Public ReadOnly Presets As LibraryPreset() = {
        New LibraryPreset(CatSupply, "Compressed air supply", Function() New AirSupply()),
        New LibraryPreset(CatSupply, "Pressure gauge", Function() New PressureGauge()),
        New LibraryPreset(CatActuators, "Single-acting cylinder", Function() New SingleActingCylinder()),
        New LibraryPreset(CatActuators, "Double-acting cylinder", Function() New DoubleActingCylinder()),
        New LibraryPreset(CatDirectional, "3/2 valve, push button, NC", Function() V32(ValveActuator.PushButton)),
        New LibraryPreset(CatDirectional, "3/2 valve, push button, NO", Function() V32(ValveActuator.PushButton, normallyOpen:=True)),
        New LibraryPreset(CatDirectional, "3/2 valve, selector switch, NC", Function() V32(ValveActuator.Selector)),
        New LibraryPreset(CatDirectional, "3/2 valve, roller lever, NC", Function() V32(ValveActuator.RollerLever)),
        New LibraryPreset(CatDirectional, "3/2 valve, pneumatic pilot, NC", Function() V32(ValveActuator.Pilot)),
        New LibraryPreset(CatDirectional, "Time delay valve, 3/2 NC", Function() V32(ValveActuator.DelayedPilot)),
        New LibraryPreset(CatDirectional, "5/2 valve, push button", Function() V52(ValveActuator.PushButton, ValveReturn.Spring)),
        New LibraryPreset(CatDirectional, "5/2 valve, selector switch", Function() V52(ValveActuator.Selector, ValveReturn.Spring)),
        New LibraryPreset(CatDirectional, "5/2 valve, single pilot", Function() V52(ValveActuator.Pilot, ValveReturn.Spring)),
        New LibraryPreset(CatDirectional, "5/2 valve, double pilot (memory)", Function() V52(ValveActuator.Pilot, ValveReturn.Pilot)),
        New LibraryPreset(CatFlow, "Shuttle valve (OR)", Function() New ShuttleValve()),
        New LibraryPreset(CatFlow, "Two-pressure valve (AND)", Function() New TwoPressureValve()),
        New LibraryPreset(CatFlow, "One-way flow control valve", Function() New FlowControlValve()),
        New LibraryPreset(CatFlow, "Flow control valve", Function() New FlowControlValve() With {.HasCheckValve = False})
    }

    Public Function V32(actuator As ValveActuator, Optional normallyOpen As Boolean = False) As Valve32
        Return New Valve32() With {.Actuator = actuator, .NormallyOpen = normallyOpen}
    End Function

    Public Function V52(actuator As ValveActuator, ret As ValveReturn) As Valve52
        Return New Valve52() With {.Actuator = actuator, .ReturnType = ret}
    End Function

    ''' <summary>Renders a preview of an element scaled to fit the given size.</summary>
    Public Function RenderThumbnail(e As CircuitElement, width As Integer, height As Integer) As Bitmap
        Dim bmp As New Bitmap(width, height)
        Using g = Graphics.FromImage(bmp), ctx As New RenderContext()
            g.SmoothingMode = SmoothingMode.AntiAlias
            g.Clear(Color.White)
            Dim b = e.LocalBounds
            Dim scale = Math.Min((width - 6) / b.Width, (height - 6) / b.Height)
            scale = Math.Min(scale, 1.0F)
            g.TranslateTransform(width / 2.0F, height / 2.0F)
            g.ScaleTransform(scale, scale)
            g.TranslateTransform(-(b.Left + b.Width / 2), -(b.Top + b.Height / 2))
            e.DrawSymbol(g, ctx)
        End Using
        Return bmp
    End Function
End Module
