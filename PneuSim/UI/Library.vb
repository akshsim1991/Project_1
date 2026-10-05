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
    Public Const CatSupply = "Supply, air preparation and measuring"
    Public Const CatActuators = "Actuators"
    Public Const CatDirectional = "Directional control valves"
    Public Const CatSolenoid = "Solenoid valves"
    Public Const CatFlow = "Logic, non-return and flow valves"
    Public Const CatElectric = "Electrical (24 V DC)"
    Public Const CatDrawing = "Drawing"

    Public ReadOnly Presets As LibraryPreset() = {
        New LibraryPreset(CatSupply, "Compressed air supply", Function() New AirSupply()),
        New LibraryPreset(CatSupply, "Service unit", Function() New PressureRegulator() With {.Style = RegulatorStyle.ServiceUnit, .Setting = 6}),
        New LibraryPreset(CatSupply, "Pressure regulator", Function() New PressureRegulator()),
        New LibraryPreset(CatSupply, "Pressure gauge", Function() New PressureGauge()),
        New LibraryPreset(CatSupply, "Silencer", Function() New Silencer()),
        New LibraryPreset(CatSupply, "Tube junction (T)", Function() New Junction()),
        New LibraryPreset(CatActuators, "Single-acting cylinder", Function() New SingleActingCylinder()),
        New LibraryPreset(CatActuators, "Double-acting cylinder", Function() New DoubleActingCylinder()),
        New LibraryPreset(CatActuators, "Semi-rotary actuator", Function() New SemiRotaryActuator()),
        New LibraryPreset(CatActuators, "Air motor", Function() New AirMotor()),
        New LibraryPreset(CatDirectional, "2/2 valve, push button, NC", Function() New Valve22() With {.Actuator = ValveActuator.PushButton}),
        New LibraryPreset(CatDirectional, "3/2 valve, push button, NC", Function() V32(ValveActuator.PushButton)),
        New LibraryPreset(CatDirectional, "3/2 valve, push button, NO", Function() V32(ValveActuator.PushButton, normallyOpen:=True)),
        New LibraryPreset(CatDirectional, "3/2 valve, selector switch, NC", Function() V32(ValveActuator.Selector)),
        New LibraryPreset(CatDirectional, "3/2 valve, roller lever, NC", Function() V32(ValveActuator.RollerLever)),
        New LibraryPreset(CatDirectional, "3/2 valve, pneumatic pilot, NC", Function() V32(ValveActuator.Pilot)),
        New LibraryPreset(CatDirectional, "Time delay valve, 3/2 NC", Function() V32(ValveActuator.DelayedPilot)),
        New LibraryPreset(CatDirectional, "4/2 valve, push button", Function() New Valve42() With {.Actuator = ValveActuator.PushButton}),
        New LibraryPreset(CatDirectional, "5/2 valve, push button", Function() V52(ValveActuator.PushButton, ValveReturn.Spring)),
        New LibraryPreset(CatDirectional, "5/2 valve, selector switch", Function() V52(ValveActuator.Selector, ValveReturn.Spring)),
        New LibraryPreset(CatDirectional, "5/2 valve, single pilot", Function() V52(ValveActuator.Pilot, ValveReturn.Spring)),
        New LibraryPreset(CatDirectional, "5/2 valve, double pilot (memory)", Function() V52(ValveActuator.Pilot, ValveReturn.Pilot)),
        New LibraryPreset(CatDirectional, "5/3 valve, double pilot, closed centre", Function() New Valve53()),
        New LibraryPreset(CatDirectional, "5/3 valve, double pilot, exhaust centre", Function() New Valve53() With {.Centre = CentrePosition.Exhausted}),
        New LibraryPreset(CatSolenoid, "3/2 valve, solenoid, NC", Function() V32(ValveActuator.Solenoid)),
        New LibraryPreset(CatSolenoid, "5/2 valve, single solenoid", Function() V52(ValveActuator.Solenoid, ValveReturn.Spring)),
        New LibraryPreset(CatSolenoid, "5/2 valve, double solenoid", Function() V52(ValveActuator.Solenoid, ValveReturn.Solenoid)),
        New LibraryPreset(CatSolenoid, "5/3 valve, double solenoid, closed centre", Function() New Valve53() With {.Actuator = ValveActuator.Solenoid, .ReturnType = ValveReturn.Solenoid}),
        New LibraryPreset(CatFlow, "Shuttle valve (OR)", Function() New ShuttleValve()),
        New LibraryPreset(CatFlow, "Two-pressure valve (AND)", Function() New TwoPressureValve()),
        New LibraryPreset(CatFlow, "Check valve", Function() New CheckValve()),
        New LibraryPreset(CatFlow, "Quick exhaust valve", Function() New QuickExhaustValve()),
        New LibraryPreset(CatFlow, "One-way flow control valve", Function() New FlowControlValve()),
        New LibraryPreset(CatFlow, "Flow control valve", Function() New FlowControlValve() With {.HasCheckValve = False}),
        New LibraryPreset(CatElectric, "+24 V connection", Function() New PowerTerminal()),
        New LibraryPreset(CatElectric, "0 V connection", Function() New PowerTerminal() With {.Polarity = Polarity.Zero0V}),
        New LibraryPreset(CatElectric, "Push button, NO", Function() Contact(ContactOperator.PushButton, False, "S1")),
        New LibraryPreset(CatElectric, "Push button, NC", Function() Contact(ContactOperator.PushButton, True, "S0")),
        New LibraryPreset(CatElectric, "Selector switch, NO", Function() Contact(ContactOperator.Selector, False, "S2")),
        New LibraryPreset(CatElectric, "Relay contact, NO", Function() Contact(ContactOperator.Relay, False, "", "K1")),
        New LibraryPreset(CatElectric, "Relay contact, NC", Function() Contact(ContactOperator.Relay, True, "", "K1")),
        New LibraryPreset(CatElectric, "Proximity sensor, NO", Function() Contact(ContactOperator.ProximitySensor, False, "", "1B1")),
        New LibraryPreset(CatElectric, "Limit switch, NO", Function() Contact(ContactOperator.LimitSwitch, False, "", "1S1")),
        New LibraryPreset(CatElectric, "Relay coil", Function() New ElectricCoil() With {.Label = "K1"}),
        New LibraryPreset(CatElectric, "Timer relay, on-delay", Function() New ElectricCoil() With {.Kind = CoilKind.OnDelayTimer, .Label = "K2"}),
        New LibraryPreset(CatElectric, "Timer relay, off-delay", Function() New ElectricCoil() With {.Kind = CoilKind.OffDelayTimer, .Label = "K3"}),
        New LibraryPreset(CatElectric, "Valve solenoid", Function() New ElectricCoil() With {.Kind = CoilKind.Solenoid, .Label = "1M1"}),
        New LibraryPreset(CatElectric, "Indicator lamp", Function() New ElectricCoil() With {.Kind = CoilKind.Lamp, .Label = "H1"}),
        New LibraryPreset(CatElectric, "Wire junction", Function() New Junction() With {.IsElectric = True}),
        New LibraryPreset(CatDrawing, "Text", Function() New TextNote())
    }

    Public Function V32(actuator As ValveActuator, Optional normallyOpen As Boolean = False) As Valve32
        Return New Valve32() With {.Actuator = actuator, .NormallyOpen = normallyOpen}
    End Function

    Public Function V52(actuator As ValveActuator, ret As ValveReturn) As Valve52
        Return New Valve52() With {.Actuator = actuator, .ReturnType = ret}
    End Function

    Public Function Contact(op As ContactOperator, normallyClosed As Boolean, label As String, Optional reference As String = "") As ElectricContact
        Return New ElectricContact() With {.Operator = op, .NormallyClosed = normallyClosed, .Label = label, .Reference = reference}
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
