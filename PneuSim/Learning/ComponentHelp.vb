''' <summary>Short explanations shown when hovering over a component.</summary>
Public Module ComponentHelp

    Public Function HelpFor(e As CircuitElement) As String
        Dim head = e.DisplayName & If(String.IsNullOrWhiteSpace(e.Label), "", $"  ({e.Label})")
        Return head & Environment.NewLine & Body(e)
    End Function

    Private Function Body(e As CircuitElement) As String
        Select Case True
            Case TypeOf e Is Compressor
                Return "Compressor with pressure switch: charges the air receiver, starting at the cut-in pressure and stopping at the cut-out pressure. Click it during simulation to switch it off or on."
            Case TypeOf e Is AirReceiver
                Return "Air receiver (tank): stores compressed air so short peaks of air use do not make the pressure drop; its pressure falls as air is used until the compressor starts again."
            Case TypeOf e Is ShutOffValve
                Return "Shut-off (ball) valve: opens or closes the line by hand. Click it during simulation to turn the handle."
            Case TypeOf e Is FlowMeter
                Return $"Flow meter: shows how much {If(DirectCast(e, FlowMeter).Hydraulic, "oil (l/min)", "free air (NL/min)")} flows through the line and in which direction."
            Case TypeOf e Is ForceSensor
                Return $"Force sensor (load cell) on the rod of cylinder {DirectCast(e, ForceSensor).Cylinder}: shows the force the piston pushes or pulls with."
            Case TypeOf e Is PressureSwitch
                Dim ps = DirectCast(e, PressureSwitch)
                Return If(ps.Setting < 0,
                          $"Vacuum switch: switches when the vacuum reaches {ps.Setting:0.0#} bar, e.g. to report that a suction cup holds its part. Contacts with Reference {ps.Label} follow it.",
                          $"Pressure switch: switches when the pressure reaches {ps.Setting:0.0#} bar (e.g. clamping pressure reached). Contacts with Reference {ps.Label} follow it.")
            Case TypeOf e Is VacuumGenerator
                Return "Vacuum generator (ejector): compressed air rushing through a nozzle sucks the air out of port V. It only works while it gets compressed air."
            Case TypeOf e Is SuctionCup
                Return "Suction cup: holds the workpiece while the vacuum is strong enough. Click it during simulation to put a workpiece under it or take it away."
            Case TypeOf e Is Gripper
                Return "Parallel gripper: air at port 1 closes the jaws, air at port 2 opens them. The position marks report open and closed."
            Case TypeOf e Is ElectricCounter
                Return $"Preset counter: counts the pulses on A1/A2. When the count reaches {DirectCast(e, ElectricCounter).Preset}, its contacts (Reference {e.Label}) switch. A pulse on R1/R2, or a click during simulation, resets it."
            Case TypeOf e Is AirSupply
                Return "Delivers compressed air at the set pressure (usually 6 bar) from the compressor and air preparation."
            Case TypeOf e Is PressureRegulator AndAlso DirectCast(e, PressureRegulator).Hydraulic
                Return "Pressure reducing valve: keeps the oil pressure after the valve at its setting (e.g. a lower clamping pressure), whatever the pump pressure."
            Case TypeOf e Is PressureRegulator
                Return If(DirectCast(e, PressureRegulator).Style = RegulatorStyle.ServiceUnit,
                          "Service unit: filters the air, removes water and sets a constant working pressure (with a gauge).",
                          "Keeps the outlet pressure constant at the set value, whatever the inlet pressure.")
            Case TypeOf e Is PressureGauge
                Return If(DirectCast(e, PressureGauge).Hydraulic, "Hydraulic pressure gauge (0–160 bar): shows the oil pressure at its connection.",
                          "Pressure gauge (0–10 bar): shows the air pressure at its connection.")
            Case TypeOf e Is Silencer
                Return "Reduces the noise of exhausting air."
            Case TypeOf e Is Junction
                Return "A T-connection or bend point for tubes or wires. Select it and drag to move it."
            Case TypeOf e Is HydraulicCylinder
                Return "Hydraulic cylinder: speed = pump flow ÷ piston area; pressure = load force ÷ area."
            Case TypeOf e Is HydraulicSingleActingCylinder
                Return "Single-acting hydraulic cylinder (ram): oil pushes it out; the spring or the load pushes it back when the valve opens the line to tank."
            Case TypeOf e Is SingleActingCylinder
                Return "Air pushes the piston out; a spring returns it. Uses air in one direction only."
            Case TypeOf e Is SemiRotaryActuator
                Return "Turns a shaft through 0–180° with air: extending turns one way, retracting the other."
            Case TypeOf e Is DoubleActingCylinder
                Return "Air pushes the piston out (port 1) and back (port 2). The rod side has a smaller area, so retracting force is lower and speed higher."
            Case TypeOf e Is AirMotor
                Return "Turns continuously while air flows through it."
            Case TypeOf e Is Valve53
                Dim centreText As String
                Select Case DirectCast(e, Valve53).Centre
                    Case CentrePosition.Exhausted : centreText = "Exhaust centre: both cylinder lines are vented, so the cylinder can be moved by hand and stops pushing."
                    Case CentrePosition.Pressurized : centreText = "Pressure centre: both cylinder lines get air; a double-acting cylinder creeps out (larger piston area)."
                    Case Else : centreText = "Closed centre: all ports are blocked, so the cylinder stops and holds its position."
                End Select
                Return "5/3-way valve: two working positions and a spring-centred middle position. " & centreText
            Case TypeOf e Is HydraulicValve43
                Return "4/3-way hydraulic valve: P (pump), T (tank), A and B (actuator). The centre position decides what happens when no solenoid is on."
            Case TypeOf e Is DirectionalValve
                Dim v = DirectCast(e, DirectionalValve)
                Dim how = ""
                Select Case v.Actuator
                    Case ValveActuator.PushButton : how = "Operated while you hold the push button."
                    Case ValveActuator.Selector : how = "A selector switch stays where you put it (detent)."
                    Case ValveActuator.IdleReturnRoller : how = $"Idle-return roller: gives only a short pulse when the cylinder arrives at {If(String.IsNullOrWhiteSpace(v.TriggerMark), "its mark", v.TriggerMark)}, and none when it moves back. Used to avoid signal overlap."
                    Case ValveActuator.RollerLever : how = If(String.IsNullOrWhiteSpace(v.TriggerMark),
                                                              "Roller lever valve: enter the cylinder position mark that should operate it under 'Roller mark'.",
                                                              $"Operated when a cylinder reaches position mark {v.TriggerMark}.")
                    Case ValveActuator.Pilot : how = If(v.SwitchingPressure > Simulator.PilotThreshold + 0.01,
                                                        $"Pressure sequence valve: switches when the pilot pressure reaches {v.SwitchingPressure:0.#} bar.",
                                                        "Switched by air pressure on its pilot port.")
                    Case ValveActuator.DelayedPilot : how = $"Time delay valve: switches {v.DelaySeconds:0.#} s after the pilot signal arrives."
                    Case ValveActuator.Solenoid : how = $"Switched by the electrical solenoid {v.SolenoidLabel}. Click it during simulation for the manual override."
                End Select
                Dim ret = If(v.IsMemoryValve, " It is a memory (impulse) valve: it stays in its last position until the opposite signal arrives.",
                             If(v.ReturnType = ValveReturn.Spring, " A spring returns it when the signal goes.", ""))
                Dim medium = If(v.Ports.Any(Function(p) p.Kind = PortKind.Hydraulic), "oil", "air")
                Return $"Directs the {medium}: the boxes show the flow paths in each position; the active box sits over the ports. " & how & ret
            Case TypeOf e Is ShuttleValve
                Return "OR function: the output gets air if input 1a OR input 1b has air."
            Case TypeOf e Is TwoPressureValve
                Return "AND function: the output gets air only if input 1a AND input 1b have air (two-hand safety controls)."
            Case TypeOf e Is CheckValve
                Return $"Non-return valve: {If(DirectCast(e, CheckValve).Hydraulic, "oil", "air")} flows freely from 1 to 2 and is blocked from 2 to 1."
            Case TypeOf e Is QuickExhaustValve
                Return "Lets a cylinder exhaust straight to atmosphere next to the cylinder, so it returns much faster."
            Case TypeOf e Is FlowControlValve
                Dim fc = DirectCast(e, FlowControlValve)
                Dim fluid = If(fc.Hydraulic, "oil", "air")
                Return If(fc.HasCheckValve,
                          $"One-way flow control: throttles flow from 2 to 1, free flow from 1 to 2. Fit it so it throttles the {fluid} leaving the cylinder (meter-out).",
                          $"Throttles the {fluid} flow in both directions; slows the actuator either way.")
            Case TypeOf e Is PowerTerminal
                Return "Connection to the 24 V DC control voltage."
            Case TypeOf e Is ElectricContact
                Dim k = DirectCast(e, ElectricContact)
                Dim kind = If(k.NormallyClosed, "Normally closed (NC) contact: conducts until it is operated.", "Normally open (NO) contact: conducts while it is operated.")
                Select Case k.Operator
                    Case ContactOperator.Relay : Return kind & $" Operated by relay {k.Reference}."
                    Case ContactOperator.ProximitySensor : Return kind & $" Proximity sensor at cylinder position {k.Reference}."
                    Case ContactOperator.LimitSwitch : Return kind & $" Limit switch at cylinder position {k.Reference}."
                    Case ContactOperator.PressureSwitch : Return kind & $" Operated by pressure switch {k.Reference}."
                    Case ContactOperator.EmergencyStop : Return "Emergency stop: a red mushroom button that latches when pressed and opens the control circuit. Click again to release it (as turning a real one)."

                    Case Else : Return kind & " Operated by hand."
                End Select
            Case TypeOf e Is ElectricCoil
                Select Case DirectCast(e, ElectricCoil).Kind
                    Case CoilKind.Solenoid : Return $"Valve solenoid: when current flows it switches the valve that uses solenoid {e.Label}."
                    Case CoilKind.Lamp : Return "Indicator lamp."
                    Case CoilKind.OnDelayTimer : Return "On-delay timer relay: its contacts switch a set time after the coil is energized."
                    Case CoilKind.OffDelayTimer : Return "Off-delay timer relay: its contacts switch at once and switch back a set time after the coil goes off."
                    Case Else : Return $"Relay coil: when energized, all contacts referring to {e.Label} change over."
                End Select
            Case TypeOf e Is HydraulicPump
                Return "Pump driven by an electric motor; delivers a fixed flow. Click it during simulation to start or stop it."
            Case TypeOf e Is HydraulicTank
                Return "Oil tank: return lines from valves end here."
            Case TypeOf e Is ReliefValve
                Return "Pressure relief valve: opens at its setting and sends the pump flow back to tank, so the pressure cannot rise higher. Every hydraulic circuit needs one."
            Case TypeOf e Is HydraulicMotor
                Return "Turns when oil flows through it; speed = flow ÷ displacement."
            Case TypeOf e Is Accumulator
                Return "Stores oil under gas pressure; it can move actuators when the pump is off."
            Case TypeOf e Is CompensatedFlowControl
                Return "Keeps the flow constant even when the load pressure changes, so the speed stays constant."
            Case TypeOf e Is CounterbalanceValve
                Dim cb = DirectCast(e, CounterbalanceValve)
                Return $"Holds a hanging load: {If(cb.Hydraulic, "oil", "air")} can leave the cylinder (2 → 1) only when the load pressure rises above {cb.Setting:0.#} bar{If(cb.ExternalPilot, " or the pilot X opens it", "")}; it then opens just enough to let the load down under control. The built-in check valve lets {If(cb.Hydraulic, "oil", "air")} in freely (1 → 2) to lift it."
            Case TypeOf e Is PageConnector
                Return "Continues the line on another page: connectors with the same name are connected."
            Case TypeOf e Is TextNote
                Return "Text on the drawing (title, notes). Edit it in the Properties panel."
            Case Else
                Return ""
        End Select
    End Function
End Module
